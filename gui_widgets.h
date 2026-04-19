#ifndef GUI_WIDGETS_H
#define GUI_WIDGETS_H

#include <QHash>
#include <QRect>
#include <QWidget>
#include <atomic>

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
class QSpacerItem;
class QProcess;
class QThread;
class RoundedProgressStrip;
class QParallelAnimationGroup;
class QQuickWidget;

enum class SessionMode {
    Learning,
    Review,
    CountabilityLearning,
    CountabilityReview,
    PolysemyLearning,
    PolysemyReview,
};

struct PracticeRecord {
    WordItem word;
    SpellingResult result = SpellingResult::Unfamiliar;
    QString userInput;
    bool skipped = false;
};

struct DashboardCardState {
    QString trainingType;
    QString modeTitle;
    QString themeColor;
    QString bookName;
    QString coverName;
    int totalWords = 0;
    int masteredWords = 0;
    int unlearnedCount = 0;
    int dueReviewCount = 0;
    bool hasActiveBook = false;
    bool learningEnabled = false;
    bool reviewEnabled = false;
    int activeBookId = -1;
};

class HomePageWidget : public QWidget {
    Q_OBJECT
public:
    explicit HomePageWidget(QWidget *parent = nullptr);

    void setDashboardCards(const QVector<DashboardCardState> &cards,
                           int currentIndex,
                           int todayLearningCount,
                           int todayReviewCount,
                           int todayStudyMinutes);
    QRect launchRect(SessionMode mode) const;
    int currentCardIndex() const;
    void focusDashboard();

signals:
    void startLearningClicked();
    void startReviewClicked();
    void startCountabilityLearningClicked();
    void startCountabilityReviewClicked();
    void startPolysemyLearningClicked();
    void startPolysemyReviewClicked();
    void changeBookRequested(const QString &trainingType);
    void dashboardIndexChanged(int index);
    void booksClicked();
    void statsClicked();

private:
    void handleStartRequest(int modeIndex, bool isReview, const QRect &globalRect);
    void handleChangeBookRequest(int modeIndex);
    void handleCurrentIndexChanged(int index);
    void handleStatsRequest();
    void updateCardModel();

    QVector<DashboardCardState> cards_;
    QHash<int, QRect> launchRectByMode_;
    int currentCardIndex_ = 0;
    QLabel *topMetaLabel_ = nullptr;
    QLabel *footerLabel_ = nullptr;
    QQuickWidget *dashboardView_ = nullptr;
    QObject *dashboardBridge_ = nullptr;
};

class MappingPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit MappingPageWidget(QWidget *parent = nullptr);

    void setCsvData(const QString &csvPath,
                    const QStringList &headers,
                    const QVector<QStringList> &previewRows);

signals:
    void importConfirmed(int wordColumn, int translationColumn, int phoneticColumn,
                         int countabilityColumn, int pluralColumn, int notesColumn, int polysemyColumn);
    void cancelled();

private:
    QLabel *filePathLabel_ = nullptr;
    QComboBox *wordCombo_ = nullptr;
    QComboBox *translationCombo_ = nullptr;
    QComboBox *phoneticCombo_ = nullptr;
    QComboBox *countabilityCombo_ = nullptr;
    QComboBox *pluralCombo_ = nullptr;
    QComboBox *notesCombo_ = nullptr;
    QComboBox *polysemyCombo_ = nullptr;
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
    void setDebugMode(bool enabled);
    void setDebugInfo(const QDateTime &nextReview, int attemptCount, int correctCount);
    void clearDebugInfo();
    void playCorrectTransition(const WordItem &currentWord,
                               const WordItem &nextWord,
                               int nextIndex,
                               int totalCount,
                               bool isReviewMode);
    void playWrongShake();

signals:
    void submitted(const QString &text);
    void exitRequested();
    void skipForeverRequested();
    void proceedRequested();
    void userActivity();
    void correctTransitionFinished();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setAwaitingProceed(bool awaiting);
    void applyInputDefaultStyle();
    void refreshAnimationBasePositions();
    void showSkipForeverTip(const QPoint &anchorPos);
    void hideSkipForeverTip();

    QLabel *modeLabel_ = nullptr;
    QLabel *progressLabel_ = nullptr;
    QLabel *translationLabel_ = nullptr;
    QLineEdit *inputEdit_ = nullptr;
    QLabel *feedbackLabel_ = nullptr;
    QPushButton *skipForeverButton_ = nullptr;
    QLabel *skipForeverTip_ = nullptr;
    QWidget *debugHost_ = nullptr;
    QLabel *debugScheduleLabel_ = nullptr;
    QLabel *debugAccuracyLabel_ = nullptr;
    QPushButton *exitButton_ = nullptr;
    bool debugMode_ = false;
    bool awaitingProceed_ = false;
    bool proceedKeyArmed_ = false;
    bool inTransition_ = false;
    bool resetInputOnNextType_ = false;
    QPoint translationBasePos_;
    QPoint inputBasePos_;
};

class CountabilityPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit CountabilityPageWidget(QWidget *parent = nullptr);

    void setWord(const WordItem &word, int currentIndex, int totalCount, bool isReviewMode);

    void setOptionsEnabled(bool enabled);
    void resetOptionStyles();
    void showAnswerFeedback(CountabilityAnswer selected, CountabilityAnswer correct, bool isCorrect);
    void showDetailedFeedback(const WordItem &word, CountabilityAnswer correct, CountabilityAnswer selected);

signals:
    void exitRequested();
    void answerSubmitted(CountabilityAnswer answer);
    void continueRequested();
    void userActivity();

private:
    QLabel *modeLabel_ = nullptr;
    QLabel *progressLabel_ = nullptr;
    QLabel *wordLabel_ = nullptr;
    QLabel *hintLabel_ = nullptr;

    QPushButton *exitButton_ = nullptr;
    QPushButton *countableButton_ = nullptr;
    QPushButton *uncountableButton_ = nullptr;
    QPushButton *bothButton_ = nullptr;
    QPushButton *buttonForAnswer(CountabilityAnswer answer) const;

    QWidget *usageDetailsHost_ = nullptr;
    QLabel *usageDetailCorrectLabel_ = nullptr;
    QLabel *usageDetailNotesLabel_ = nullptr;
    QPushButton *usageContinueButton_ = nullptr;

    // 布局与动画辅助
    QVBoxLayout *rootLayout_ = nullptr;
    QVBoxLayout *contentLayout_ = nullptr;
    QWidget *contentHost_ = nullptr;
    QSpacerItem *topSpacer_ = nullptr;
    QSpacerItem *bottomSpacer_ = nullptr;
    QParallelAnimationGroup *detailsTransitionGroup_ = nullptr;
    bool isDetailsMode_ = false;

    void refreshBasePositions();
};

class PolysemyPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit PolysemyPageWidget(QWidget *parent = nullptr);

    void setWord(const WordItem &word, int currentIndex, int totalCount, bool isReviewMode);
    void setOptionsEnabled(bool enabled);
    void resetRevealState();

signals:
    void exitRequested();
    void revealRequested();
    void ratingSubmitted(SpellingResult result);
    void continueRequested();
    void userActivity();

private:
    QString buildPolysemyText(const WordItem &word) const;

    QLabel *modeLabel_ = nullptr;
    QLabel *progressLabel_ = nullptr;
    QLabel *wordLabel_ = nullptr;
    QLabel *meaningLabel_ = nullptr;
    QPushButton *exitButton_ = nullptr;
    QPushButton *revealButton_ = nullptr;
    QPushButton *masteredButton_ = nullptr;
    QPushButton *blurryButton_ = nullptr;
    QPushButton *unfamiliarButton_ = nullptr;
    bool revealed_ = false;
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

    void setWordBooks(const QVector<WordBookItem> &books,
                      int activeBookId,
                      const QString &trainingType,
                      const QString &trainingDisplayName);
    void setAudioDownloadStatus(const QString &text, int current, int total, bool running);

signals:
    void backClicked();
    void addBookClicked();
    void wordBookSelected(int bookId);
    void wordBookDeleteRequested(int bookId);
    void downloadAudioRequested(int bookId);
    void audioDownloadStopRequested();

private:
    void rebuildList();
    QString bindingTagText(const WordBookItem &book) const;

    QVector<WordBookItem> books_;
    int activeBookId_ = -1;
    QString currentTrainingType_;
    QString currentTrainingDisplayName_;
    QLabel *metaLabel_ = nullptr;
    QLabel *currentTitleLabel_ = nullptr;
    QWidget *currentCardHost_ = nullptr;
    QVBoxLayout *currentCardLayout_ = nullptr;
    QLabel *otherTitleLabel_ = nullptr;
    QListWidget *booksList_ = nullptr;
    QPushButton *backButton_ = nullptr;
    QPushButton *addBookButton_ = nullptr;
    QWidget *audioStatusHost_ = nullptr;
    QLabel *audioStatusLabel_ = nullptr;
    RoundedProgressStrip *audioProgressBar_ = nullptr;
    QPushButton *audioStopButton_ = nullptr;
};

