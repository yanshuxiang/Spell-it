#ifndef GUI_WIDGETS_H
#define GUI_WIDGETS_H

#include <QHash>
#include <QRect>
#include <QWidget>

#include "database_manager.h"

class QLabel;
class QComboBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QCloseEvent;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QVBoxLayout;
class QGraphicsOpacityEffect;

struct PracticeRecord {
    WordItem word;
    SpellingResult result = SpellingResult::Unfamiliar;
    QString userInput;
    bool skipped = false;
};

class HomePageWidget : public QWidget {
    Q_OBJECT
public:
    explicit HomePageWidget(QWidget *parent = nullptr);

    void setCounts(int learningCount,
                   int reviewCount,
                   int todayLearningCount,
                   int todayReviewCount);

signals:
    void startLearningClicked();
    void startReviewClicked();
    void booksClicked();
    void statsClicked();

private:
    QLabel *learningCountLabel_ = nullptr;
    QLabel *reviewCountLabel_ = nullptr;
    QPushButton *learningButton_ = nullptr;
    QPushButton *reviewButton_ = nullptr;
};

class MappingPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit MappingPageWidget(QWidget *parent = nullptr);

    void setCsvData(const QString &csvPath,
                    const QStringList &headers,
                    const QVector<QStringList> &previewRows);

signals:
    void importConfirmed(int wordColumn, int translationColumn, int phoneticColumn);
    void cancelled();

private:
    QLabel *filePathLabel_ = nullptr;
    QComboBox *wordCombo_ = nullptr;
    QComboBox *translationCombo_ = nullptr;
    QComboBox *phoneticCombo_ = nullptr;
    QTableWidget *previewTable_ = nullptr;
};

class SpellingPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SpellingPageWidget(QWidget *parent = nullptr);

    void setWord(const WordItem &word, int currentIndex, int totalCount, bool isReviewMode);
    void setInputEnabled(bool enabled);
    void showFeedback(const QString &text, const QString &colorHex);
    void clearFeedback();

signals:
    void submitted(const QString &text);
    void skipped();
    void exitRequested();
    void proceedRequested();
    void userActivity();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setAwaitingProceed(bool awaiting);

    QLabel *modeLabel_ = nullptr;
    QLabel *progressLabel_ = nullptr;
    QLabel *translationLabel_ = nullptr;
    QLineEdit *inputEdit_ = nullptr;
    QLabel *feedbackLabel_ = nullptr;
    QPushButton *exitButton_ = nullptr;
    QPushButton *skipButton_ = nullptr;
    bool awaitingProceed_ = false;
    bool proceedKeyArmed_ = false;
};

class SummaryPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SummaryPageWidget(QWidget *parent = nullptr);

    void setSummary(const QVector<PracticeRecord> &records, bool reviewMode);

signals:
    void backHomeClicked();
    void nextGroupClicked();

private:
    QLabel *topTipLabel_ = nullptr;
    QLabel *accuracyLabel_ = nullptr;
    QLabel *statsLabel_ = nullptr;
    QListWidget *wrongWordsList_ = nullptr;
    QLabel *footerLabel_ = nullptr;
    QPushButton *backHomeButton_ = nullptr;
    QPushButton *nextGroupButton_ = nullptr;
    bool reviewMode_ = false;
};

class StatisticsPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit StatisticsPageWidget(QWidget *parent = nullptr);

    void setLogs(const QVector<DatabaseManager::DailyLog> &logs);

signals:
    void backClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct HoverBarInfo {
        QRect rect;
        QString text;
    };

    QPushButton *backButton_ = nullptr;
    QLabel *hoverTip_ = nullptr;
    QVector<DatabaseManager::DailyLog> logs_;
    QVector<HoverBarInfo> hoverBars_;
    int hoveredBarIndex_ = -1;
};

class WordBooksPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit WordBooksPageWidget(QWidget *parent = nullptr);

    void setWordBooks(const QVector<WordBookItem> &books, int activeBookId);

signals:
    void backClicked();
    void addBookClicked();
    void wordBookSelected(int bookId);
    void wordBookDeleteRequested(int bookId);

private:
    void rebuildList();

    QVector<WordBookItem> books_;
    int activeBookId_ = -1;
    QLabel *metaLabel_ = nullptr;
    QLabel *currentTitleLabel_ = nullptr;
    QWidget *currentCardHost_ = nullptr;
    QVBoxLayout *currentCardLayout_ = nullptr;
    QLabel *otherTitleLabel_ = nullptr;
    QListWidget *booksList_ = nullptr;
    QPushButton *backButton_ = nullptr;
    QPushButton *addBookButton_ = nullptr;
};

class VibeSpellerWindow : public QWidget {
    Q_OBJECT
public:
    explicit VibeSpellerWindow(QWidget *parent = nullptr);

private slots:
    void onStartLearning();
    void onStartReview();
    void onSubmitAnswer(const QString &text);
    void onProceedAfterFeedback();
    void onSkipWord();
    void onExitSession();
    void onOpenWordBooks();
    void onSelectWordBook(int bookId);
    void onDeleteWordBook(int bookId);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    enum class SessionMode {
        Learning,
        Review,
    };

    void initializeDatabase();
    void refreshHomeCounts();
    void refreshWordBooks();
    void updateStudyTimeTracking();
    void flushStudyTimeTracking();
    void markStudyUserActivity();
    QDateTime effectiveStudyTrackingEndTime(const QDateTime &now) const;
    void accumulateStudyDuration(const QDateTime &start, const QDateTime &end);
    void requestCsvImportIfNeeded();
    bool pickCsvAndShowMapping(bool returnToWordBooks = false);
    bool tryResumeSession(SessionMode mode);

    void startSession(SessionMode mode, QVector<WordItem> words, int startIndex = 0);
    void showCurrentWord();
    void persistCurrentSession();
    void clearSessionForMode(SessionMode mode);
    void moveToNextWord();
    void finishSession();
    void continueNextGroup();
    QString modeKey(SessionMode mode) const;

    QString resultLabel(SpellingResult result) const;
    QString resultColor(SpellingResult result) const;

    DatabaseManager db_;

    QStackedWidget *stack_ = nullptr;
    HomePageWidget *homePage_ = nullptr;
    MappingPageWidget *mappingPage_ = nullptr;
    SpellingPageWidget *spellingPage_ = nullptr;
    SummaryPageWidget *summaryPage_ = nullptr;
    StatisticsPageWidget *statisticsPage_ = nullptr;
    WordBooksPageWidget *wordBooksPage_ = nullptr;

    QString pendingCsvPath_;
    bool returnToWordBooksAfterImport_ = false;
    QVector<WordItem> currentWords_;
    QVector<PracticeRecord> records_;
    QHash<int, int> roundMistakeCounts_;
    int sessionWordTargetCount_ = 0;
    int currentIndex_ = 0;
    SessionMode currentMode_ = SessionMode::Learning;
    bool isStudyTrackingActive_ = false;
    QDateTime studyTrackingStartTime_;
    QDateTime lastStudyUserActionTime_;
    QTimer *studyIdleTimer_ = nullptr;
};

#endif // GUI_WIDGETS_H
