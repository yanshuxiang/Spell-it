#include "countability_downloader.h"

#include <QDateTime>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>

#include <algorithm>
#include <future>
#include <vector>

namespace {
constexpr int kOxfordTimeoutMs = 15000;
constexpr int kGlobalRequestGapMs = 900;
constexpr int kMaxRetries = 2;
constexpr int kRetryBackoffMs = 600;

QMutex gCountabilityGapMutex;
qint64 gCountabilityLastRequestMs = 0;

int getBestWorkerCount(int target = 8) {
    int ideal = QThread::idealThreadCount();
    if (ideal <= 0) return 1;
    int count = target;
    while (count > ideal && count > 1) {
        count -= 2;
    }
    return qMax(1, count);
}

QString toLowerNoTags(QString s) {
    s = s.toLower();
    s.remove(QRegularExpression(QStringLiteral("<script\\b[^>]*>[\\s\\S]*?</script>")));
    s.remove(QRegularExpression(QStringLiteral("<style\\b[^>]*>[\\s\\S]*?</style>")));
    s.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QStringLiteral(" "));
    s = s.simplified();
    return s;
}
} // namespace

CountabilityDownloader::CountabilityDownloader() = default;

CountabilityDownloader::Result CountabilityDownloader::downloadCountabilityForWords(
    const QVector<WordItem> &words,
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
        onProgress(0, words.size(), QStringLiteral("准备开始..."), Update{});
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

        const QString normalizedWord = r.update.word.toLower();
        const QString encodedWord = QString::fromLatin1(QUrl::toPercentEncoding(normalizedWord));
        const QUrl directUrl(QStringLiteral("https://www.oxfordlearnersdictionaries.com/definition/english/%1")
                                 .arg(encodedWord));

        QByteArray htmlData;
        QString fetchError;
        QString sourceUrl = directUrl.toString();
        bool ok = fetchUrl(directUrl, htmlData, fetchError, kOxfordTimeoutMs);
        if (!ok) {
            const QUrl searchUrl(QStringLiteral("https://www.oxfordlearnersdictionaries.com/search/english/?q=%1")
                                     .arg(encodedWord));
            sourceUrl = searchUrl.toString();
            QByteArray searchData;
            QString searchError;
            if (!fetchUrl(searchUrl, searchData, searchError, kOxfordTimeoutMs)) {
                r.failed = true;
                r.errorText = searchError;
                return r;
            }
            const QString path = extractFirstDefinitionPath(QString::fromUtf8(searchData));
            if (path.isEmpty()) {
                r.skipped = true;
                r.update.label = QStringLiteral("NA");
                r.update.source = QStringLiteral("oxford:%1").arg(sourceUrl);
                return r;
            }
            QUrl resolved = searchUrl.resolved(QUrl(path));
            sourceUrl = resolved.toString();
            if (!fetchUrl(resolved, htmlData, fetchError, kOxfordTimeoutMs)) {
                r.failed = true;
                r.errorText = fetchError;
                return r;
            }
        }

        const QString label = detectCountability(QString::fromUtf8(htmlData));
        r.update.label = label;
        r.update.source = QStringLiteral("oxford:%1").arg(sourceUrl);
        if (label == QStringLiteral("NA")) {
            r.skipped = true;
        } else {
            r.updated = true;
        }
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
                ++result.updated;
                result.updates.push_back(r.update);
            } else if (r.skipped) {
                ++result.skipped;
                if (r.update.wordId > 0 && !r.update.label.isEmpty()) {
                    result.updates.push_back(r.update);
                }
            } else if (r.failed) {
                ++result.failed;
                if (errorText.isEmpty()) {
                    errorText = r.errorText;
                }
            }

            if (onProgress) {
                const QString shownWord = r.update.word.isEmpty() ? QStringLiteral("(空单词)") : r.update.word;
                onProgress(r.index, words.size(), shownWord, r.update);
            }
        }

        cursor = batchEnd;
    }

    if (onProgress && !result.cancelled) {
        onProgress(words.size(), words.size(), QStringLiteral("下载完成"), Update{});
    }
    return result;
}

QString CountabilityDownloader::normalizeHtmlText(const QString &htmlText) {
    return toLowerNoTags(htmlText);
}

