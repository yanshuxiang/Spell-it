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

namespace {
constexpr int kDictionaryTimeoutMs = 12000;
constexpr int kAudioTimeoutMs = 18000;
constexpr int kRequestGapMs = 450;
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

    for (int i = startIndex; i < words.size(); ++i) {
        if (shouldCancel && shouldCancel()) {
            result.cancelled = true;
            break;
        }

        const QString rawWord = words.at(i).word.trimmed();
        if (onProgress) {
            onProgress(i, words.size(), rawWord);
        }
        if (rawWord.isEmpty()) {
            ++result.failed;
            saveLastCompletedIndex(bookId, i);
            if (onProgress) {
                onProgress(i, words.size(), QStringLiteral("(空单词)"));
            }
            continue;
        }

        const QString fileName = safeAudioFileName(rawWord) + QStringLiteral(".mp3");
        const QString finalPath = dir.filePath(fileName);

        QFile existing(finalPath);
        if (existing.exists()) {
            if (existing.size() > 0) {
                ++result.reused;
                saveLastCompletedIndex(bookId, i);
                if (onProgress) {
                    onProgress(i, words.size(), rawWord);
                }
                QThread::msleep(kRequestGapMs);
                continue;
            }
            existing.remove();
        }

        QByteArray dictionaryJson;
        QString fetchError;
        const QUrl queryUrl(QStringLiteral("https://api.dictionaryapi.dev/api/v2/entries/en/%1")
                                .arg(QString::fromLatin1(QUrl::toPercentEncoding(rawWord))));

        if (!fetchUrl(queryUrl, dictionaryJson, fetchError, kDictionaryTimeoutMs)) {
            ++result.failed;
            saveLastCompletedIndex(bookId, i);
            if (onProgress) {
                onProgress(i, words.size(), rawWord);
            }
            QThread::msleep(kRequestGapMs);
            continue;
        }

        const QString mp3Url = extractMp3Url(dictionaryJson);
        if (mp3Url.isEmpty()) {
            ++result.noMp3;
            saveLastCompletedIndex(bookId, i);
            if (onProgress) {
                onProgress(i, words.size(), rawWord);
            }
            QThread::msleep(kRequestGapMs);
            continue;
        }

        QThread::msleep(kRequestGapMs);
        QByteArray audioBytes;
        if (!fetchUrl(QUrl(mp3Url), audioBytes, fetchError, kAudioTimeoutMs)) {
            ++result.failed;
            saveLastCompletedIndex(bookId, i);
            if (onProgress) {
                onProgress(i, words.size(), rawWord);
            }
            QThread::msleep(kRequestGapMs);
            continue;
        }

        if (audioBytes.isEmpty()) {
            ++result.failed;
            saveLastCompletedIndex(bookId, i);
            if (onProgress) {
                onProgress(i, words.size(), rawWord);
            }
            QThread::msleep(kRequestGapMs);
            continue;
        }

        QString writeError;
        if (!writeAudioAtomically(finalPath, audioBytes, writeError)) {
            ++result.failed;
            saveLastCompletedIndex(bookId, i);
            if (onProgress) {
                onProgress(i, words.size(), rawWord);
            }
            QThread::msleep(kRequestGapMs);
            continue;
        }

        ++result.downloaded;
        saveLastCompletedIndex(bookId, i);
        if (onProgress) {
            onProgress(i, words.size(), rawWord);
        }
        QThread::msleep(kRequestGapMs);
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
