#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <QDateTime>
#include <QDate>
#include <QHash>
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
    QString partOfSpeech;
    QString countabilityLabel;
    QString countabilityPlural;
    QString countabilityNotes;
    QString polysemyJson;
    double easeFactor = 2.5;
    int interval = 0;
    QDateTime nextReview;
    int status = 0;
    bool skipForever = false;
};

struct WordBookItem {
    int id = -1;
    QString name;
    int wordCount = 0;
    int learnedCount = 0;
    bool isActive = false;
    bool boundSpelling = false;
    bool boundCountability = false;
    bool boundPolysemy = false;
};

struct WordDebugStats {
    QDateTime nextReview;
    int attemptCount = 0;
    int correctCount = 0;
};

enum class SpellingResult {
    Mastered,
    Blurry,
    Unfamiliar,
};

enum class CountabilityAnswer {
    Countable = 0,
    Uncountable = 1,
    Both = 2,
};

struct LearningEvent {
    QDateTime eventTime;
    int wordId = -1;
    QString trainingType;
    SpellingResult result = SpellingResult::Unfamiliar;
    bool skipped = false;
    QString userInput;
};

struct DailyWordSummary {
    int wordId = -1;
    QString word;
    QString trainingType;
    int attempts = 0;
    SpellingResult lastResult = SpellingResult::Unfamiliar;
    QDateTime lastTime;
};

struct TrainingProgressDetail {
    QString trainingType;
    double easeFactor = 2.5;
    int interval = 0;
    QDateTime nextReview;
    int status = 0;
    int correctCount = 0;
    int wrongCount = 0;
    QDateTime updatedAt;
};

struct WordEventItem {
    QDateTime eventTime;
    QString trainingType;
    SpellingResult result = SpellingResult::Unfamiliar;
    bool skipped = false;
    QString userInput;
};

struct WordFullDetail {
    WordItem word;
    QVector<TrainingProgressDetail> progressByType;
    int spellingAttemptCount = 0;
    int spellingCorrectCount = 0;
    QDateTime spellingStatsUpdatedAt;
    QVector<WordBookItem> books;
    int totalEventCount = 0;
    QDateTime lastEventTime;
    QVector<WordEventItem> recentEvents;
};

struct PhraseItem {
    int id = -1;
    QString clusterZh;
    QStringList keywordsEn;
    QString theme;
    QStringList examLabels;
    QStringList examplesCn;
    double easeFactor = 2.5;
    int interval = 0;
    QDateTime nextReview;
    int status = 0;
};

struct PhraseBookItem {
    int id = -1;
    QString name;
    int itemCount = 0;
    int learnedCount = 0;
    bool isActive = false;
};

struct PhraseLearningEvent {
    QDateTime eventTime;
    int phraseId = -1;
    QString mode;
    bool correct = false;
    bool skipped = false;
    QString userInput;
    QString matchedAnswer;
};

