#include "audio_downloader.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <future>
#include <vector>

namespace {
constexpr int kDictionaryTimeoutMs = 12000;
constexpr int kAudioTimeoutMs = 18000;
// 折中值：并发不要太高，避免 API 频繁拒绝。
constexpr int kAudioDownloadWorkers = 2;
// 全局请求最小间隔（毫秒），控制所有线程总频率。
constexpr qint64 kGlobalRequestGapMs = 260;
// 限流/网关抖动时的重试策略。
constexpr int kMaxRetries = 3;
constexpr int kRetryBaseBackoffMs = 450;

QMutex gRequestGapMutex;
qint64 gLastRequestMs = 0;
}

AudioDownloader::AudioDownloader() = default;

AudioDownloader::Result AudioDownloader::downloadBookAudio(const QVector<WordItem> &words,
                                                           int bookId,
                                                           const ProgressCallback &onProgress,
                                                           const CancelChecker &shouldCancel,
                                                           QString &errorText) const {
    Result result;
    errorText.clear();
    Q_UNUSED(bookId);
    result.totalWords = words.size();
    if (words.isEmpty()) {
        return result;
    }

    QDir dir(audioDirPath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        errorText = QStringLiteral("无法创建音频目录：%1").arg(dir.absolutePath());
        return result;
    }

    QHash<QString, QString> hashManifest;
    if (!loadHashManifest(hashManifest, errorText)) {
        return result;
    }

    if (onProgress) {
        onProgress(0, words.size(), QStringLiteral("准备校验..."));
    }

    struct ItemResult {
        enum class Status {
            Downloaded,
            Reused,
            NoMp3,
            HashMismatch,
            Failed,
        };

        int index = -1;
        QString word;
        QString manifestKey;
        QString hashHex;
        bool shouldPersistHash = false;
        Status status = Status::Failed;
        QString errorText;
    };

    const auto downloadOne = [this, &dir, &hashManifest](int index, const WordItem &item) -> ItemResult {
        ItemResult itemResult;
        itemResult.index = index;

        const QString rawWord = item.word.trimmed();
        itemResult.word = rawWord;
        itemResult.manifestKey = manifestKeyForWord(rawWord);
        if (rawWord.isEmpty()) {
            itemResult.status = ItemResult::Status::Failed;
            itemResult.errorText = QStringLiteral("(空单词)");
            return itemResult;
        }

        const QString fileName = safeAudioFileName(rawWord) + QStringLiteral(".mp3");
        const QString finalPath = dir.filePath(fileName);
        const QString expectedHash = hashManifest.value(itemResult.manifestKey).trimmed();

        QByteArray existingData;
        QString readError;
        const bool hasExistingFile = readFileBytes(finalPath, existingData, readError);
        if (!readError.isEmpty()) {
            itemResult.status = ItemResult::Status::Failed;
            itemResult.errorText = readError;
            return itemResult;
        }
        if (hasExistingFile && !existingData.isEmpty()) {
            const QString existingHash = sha256Hex(existingData);
            if (expectedHash.isEmpty()) {
                // 历史文件首次纳入哈希清单，作为基准。
                itemResult.status = ItemResult::Status::Reused;
                itemResult.hashHex = existingHash;
                itemResult.shouldPersistHash = true;
                return itemResult;
            }
            if (QString::compare(existingHash, expectedHash, Qt::CaseInsensitive) == 0) {
                itemResult.status = ItemResult::Status::Reused;
                return itemResult;
            }
        }

        QByteArray dictionaryJson;
        QString fetchError;
        const QString encodedWord = QString::fromLatin1(QUrl::toPercentEncoding(rawWord));
        const QUrl dictionaryQueryUrl(QStringLiteral("https://api.dictionaryapi.dev/api/v2/entries/en/%1")
                                          .arg(encodedWord));
        const QUrl youdaoFallbackUrl(QStringLiteral("http://dict.youdao.com/dictvoice?type=1&audio=%1")
                                         .arg(encodedWord));

        QString dictionaryMp3Url;
        if (fetchUrl(dictionaryQueryUrl, dictionaryJson, fetchError, kDictionaryTimeoutMs)) {
            dictionaryMp3Url = extractMp3Url(dictionaryJson);
        }

        QVector<QUrl> candidateAudioUrls;
        if (!dictionaryMp3Url.isEmpty()) {
            candidateAudioUrls.push_back(QUrl(dictionaryMp3Url));
        }
        candidateAudioUrls.push_back(youdaoFallbackUrl);

        QByteArray audioBytes;
        QStringList fetchErrors;
        for (const QUrl &audioUrl : candidateAudioUrls) {
            if (!audioUrl.isValid()) {
                continue;
            }
            fetchError.clear();
            audioBytes.clear();
            if (fetchUrl(audioUrl, audioBytes, fetchError, kAudioTimeoutMs) && !audioBytes.isEmpty()) {
                break;
            }
            if (!fetchError.trimmed().isEmpty()) {
                fetchErrors.push_back(fetchError);
            }
        }

        if (audioBytes.isEmpty()) {
            if (dictionaryMp3Url.isEmpty()) {
                itemResult.status = ItemResult::Status::NoMp3;
            } else {
                itemResult.status = ItemResult::Status::Failed;
            }
            itemResult.errorText = fetchErrors.join(QStringLiteral(" | "));
            return itemResult;
        }

        QString writeError;
        if (!writeAudioAtomically(finalPath, audioBytes, writeError)) {
            itemResult.status = ItemResult::Status::Failed;
            itemResult.errorText = writeError;
            return itemResult;
        }

        const QString downloadedHash = sha256Hex(audioBytes);
        if (!expectedHash.isEmpty()
            && QString::compare(downloadedHash, expectedHash, Qt::CaseInsensitive) != 0) {
            itemResult.status = ItemResult::Status::HashMismatch;
            itemResult.hashHex = downloadedHash;
            itemResult.errorText = QStringLiteral("哈希不匹配：expected=%1 actual=%2")
                                       .arg(expectedHash, downloadedHash);
            return itemResult;
        }

        itemResult.status = ItemResult::Status::Downloaded;
        itemResult.hashHex = downloadedHash;
        itemResult.shouldPersistHash = true;
        return itemResult;
    };

    const int workerCount = qBound(1, kAudioDownloadWorkers, qMax(1, QThread::idealThreadCount()));
    int cursor = 0;
    while (cursor < words.size()) {
        if (shouldCancel && shouldCancel()) {
            result.cancelled = true;
            break;
        }

        const int batchEnd = qMin(words.size(), cursor + workerCount);
        std::vector<QThread*> threads;
        threads.reserve(static_cast<size_t>(batchEnd - cursor));
        QVector<ItemResult> batchResults(batchEnd - cursor);

        for (int i = cursor; i < batchEnd; ++i) {
            const int offset = i - cursor;
            QThread *th = QThread::create([offset, i, &words, &batchResults, &downloadOne]() {
                batchResults[offset] = downloadOne(i, words.at(i));
            });
            th->start();
            threads.push_back(th);
        }

        for (QThread *th : threads) {
            th->wait();
            th->deleteLater();
        }

        for (const ItemResult &itemResult : batchResults) {
            ++result.checked;
            if (itemResult.status == ItemResult::Status::Downloaded) {
                ++result.downloaded;
            } else if (itemResult.status == ItemResult::Status::Reused) {
                ++result.reused;
            } else if (itemResult.status == ItemResult::Status::NoMp3) {
                ++result.noMp3;
            } else if (itemResult.status == ItemResult::Status::HashMismatch) {
                ++result.hashMismatched;
                ++result.failed;
            } else {
                ++result.failed;
            }

            if (itemResult.shouldPersistHash
                && !itemResult.manifestKey.isEmpty()
                && !itemResult.hashHex.isEmpty()) {
                hashManifest.insert(itemResult.manifestKey, itemResult.hashHex.toLower());
            }

            if ((itemResult.status == ItemResult::Status::Failed
                 || itemResult.status == ItemResult::Status::HashMismatch)
                && !itemResult.errorText.isEmpty()) {
                qWarning().noquote() << QStringLiteral("[audio] %1 -> %2")
                                            .arg(itemResult.word, itemResult.errorText);
            }

            if (onProgress) {
                const QString shownWord = itemResult.word.isEmpty() ? QStringLiteral("(空单词)") : itemResult.word;
                onProgress(itemResult.index, words.size(), shownWord);
            }
        }

        cursor = batchEnd;
    }

    if (onProgress && !result.cancelled) {
        onProgress(words.size(), words.size(), QStringLiteral("下载完成"));
    }

    if (!saveHashManifest(hashManifest, errorText)) {
        return result;
    }

    return result;
}

