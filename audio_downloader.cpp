#include "audio_downloader.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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
constexpr int kAudioDownloadWorkers = 4;
}

AudioDownloader::AudioDownloader() = default;

AudioDownloader::Result AudioDownloader::downloadBookAudio(const QVector<WordItem> &words,
                                                           int bookId,
                                                           const ProgressCallback &onProgress,
                                                           const CancelChecker &shouldCancel,
                                                           QString &errorText) const {
    Result result;
    errorText.clear();
    result.totalWords = words.size();
    if (words.isEmpty()) {
        return result;
    }

    QDir dir(audioDirPath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        errorText = QStringLiteral("无法创建音频目录：%1").arg(dir.absolutePath());
        return result;
    }

    int startIndex = 0;
    const int lastCompleted = loadLastCompletedIndex(bookId);
    if (lastCompleted >= 0) {
        startIndex = qBound(0, lastCompleted, words.size() - 1);
    }
    result.resumeStartIndex = startIndex;

    if (onProgress) {
        onProgress(startIndex, words.size(), QStringLiteral("准备开始..."));
    }

    struct ItemResult {
        enum class Status {
            Downloaded,
            Reused,
            NoMp3,
            Failed,
        };

        int index = -1;
        QString word;
        Status status = Status::Failed;
        QString errorText;
    };

    const auto downloadOne = [this, &dir](int index, const WordItem &item) -> ItemResult {
        ItemResult itemResult;
        itemResult.index = index;

        const QString rawWord = item.word.trimmed();
        itemResult.word = rawWord;
        if (rawWord.isEmpty()) {
            itemResult.status = ItemResult::Status::Failed;
            itemResult.errorText = QStringLiteral("(空单词)");
            return itemResult;
        }

        const QString fileName = safeAudioFileName(rawWord) + QStringLiteral(".mp3");
        const QString finalPath = dir.filePath(fileName);

        QFile existing(finalPath);
        if (existing.exists()) {
            if (existing.size() > 0) {
                itemResult.status = ItemResult::Status::Reused;
                return itemResult;
            }
            existing.remove();
        }

        QByteArray dictionaryJson;
        QString fetchError;
        const QUrl queryUrl(QStringLiteral("https://api.dictionaryapi.dev/api/v2/entries/en/%1")
                                .arg(QString::fromLatin1(QUrl::toPercentEncoding(rawWord))));

        if (!fetchUrl(queryUrl, dictionaryJson, fetchError, kDictionaryTimeoutMs)) {
            itemResult.status = ItemResult::Status::Failed;
            itemResult.errorText = fetchError;
            return itemResult;
        }

        const QString mp3Url = extractMp3Url(dictionaryJson);
        if (mp3Url.isEmpty()) {
            itemResult.status = ItemResult::Status::NoMp3;
            return itemResult;
        }

        QByteArray audioBytes;
        if (!fetchUrl(QUrl(mp3Url), audioBytes, fetchError, kAudioTimeoutMs)) {
            itemResult.status = ItemResult::Status::Failed;
            itemResult.errorText = fetchError;
            return itemResult;
        }

        if (audioBytes.isEmpty()) {
            itemResult.status = ItemResult::Status::Failed;
            itemResult.errorText = QStringLiteral("下载到的音频为空");
            return itemResult;
        }

        QString writeError;
        if (!writeAudioAtomically(finalPath, audioBytes, writeError)) {
            itemResult.status = ItemResult::Status::Failed;
            itemResult.errorText = writeError;
            return itemResult;
        }

        itemResult.status = ItemResult::Status::Downloaded;
        return itemResult;
    };

    const int workerCount = qBound(1, kAudioDownloadWorkers, qMax(1, QThread::idealThreadCount()));
    int cursor = startIndex;
    while (cursor < words.size()) {
        if (shouldCancel && shouldCancel()) {
            result.cancelled = true;
            break;
        }

        const int batchEnd = qMin(words.size(), cursor + workerCount);
        std::vector<std::future<ItemResult>> futures;
        futures.reserve(static_cast<size_t>(batchEnd - cursor));
        for (int i = cursor; i < batchEnd; ++i) {
            futures.emplace_back(std::async(std::launch::async, downloadOne, i, words.at(i)));
        }

        QVector<ItemResult> batchResults;
        batchResults.reserve(batchEnd - cursor);
        for (auto &future : futures) {
            batchResults.push_back(future.get());
        }
        std::sort(batchResults.begin(), batchResults.end(), [](const ItemResult &a, const ItemResult &b) {
            return a.index < b.index;
        });

        for (const ItemResult &itemResult : batchResults) {
            if (itemResult.status == ItemResult::Status::Downloaded) {
                ++result.downloaded;
            } else if (itemResult.status == ItemResult::Status::Reused) {
                ++result.reused;
            } else if (itemResult.status == ItemResult::Status::NoMp3) {
                ++result.noMp3;
            } else {
                ++result.failed;
            }

            saveLastCompletedIndex(bookId, itemResult.index);
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

    if (!result.cancelled) {
        clearProgress(bookId);
    }

    return result;
}

QString AudioDownloader::audioDirPath() const {
    return QStringLiteral(VIBESPELLER_SOURCE_DIR) + QStringLiteral("/assets/audio");
}

QString AudioDownloader::progressFilePath(int bookId) const {
    return QStringLiteral(VIBESPELLER_SOURCE_DIR)
           + QStringLiteral("/assets/audio/.download_progress_%1.txt").arg(bookId);
}

int AudioDownloader::loadLastCompletedIndex(int bookId) const {
    QFile file(progressFilePath(bookId));
    if (!file.exists()) {
        return -1;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return -1;
    }
    bool ok = false;
    const int value = QString::fromUtf8(file.readAll()).trimmed().toInt(&ok);
    return ok ? value : -1;
}

bool AudioDownloader::saveLastCompletedIndex(int bookId, int index) const {
    QSaveFile file(progressFilePath(bookId));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    file.write(QString::number(index).toUtf8());
    return file.commit();
}

void AudioDownloader::clearProgress(int bookId) const {
    QFile::remove(progressFilePath(bookId));
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

    if (reply->error() != QNetworkReply::NoError) {
        errorText = reply->errorString();
        reply->deleteLater();
        return false;
    }

    data = reply->readAll();
    reply->deleteLater();
    return true;
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