struct PhraseDashboardStats {
    int activeBookId = -1;
    QString activeBookName;
    bool hasActiveBook = false;
    int totalCount = 0;
    int learnedCount = 0;
    int unlearnedCount = 0;
    int dueReviewCount = 0;
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
                       int countabilityColumn,
                       int pluralColumn,
                       int polysemyColumn,
                       int notesColumn,
                       const QString &trainingType,
                       int &importedCount);

    QVector<WordBookItem> fetchWordBooks(const QString &trainingType = QString()) const;
    bool setActiveWordBook(int bookId);
    bool deleteWordBook(int bookId);
    int activeWordBookId() const;

    int unlearnedCount() const;
    int dueReviewCount(const QDateTime &now = QDateTime::currentDateTime()) const;
    int countabilityUnlearnedCount() const;
    int countabilityDueReviewCount(const QDateTime &now = QDateTime::currentDateTime()) const;

    QVector<WordItem> fetchLearningBatch(int limit) const;
    QVector<WordItem> fetchReviewBatch(const QDateTime &now, int limit) const;
    QVector<WordItem> fetchCountabilityLearningBatch(int limit) const;
    QVector<WordItem> fetchCountabilityReviewBatch(const QDateTime &now, int limit) const;
    QVector<WordItem> fetchWordsForBook(int bookId) const;
    bool saveSessionProgress(const QString &mode, const QVector<WordItem> &words, int currentIndex);
    bool loadSessionProgress(const QString &mode, QVector<WordItem> &words, int &currentIndex);
    bool clearSessionProgress(const QString &mode);
    bool hasSessionProgress(const QString &mode) const;
    bool recordSpellingAttempt(int wordId, bool correct);
    bool recordLearningEvent(const LearningEvent &event);
    bool fetchWordDebugStats(int wordId, WordDebugStats &stats) const;
    QVector<DailyWordSummary> fetchDailyWordSummaries(const QDate &date,
                                                      const QString &trainingType = QStringLiteral("all")) const;
    int fetchDailyEventCount(const QDate &date, const QString &trainingType = QStringLiteral("all")) const;
    bool fetchWordFullDetail(int wordId, WordFullDetail &detail) const;
    QDate firstLearningEventDate() const;
    QHash<QDate, int> fetchStudyMinutesRange(const QDate &startDate, const QDate &endDate) const;
    bool setWordSkipForever(int wordId, bool skipForever = true);
    QVector<PhraseBookItem> fetchPhraseBooks() const;
    int activePhraseBookId() const;
    bool setActivePhraseBook(int bookId);
    bool createPhraseBook(const QString &name, int *newBookId = nullptr);
    bool deletePhraseBook(int bookId);
    bool importPhraseBookFromJson(const QString &jsonPath,
                                  int targetBookId,
                                  int &importedCount);
    bool importPhraseBookFromCsv(const QString &csvPath,
                                 int targetBookId,
                                 int &importedCount);
    QVector<PhraseItem> fetchPhraseLearningBatch(int limit) const;
    QVector<PhraseItem> fetchPhraseReviewBatch(const QDateTime &now, int limit) const;
    bool applyPhraseReviewResult(int phraseId,
                                 bool correct,
                                 bool skipped = false,
                                 const QDateTime &now = QDateTime::currentDateTime());
    bool recordPhraseLearningEvent(const PhraseLearningEvent &event);
    PhraseDashboardStats phraseDashboardStats(const QDateTime &now = QDateTime::currentDateTime()) const;

    SpellingResult evaluateSpelling(const QString &input, const QString &target) const;

    // 根据判定结果更新记忆状态。
    bool applyReviewResult(int wordId,
                           SpellingResult result,
                           bool skipped,
                           const QDateTime &now = QDateTime::currentDateTime());
    bool applyCountabilityResult(int wordId,
                                 bool correct,
                                 const QDateTime &now = QDateTime::currentDateTime());
    bool applyPolysemyResult(int wordId,
                             SpellingResult result,
                             bool skipped,
                             const QDateTime &now = QDateTime::currentDateTime());
    bool applyTrainingReviewResult(int wordId,
                                   const QString &trainingType,
                                   SpellingResult result,
                                   bool skipped,
                                   const QDateTime &now = QDateTime::currentDateTime());

    int activeBookIdForTraining(const QString &trainingType) const;
    bool setActiveBookIdForTraining(const QString &trainingType, int bookId);
    int lastDashboardCardIndex() const;
    bool setLastDashboardCardIndex(int index);
    bool isCsvPromptHandled() const;
    bool markCsvPromptHandled();

    int totalWordCountForTraining(const QString &trainingType) const;
    int masteredWordCountForTraining(const QString &trainingType) const;
    int unlearnedCountForTraining(const QString &trainingType) const;
    int dueReviewCountForTraining(const QString &trainingType,
                                  const QDateTime &now = QDateTime::currentDateTime()) const;
    QVector<WordItem> fetchLearningBatchForTraining(const QString &trainingType, int limit) const;
    QVector<WordItem> fetchReviewBatchForTraining(const QString &trainingType,
                                                  const QDateTime &now,
                                                  int limit) const;

    bool incrementDailyCount(bool isLearning, bool isCountability = false, const QDate &date = QDate::currentDate());
    bool addDailyStudySeconds(int seconds, bool isCountability = false, const QDate &date = QDate::currentDate());

    struct DailyLog {
        QString date;
        int learningCount = 0;
        int reviewCount = 0;
        int countabilityLearningCount = 0;
        int countabilityReviewCount = 0;
        int studyMinutes = 0;
        int spellingSeconds = 0;
        int countabilitySeconds = 0;
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
    bool isValidTrainingType(const QString &trainingType) const;
    bool upsertSetting(const QString &key, const QString &value);
    QString settingValue(const QString &key, const QString &defaultValue = QString()) const;
    QString activeBookNameForTraining(const QString &trainingType) const;
    bool isCountabilityCandidate(const WordItem &word) const;
    bool hasTrainingProgress(int wordId, const QString &trainingType) const;

    bool queryWordById(int wordId, WordItem &item) const;
    int activeWordBookIdInternal() const;
};

#endif // DATABASE_MANAGER_H