QString AudioDownloader::audioDirPath() const {
    return QStringLiteral(VIBESPELLER_SOURCE_DIR) + QStringLiteral("/assets/audio");
}

QString AudioDownloader::hashManifestPath() const {
    return QStringLiteral(VIBESPELLER_SOURCE_DIR)
           + QStringLiteral("/assets/audio/.hash_manifest.json");
}

QString AudioDownloader::safeAudioFileName(const QString &word) {
    QString name = word.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    name = name.trimmed();
    if (name.isEmpty()) {
        name = QStringLiteral("word");
    }
    return name;
}

QString AudioDownloader::manifestKeyForWord(const QString &word) {
    return word.trimmed().toLower();
}

QString AudioDownloader::sha256Hex(const QByteArray &data) {
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString AudioDownloader::extractMp3Url(const QByteArray &jsonData) {
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isArray()) {
        return QString();
    }

    const QJsonArray entries = doc.array();
    for (const QJsonValue &entryValue : entries) {
        const QJsonObject entry = entryValue.toObject();
        const QJsonArray phonetics = entry.value(QStringLiteral("phonetics")).toArray();
        for (const QJsonValue &phoneticValue : phonetics) {
            QString audio = phoneticValue.toObject().value(QStringLiteral("audio")).toString().trimmed();
            if (audio.isEmpty()) {
                continue;
            }
            if (audio.startsWith(QStringLiteral("//"))) {
                audio.prepend(QStringLiteral("https:"));
            }
            if (audio.contains(QStringLiteral(".mp3"), Qt::CaseInsensitive)) {
                return audio;
            }
        }
    }

    return QString();
}

