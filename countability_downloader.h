#ifndef COUNTABILITY_DOWNLOADER_H
#define COUNTABILITY_DOWNLOADER_H

#include "database_manager.h"

#include <QString>
#include <QVector>
#include <functional>

class QUrl;

class CountabilityDownloader {
public:
    struct Update {
        int wordId = -1;
        QString word;
        QString label;   // C / U / B / NA
        QString source;
    };

    struct Result {
        int totalWords = 0;
        int processed = 0;
        int updated = 0;
        int skipped = 0;
        int failed = 0;
        bool cancelled = false;
        QVector<Update> updates;
    };

    using ProgressCallback = std::function<void(int current, int total, const QString &word, const Update &update)>;
    using CancelChecker = std::function<bool()>;

    CountabilityDownloader();

    Result downloadCountabilityForWords(const QVector<WordItem> &words,
                                        const ProgressCallback &onProgress,
                                        const CancelChecker &shouldCancel,
                                        QString &errorText) const;

private:
    static QString detectCountability(const QString &htmlText);
    static QString extractFirstDefinitionPath(const QString &searchHtml);
    static QString normalizeHtmlText(const QString &htmlText);

    bool fetchUrl(const QUrl &url,
                  QByteArray &data,
                  QString &errorText,
                  int timeoutMs) const;
};

#endif // COUNTABILITY_DOWNLOADER_H

