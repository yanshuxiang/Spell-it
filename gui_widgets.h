#ifndef GUI_WIDGETS_H
#define GUI_WIDGETS_H

#include <QHash>
#include <QRect>
#include <QWidget>
#include <atomic>

#include "database_manager.h"

class QLabel;
class QComboBox;
class QTabBar;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QCloseEvent;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QResizeEvent;
class HoverScaleButton;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QVBoxLayout;
class QHBoxLayout;
class QSpacerItem;
class QFrame;
class QGridLayout;
class QTextBrowser;
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
    PhraseClusterLearning,
    PhraseClusterReview,
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
    bool changeBookEnabled = true;
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
    void startPhraseClusterLearningClicked();
    void startPhraseClusterReviewClicked();
    void changeBookRequested(const QString &trainingType);
    void dashboardIndexChanged(int index);
    void booksClicked();
    void calendarClicked();
    void statsClicked();
    void managementClicked();

private:
    void handleStartRequest(int modeIndex, bool isReview, const QRect &globalRect);
    void handleChangeBookRequest(int modeIndex);
    void handleCurrentIndexChanged(int index);
    void handleCalendarRequest();
    void handleStatsRequest();
    void handleManagementRequest();
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
                    const QVector<QStringList> &previewRows,
                    const QString &trainingType);

signals:
    void importConfirmed(int wordColumn, int translationColumn, int phoneticColumn,
                         int countabilityColumn, int pluralColumn, int notesColumn, int polysemyColumn);
    void cancelled();

private:
    void applyTrainingTypeVisibility();

    QString currentTrainingType_;
    QLabel *filePathLabel_ = nullptr;
    QWidget *wordRow_ = nullptr;
    QWidget *translationRow_ = nullptr;
    QWidget *phoneticRow_ = nullptr;
    QWidget *countabilityRow_ = nullptr;
    QWidget *pluralRow_ = nullptr;
    QWidget *notesRow_ = nullptr;
    QWidget *polysemyRow_ = nullptr;
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
    HoverScaleButton *skipForeverButton_ = nullptr;
    QLabel *skipForeverTip_ = nullptr;
    QWidget *debugHost_ = nullptr;
    QLabel *debugScheduleLabel_ = nullptr;
    QLabel *debugAccuracyLabel_ = nullptr;
    HoverScaleButton *exitButton_ = nullptr;
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

    HoverScaleButton *exitButton_ = nullptr;
    HoverScaleButton *countableButton_ = nullptr;
    HoverScaleButton *uncountableButton_ = nullptr;
    HoverScaleButton *bothButton_ = nullptr;
    HoverScaleButton *buttonForAnswer(CountabilityAnswer answer) const;

    QWidget *usageDetailsHost_ = nullptr;
    QLabel *usageDetailCorrectLabel_ = nullptr;
    QLabel *usageDetailNotesLabel_ = nullptr;
    HoverScaleButton *usageContinueButton_ = nullptr;

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
    void showDetail(const WordItem &word, SpellingResult selectedResult);
    void setOptionsEnabled(bool enabled);
    void resetRevealState();

signals:
    void exitRequested();
    void revealRequested();
    void ratingSubmitted(SpellingResult result);
    void continueRequested();
    void userActivity();

private:
    QString buildPolysemyDetailHtml(const WordItem &word) const;
    QString ratingText(SpellingResult result) const;

    QLabel *modeLabel_ = nullptr;
    QLabel *progressLabel_ = nullptr;
    QStackedWidget *stageStack_ = nullptr;
    QWidget *quizPage_ = nullptr;
    QWidget *detailPage_ = nullptr;
    QLabel *translationLabel_ = nullptr;
    QLabel *detailWordLabel_ = nullptr;
    QLabel *detailRatingLabel_ = nullptr;
    QTextBrowser *detailBrowser_ = nullptr;
    HoverScaleButton *continueButton_ = nullptr;
    HoverScaleButton *exitButton_ = nullptr;
    HoverScaleButton *masteredButton_ = nullptr;
    HoverScaleButton *blurryButton_ = nullptr;
    HoverScaleButton *unfamiliarButton_ = nullptr;
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
    HoverScaleButton *backHomeButton_ = nullptr;
    HoverScaleButton *nextGroupButton_ = nullptr;
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

    HoverScaleButton *backButton_ = nullptr;
    QLabel *hoverTip_ = nullptr;
    
    QVector<DatabaseManager::DailyLog> logs_;
    QVector<HoverBarInfo> hoverBars_;
    int hoveredBarIndex_ = -1;
};

class PhraseClusterPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit PhraseClusterPageWidget(QWidget *parent = nullptr);
    void setDatabaseManager(DatabaseManager *db);
    void openMode(bool reviewMode);
    void openManagementPanel();

signals:
    void backClicked();
    void userActivity();