bool AudioDownloader::fetchUrl(const QUrl &url,
                               QByteArray &data,
                               QString &errorText,
                               int timeoutMs) const {
    data.clear();
    errorText.clear();

    if (!url.isValid() || url.scheme().isEmpty()) {
        errorText = QStringLiteral("无效 URL：%1").arg(url.toString());
        return false;
    }

    for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
        {
            // 全局节流：确保所有下载线程总请求频率可控。
            QMutexLocker locker(&gRequestGapMutex);
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 elapsed = nowMs - gLastRequestMs;
            if (elapsed < kGlobalRequestGapMs) {
                QThread::msleep(static_cast<unsigned long>(kGlobalRequestGapMs - elapsed));
            }
            gLastRequestMs = QDateTime::currentMSecsSinceEpoch();
        }

        QNetworkAccessManager manager;
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("VibeSpeller/1.0 (Qt)"));

        QEventLoop loop;
        QNetworkReply *reply = manager.get(request);

        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
            if (reply && reply->isRunning()) {
                reply->abort();
            }
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(timeoutMs);
        loop.exec();

        timer.stop();

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool needRetryByStatus = (statusCode == 429 || statusCode == 500 || statusCode == 502
                                        || statusCode == 503 || statusCode == 504);
        const bool needRetryByError = (reply->error() == QNetworkReply::TimeoutError
                                       || reply->error() == QNetworkReply::TemporaryNetworkFailureError
                                       || reply->error() == QNetworkReply::OperationCanceledError);

        if (reply->error() == QNetworkReply::NoError) {
            data = reply->readAll();
            reply->deleteLater();
            return true;
        }

        const QString statusPart = (statusCode > 0) ? QStringLiteral("HTTP %1, ").arg(statusCode) : QString();
        errorText = statusPart + reply->errorString();
        reply->deleteLater();

        if (attempt < kMaxRetries && (needRetryByStatus || needRetryByError)) {
            const int backoff = kRetryBaseBackoffMs * (attempt + 1);
            QThread::msleep(static_cast<unsigned long>(backoff));
            continue;
        }
        return false;
    }
    return false;
}

bool AudioDownloader::writeAudioAtomically(const QString &finalPath,
                                           const QByteArray &data,
                                           QString &errorText) const {
    errorText.clear();

    QSaveFile file(finalPath);
    if (!file.open(QIODevice::WriteOnly)) {
        errorText = file.errorString();
        return false;
    }

    if (file.write(data) != data.size()) {
        errorText = file.errorString();
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        errorText = file.errorString();
        return false;
    }

    QFile saved(finalPath);
    if (!saved.exists() || saved.size() <= 0) {
        errorText = QStringLiteral("音频文件保存后为空");
        return false;
    }
    return true;
}

bool AudioDownloader::readFileBytes(const QString &path, QByteArray &data, QString &errorText) const {
    data.clear();
    errorText.clear();

    QFile file(path);
    if (!file.exists()) {
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        errorText = QStringLiteral("读取音频失败：%1").arg(file.errorString());
        return false;
    }

    data = file.readAll();
    return true;
}

bool AudioDownloader::loadHashManifest(QHash<QString, QString> &manifest, QString &errorText) const {
    manifest.clear();
    errorText.clear();

    QFile file(hashManifestPath());
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorText = QStringLiteral("无法读取音频哈希清单：%1").arg(file.errorString());
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        errorText = QStringLiteral("音频哈希清单格式错误");
        return false;
    }

    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        manifest.insert(it.key(), it.value().toString().trimmed().toLower());
    }
    return true;
}

bool AudioDownloader::saveHashManifest(const QHash<QString, QString> &manifest, QString &errorText) const {
    errorText.clear();

    QJsonObject obj;
    for (auto it = manifest.constBegin(); it != manifest.constEnd(); ++it) {
        if (!it.key().trimmed().isEmpty() && !it.value().trimmed().isEmpty()) {
            obj.insert(it.key(), it.value().trimmed().toLower());
        }
    }

    QSaveFile file(hashManifestPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorText = QStringLiteral("无法写入音频哈希清单：%1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        errorText = QStringLiteral("音频哈希清单保存失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}
