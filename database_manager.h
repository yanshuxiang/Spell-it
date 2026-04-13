#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <QDateTime>
#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

class QSqlDatabase;

// 单词记录。
struct WordItem {
    int id = -1;
    QString word;
    QString phonetic;
    QString translation;
    double easeFactor = 2.5;
    int interval = 0;
    QDateTime nextReview;
    int status = 0;
};

struct WordBookItem {
    int id = -1;
    QString name;
    int wordCount = 0;
    bool isActive = false;
};

enum class SpellingResult {
    Mastered,
    Blurry,
    Unfamiliar,
};

class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();

    bool open(const QString &dbPath);
    bool initialize();

    // 读取 CSV 头与部分样例行，供列映射页面使用。
    bool readCsvPreview(const QString &csvPath,
                        QStringList &headers,
                        QVector<QStringList> &previewRows,
                        int sampleCount = 5) const;

    // 导入 CSV。wordColumn/translationColumn 必填，phoneticColumn 可选(-1 表示不用)。
    bool importFromCsv(const QString &csvPath,
                       int wordColumn,
                       int translationColumn,
                       int phoneticColumn,
                       int &importedCount);

    QVector<WordBookItem> fetchWordBooks() const;
    bool setActiveWordBook(int bookId);
    bool deleteWordBook(int bookId);
    int activeWordBookId() const;

    int unlearnedCount() const;
    int dueReviewCount(const QDateTime &now = QDateTime::currentDateTime()) const;

    QVector<WordItem> fetchLearningBatch(int limit = 20) const;
    QVector<WordItem> fetchReviewBatch(const QDateTime &now = QDateTime::currentDateTime(), int limit = 20) const;
    bool saveSessionProgress(const QString &mode, const QVector<WordItem> &words, int currentIndex);
    bool loadSessionProgress(const QString &mode, QVector<WordItem> &words, int &currentIndex);
    bool clearSessionProgress(const QString &mode);
    bool hasSessionProgress(const QString &mode) const;

    SpellingResult evaluateSpelling(const QString &input, const QString &target) const;

    // 根据判定结果更新记忆状态。
    bool applyReviewResult(int wordId,
                           SpellingResult result,
                           bool skipped,
                           const QDateTime &now = QDateTime::currentDateTime());

    bool incrementDailyCount(bool isLearning, const QDate &date = QDate::currentDate());

    struct DailyLog {
        QString date;
        int learningCount = 0;
        int reviewCount = 0;
    };
    QVector<DailyLog> fetchWeeklyLogs(const QDate &endDate = QDate::currentDate()) const;
    bool reconcileFirstDayDailyLog(const QDate &date = QDate::currentDate());

    QString lastError() const;

private:
    QString connectionName_;
    mutable QString lastError_;

    bool ensureDatabaseOpen() const;
    QSqlDatabase database() const;

    static QStringList parseCsvLine(const QString &line);
    static QString normalizedToken(const QString &text);

    int levenshteinDistance(const QString &a, const QString &b) const;

    int nextIntervalForMastered(int currentInterval) const;
    int nextIntervalForBlurry(int currentInterval) const;
    int nextIntervalForUnfamiliar() const;

    bool queryWordById(int wordId, WordItem &item) const;
    int activeWordBookIdInternal() const;
};

#endif // DATABASE_MANAGER_H