class VibeSpellerWindow : public QWidget {
    Q_OBJECT
public:
    explicit VibeSpellerWindow(QWidget *parent = nullptr);

private slots:
    void onStartLearning();
    void onStartReview();
    void onStartCountabilityLearning();
    void onStartCountabilityReview();
    void onStartPolysemyLearning();
    void onStartPolysemyReview();
    void onSubmitAnswer(const QString &text);
    void onCountabilityAnswer(CountabilityAnswer answer);
    void onPolysemyRated(SpellingResult result);
    void onProceedAfterFeedback();
    void onExitSession();
    void onSkipForeverCurrentWord();
    void onOpenWordBooks();
    void onChangeBookForTraining(const QString &trainingType);
    void onSelectWordBook(int bookId);
    void onDeleteWordBook(int bookId);
    void onDownloadAudio(int bookId);
    void onAudioDownloadStopRequested();

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:

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
    void playPronunciationForWord(const QString &word);
    qreal computeNormalizedVolume(const QString &audioFilePath);
    void animateHomeToPageTransition(const QRect &sourceRect, QWidget *targetPage);
    void animatePageToHomeTransition(QWidget *sourcePage, const QRect &targetRect);
    void applyRoundedWindowMask();

    void startSession(SessionMode mode, QVector<WordItem> words, int startIndex = 0);
    void showCurrentWord();
    void updateSpellingDebugInfo(int wordId);
    void persistCurrentSession();
    void clearSessionForMode(SessionMode mode);
    void moveToNextWord();
    void moveToNextCountabilityWord();
    void finishSession();
    void continueNextGroup();
    QString modeKey(SessionMode mode) const;
    QString trainingTypeForMode(SessionMode mode) const;
    QString trainingDisplayName(const QString &trainingType) const;
    SessionMode reviewModeForTraining(const QString &trainingType) const;
    SessionMode learningModeForTraining(const QString &trainingType) const;
    void rememberHomeCardIndex();
    void restoreHomeCardIndex(bool refreshCounts);
    void openWordBooksForTraining(const QString &trainingType);
    bool ensureActiveBookForTraining(const QString &trainingType, const QString &title);
    SessionMode modeForDashboardRequest(int modeIndex, bool isReview) const;

    QString resultLabel(SpellingResult result) const;
    QString resultColor(SpellingResult result) const;

    DatabaseManager db_;

    QStackedWidget *stack_ = nullptr;
    HomePageWidget *homePage_ = nullptr;
    MappingPageWidget *mappingPage_ = nullptr;
    SpellingPageWidget *spellingPage_ = nullptr;
    CountabilityPageWidget *countabilityPage_ = nullptr;
    PolysemyPageWidget *polysemyPage_ = nullptr;
    SummaryPageWidget *summaryPage_ = nullptr;
    StatisticsPageWidget *statisticsPage_ = nullptr;
    WordBooksPageWidget *wordBooksPage_ = nullptr;

    QString pendingCsvPath_;
    bool returnToWordBooksAfterImport_ = false;
    QVector<WordItem> currentWords_;
    QVector<PracticeRecord> records_;
    QHash<int, int> roundMistakeCounts_;
    QHash<int, QString> firstWrongInputs_;
    QHash<int, int> countabilityWrongCounts_;
    int sessionWordTargetCount_ = 0;
    int currentIndex_ = 0;
    QRect pendingHomeLaunchRect_;
    SessionMode currentMode_ = SessionMode::Learning;
    QString pendingBookSelectionTrainingType_;
    bool isStudyTrackingActive_ = false;
    QDateTime studyTrackingStartTime_;
    QDateTime lastStudyUserActionTime_;
    QTimer *studyIdleTimer_ = nullptr;
    bool audioDownloadRunning_ = false;
    std::atomic_bool audioDownloadCancelRequested_ {false};
    QThread *audioDownloadThread_ = nullptr;
    bool debugMode_ = false;
    QProcess *pronunciationProcess_ = nullptr;
    QHash<QString, qreal> pronunciationVolumeCache_;
    int preservedHomeCardIndex_ = -1;
};

#endif // GUI_WIDGETS_H