private:
    void setManagementOnlyView(bool managementOnly);
    void refreshManagementButtonsLayout();
    void rebuildPhraseBookManagement(const QVector<PhraseBookItem> &books, int activeBookId);
    void refreshBooks();
    void reloadSession();
    void showCurrentPhrase();
    QString normalizedAnswer(const QString &text) const;
    QString tryMatchAnswer(const PhraseItem &item, const QString &input) const;

    DatabaseManager *db_ = nullptr;
    bool reviewMode_ = false;
    bool managementOnlyView_ = false;
    bool currentAnswered_ = false;
    int currentIndex_ = -1;
    int sessionSize_ = 10;
    int correctCount_ = 0;
    int wrongCount_ = 0;
    QVector<PhraseItem> currentBatch_;

    HoverScaleButton *backButton_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *titleMetaLabel_ = nullptr;
    QFrame *controlPanel_ = nullptr;
    QTabBar *modeTabs_ = nullptr;
    QComboBox *bookCombo_ = nullptr;
    QComboBox *sessionSizeCombo_ = nullptr;
    HoverScaleButton *manageButton_ = nullptr;
    QFrame *managementPanel_ = nullptr;
    QHBoxLayout *managementButtonsLayout_ = nullptr;
    HoverScaleButton *addBookButton_ = nullptr;
    HoverScaleButton *deleteBookButton_ = nullptr;
    HoverScaleButton *importJsonButton_ = nullptr;
    HoverScaleButton *importCsvButton_ = nullptr;
    QFrame *trainPanel_ = nullptr;
    QWidget *bookManageView_ = nullptr;
    QLabel *manageCurrentTitle_ = nullptr;
    QWidget *manageCurrentHost_ = nullptr;
    QVBoxLayout *manageCurrentLayout_ = nullptr;
    QLabel *manageOtherTitle_ = nullptr;
    QListWidget *manageOtherList_ = nullptr;
    HoverScaleButton *manageAddButton_ = nullptr;
    QLabel *progressLabel_ = nullptr;
    QLabel *clusterLabel_ = nullptr;
    QLabel *metaLabel_ = nullptr;
    QLabel *exampleLabel_ = nullptr;
    QLineEdit *answerEdit_ = nullptr;
    QLabel *feedbackLabel_ = nullptr;
    HoverScaleButton *submitButton_ = nullptr;
    HoverScaleButton *skipButton_ = nullptr;
    HoverScaleButton *nextButton_ = nullptr;
};

class CalendarPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit CalendarPageWidget(QWidget *parent = nullptr);

    void setMonth(const QDate &monthAnchor, const QHash<QDate, int> &studyMinutesByDate);
    void setDailySummaries(const QDate &date,
                           int totalEvents,
                           const QVector<DailyWordSummary> &summaries,
                           const QDate &eventStartDate);
    QDate currentMonth() const;
    QString selectedTrainingFilter() const;
    QDate selectedDate() const;
    void setTrainingFilter(const QString &trainingType);
    void syncLayoutForAnimation();

signals:
    void backClicked();
    void monthChanged(const QDate &monthAnchor);
    void daySelected(const QDate &date);
    void trainingFilterChanged(const QString &trainingType);
    void wordDetailRequested(int wordId);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    class CalendarCellButton;
    void updateCalendarGeometry();

    HoverScaleButton *backButton_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    HoverScaleButton *prevButton_ = nullptr;
    HoverScaleButton *nextButton_ = nullptr;
    QWidget *calendarPanel_ = nullptr;
    QFrame *drawerFrame_ = nullptr;
    QGridLayout *calendarGrid_ = nullptr;
    QLabel *selectedDateLabel_ = nullptr;
    QLabel *eventCountLabel_ = nullptr;
    QTabBar *trainingFilterTabs_ = nullptr;
    QListWidget *dailyList_ = nullptr;
    QLabel *emptyLabel_ = nullptr;

    QVector<CalendarCellButton *> dayButtons_;
    QVector<QDate> cellDates_;
    QHash<QDate, int> studyMinutesByDate_;
    QDate currentMonth_;
    QDate selectedDate_;
    QDate eventStartDate_;
};

class WordDetailPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit WordDetailPageWidget(QWidget *parent = nullptr);

    void setLoading(int wordId);
    void setError(const QString &message);
    void setDetail(const WordFullDetail &detail);

signals:
    void backClicked();

private:
    void renderDetailHtml();

    HoverScaleButton *backButton_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QTextBrowser *contentLabel_ = nullptr;
    WordFullDetail detail_;
    bool hasDetailData_ = false;
    bool jsonExpanded_ = false;
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
    bool isManagementMode_ = false;
    QString currentTrainingType_;
    QString currentTrainingDisplayName_;
    QLabel *titleLabel_ = nullptr;
    QLabel *metaLabel_ = nullptr;
    QLabel *currentTitleLabel_ = nullptr;
    QWidget *currentCardHost_ = nullptr;
    QVBoxLayout *currentCardLayout_ = nullptr;
    QLabel *otherTitleLabel_ = nullptr;
    QListWidget *booksList_ = nullptr;
    HoverScaleButton *backButton_ = nullptr;
    HoverScaleButton *addBookButton_ = nullptr;
    QWidget *audioStatusHost_ = nullptr;
    QLabel *audioStatusLabel_ = nullptr;
    RoundedProgressStrip *audioProgressBar_ = nullptr;
    HoverScaleButton *audioStopButton_ = nullptr;
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
    void onStartPhraseClusterLearning();
    void onStartPhraseClusterReview();
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
    void onOpenCalendar();
    void onCalendarMonthChanged(const QDate &monthAnchor);
    void onCalendarDaySelected(const QDate &date);
    void onCalendarFilterChanged(const QString &trainingType);
    void onCalendarWordDetailRequested(int wordId);
    void onWordDetailBack();
    void onPhraseClusterBack();

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
    void animateStatisticsPageRise();
    void animateStatisticsPageBack();
    void animateCalendarRise();
    void animateCalendarBack();
    void animateWordBooksRise(const QString &trainingType);
    void animateWordBooksBack();
    void refreshCalendarMonthData();
    void refreshCalendarDayData();
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
    PhraseClusterPageWidget *phraseClusterPage_ = nullptr;
    CalendarPageWidget *calendarPage_ = nullptr;
    WordDetailPageWidget *wordDetailPage_ = nullptr;
    WordBooksPageWidget *wordBooksPage_ = nullptr;

    QString pendingCsvPath_;
    QString pendingImportTrainingType_;
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
    int pendingDashboardIndexToSave_ = 0;
    bool inTransition_ = false;
    QString calendarFilterType_ = QStringLiteral("all");
};

#endif // GUI_WIDGETS_H
