#ifndef POS_DOWNLOADER_H
#define POS_DOWNLOADER_H

#include "database_manager.h"

#include <QString>
#include <QVector>
#include <functional>

class QUrl;

class PosDownloader {
public:
    struct Update {
        int wordId = -1;
        QString word;
        QString partOfSpeech;
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

    using ProgressCallback = std::function<void(int current, int total, const QString &word)>;
    using CancelChecker = std::function<bool()>;

    PosDownloader();

    Result downloadPartOfSpeechForWords(const QVector<WordItem> &words,
                                        const ProgressCallback &onProgress,
                                        const CancelChecker &shouldCancel,
                                        QString &errorText) const;

private:
    static QString extractPartOfSpeech(const QByteArray &jsonData);

    bool fetchUrl(const QUrl &url,
                  QByteArray &data,
                  QString &errorText,
                  int timeoutMs) const;
};

#endif // POS_DOWNLOADER_H
