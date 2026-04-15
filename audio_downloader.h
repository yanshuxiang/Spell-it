#ifndef AUDIO_DOWNLOADER_H
#define AUDIO_DOWNLOADER_H

#include "database_manager.h"

#include <QString>
#include <QVector>
#include <functional>

class QUrl;

class AudioDownloader {
public:
    struct Result {
        int totalWords = 0;
        int resumeStartIndex = 0;
        int downloaded = 0;
        int reused = 0;
        int noMp3 = 0;
        int failed = 0;
        bool cancelled = false;
    };
    using ProgressCallback = std::function<void(int current, int total, const QString &word)>;
    using CancelChecker = std::function<bool()>;

    AudioDownloader();

    Result downloadBookAudio(const QVector<WordItem> &words,
                             int bookId,
                             const ProgressCallback &onProgress,
                             const CancelChecker &shouldCancel,
                             QString &errorText) const;

private:
    QString audioDirPath() const;
    QString progressFilePath(int bookId) const;

    int loadLastCompletedIndex(int bookId) const;
    bool saveLastCompletedIndex(int bookId, int index) const;
    void clearProgress(int bookId) const;

    static QString safeAudioFileName(const QString &word);
    static QString extractMp3Url(const QByteArray &jsonData);

    bool fetchUrl(const QUrl &url,
                  QByteArray &data,
                  QString &errorText,
                  int timeoutMs) const;
    bool writeAudioAtomically(const QString &finalPath,
                              const QByteArray &data,
                              QString &errorText) const;
};

#endif // AUDIO_DOWNLOADER_H
