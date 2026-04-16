#include "pos_downloader.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <future>
#include <vector>

namespace {
constexpr int kDictionaryTimeoutMs = 12000;
constexpr int kGlobalRequestGapMs = 260;
constexpr int kMaxRetries = 3;
constexpr int kRetryBaseBackoffMs = 450;
QMutex gRequestGapMutex;
qint64 gLastRequestMs = 0;

int getBestWorkerCount(int target = 8) {
    int ideal = QThread::idealThreadCount();
    if (ideal <= 0) return 1;
    int count = target;
    while (count > ideal && count > 1) {
        count -= 2;
    }
    return qMax(1, count);
}
}

PosDownloader::PosDownloader() = default;

PosDownloader::Result PosDownloader::downloadPartOfSpeechForWords(const QVector<WordItem> &words,
                                                                  const ProgressCallback &onProgress,
                                                                  const CancelChecker &shouldCancel,
                                                                  QString &errorText) const {
    Result result;
    errorText.clear();
    result.totalWords = words.size();
    if (words.isEmpty()) {
        return result;
    }

    if (onProgress) {
        onProgress(0, words.size(), QStringLiteral("准备开始..."));
    }

    struct ItemResult {
        int index = -1;
        bool updated = false;
        bool skipped = false;
        bool failed = false;
        Update update;
        QString errorText;
    };

    const auto processOne = [this](int index, const WordItem &item) -> ItemResult {
        ItemResult r;
        r.index = index;
        r.update.wordId = item.id;
        r.update.word = item.word.trimmed();

        if (r.update.word.isEmpty() || item.id <= 0) {
            r.skipped = true;
            return r;
        }

        const QUrl queryUrl(QStringLiteral("https://api.dictionaryapi.dev/api/v2/entries/en/%1")
                                .arg(QString::fromLatin1(QUrl::toPercentEncoding(r.update.word))));
        QByteArray jsonData;
        QString fetchError;
        if (!fetchUrl(queryUrl, jsonData, fetchError, kDictionaryTimeoutMs)) {
            r.failed = true;
            r.errorText = fetchError;
            return r;
        }

        const QString pos = extractPartOfSpeech(jsonData);
        if (pos.isEmpty()) {
            r.skipped = true;
            return r;
        }

        r.update.partOfSpeech = pos;
        r.updated = true;
        return r;
    };

    const int workerCount = getBestWorkerCount(8);
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
            QThread *th = QThread::create([offset, i, &words, &batchResults, &processOne]() {
                batchResults[offset] = processOne(i, words.at(i));
            });
            th->start();
            threads.push_back(th);
        }

        for (QThread *th : threads) {
            th->wait();
            th->deleteLater();
        }

        for (const ItemResult &r : batchResults) {
            ++result.processed;
            if (r.updated) {
                result.updates.push_back(r.update);
                ++result.updated;
            } else if (r.skipped) {
                ++result.skipped;
            } else if (r.failed) {
                ++result.failed;
                if (errorText.isEmpty()) {
                    errorText = r.errorText;
                }
            }

            if (onProgress) {
                const QString shownWord = r.update.word.isEmpty() ? QStringLiteral("(空单词)") : r.update.word;
                onProgress(r.index, words.size(), shownWord);
            }
        }

        cursor = batchEnd;
    }

    if (onProgress && !result.cancelled) {
        onProgress(words.size(), words.size(), QStringLiteral("下载完成"));
    }

    return result;
}

QString PosDownloader::extractPartOfSpeech(const QByteArray &jsonData) {
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isArray()) {
        return QString();
    }

    QSet<QString> uniqueSet;
    QStringList ordered;
    const QJsonArray entries = doc.array();
    for (const QJsonValue &entryValue : entries) {
        const QJsonObject entry = entryValue.toObject();
        const QJsonArray meanings = entry.value(QStringLiteral("meanings")).toArray();
        for (const QJsonValue &meaningValue : meanings) {
            const QString pos = meaningValue.toObject()
                                    .value(QStringLiteral("partOfSpeech"))
                                    .toString()
                                    .trimmed()
                                    .toLower();
            if (pos.isEmpty() || uniqueSet.contains(pos)) {
                continue;
            }
            uniqueSet.insert(pos);
            ordered.push_back(pos);
        }
    }

    return ordered.join(QStringLiteral(","));
}

bool PosDownloader::fetchUrl(const QUrl &url,
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