QString CountabilityDownloader::detectCountability(const QString &htmlText) {
    // 仅分析主词条区域，避免侧栏/推荐词条污染。
    int scopeStart = htmlText.indexOf(QStringLiteral("id=\"entryContent\""), 0, Qt::CaseInsensitive);
    if (scopeStart < 0) {
        scopeStart = htmlText.indexOf(QStringLiteral("class=\"entry\""), 0, Qt::CaseInsensitive);
    }
    int scopeEnd = -1;
    if (scopeStart >= 0) {
        scopeEnd = htmlText.indexOf(QStringLiteral("id=\"relatedentries\""), scopeStart, Qt::CaseInsensitive);
    }
    const QString scopeHtml = (scopeStart >= 0)
                                  ? htmlText.mid(scopeStart, (scopeEnd > scopeStart) ? (scopeEnd - scopeStart) : -1)
                                  : htmlText;

    // 仅提取 grammar 标签（如 [countable] / [uncountable]）。
    QRegularExpression grammarRe(
        QStringLiteral("<span[^>]*class\\s*=\\s*\"[^\"]*\\bgrammar\\b[^\"]*\"[^>]*>(.*?)</span>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression uncountableRe(QStringLiteral("\\buncountable\\b"),
                                     QRegularExpression::CaseInsensitiveOption);
    QRegularExpression countableRe(QStringLiteral("\\bcountable\\b"),
                                   QRegularExpression::CaseInsensitiveOption);
    QRegularExpression uBracketRe(QStringLiteral("\\[\\s*u\\s*\\]"),
                                  QRegularExpression::CaseInsensitiveOption);
    QRegularExpression cBracketRe(QStringLiteral("\\[\\s*c\\s*\\]"),
                                  QRegularExpression::CaseInsensitiveOption);

    bool hasUncountable = false;
    bool hasCountable = false;
    auto it = grammarRe.globalMatch(scopeHtml);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString grammarText = normalizeHtmlText(match.captured(1));
        if (grammarText.isEmpty()) {
            continue;
        }

        const bool hasUInThis = grammarText.contains(uncountableRe) || grammarText.contains(uBracketRe);
        QString textWithoutUn = grammarText;
        textWithoutUn.replace(uncountableRe, QStringLiteral(" "));
        const bool hasCInThis = textWithoutUn.contains(countableRe) || grammarText.contains(cBracketRe);

        hasUncountable = hasUncountable || hasUInThis;
        hasCountable = hasCountable || hasCInThis;
    }

    if (hasUncountable && hasCountable) {
        return QStringLiteral("B");
    }
    if (hasUncountable) {
        return QStringLiteral("U");
    }
    if (hasCountable) {
        return QStringLiteral("C");
    }
    return QStringLiteral("NA");
}

QString CountabilityDownloader::extractFirstDefinitionPath(const QString &searchHtml) {
    QRegularExpression re(QStringLiteral("href=\"(/definition/english/[^\"]+)\""),
                          QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(searchHtml);
    if (!m.hasMatch()) {
        return QString();
    }
    return m.captured(1);
}

bool CountabilityDownloader::fetchUrl(const QUrl &url,
                                      QByteArray &data,
                                      QString &errorText,
                                      int timeoutMs) const {
    data.clear();
    errorText.clear();
    if (!url.isValid()) {
        errorText = QStringLiteral("无效 URL：%1").arg(url.toString());
        return false;
    }

    for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
        {
            QMutexLocker locker(&gCountabilityGapMutex);
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const qint64 elapsed = nowMs - gCountabilityLastRequestMs;
            if (elapsed < kGlobalRequestGapMs) {
                QThread::msleep(static_cast<unsigned long>(kGlobalRequestGapMs - elapsed));
            }
            gCountabilityLastRequestMs = QDateTime::currentMSecsSinceEpoch();
        }

        QNetworkAccessManager manager;
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("Mozilla/5.0 (Macintosh; Intel Mac OS X) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36"));
        request.setRawHeader("Accept-Language", "en-US,en;q=0.9");

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
        const bool retryStatus = (statusCode == 429 || statusCode == 500 || statusCode == 502
                                  || statusCode == 503 || statusCode == 504);
        const bool retryError = (reply->error() == QNetworkReply::TimeoutError
                                 || reply->error() == QNetworkReply::TemporaryNetworkFailureError
                                 || reply->error() == QNetworkReply::OperationCanceledError);

        if (reply->error() == QNetworkReply::NoError) {
            data = reply->readAll();
            reply->deleteLater();
            return true;
        }

        errorText = QStringLiteral("HTTP %1, %2").arg(statusCode).arg(reply->errorString());
        reply->deleteLater();

        if (attempt < kMaxRetries && (retryStatus || retryError)) {
            QThread::msleep(static_cast<unsigned long>(kRetryBackoffMs * (attempt + 1)));
            continue;
        }
        return false;
    }
    return false;
}
