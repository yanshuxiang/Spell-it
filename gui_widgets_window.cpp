#include "gui_widgets.h"
#include "gui_widgets_internal.h"
#include "audio_downloader.h"
#include "app_logger.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPointer>
#include <QRegularExpression>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

using namespace GuiWidgetsInternal;

namespace {
constexpr int kUnifiedCornerRadiusPx = 18;
constexpr int kPageLaunchDurationMs = 300;
constexpr int kCardBorderPx = 2;
constexpr int kCountabilityFeedbackMs = 450;

QString countabilityAnswerText(CountabilityAnswer answer) {
    switch (answer) {
    case CountabilityAnswer::Countable:
        return QStringLiteral("可数");
    case CountabilityAnswer::Uncountable:
        return QStringLiteral("不可数");
    case CountabilityAnswer::Both:
        return QStringLiteral("可数且不可数");
    }
    return QStringLiteral("可数");
}

QString sessionModeName(SessionMode mode) {
    switch (mode) {
    case SessionMode::Learning:
        return QStringLiteral("spelling_learning");
    case SessionMode::Review:
        return QStringLiteral("spelling_review");
    case SessionMode::CountabilityLearning:
        return QStringLiteral("countability_learning");
    case SessionMode::CountabilityReview:
        return QStringLiteral("countability_review");
    case SessionMode::PolysemyLearning:
        return QStringLiteral("polysemy_learning");
    case SessionMode::PolysemyReview:
        return QStringLiteral("polysemy_review");
    }
    return QStringLiteral("unknown");
}

int dashboardIndexForMode(SessionMode mode) {
    switch (mode) {
    case SessionMode::CountabilityLearning:
    case SessionMode::CountabilityReview:
        return 1;
    case SessionMode::PolysemyLearning:
    case SessionMode::PolysemyReview:
        return 2;
    case SessionMode::Learning:
    case SessionMode::Review:
    default:
        return 0;
    }
}

int dashboardIndexForTrainingType(const QString &trainingType) {
    const QString type = trainingType.trimmed().toLower();
    if (type == QStringLiteral("countability")) {
        return 1;
    }
    if (type == QStringLiteral("polysemy")) {
        return 2;
    }
    return 0;
}

bool parseCountabilityLabel(const QString &label, CountabilityAnswer &answerOut) {
    const QString normalized = label.trimmed().toUpper();
    if (normalized == QStringLiteral("C")) {
        answerOut = CountabilityAnswer::Countable;
        return true;
    }
    if (normalized == QStringLiteral("U")) {
        answerOut = CountabilityAnswer::Uncountable;
        return true;
    }
    if (normalized == QStringLiteral("B")) {
        answerOut = CountabilityAnswer::Both;
        return true;
    }
    return false;
}

class LaunchSnapshotCard final : public QWidget {
public:
    explicit LaunchSnapshotCard(QWidget *parent = nullptr)
        : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setSnapshot(const QPixmap &pixmap) {
        snapshot_ = pixmap;
        update();
    }

    void setCornerRadius(qreal radius) {
        cornerRadius_ = qMax<qreal>(0.0, radius);
        update();
    }

    void setBorderWidth(qreal width) {
        borderWidth_ = qMax<qreal>(0.0, width);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const qreal halfBorder = borderWidth_ * 0.5;
        const QRectF bounds = rect().adjusted(halfBorder, halfBorder, -halfBorder, -halfBorder);
        if (bounds.isEmpty()) {
            return;
        }

        QPainterPath clipPath;
        clipPath.addRoundedRect(bounds, cornerRadius_, cornerRadius_);
        painter.setClipPath(clipPath);
        painter.fillRect(bounds, QColor("#ffffff"));

        if (!snapshot_.isNull()) {
            painter.drawPixmap(bounds.toRect(), snapshot_);
        }

        painter.setClipping(false);
        QPen pen(QColor("#cbd5e1"));
        pen.setWidthF(borderWidth_);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(clipPath);
    }

private:
    QPixmap snapshot_;
    qreal cornerRadius_ = 18.0;
    qreal borderWidth_ = 2.0;
};
} // namespace

VibeSpellerWindow::VibeSpellerWindow(QWidget *parent)
    : QWidget(parent) {
    AppLogger::step(QStringLiteral("Window"), QStringLiteral("construct window"));
    setWindowTitle(QStringLiteral("VibeSpeller"));
    resize(540, 960);
    setMinimumSize(405, 720);

    setStyleSheet(QStringLiteral(
        "QWidget {"
        "  background: #FFFFFF;"
        "  color: #111827;"
        "  font-family: 'PingFang SC','Microsoft YaHei','Noto Sans CJK SC','Helvetica Neue',sans-serif;"
        "}"
        "QPushButton {"
        "  border: none;"
        "  border-radius: 16px;"
        "  background: #f3f4f6;"
        "  padding: 12px 20px;"
        "  font-size: 22px;"
        "}"
        "QPushButton:hover { background: #e5e7eb; }"
        "QPushButton:pressed { background: #d1d5db; }"
        "QLineEdit:disabled { color: #9ca3af; }"
        "QTableWidget, QListWidget {"
        "  border: 1px solid #eef2f7;"
        "  border-radius: 12px;"
        "  background: #ffffff;"
        "}"));

    stack_ = new QStackedWidget(this);
    homePage_ = new HomePageWidget(this);
    mappingPage_ = new MappingPageWidget(this);
    spellingPage_ = new SpellingPageWidget(this);
    countabilityPage_ = new CountabilityPageWidget(this);
    polysemyPage_ = new PolysemyPageWidget(this);
    summaryPage_ = new SummaryPageWidget(this);
    statisticsPage_ = new StatisticsPageWidget(this);
    wordBooksPage_ = new WordBooksPageWidget(this);
    debugMode_ = qApp->property("vibespeller_debug").toBool();
    spellingPage_->setDebugMode(debugMode_);
    pronunciationProcess_ = new QProcess(this);

    stack_->addWidget(homePage_);
    stack_->addWidget(mappingPage_);
    stack_->addWidget(spellingPage_);
    stack_->addWidget(countabilityPage_);
    stack_->addWidget(polysemyPage_);
    stack_->addWidget(summaryPage_);
    stack_->addWidget(statisticsPage_);
    stack_->addWidget(wordBooksPage_);
    stack_->setCurrentWidget(homePage_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    connect(stack_, &QStackedWidget::currentChanged, this, [this](int) {
        updateStudyTimeTracking();
        if (stack_ != nullptr && stack_->currentWidget() == homePage_ && homePage_ != nullptr) {
            QTimer::singleShot(0, this, [this]() {
                if (homePage_ == nullptr) {
                    return;
                }
                homePage_->focusDashboard();
                AppLogger::info(QStringLiteral("Home"), QStringLiteral("dashboard focus restored"));
            });
        }
    });
    studyIdleTimer_ = new QTimer(this);
    studyIdleTimer_->setInterval(5000);
    connect(studyIdleTimer_, &QTimer::timeout, this, &VibeSpellerWindow::updateStudyTimeTracking);
    studyIdleTimer_->start();

    connect(homePage_, &HomePageWidget::startLearningClicked, this, &VibeSpellerWindow::onStartLearning);
    connect(homePage_, &HomePageWidget::startReviewClicked, this, &VibeSpellerWindow::onStartReview);
    connect(homePage_, &HomePageWidget::startCountabilityLearningClicked, this, &VibeSpellerWindow::onStartCountabilityLearning);
    connect(homePage_, &HomePageWidget::startCountabilityReviewClicked, this, &VibeSpellerWindow::onStartCountabilityReview);
    connect(homePage_, &HomePageWidget::startPolysemyLearningClicked, this, &VibeSpellerWindow::onStartPolysemyLearning);
    connect(homePage_, &HomePageWidget::startPolysemyReviewClicked, this, &VibeSpellerWindow::onStartPolysemyReview);
    connect(homePage_, &HomePageWidget::changeBookRequested, this, &VibeSpellerWindow::onChangeBookForTraining);
    connect(homePage_, &HomePageWidget::dashboardIndexChanged, this, [this](int index) {
        db_.setLastDashboardCardIndex(index);
    });
    connect(homePage_, &HomePageWidget::booksClicked, this, &VibeSpellerWindow::onOpenWordBooks);
    connect(homePage_, &HomePageWidget::statsClicked, this, [this]() {
        rememberHomeCardIndex();
        statisticsPage_->setLogs(db_.fetchWeeklyLogs());
        stack_->setCurrentWidget(statisticsPage_);
    });

    connect(statisticsPage_, &StatisticsPageWidget::backClicked, this, [this]() {
        restoreHomeCardIndex(false);
        stack_->setCurrentWidget(homePage_);
    });

    connect(mappingPage_, &MappingPageWidget::cancelled, this, [this]() {
        if (returnToWordBooksAfterImport_) {
            refreshWordBooks();
            stack_->setCurrentWidget(wordBooksPage_);
        } else {
            stack_->setCurrentWidget(homePage_);
        }
        returnToWordBooksAfterImport_ = false;
    });

    connect(mappingPage_, &MappingPageWidget::importConfirmed, this,
            [this](int wordCol, int transCol, int phoCol, int countCol, int plurCol, int noteCol, int polyCol) {
                AppLogger::step(
                    QStringLiteral("Import"),
                    QStringLiteral("mapping confirmed word=%1 trans=%2 phonetic=%3 countability=%4 plural=%5 notes=%6 polysemy=%7 csv=%8")
                        .arg(wordCol)
                        .arg(transCol)
                        .arg(phoCol)
                        .arg(countCol)
                        .arg(plurCol)
                        .arg(noteCol)
                        .arg(polyCol)
                        .arg(pendingCsvPath_));
                if (pendingCsvPath_.isEmpty()) {
                    showWarningPrompt(this,
                                      QStringLiteral("导入失败"),
                                      QStringLiteral("没有可导入的 CSV 文件路径。"));
                    AppLogger::warn(QStringLiteral("Import"), QStringLiteral("mapping confirmed but csv path empty"));
                    return;
                }

                int importedCount = 0;
                if (!db_.importFromCsv(pendingCsvPath_,
                                       wordCol,
                                       transCol,
                                       phoCol,
                                       countCol,
                                       plurCol,
                                       polyCol,
                                       noteCol,
                                       importedCount)) {
                    showErrorPrompt(this,
                                    QStringLiteral("导入失败"),
                                    QStringLiteral("CSV 导入失败：%1").arg(db_.lastError()));
                    AppLogger::error(QStringLiteral("Import"),
                                     QStringLiteral("csv import failed, error=%1").arg(db_.lastError()));
                    return;
                }

                showInfoPrompt(this,
                               QStringLiteral("导入完成"),
                               QStringLiteral("成功导入 %1 条新单词。\n重复单词会自动跳过。")
                                   .arg(importedCount));

                pendingCsvPath_.clear();
                refreshHomeCounts();
                refreshWordBooks();
                AppLogger::info(QStringLiteral("Import"),
                                QStringLiteral("csv import success, imported=%1").arg(importedCount));
                if (returnToWordBooksAfterImport_) {
                    stack_->setCurrentWidget(wordBooksPage_);
                } else {
                    stack_->setCurrentWidget(homePage_);
                }
                returnToWordBooksAfterImport_ = false;
            });

    connect(spellingPage_, &SpellingPageWidget::submitted, this, &VibeSpellerWindow::onSubmitAnswer);
    connect(spellingPage_, &SpellingPageWidget::proceedRequested, this, &VibeSpellerWindow::onProceedAfterFeedback);
    connect(spellingPage_, &SpellingPageWidget::exitRequested, this, &VibeSpellerWindow::onExitSession);
    connect(spellingPage_, &SpellingPageWidget::skipForeverRequested, this, &VibeSpellerWindow::onSkipForeverCurrentWord);
    connect(spellingPage_, &SpellingPageWidget::userActivity, this, &VibeSpellerWindow::markStudyUserActivity);
    connect(countabilityPage_, &CountabilityPageWidget::userActivity, this, &VibeSpellerWindow::markStudyUserActivity);
    connect(countabilityPage_, &CountabilityPageWidget::exitRequested, this, &VibeSpellerWindow::onExitSession);
    connect(countabilityPage_, &CountabilityPageWidget::answerSubmitted, this, &VibeSpellerWindow::onCountabilityAnswer);
    connect(countabilityPage_, &CountabilityPageWidget::continueRequested, this, &VibeSpellerWindow::moveToNextCountabilityWord);
    connect(polysemyPage_, &PolysemyPageWidget::userActivity, this, &VibeSpellerWindow::markStudyUserActivity);
    connect(polysemyPage_, &PolysemyPageWidget::exitRequested, this, &VibeSpellerWindow::onExitSession);
    connect(polysemyPage_, &PolysemyPageWidget::ratingSubmitted, this, &VibeSpellerWindow::onPolysemyRated);

    connect(summaryPage_, &SummaryPageWidget::backHomeClicked, this, [this]() {
        refreshHomeCounts();
        stack_->setCurrentWidget(homePage_);
    });
    connect(summaryPage_, &SummaryPageWidget::nextGroupClicked, this, &VibeSpellerWindow::continueNextGroup);
    connect(wordBooksPage_, &WordBooksPageWidget::backClicked, this, [this]() {
        restoreHomeCardIndex(true);
        stack_->setCurrentWidget(homePage_);
    });
    connect(wordBooksPage_, &WordBooksPageWidget::addBookClicked, this, [this]() {
        pickCsvAndShowMapping(true);
    });
    connect(wordBooksPage_, &WordBooksPageWidget::wordBookSelected, this, &VibeSpellerWindow::onSelectWordBook);
    connect(wordBooksPage_, &WordBooksPageWidget::wordBookDeleteRequested, this, &VibeSpellerWindow::onDeleteWordBook);
    connect(wordBooksPage_, &WordBooksPageWidget::downloadAudioRequested, this, &VibeSpellerWindow::onDownloadAudio);
    connect(wordBooksPage_, &WordBooksPageWidget::audioDownloadStopRequested, this, &VibeSpellerWindow::onAudioDownloadStopRequested);

    auto *importShortcut = new QShortcut(QKeySequence::Open, this);
    connect(importShortcut, &QShortcut::activated, this, [this]() {
        pickCsvAndShowMapping(false);
    });

    initializeDatabase();
    refreshHomeCounts();
    refreshWordBooks();
    applyRoundedWindowMask();
    AppLogger::info(QStringLiteral("Window"), QStringLiteral("window initialized"));

    // 启动后若词库为空，提示导入 CSV。
    QTimer::singleShot(0, this, &VibeSpellerWindow::requestCsvImportIfNeeded);
}

void VibeSpellerWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    applyRoundedWindowMask();
}

void VibeSpellerWindow::applyRoundedWindowMask() {
    if (width() <= 0 || height() <= 0) {
        clearMask();
        return;
    }

    QPainterPath path;
    path.addRoundedRect(rect(), kUnifiedCornerRadiusPx, kUnifiedCornerRadiusPx);
    const QPolygon polygon = path.toFillPolygon().toPolygon();
    setMask(QRegion(polygon));
}

void VibeSpellerWindow::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        updateStudyTimeTracking();
    }
}

void VibeSpellerWindow::closeEvent(QCloseEvent *event) {
    flushStudyTimeTracking();
    audioDownloadCancelRequested_.store(true);
    QWidget::closeEvent(event);
}

void VibeSpellerWindow::onStartLearning() {
    AppLogger::step(QStringLiteral("Session"), QStringLiteral("start requested, mode=spelling_learning"));
    pendingHomeLaunchRect_ = homePage_->launchRect(SessionMode::Learning);
    if (tryResumeSession(SessionMode::Learning)) {
        AppLogger::info(QStringLiteral("Session"), QStringLiteral("resume hit, mode=spelling_learning"));
        return;
    }

    if (!ensureActiveBookForTraining(QStringLiteral("spelling"), QStringLiteral("拼写学习"))) {
        AppLogger::warn(QStringLiteral("Session"), QStringLiteral("start blocked, no active spelling book"));
        return;
    }

    const QVector<WordItem> words = db_.fetchLearningBatchForTraining(QStringLiteral("spelling"), kSessionBatchSize);
    AppLogger::info(QStringLiteral("Session"),
                    QStringLiteral("fetched words, mode=spelling_learning, count=%1").arg(words.size()));
    if (words.isEmpty()) {
        showInfoPrompt(this,
                       QStringLiteral("暂无学习任务"),
                       QStringLiteral("当前词书没有可学习的新词。"));
        AppLogger::warn(QStringLiteral("Session"), QStringLiteral("no learning task, mode=spelling_learning"));
        return;
    }

    startSession(SessionMode::Learning, words, 0);
}

void VibeSpellerWindow::onStartReview() {
    AppLogger::step(QStringLiteral("Session"), QStringLiteral("start requested, mode=spelling_review"));
    pendingHomeLaunchRect_ = homePage_->launchRect(SessionMode::Review);
    if (tryResumeSession(SessionMode::Review)) {
        AppLogger::info(QStringLiteral("Session"), QStringLiteral("resume hit, mode=spelling_review"));
        return;
    }

    const QVector<WordItem> words = db_.fetchReviewBatchForTraining(
        QStringLiteral("spelling"),
        QDateTime::currentDateTime(),
        kSessionBatchSize);
    AppLogger::info(QStringLiteral("Session"),
                    QStringLiteral("fetched words, mode=spelling_review, count=%1").arg(words.size()));
    if (words.isEmpty()) {
        const int tomorrowCount = db_.dueReviewCountForTraining(
            QStringLiteral("spelling"),
            QDateTime::currentDateTime().addDays(1));
        showInfoPrompt(this,
                       QStringLiteral("暂无复习任务"),
                       QStringLiteral("今天已经复习完，明天需要复习%1个").arg(tomorrowCount));
        AppLogger::warn(QStringLiteral("Session"),
                        QStringLiteral("no review task, mode=spelling_review, tomorrow=%1").arg(tomorrowCount));
        return;
    }

    startSession(SessionMode::Review, words, 0);
}

void VibeSpellerWindow::onStartCountabilityLearning() {
    AppLogger::step(QStringLiteral("Session"), QStringLiteral("start requested, mode=countability_learning"));
    pendingHomeLaunchRect_ = homePage_->launchRect(SessionMode::CountabilityLearning);
    if (tryResumeSession(SessionMode::CountabilityLearning)) {
        AppLogger::info(QStringLiteral("Session"), QStringLiteral("resume hit, mode=countability_learning"));
        return;
    }

    if (!ensureActiveBookForTraining(QStringLiteral("countability"), QStringLiteral("可数性辨析"))) {
        AppLogger::warn(QStringLiteral("Session"), QStringLiteral("start blocked, no active countability book"));
        return;
    }

    const QVector<WordItem> words = db_.fetchLearningBatchForTraining(QStringLiteral("countability"),
                                                                       kSessionBatchSize);
    AppLogger::info(QStringLiteral("Session"),
                    QStringLiteral("fetched words, mode=countability_learning, count=%1").arg(words.size()));
    if (words.isEmpty()) {
        showInfoPrompt(this,
                       QStringLiteral("暂无辨析任务"),
                       QStringLiteral("当前词书没有可进行可数性辨析的新词。"));
        AppLogger::warn(QStringLiteral("Session"), QStringLiteral("no learning task, mode=countability_learning"));
        return;
    }
    startSession(SessionMode::CountabilityLearning, words, 0);
}

void VibeSpellerWindow::onStartCountabilityReview() {
    AppLogger::step(QStringLiteral("Session"), QStringLiteral("start requested, mode=countability_review"));
    pendingHomeLaunchRect_ = homePage_->launchRect(SessionMode::CountabilityReview);
    if (tryResumeSession(SessionMode::CountabilityReview)) {
        AppLogger::info(QStringLiteral("Session"), QStringLiteral("resume hit, mode=countability_review"));
        return;
    }

    const QVector<WordItem> words = db_.fetchReviewBatchForTraining(
        QStringLiteral("countability"),
        QDateTime::currentDateTime(),
        kSessionBatchSize);
    AppLogger::info(QStringLiteral("Session"),
                    QStringLiteral("fetched words, mode=countability_review, count=%1").arg(words.size()));
    if (words.isEmpty()) {
        const int tomorrowCount = db_.dueReviewCountForTraining(
            QStringLiteral("countability"),
            QDateTime::currentDateTime().addDays(1));
        showInfoPrompt(this,
                       QStringLiteral("暂无复习任务"),
                       QStringLiteral("今天已经完成可数性复习，明天需要复习%1个").arg(tomorrowCount));
        AppLogger::warn(QStringLiteral("Session"),
                        QStringLiteral("no review task, mode=countability_review, tomorrow=%1").arg(tomorrowCount));
        return;
    }
    startSession(SessionMode::CountabilityReview, words, 0);
}

void VibeSpellerWindow::onStartPolysemyLearning() {
    AppLogger::step(QStringLiteral("Session"), QStringLiteral("start requested, mode=polysemy_learning"));
    pendingHomeLaunchRect_ = homePage_->launchRect(SessionMode::PolysemyLearning);
    if (tryResumeSession(SessionMode::PolysemyLearning)) {
        AppLogger::info(QStringLiteral("Session"), QStringLiteral("resume hit, mode=polysemy_learning"));
        return;
    }

    if (!ensureActiveBookForTraining(QStringLiteral("polysemy"), QStringLiteral("熟词生义学习"))) {
        AppLogger::warn(QStringLiteral("Session"), QStringLiteral("start blocked, no active polysemy book"));
        return;
    }

    const QVector<WordItem> words = db_.fetchLearningBatchForTraining(QStringLiteral("polysemy"),
                                                                       kSessionBatchSize);
    AppLogger::info(QStringLiteral("Session"),
                    QStringLiteral("fetched words, mode=polysemy_learning, count=%1").arg(words.size()));
    if (words.isEmpty()) {
        showInfoPrompt(this,
                       QStringLiteral("暂无学习任务"),
                       QStringLiteral("当前词书没有可进行熟词生义学习的新词。"));
        AppLogger::warn(QStringLiteral("Session"), QStringLiteral("no learning task, mode=polysemy_learning"));
        return;
    }
    startSession(SessionMode::PolysemyLearning, words, 0);
}

void VibeSpellerWindow::onStartPolysemyReview() {
    AppLogger::step(QStringLiteral("Session"), QStringLiteral("start requested, mode=polysemy_review"));
    pendingHomeLaunchRect_ = homePage_->launchRect(SessionMode::PolysemyReview);
    if (tryResumeSession(SessionMode::PolysemyReview)) {
        AppLogger::info(QStringLiteral("Session"), QStringLiteral("resume hit, mode=polysemy_review"));
        return;
    }

    const QVector<WordItem> words = db_.fetchReviewBatchForTraining(
        QStringLiteral("polysemy"),
        QDateTime::currentDateTime(),
        kSessionBatchSize);
    AppLogger::info(QStringLiteral("Session"),
                    QStringLiteral("fetched words, mode=polysemy_review, count=%1").arg(words.size()));
    if (words.isEmpty()) {
        const int tomorrowCount = db_.dueReviewCountForTraining(
            QStringLiteral("polysemy"),
            QDateTime::currentDateTime().addDays(1));
        showInfoPrompt(this,
                       QStringLiteral("暂无复习任务"),
                       QStringLiteral("今天已经完成熟词生义复习，明天需要复习%1个").arg(tomorrowCount));
        AppLogger::warn(QStringLiteral("Session"),
                        QStringLiteral("no review task, mode=polysemy_review, tomorrow=%1").arg(tomorrowCount));
        return;
    }
    startSession(SessionMode::PolysemyReview, words, 0);
}

void VibeSpellerWindow::onSubmitAnswer(const QString &text) {
    if (currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        return;
    }

    const WordItem current = currentWords_.at(currentIndex_);
    const SpellingResult evaluated = db_.evaluateSpelling(text, current.word);
    if (!db_.recordSpellingAttempt(current.id, evaluated == SpellingResult::Mastered)) {
        showWarningPrompt(this,
                          QStringLiteral("统计保存失败"),
                          QStringLiteral("保存单词正确率失败：%1").arg(db_.lastError()));
    }
    const int existingMistakes = roundMistakeCounts_.value(current.id, 0);
    const bool hasWrongHistory = firstWrongInputs_.contains(current.id);
    const auto hasSameWordAhead = [this, &current]() -> bool {
        for (int i = currentIndex_ + 1; i < currentWords_.size(); ++i) {
            if (currentWords_.at(i).id == current.id) {
                return true;
            }
        }
        return false;
    };
    const auto removeSameWordAhead = [this, &current]() {
        for (int i = currentWords_.size() - 1; i > currentIndex_; --i) {
            if (currentWords_.at(i).id == current.id) {
                currentWords_.removeAt(i);
            }
        }
    };

    if (evaluated == SpellingResult::Mastered) {
        playPronunciationForWord(current.word);
        // 若本词本轮出现过拼写错误，本次“拼对”仅用于加深记忆，不升级为熟悉。
        if (hasWrongHistory) {
            // 若后面还有同词，说明本轮仍需在队尾继续复现，不在当前这次结算。
            if (!hasSameWordAhead()) {
                PracticeRecord record;
                record.word = current;
                record.result = SpellingResult::Unfamiliar;
                record.userInput = firstWrongInputs_.value(current.id, text);
                record.skipped = false;
                records_.push_back(record);
                roundMistakeCounts_.remove(current.id);
                firstWrongInputs_.remove(current.id);
                if (currentIndex_ + 1 < currentWords_.size()) {
                    const int nextIndex = currentIndex_ + 1;
                    const int totalTarget = qMax(1, sessionWordTargetCount_);
                    const int displayIndex = qMin(records_.size() + 1, totalTarget);
                    const WordItem nextWord = currentWords_.at(nextIndex);
                    currentIndex_ = nextIndex;
                    persistCurrentSession();
                    updateSpellingDebugInfo(nextWord.id);
                    spellingPage_->playCorrectTransition(current, nextWord, displayIndex, totalTarget, currentMode_ == SessionMode::Review);
                    return;
                }
                moveToNextWord();
                return;
            } else {
                // 进入下一次回尾复现前重置“本次出现”的错误计数，
                // 避免旧计数让下一次出现出现“无论对错都跳词”的体感。
                roundMistakeCounts_.insert(current.id, 0);
            }
            if (currentIndex_ + 1 < currentWords_.size()) {
                const int nextIndex = currentIndex_ + 1;
                const int totalTarget = qMax(1, sessionWordTargetCount_);
                const int displayIndex = qMin(records_.size() + 1, totalTarget);
                const WordItem nextWord = currentWords_.at(nextIndex);
                currentIndex_ = nextIndex;
                persistCurrentSession();
                updateSpellingDebugInfo(nextWord.id);
                spellingPage_->playCorrectTransition(current, nextWord, displayIndex, totalTarget, currentMode_ == SessionMode::Review);
                return;
            }
            moveToNextWord();
            return;
        }

        if (!db_.applyReviewResult(current.id, SpellingResult::Mastered, false, QDateTime::currentDateTime())) {
            showWarningPrompt(this,
                              QStringLiteral("更新失败"),
                              QStringLiteral("保存复习结果失败：%1").arg(db_.lastError()));
        }

        PracticeRecord record;
        record.word = current;
        record.result = SpellingResult::Mastered;
        record.userInput = text;
        record.skipped = false;
        records_.push_back(record);
        roundMistakeCounts_.remove(current.id);
        firstWrongInputs_.remove(current.id);
        if (currentIndex_ + 1 < currentWords_.size()) {
            const int nextIndex = currentIndex_ + 1;
            const int totalTarget = qMax(1, sessionWordTargetCount_);
            const int displayIndex = qMin(records_.size() + 1, totalTarget);
            const WordItem nextWord = currentWords_.at(nextIndex);
            currentIndex_ = nextIndex;
            persistCurrentSession();
            updateSpellingDebugInfo(nextWord.id);
            spellingPage_->playCorrectTransition(current, nextWord, displayIndex, totalTarget, currentMode_ == SessionMode::Review);
            return;
        }
        moveToNextWord();
        return;
    }

    const int mistakeCount = existingMistakes + 1;
    roundMistakeCounts_.insert(current.id, mistakeCount);
    if (!hasWrongHistory) {
        firstWrongInputs_.insert(current.id, text);
    }

    // 本词本轮第一次拼错时，按“不熟悉”更新复习安排；后续错不重复入库。
    if (!hasWrongHistory && mistakeCount == 1) {
        if (!db_.applyReviewResult(current.id, SpellingResult::Unfamiliar, true, QDateTime::currentDateTime())) {
            showWarningPrompt(this,
                              QStringLiteral("更新失败"),
                              QStringLiteral("保存复习结果失败：%1").arg(db_.lastError()));
        }
    }

    if (mistakeCount > 3) {
        // 超过 3 次则本轮跳过：删除后续同词，不再继续推送。
        removeSameWordAhead();
        PracticeRecord record;
        record.word = current;
        record.result = SpellingResult::Unfamiliar;
        record.userInput = firstWrongInputs_.value(current.id, text);
        record.skipped = true;
        records_.push_back(record);
        roundMistakeCounts_.remove(current.id);
        firstWrongInputs_.remove(current.id);
        moveToNextWord();
        return;
    }

    // 本词拼错后加入本轮末尾；若已在后续队列中则不重复追加。
    if (!hasSameWordAhead()) {
        currentWords_.push_back(current);
    }

    spellingPage_->setInputEnabled(true);
    spellingPage_->playWrongShake();
    spellingPage_->showFeedback(
        QStringLiteral("正确拼写：<b>%1</b>").arg(current.word.toHtmlEscaped()),
        QStringLiteral("#4bc816b6"));
    updateSpellingDebugInfo(current.id);
    persistCurrentSession();
}

void VibeSpellerWindow::onCountabilityAnswer(CountabilityAnswer answer) {
    if (currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        return;
    }
    if (currentMode_ != SessionMode::CountabilityLearning
        && currentMode_ != SessionMode::CountabilityReview) {
        return;
    }

    markStudyUserActivity();

    const WordItem current = currentWords_.at(currentIndex_);
    
    // 不论正确与否，点击即播放音频
    playPronunciationForWord(current.word);

    CountabilityAnswer expected = CountabilityAnswer::Countable;
    const bool hasExpected = parseCountabilityLabel(current.countabilityLabel, expected);
    if (!hasExpected) {
        if (countabilityPage_ != nullptr) {
            countabilityPage_->setOptionsEnabled(false);
        }
        // 修复问题5：不在这里提前 persist，moveToNextCountabilityWord 内部会再次 persist，
        // 两次 persist 的第一次保存的是当前（未推进的）index，纯属多余写库。
        QPointer<VibeSpellerWindow> guard(this);
        QTimer::singleShot(kCountabilityFeedbackMs, this, [guard]() {
            if (!guard) {
                return;
            }
            guard->moveToNextCountabilityWord();
        });
        return;
    }
    const bool correct = (answer == expected);
    if (countabilityPage_ != nullptr) {
        countabilityPage_->setOptionsEnabled(false);
        countabilityPage_->showAnswerFeedback(answer, expected, correct);
    }

    const int existingMistakes = countabilityWrongCounts_.value(current.id, 0);
    const auto hasSameWordAhead = [this, &current]() -> bool {
        for (int i = currentIndex_ + 1; i < currentWords_.size(); ++i) {
            if (currentWords_.at(i).id == current.id) {
                return true;
            }
        }
        return false;
    };
    const auto removeSameWordAhead = [this, &current]() {
        for (int i = currentWords_.size() - 1; i > currentIndex_; --i) {
            if (currentWords_.at(i).id == current.id) {
                currentWords_.removeAt(i);
            }
        }
    };
    const auto delayedAdvance = [this]() {
        QPointer<VibeSpellerWindow> guard(this);
        QTimer::singleShot(kCountabilityFeedbackMs, this, [guard]() {
            if (!guard) {
                return;
            }
            guard->moveToNextCountabilityWord();
        });
    };

    const auto delayedShowDetails = [this, current, expected, answer]() {
        QPointer<VibeSpellerWindow> guard(this);
        QTimer::singleShot(kCountabilityFeedbackMs, this, [guard, current, expected, answer]() {
            if (!guard || !guard->countabilityPage_) {
                return;
            }
            guard->countabilityPage_->showDetailedFeedback(current, expected, answer);
        });
    };

    if (correct) {
        if (existingMistakes == 0) {
            if (!db_.applyCountabilityResult(current.id, true, QDateTime::currentDateTime())) {
                showWarningPrompt(this,
                                  QStringLiteral("更新失败"),
                                  QStringLiteral("保存可数性结果失败：%1").arg(db_.lastError()));
            }
            PracticeRecord record;
            record.word = current;
            record.result = SpellingResult::Mastered;
            record.userInput = countabilityAnswerText(answer);
            records_.push_back(record);
        } else if (!hasSameWordAhead()) {
            PracticeRecord record;
            record.word = current;
            record.result = SpellingResult::Unfamiliar;
            record.userInput = firstWrongInputs_.value(current.id, countabilityAnswerText(answer));
            records_.push_back(record);
        }
        if (!hasSameWordAhead()) {
            countabilityWrongCounts_.remove(current.id);
            firstWrongInputs_.remove(current.id);
        }
        persistCurrentSession();
        if (countabilityPage_) {
            delayedShowDetails();
        }
        return;
    }

    if (!firstWrongInputs_.contains(current.id)) {
        firstWrongInputs_.insert(current.id, countabilityAnswerText(answer));
    }
    const int mistakeCount = existingMistakes + 1;
    countabilityWrongCounts_.insert(current.id, mistakeCount);

    if (mistakeCount == 1) {
        // 修复问题2：首次答错时更新记忆曲线，但不写入拼写统计计数。
        if (!db_.applyCountabilityResult(current.id, false, QDateTime::currentDateTime())) {
            showWarningPrompt(this,
                              QStringLiteral("更新失败"),
                              QStringLiteral("保存可数性结果失败：%1").arg(db_.lastError()));
        }
    }

    if (mistakeCount > 3) {
        removeSameWordAhead();
        PracticeRecord record;
        record.word = current;
        record.result = SpellingResult::Unfamiliar;
        record.userInput = firstWrongInputs_.value(current.id, countabilityAnswerText(answer));
        record.skipped = true;
        records_.push_back(record);
        countabilityWrongCounts_.remove(current.id);
        firstWrongInputs_.remove(current.id);
        persistCurrentSession();
        if (countabilityPage_) {
            delayedShowDetails();
        }
        return;
    }

    if (!hasSameWordAhead()) {
        currentWords_.push_back(current);
    }

    persistCurrentSession();
    if (countabilityPage_) {
        delayedShowDetails();
    }
}

void VibeSpellerWindow::onPolysemyRated(SpellingResult result) {
    if (currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        return;
    }
    if (currentMode_ != SessionMode::PolysemyLearning
        && currentMode_ != SessionMode::PolysemyReview) {
        return;
    }

    markStudyUserActivity();
    const WordItem current = currentWords_.at(currentIndex_);
    if (polysemyPage_ != nullptr) {
        polysemyPage_->setOptionsEnabled(false);
    }

    if (!db_.applyPolysemyResult(current.id, result, false, QDateTime::currentDateTime())) {
        showWarningPrompt(this,
                          QStringLiteral("更新失败"),
                          QStringLiteral("保存熟词生义结果失败：%1").arg(db_.lastError()));
    }

    PracticeRecord record;
    record.word = current;
    record.result = result;
    record.userInput = briefResult(result);
    records_.push_back(record);
    persistCurrentSession();

    QPointer<VibeSpellerWindow> guard(this);
    QTimer::singleShot(180, this, [guard]() {
        if (!guard) {
            return;
        }
        guard->moveToNextWord();
    });
}

void VibeSpellerWindow::onProceedAfterFeedback() {
    if (currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        return;
    }
    moveToNextWord();
}

void VibeSpellerWindow::onExitSession() {
    AppLogger::step(QStringLiteral("Session"),
                    QStringLiteral("exit requested, mode=%1, words=%2, index=%3")
                        .arg(sessionModeName(currentMode_))
                        .arg(currentWords_.size())
                        .arg(currentIndex_));
    if (currentWords_.isEmpty() || currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        stack_->setCurrentWidget(homePage_);
        AppLogger::info(QStringLiteral("Session"), QStringLiteral("exit direct to home (no active word)"));
        return;
    }

    persistCurrentSession();
    const int homeIndex = dashboardIndexForMode(currentMode_);
    if (!db_.setLastDashboardCardIndex(homeIndex)) {
        AppLogger::warn(QStringLiteral("Home"),
                        QStringLiteral("failed to pin dashboard index on exit, index=%1, error=%2")
                            .arg(homeIndex)
                            .arg(db_.lastError()));
    } else {
        AppLogger::info(QStringLiteral("Home"),
                        QStringLiteral("pin dashboard index on exit, index=%1").arg(homeIndex));
    }
    refreshHomeCounts();
    QWidget *sourcePage = spellingPage_;
    if (currentMode_ == SessionMode::CountabilityLearning
        || currentMode_ == SessionMode::CountabilityReview) {
        sourcePage = countabilityPage_;
    } else if (currentMode_ == SessionMode::PolysemyLearning
               || currentMode_ == SessionMode::PolysemyReview) {
        sourcePage = polysemyPage_;
    }
    const QRect launchRect = homePage_->launchRect(currentMode_);
    animatePageToHomeTransition(sourcePage, launchRect);
}

void VibeSpellerWindow::onSkipForeverCurrentWord() {
    if (currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        return;
    }

    const WordItem current = currentWords_.at(currentIndex_);
    if (current.id <= 0) {
        return;
    }

    if (!db_.setWordSkipForever(current.id, true)) {
        showWarningPrompt(this,
                          QStringLiteral("标记失败"),
                          QStringLiteral("标记永久跳过失败：%1").arg(db_.lastError()));
        return;
    }

    for (int i = currentWords_.size() - 1; i >= 0; --i) {
        if (currentWords_.at(i).id == current.id) {
            currentWords_.removeAt(i);
        }
    }

    roundMistakeCounts_.remove(current.id);
    firstWrongInputs_.remove(current.id);

    if (currentWords_.isEmpty() || currentIndex_ >= currentWords_.size()) {
        finishSession();
        return;
    }

    persistCurrentSession();
    showCurrentWord();
}

void VibeSpellerWindow::onOpenWordBooks() {
    const int index = homePage_ != nullptr ? homePage_->currentCardIndex() : 0;
    const SessionMode mode = modeForDashboardRequest(index, false);
    AppLogger::step(QStringLiteral("BookBinding"),
                    QStringLiteral("open word books from dashboard, index=%1, mode=%2")
                        .arg(index)
                        .arg(sessionModeName(mode)));
    openWordBooksForTraining(trainingTypeForMode(mode));
}

void VibeSpellerWindow::onChangeBookForTraining(const QString &trainingType) {
    AppLogger::step(QStringLiteral("BookBinding"),
                    QStringLiteral("change book requested, trainingType=%1").arg(trainingType));
    openWordBooksForTraining(trainingType);
}

void VibeSpellerWindow::onSelectWordBook(int bookId) {
    AppLogger::step(QStringLiteral("BookBinding"),
                    QStringLiteral("select word book requested, bookId=%1").arg(bookId));
    if (bookId <= 0) {
        AppLogger::warn(QStringLiteral("BookBinding"), QStringLiteral("select ignored, invalid bookId"));
        return;
    }

    const QString trainingType = pendingBookSelectionTrainingType_.isEmpty()
                                     ? QStringLiteral("spelling")
                                     : pendingBookSelectionTrainingType_;
    preservedHomeCardIndex_ = dashboardIndexForTrainingType(trainingType);
    AppLogger::info(QStringLiteral("BookBinding"),
                    QStringLiteral("select context, trainingType=%1, currentActive=%2")
                        .arg(trainingType)
                        .arg(db_.activeBookIdForTraining(trainingType)));
    if (bookId == db_.activeBookIdForTraining(trainingType)) {
        stack_->setCurrentWidget(homePage_);
        AppLogger::info(QStringLiteral("BookBinding"), QStringLiteral("select no-op, already active"));
        return;
    }

    const bool confirmSwitch = showQuestionPrompt(
        this,
        QStringLiteral("切换词书"),
        QStringLiteral("是否将“%1”切换到新词书？\n当前模式进度会保留，已学习单词仍会继续推送复习。")
            .arg(trainingDisplayName(trainingType)));
    if (!confirmSwitch) {
        AppLogger::info(QStringLiteral("BookBinding"), QStringLiteral("switch cancelled by user"));
        return;
    }

    if (!db_.setActiveBookIdForTraining(trainingType, bookId)) {
        showWarningPrompt(this,
                          QStringLiteral("切换失败"),
                          QStringLiteral("切换当前词书失败：%1").arg(db_.lastError()));
        AppLogger::error(QStringLiteral("BookBinding"),
                         QStringLiteral("switch failed, type=%1, bookId=%2, error=%3")
                             .arg(trainingType)
                             .arg(bookId)
                             .arg(db_.lastError()));
        return;
    }

    clearSessionForMode(learningModeForTraining(trainingType));
    refreshHomeCounts();
    refreshWordBooks();
    stack_->setCurrentWidget(homePage_);
    AppLogger::info(QStringLiteral("BookBinding"),
                    QStringLiteral("switch success, type=%1, bookId=%2").arg(trainingType).arg(bookId));
}

void VibeSpellerWindow::onDeleteWordBook(int bookId) {
    if (bookId <= 0) {
        return;
    }

    QString bookName;
    int wordCount = 0;
    const QVector<WordBookItem> books = db_.fetchWordBooks();
    for (const WordBookItem &book : books) {
        if (book.id == bookId) {
            bookName = book.name;
            wordCount = book.wordCount;
            break;
        }
    }
    if (bookName.isEmpty()) {
        return;
    }

    const bool firstConfirm = showQuestionPrompt(
        this,
        QStringLiteral("删除词书"),
        QStringLiteral("将删除词书“%1”（%2 词）。是否继续？").arg(bookName).arg(wordCount));
    if (!firstConfirm) {
        return;
    }

    const bool secondConfirm = showQuestionPrompt(
        this,
        QStringLiteral("再次确认"),
        QStringLiteral("删除后无法恢复，确定删除“%1”吗？").arg(bookName));
    if (!secondConfirm) {
        return;
    }

    if (!db_.deleteWordBook(bookId)) {
        showWarningPrompt(this,
                          QStringLiteral("删除失败"),
                          QStringLiteral("删除词书失败：%1").arg(db_.lastError()));
        return;
    }

    clearSessionForMode(SessionMode::Learning);
    clearSessionForMode(SessionMode::Review);
    clearSessionForMode(SessionMode::CountabilityLearning);
    clearSessionForMode(SessionMode::CountabilityReview);
    clearSessionForMode(SessionMode::PolysemyLearning);
    clearSessionForMode(SessionMode::PolysemyReview);
    refreshHomeCounts();
    refreshWordBooks();
}

void VibeSpellerWindow::onDownloadAudio(int bookId) {
    if (bookId <= 0) {
        return;
    }
    if (audioDownloadRunning_) {
        wordBooksPage_->setAudioDownloadStatus(QStringLiteral("已有下载任务进行中"), -1, -1, true);
        return;
    }

    const QVector<WordItem> words = db_.fetchWordsForBook(bookId);
    if (words.isEmpty()) {
        wordBooksPage_->setAudioDownloadStatus(QStringLiteral("当前词书没有可下载单词"), 0, 0, false);
        return;
    }

    audioDownloadRunning_ = true;
    audioDownloadCancelRequested_.store(false);
    wordBooksPage_->setAudioDownloadStatus(QStringLiteral("准备下载..."), 0, words.size(), true);
    QPointer<VibeSpellerWindow> window(this);
    auto *thread = QThread::create([window, words, bookId]() {
        AudioDownloader downloader;
        QString errorText;
        const AudioDownloader::Result result = downloader.downloadBookAudio(
            words,
            bookId,
            [window](int current, int total, const QString &word) {
                if (!window) {
                    return;
                }
                const int displayIndex = qMin(total, qMax(1, current + 1));
                const QString status = QStringLiteral("下载中：%1（%2/%3）")
                                           .arg(word)
                                           .arg(displayIndex)
                                           .arg(total);
                QMetaObject::invokeMethod(window.data(), [window, status, current, total]() {
                    if (window && window->wordBooksPage_) {
                        window->wordBooksPage_->setAudioDownloadStatus(status, current, total, true);
                    }
                }, Qt::QueuedConnection);
            },
            [window]() {
                return !window || window->audioDownloadCancelRequested_.load();
            },
            errorText);

        if (!window) {
            return;
        }

        QMetaObject::invokeMethod(window.data(), [window, result, errorText]() {
            if (!window) {
                return;
            }

            window->audioDownloadRunning_ = false;
            if (window->audioDownloadThread_ != nullptr) {
                window->audioDownloadThread_->deleteLater();
                window->audioDownloadThread_ = nullptr;
            }

            const int processed = result.checked;
            if (!errorText.isEmpty()) {
                window->wordBooksPage_->setAudioDownloadStatus(
                    QStringLiteral("下载异常：%1").arg(errorText),
                    qMin(processed, result.totalWords),
                    result.totalWords,
                    false);
                return;
            }

            const QString finalText = result.cancelled
                                          ? QStringLiteral("已暂停（%1/%2），下次将自动校验并补全缺失音频")
                                                .arg(qMin(processed, result.totalWords))
                                                .arg(result.totalWords)
                                          : QStringLiteral("完成：新增%1 已有%2 无MP3%3 失败%4（哈希不匹配%5）")
                                                .arg(result.downloaded)
                                                .arg(result.reused)
                                                .arg(result.noMp3)
                                                .arg(result.failed)
                                                .arg(result.hashMismatched);
            window->wordBooksPage_->setAudioDownloadStatus(finalText,
                                                           qMin(processed, result.totalWords),
                                                           result.totalWords,
                                                           false);
        }, Qt::QueuedConnection);
    });

    audioDownloadThread_ = thread;
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VibeSpellerWindow::onAudioDownloadStopRequested() {
    if (!audioDownloadRunning_) {
        return;
    }
    audioDownloadCancelRequested_.store(true);
    wordBooksPage_->setAudioDownloadStatus(QStringLiteral("正在停止..."), -1, -1, true);
}


void VibeSpellerWindow::playPronunciationForWord(const QString &word) {
    if (word.trimmed().isEmpty() || pronunciationProcess_ == nullptr) {
        return;
    }

    const QDir audioDir(QStringLiteral(VIBESPELLER_SOURCE_DIR) + QStringLiteral("/assets/audio"));
    const QString rawName = word.trimmed();
    const QStringList candidates = {
        safeAudioFileName(rawName) + QStringLiteral(".mp3"),
        rawName + QStringLiteral(".mp3"),
        rawName.toLower() + QStringLiteral(".mp3")
    };

    QString audioPath;
    for (const QString &fileName : candidates) {
        const QString path = audioDir.filePath(fileName);
        if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
            audioPath = path;
            break;
        }
    }
    if (audioPath.isEmpty()) {
        return;
    }

    const qreal volume = computeNormalizedVolume(audioPath);
    if (pronunciationProcess_->state() != QProcess::NotRunning) {
        pronunciationProcess_->kill();
        pronunciationProcess_->waitForFinished(200);
    }

    QStringList arguments;
    arguments << QStringLiteral("-v")
              << QString::number(volume, 'f', 3)
              << audioPath;
    pronunciationProcess_->start(QStringLiteral("/usr/bin/afplay"), arguments);
}

qreal VibeSpellerWindow::computeNormalizedVolume(const QString &audioFilePath) {
    const auto cached = pronunciationVolumeCache_.constFind(audioFilePath);
    if (cached != pronunciationVolumeCache_.constEnd()) {
        return cached.value();
    }

    qreal volume = kBasePlaybackVolume;
    const QString ffmpegPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpegPath.isEmpty()) {
        pronunciationVolumeCache_.insert(audioFilePath, volume);
        return volume;
    }

    QProcess ffmpeg;
    QStringList args;
    args << QStringLiteral("-i")
         << audioFilePath
         << QStringLiteral("-af")
         << QStringLiteral("volumedetect")
         << QStringLiteral("-f")
         << QStringLiteral("null")
         << QStringLiteral("-");
    ffmpeg.start(ffmpegPath, args);
    if (ffmpeg.waitForFinished(kAnalyzeTimeoutMs)) {
        const QString output = QString::fromUtf8(ffmpeg.readAllStandardError());
        QRegularExpression meanRe(QStringLiteral("mean_volume:\\s*([-+]?\\d+(?:\\.\\d+)?)\\s*dB"));
        QRegularExpression maxRe(QStringLiteral("max_volume:\\s*([-+]?\\d+(?:\\.\\d+)?)\\s*dB"));
        const QRegularExpressionMatch meanMatch = meanRe.match(output);
        const QRegularExpressionMatch maxMatch = maxRe.match(output);

        bool meanOk = false;
        bool maxOk = false;
        const double meanDb = meanMatch.hasMatch() ? meanMatch.captured(1).toDouble(&meanOk) : 0.0;
        const double maxDb = maxMatch.hasMatch() ? maxMatch.captured(1).toDouble(&maxOk) : 0.0;
        if (meanOk) {
            double gainDb = kTargetMeanDb - meanDb;
            if (maxOk && maxDb + gainDb > kMaxPeakDb) {
                gainDb = kMaxPeakDb - maxDb;
            }
            volume = static_cast<qreal>(std::pow(10.0, gainDb / 20.0));
            volume = qBound<qreal>(0.25, volume, 2.0);
        }
    }

    pronunciationVolumeCache_.insert(audioFilePath, volume);
    return volume;
}

void VibeSpellerWindow::updateStudyTimeTracking() {
    const bool shouldTrack = stack_ != nullptr
                             && (stack_->currentWidget() == spellingPage_
                                 || stack_->currentWidget() == countabilityPage_
                                 || stack_->currentWidget() == polysemyPage_)
                             && isActiveWindow();
    const QDateTime now = QDateTime::currentDateTime();

    if (shouldTrack) {
        if (!isStudyTrackingActive_) {
            // 超时停表后，不应自动重启；必须有新的输入事件才重启。
            if (!lastStudyUserActionTime_.isValid()
                || lastStudyUserActionTime_.secsTo(now) <= kStudyIdleCutoffSeconds) {
                isStudyTrackingActive_ = true;
                studyTrackingStartTime_ = now;
                if (!lastStudyUserActionTime_.isValid()) {
                    lastStudyUserActionTime_ = now;
                }
            }
        } else {
            const QDateTime effectiveEnd = effectiveStudyTrackingEndTime(now);
            if (effectiveEnd < now) {
                // 已经超过无输入阈值：停止计时，并扣除这段等待时间。
                accumulateStudyDuration(studyTrackingStartTime_, effectiveEnd);
                isStudyTrackingActive_ = false;
            }
        }
        return;
    }

    if (isStudyTrackingActive_) {
        accumulateStudyDuration(studyTrackingStartTime_, effectiveStudyTrackingEndTime(now));
        isStudyTrackingActive_ = false;
    }
}

void VibeSpellerWindow::flushStudyTimeTracking() {
    if (!isStudyTrackingActive_) {
        return;
    }
    accumulateStudyDuration(studyTrackingStartTime_, effectiveStudyTrackingEndTime(QDateTime::currentDateTime()));
    isStudyTrackingActive_ = false;
}

void VibeSpellerWindow::markStudyUserActivity() {
    const QDateTime now = QDateTime::currentDateTime();
    lastStudyUserActionTime_ = now;

    const bool canTrack = stack_ != nullptr
                          && (stack_->currentWidget() == spellingPage_
                              || stack_->currentWidget() == countabilityPage_
                              || stack_->currentWidget() == polysemyPage_)
                          && isActiveWindow();
    if (canTrack && !isStudyTrackingActive_) {
        isStudyTrackingActive_ = true;
        studyTrackingStartTime_ = now;
    }
}

QDateTime VibeSpellerWindow::effectiveStudyTrackingEndTime(const QDateTime &now) const {
    if (!lastStudyUserActionTime_.isValid()) {
        return now;
    }

    const qint64 idleSeconds = lastStudyUserActionTime_.secsTo(now);
    if (idleSeconds > kStudyIdleCutoffSeconds) {
        // 超过 2 分钟无输入时，连这 2 分钟等待时间也不计入。
        return lastStudyUserActionTime_;
    }
    return now;
}

void VibeSpellerWindow::accumulateStudyDuration(const QDateTime &start, const QDateTime &end) {
    if (!start.isValid() || !end.isValid() || end <= start) {
        return;
    }

    const bool isCountability = (currentMode_ == SessionMode::CountabilityLearning
                                || currentMode_ == SessionMode::CountabilityReview);

    QDateTime cursor = start;
    while (cursor.date() < end.date()) {
        const QDateTime endOfDay(cursor.date(), QTime(23, 59, 59));
        const int seconds = static_cast<int>(cursor.secsTo(endOfDay) + 1);
        if (seconds > 0) {
            db_.addDailyStudySeconds(seconds, isCountability, cursor.date());
        }
        cursor = endOfDay.addSecs(1);
    }

    const int finalSeconds = static_cast<int>(cursor.secsTo(end));
    if (finalSeconds > 0) {
        db_.addDailyStudySeconds(finalSeconds, isCountability, cursor.date());
    }
}

void VibeSpellerWindow::initializeDatabase() {
    const QString dbPath = QStringLiteral(VIBESPELLER_SOURCE_DIR) + QStringLiteral("/vibespeller.db");
    AppLogger::step(QStringLiteral("DB"), QStringLiteral("initialize database, path=%1").arg(dbPath));

    if (!db_.open(dbPath)) {
        showErrorPrompt(this,
                        QStringLiteral("数据库错误"),
                        QStringLiteral("无法打开数据库：%1").arg(db_.lastError()));
        AppLogger::error(QStringLiteral("DB"), QStringLiteral("open failed, error=%1").arg(db_.lastError()));
        return;
    }

    if (!db_.initialize()) {
        showErrorPrompt(this,
                        QStringLiteral("数据库错误"),
                        QStringLiteral("无法初始化数据库：%1").arg(db_.lastError()));
        AppLogger::error(QStringLiteral("DB"), QStringLiteral("initialize failed, error=%1").arg(db_.lastError()));
        return;
    }
    AppLogger::info(QStringLiteral("DB"), QStringLiteral("database ready"));
}

void VibeSpellerWindow::refreshHomeCounts() {
    db_.reconcileFirstDayDailyLog();
    const QVector<WordBookItem> books = db_.fetchWordBooks();
    QHash<int, WordBookItem> booksById;
    for (const WordBookItem &book : books) {
        booksById.insert(book.id, book);
    }

    const auto buildCard = [&](const QString &trainingType,
                               const QString &title,
                               const QString &themeColor) {
        DashboardCardState card;
        card.trainingType = trainingType;
        card.modeTitle = title;
        card.themeColor = themeColor;
        card.activeBookId = db_.activeBookIdForTraining(trainingType);
        card.hasActiveBook = card.activeBookId > 0;
        if (card.hasActiveBook && booksById.contains(card.activeBookId)) {
            card.bookName = booksById.value(card.activeBookId).name;
        }
        card.coverName = coverTextForBook(card.bookName);
        card.totalWords = db_.totalWordCountForTraining(trainingType);
        card.masteredWords = db_.masteredWordCountForTraining(trainingType);
        card.unlearnedCount = db_.unlearnedCountForTraining(trainingType);
        card.dueReviewCount = db_.dueReviewCountForTraining(trainingType, QDateTime::currentDateTime());
        card.learningEnabled = card.hasActiveBook && card.unlearnedCount > 0;
        card.reviewEnabled = card.dueReviewCount > 0;
        return card;
    };

    QVector<DashboardCardState> cards;
    cards.reserve(3);
    cards.push_back(buildCard(QStringLiteral("spelling"), QStringLiteral("拼写"), QStringLiteral("#0f172a")));
    cards.push_back(buildCard(QStringLiteral("countability"), QStringLiteral("可数性辨析"), QStringLiteral("#0ea5a4")));
    cards.push_back(buildCard(QStringLiteral("polysemy"), QStringLiteral("熟词生义"), QStringLiteral("#f59e0b")));

    int todayLearning = 0;
    int todayReview = 0;
    int todayStudyMinutes = 0;
    const QVector<DatabaseManager::DailyLog> logs = db_.fetchWeeklyLogs();
    if (!logs.isEmpty()) {
        const DatabaseManager::DailyLog &today = logs.last();
        todayLearning = today.learningCount + today.countabilityLearningCount;
        todayReview = today.reviewCount + today.countabilityReviewCount;
        todayStudyMinutes = today.studyMinutes;
    }

    const int dashboardIndex = qBound(0, db_.lastDashboardCardIndex(), 2);
    homePage_->setDashboardCards(cards, dashboardIndex, todayLearning, todayReview, todayStudyMinutes);
    AppLogger::info(
        QStringLiteral("Home"),
        QStringLiteral("counts refreshed, idx=%1, spelling(unlearned=%2,due=%3,total=%4), countability(unlearned=%5,due=%6,total=%7), polysemy(unlearned=%8,due=%9,total=%10)")
            .arg(dashboardIndex)
            .arg(cards.value(0).unlearnedCount)
            .arg(cards.value(0).dueReviewCount)
            .arg(cards.value(0).totalWords)
            .arg(cards.value(1).unlearnedCount)
            .arg(cards.value(1).dueReviewCount)
            .arg(cards.value(1).totalWords)
            .arg(cards.value(2).unlearnedCount)
            .arg(cards.value(2).dueReviewCount)
            .arg(cards.value(2).totalWords));
}

void VibeSpellerWindow::refreshWordBooks() {
    QString trainingType = pendingBookSelectionTrainingType_;
    if (trainingType.isEmpty()) {
        const int dashboardIndex = homePage_ != nullptr ? homePage_->currentCardIndex() : 0;
        trainingType = trainingTypeForMode(modeForDashboardRequest(dashboardIndex, false));
    }
    const int activeBookId = db_.activeBookIdForTraining(trainingType);
    const QVector<WordBookItem> books = db_.fetchWordBooks();
    wordBooksPage_->setWordBooks(books,
                                 activeBookId,
                                 trainingType,
                                 trainingDisplayName(trainingType));
    AppLogger::info(QStringLiteral("BookBinding"),
                    QStringLiteral("word books refreshed, type=%1, activeBook=%2, totalBooks=%3")
                        .arg(trainingType)
                        .arg(activeBookId)
                        .arg(books.size()));
}

void VibeSpellerWindow::requestCsvImportIfNeeded() {
    AppLogger::step(QStringLiteral("Import"), QStringLiteral("startup csv prompt check"));
    if (db_.isCsvPromptHandled()) {
        AppLogger::info(QStringLiteral("Import"), QStringLiteral("csv prompt already handled"));
        return;
    }
    db_.markCsvPromptHandled();
    if (!db_.fetchWordBooks().isEmpty()) {
        AppLogger::info(QStringLiteral("Import"), QStringLiteral("skip startup prompt, books already exist"));
        return;
    }

    const bool answer = showQuestionPrompt(this,
                                           QStringLiteral("导入词库"),
                                           QStringLiteral("当前还没有词书。现在导入 CSV 吗？"));
    AppLogger::info(QStringLiteral("Import"),
                    QStringLiteral("startup prompt result=%1").arg(answer ? QStringLiteral("yes") : QStringLiteral("no")));
    if (answer) {
        pickCsvAndShowMapping(false);
    }
}

bool VibeSpellerWindow::pickCsvAndShowMapping(bool returnToWordBooks) {
    AppLogger::step(QStringLiteral("Import"),
                    QStringLiteral("pick csv requested, returnToWordBooks=%1")
                        .arg(returnToWordBooks ? QStringLiteral("true") : QStringLiteral("false")));
    const QString csvPath = QFileDialog::getOpenFileName(this,
                                                          QStringLiteral("选择 CSV 词库"),
                                                          QDir::homePath(),
                                                          QStringLiteral("CSV Files (*.csv);;All Files (*)"));
    if (csvPath.isEmpty()) {
        AppLogger::info(QStringLiteral("Import"), QStringLiteral("pick csv cancelled"));
        return false;
    }

    QStringList headers;
    QVector<QStringList> previewRows;
    if (!db_.readCsvPreview(csvPath, headers, previewRows)) {
        showErrorPrompt(this,
                        QStringLiteral("读取失败"),
                        QStringLiteral("读取 CSV 失败：%1").arg(db_.lastError()));
        AppLogger::error(QStringLiteral("Import"),
                         QStringLiteral("read preview failed, csv=%1, error=%2").arg(csvPath, db_.lastError()));
        return false;
    }

    pendingCsvPath_ = csvPath;
    returnToWordBooksAfterImport_ = returnToWordBooks;
    mappingPage_->setCsvData(csvPath, headers, previewRows);
    stack_->setCurrentWidget(mappingPage_);
    AppLogger::info(QStringLiteral("Import"),
                    QStringLiteral("open mapping page, csv=%1, headers=%2, previewRows=%3")
                        .arg(csvPath)
                        .arg(headers.size())
                        .arg(previewRows.size()));
    return true;
}

bool VibeSpellerWindow::tryResumeSession(SessionMode mode) {
    QVector<WordItem> savedWords;
    int savedIndex = 0;
    if (!db_.loadSessionProgress(modeKey(mode), savedWords, savedIndex)) {
        AppLogger::info(QStringLiteral("Session"),
                        QStringLiteral("resume miss, mode=%1").arg(sessionModeName(mode)));
        return false;
    }

    AppLogger::info(QStringLiteral("Session"),
                    QStringLiteral("resume hit, mode=%1, words=%2, index=%3")
                        .arg(sessionModeName(mode))
                        .arg(savedWords.size())
                        .arg(savedIndex));
    startSession(mode, std::move(savedWords), savedIndex);
    return true;
}

void VibeSpellerWindow::startSession(SessionMode mode, QVector<WordItem> words, int startIndex) {
    AppLogger::step(QStringLiteral("Session"),
                    QStringLiteral("start session, mode=%1, initialWords=%2, startIndex=%3")
                        .arg(sessionModeName(mode))
                        .arg(words.size())
                        .arg(startIndex));
    currentMode_ = mode;
    currentWords_ = std::move(words);
    records_.clear();
    roundMistakeCounts_.clear();
    countabilityWrongCounts_.clear();
    firstWrongInputs_.clear();
    sessionWordTargetCount_ = 0;

    constexpr int kRoundLimit = kSessionBatchSize;
    int normalizedStart = qMax(0, startIndex);
    if (normalizedStart > currentWords_.size()) {
        normalizedStart = currentWords_.size();
    }

    // 恢复时从断点开始：已完成前缀直接移除。
    if (normalizedStart > 0) {
        currentWords_ = currentWords_.mid(normalizedStart);
    }

    // 兜底去重：避免会话中错词回尾导致持久化后重复。
    QVector<WordItem> deduped;
    deduped.reserve(currentWords_.size());
    QSet<int> pickedIds;
    for (const WordItem &word : currentWords_) {
        if (word.skipForever) {
            continue;
        }
        if (word.id > 0 && pickedIds.contains(word.id)) {
            continue;
        }
        deduped.push_back(word);
        if (word.id > 0) {
            pickedIds.insert(word.id);
        }
    }
    currentWords_ = std::move(deduped);

    // 若历史会话是按更大批次（如 20）保存，而当前批次改小（如 5），
    // 则按当前批次上限裁剪，确保所有流程都围绕 kSessionBatchSize 运行。
    if (currentWords_.size() > kRoundLimit) {
        currentWords_ = currentWords_.mid(0, kRoundLimit);
        pickedIds.clear();
        for (const WordItem &word : currentWords_) {
            if (word.id > 0) {
                pickedIds.insert(word.id);
            }
        }
    }

    // 若不足每组目标数，按当前模式从池中补齐；不够就按实际数量开组。
    if (currentWords_.size() < kRoundLimit) {
        const int fetchLimit = 200;
        QVector<WordItem> candidates;
        switch (mode) {
        case SessionMode::Review:
            candidates = db_.fetchReviewBatchForTraining(QStringLiteral("spelling"),
                                                         QDateTime::currentDateTime(),
                                                         fetchLimit);
            break;
        case SessionMode::Learning:
            candidates = db_.fetchLearningBatchForTraining(QStringLiteral("spelling"), fetchLimit);
            break;
        case SessionMode::CountabilityLearning:
            candidates = db_.fetchLearningBatchForTraining(QStringLiteral("countability"), fetchLimit);
            break;
        case SessionMode::CountabilityReview:
            candidates = db_.fetchReviewBatchForTraining(QStringLiteral("countability"),
                                                         QDateTime::currentDateTime(),
                                                         fetchLimit);
            break;
        case SessionMode::PolysemyLearning:
            candidates = db_.fetchLearningBatchForTraining(QStringLiteral("polysemy"), fetchLimit);
            break;
        case SessionMode::PolysemyReview:
            candidates = db_.fetchReviewBatchForTraining(QStringLiteral("polysemy"),
                                                         QDateTime::currentDateTime(),
                                                         fetchLimit);
            break;
        }
        for (const WordItem &candidate : candidates) {
            if (candidate.skipForever) {
                continue;
            }
            if (pickedIds.contains(candidate.id)) {
                continue;
            }
            currentWords_.push_back(candidate);
            pickedIds.insert(candidate.id);
            if (currentWords_.size() >= kRoundLimit) {
                break;
            }
        }
    }

    sessionWordTargetCount_ = qMin(kRoundLimit, currentWords_.size());
    currentIndex_ = 0;
    if (currentIndex_ >= currentWords_.size()) {
        AppLogger::warn(QStringLiteral("Session"),
                        QStringLiteral("start aborted after filtering, mode=%1").arg(sessionModeName(mode)));
        clearSessionForMode(mode);
        return;
    }

    AppLogger::info(QStringLiteral("Session"),
                    QStringLiteral("session ready, mode=%1, finalWords=%2, target=%3")
                        .arg(sessionModeName(mode))
                        .arg(currentWords_.size())
                        .arg(sessionWordTargetCount_));
    persistCurrentSession();
    showCurrentWord();
    QWidget *targetPage = spellingPage_;
    if (mode == SessionMode::CountabilityLearning || mode == SessionMode::CountabilityReview) {
        targetPage = countabilityPage_;
    } else if (mode == SessionMode::PolysemyLearning || mode == SessionMode::PolysemyReview) {
        targetPage = polysemyPage_;
    }
    animateHomeToPageTransition(pendingHomeLaunchRect_, targetPage);
    pendingHomeLaunchRect_ = QRect();
}

void VibeSpellerWindow::animateHomeToPageTransition(const QRect &sourceRect, QWidget *targetPage) {
    const QList<QWidget *> staleHosts = findChildren<QWidget *>(QStringLiteral("__transition_host__"),
                                                                 Qt::FindDirectChildrenOnly);
    for (QWidget *staleHost : staleHosts) {
        if (staleHost == nullptr) {
            continue;
        }
        staleHost->hide();
        staleHost->deleteLater();
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("home->page removed stale transition host"));
    }
    if (homePage_ != nullptr && homePage_->graphicsEffect() != nullptr) {
        QGraphicsEffect *staleEffect = homePage_->graphicsEffect();
        homePage_->setGraphicsEffect(nullptr);
        Q_UNUSED(staleEffect);
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("home->page removed stale home opacity effect"));
    }

    const QRect stackRect = stack_->geometry();
    AppLogger::step(QStringLiteral("Transition"),
                    QStringLiteral("home->page begin, source=(%1,%2,%3,%4), stack=(%5,%6,%7,%8), target=%9")
                        .arg(sourceRect.x()).arg(sourceRect.y()).arg(sourceRect.width()).arg(sourceRect.height())
                        .arg(stackRect.x()).arg(stackRect.y()).arg(stackRect.width()).arg(stackRect.height())
                        .arg(targetPage != nullptr ? targetPage->objectName() : QStringLiteral("<null>")));
    if (sourceRect.isEmpty() || stack_->currentWidget() != homePage_ || stackRect.isEmpty() || targetPage == nullptr) {
        stack_->setCurrentWidget(targetPage);
        targetPage->setFocus();
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("home->page fallback switch (invalid precondition)"));
        return;
    }

    QRect startRect(homePage_->mapTo(this, sourceRect.topLeft()), sourceRect.size());
    startRect = startRect.intersected(rect());
    if (startRect.isEmpty()) {
        stack_->setCurrentWidget(targetPage);
        targetPage->setFocus();
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("home->page fallback switch (start rect empty)"));
        return;
    }

    const QRect endRect = stackRect.intersected(rect());
    if (endRect.isEmpty()) {
        stack_->setCurrentWidget(targetPage);
        targetPage->setFocus();
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("home->page fallback switch (end rect empty)"));
        return;
    }

    constexpr int kCardCornerPx = kUnifiedCornerRadiusPx;

    targetPage->resize(endRect.size());
    targetPage->ensurePolished();
    if (targetPage->layout() != nullptr) {
        targetPage->layout()->activate();
    }

    QPixmap snapshot(targetPage->size() * devicePixelRatioF());
    snapshot.setDevicePixelRatio(devicePixelRatioF());
    snapshot.fill(Qt::transparent);
    {
        QPainter painter(&snapshot);
        targetPage->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    }

    if (snapshot.isNull()) {
        stack_->setCurrentWidget(targetPage);
        targetPage->setFocus();
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("home->page fallback switch (snapshot null)"));
        return;
    }

    auto *transitionHost = new QWidget(this);
    transitionHost->setObjectName(QStringLiteral("__transition_host__"));
    transitionHost->setAttribute(Qt::WA_TransparentForMouseEvents);
    transitionHost->setAttribute(Qt::WA_NoSystemBackground);
    transitionHost->setAttribute(Qt::WA_AcceptTouchEvents, false);
    transitionHost->setFocusPolicy(Qt::NoFocus);
    transitionHost->setEnabled(false);
    transitionHost->setStyleSheet(QStringLiteral("background: transparent;"));
    transitionHost->setGeometry(rect());
    transitionHost->show();
    transitionHost->raise();

    auto *card = new LaunchSnapshotCard(transitionHost);
    card->setSnapshot(snapshot);
    card->setCornerRadius(kCardCornerPx);
    card->setBorderWidth(kCardBorderPx);
    card->setGeometry(startRect);
    card->show();

    auto *group = new QParallelAnimationGroup(this);
    auto *cardGeomAnim = new QPropertyAnimation(card, "geometry", group);
    cardGeomAnim->setDuration(kPageLaunchDurationMs);
    cardGeomAnim->setStartValue(startRect);
    cardGeomAnim->setEndValue(endRect);
    cardGeomAnim->setEasingCurve(QEasingCurve::OutCubic);

    auto *homeOpacity = new QGraphicsOpacityEffect(homePage_);
    homePage_->setGraphicsEffect(homeOpacity);
    homeOpacity->setOpacity(1.0);
    auto *homeFadeAnim = new QPropertyAnimation(homeOpacity, "opacity", group);
    homeFadeAnim->setDuration(kPageLaunchDurationMs);
    homeFadeAnim->setStartValue(1.0);
    homeFadeAnim->setEndValue(0.0);
    homeFadeAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(group, &QParallelAnimationGroup::finished, this,
            [this, group, transitionHost, homeOpacity, targetPage]() {
        if (homeOpacity != nullptr) {
            homeOpacity->setOpacity(1.0);
            if (homePage_ != nullptr && homePage_->graphicsEffect() == homeOpacity) {
                homePage_->setGraphicsEffect(nullptr);
            }
        }
        stack_->setCurrentWidget(targetPage);
        targetPage->setFocus();
        transitionHost->hide();
        transitionHost->deleteLater();
        group->deleteLater();
        AppLogger::info(QStringLiteral("Transition"), QStringLiteral("home->page finished"));
    });

    QPointer<QParallelAnimationGroup> guardGroup(group);
    QPointer<QWidget> guardHost(transitionHost);
    QPointer<QGraphicsOpacityEffect> guardOpacity(homeOpacity);
    QPointer<QWidget> guardTarget(targetPage);
    QTimer::singleShot(kPageLaunchDurationMs + 400, this, [this, guardGroup, guardHost, guardOpacity, guardTarget]() {
        if (guardGroup == nullptr || guardGroup->state() != QAbstractAnimation::Running) {
            return;
        }
        AppLogger::warn(QStringLiteral("Transition"), QStringLiteral("home->page watchdog fallback triggered"));
        guardGroup->stop();
        if (guardOpacity != nullptr) {
            guardOpacity->setOpacity(1.0);
            if (homePage_ != nullptr && homePage_->graphicsEffect() == guardOpacity) {
                homePage_->setGraphicsEffect(nullptr);
            }
        }
        if (guardTarget != nullptr) {
            stack_->setCurrentWidget(guardTarget);
            guardTarget->setFocus();
        }
        if (guardHost != nullptr) {
            guardHost->hide();
            guardHost->deleteLater();
        }
        guardGroup->deleteLater();
    });

    QPointer<QWidget> cleanupHost(transitionHost);
    QTimer::singleShot(kPageLaunchDurationMs + 1200, this, [cleanupHost]() {
        if (cleanupHost == nullptr) {
            return;
        }
        cleanupHost->hide();
        cleanupHost->deleteLater();
    });

    group->start();
}

void VibeSpellerWindow::animatePageToHomeTransition(QWidget *sourcePage, const QRect &targetRect) {
    const QList<QWidget *> staleHosts = findChildren<QWidget *>(QStringLiteral("__transition_host__"),
                                                                 Qt::FindDirectChildrenOnly);
    for (QWidget *staleHost : staleHosts) {
        if (staleHost == nullptr) {
            continue;
        }
        staleHost->hide();
        staleHost->deleteLater();
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("page->home removed stale transition host"));
    }
    if (homePage_ != nullptr && homePage_->graphicsEffect() != nullptr) {
        QGraphicsEffect *staleEffect = homePage_->graphicsEffect();
        homePage_->setGraphicsEffect(nullptr);
        Q_UNUSED(staleEffect);
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("page->home removed stale home opacity effect"));
    }

    const QRect stackRect = stack_->geometry();
    AppLogger::step(QStringLiteral("Transition"),
                    QStringLiteral("page->home begin, sourcePage=%1, target=(%2,%3,%4,%5), stack=(%6,%7,%8,%9)")
                        .arg(sourcePage != nullptr ? sourcePage->objectName() : QStringLiteral("<null>"))
                        .arg(targetRect.x()).arg(targetRect.y()).arg(targetRect.width()).arg(targetRect.height())
                        .arg(stackRect.x()).arg(stackRect.y()).arg(stackRect.width()).arg(stackRect.height()));
    if (targetRect.isEmpty() || stack_->currentWidget() != sourcePage || stackRect.isEmpty() || sourcePage == nullptr) {
        stack_->setCurrentWidget(homePage_);
        if (homePage_ != nullptr) {
            homePage_->focusDashboard();
        }
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("page->home fallback switch (invalid precondition)"));
        return;
    }

    QRect endRect(homePage_->mapTo(this, targetRect.topLeft()), targetRect.size());
    endRect = endRect.intersected(rect());
    if (endRect.isEmpty()) {
        stack_->setCurrentWidget(homePage_);
        if (homePage_ != nullptr) {
            homePage_->focusDashboard();
        }
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("page->home fallback switch (end rect empty)"));
        return;
    }

    const QRect startRect = stackRect.intersected(rect());
    if (startRect.isEmpty()) {
        stack_->setCurrentWidget(homePage_);
        if (homePage_ != nullptr) {
            homePage_->focusDashboard();
        }
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("page->home fallback switch (start rect empty)"));
        return;
    }

    constexpr int kCardCornerPx = kUnifiedCornerRadiusPx;

    sourcePage->resize(startRect.size());
    sourcePage->ensurePolished();
    if (sourcePage->layout() != nullptr) {
        sourcePage->layout()->activate();
    }

    QPixmap snapshot(sourcePage->size() * devicePixelRatioF());
    snapshot.setDevicePixelRatio(devicePixelRatioF());
    snapshot.fill(Qt::transparent);
    {
        QPainter painter(&snapshot);
        sourcePage->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    }
    if (snapshot.isNull()) {
        stack_->setCurrentWidget(homePage_);
        if (homePage_ != nullptr) {
            homePage_->focusDashboard();
        }
        AppLogger::warn(QStringLiteral("Transition"),
                        QStringLiteral("page->home fallback switch (snapshot null)"));
        return;
    }

    auto *homeOpacity = new QGraphicsOpacityEffect(homePage_);
    homePage_->setGraphicsEffect(homeOpacity);
    homeOpacity->setOpacity(0.0);
    stack_->setCurrentWidget(homePage_);

    auto *transitionHost = new QWidget(this);
    transitionHost->setObjectName(QStringLiteral("__transition_host__"));
    transitionHost->setAttribute(Qt::WA_TransparentForMouseEvents);
    transitionHost->setAttribute(Qt::WA_NoSystemBackground);
    transitionHost->setAttribute(Qt::WA_AcceptTouchEvents, false);
    transitionHost->setFocusPolicy(Qt::NoFocus);
    transitionHost->setEnabled(false);
    transitionHost->setStyleSheet(QStringLiteral("background: transparent;"));
    transitionHost->setGeometry(rect());
    transitionHost->show();
    transitionHost->raise();

    auto *card = new LaunchSnapshotCard(transitionHost);
    card->setSnapshot(snapshot);
    card->setCornerRadius(kCardCornerPx);
    card->setBorderWidth(kCardBorderPx);
    card->setGeometry(startRect);
    card->show();

    auto *cardOpacity = new QGraphicsOpacityEffect(card);
    cardOpacity->setOpacity(1.0);
    card->setGraphicsEffect(cardOpacity);

    auto *group = new QParallelAnimationGroup(this);
    auto *cardGeomAnim = new QPropertyAnimation(card, "geometry", group);
    cardGeomAnim->setDuration(kPageLaunchDurationMs);
    cardGeomAnim->setStartValue(startRect);
    cardGeomAnim->setEndValue(endRect);
    cardGeomAnim->setEasingCurve(QEasingCurve::OutCubic);

    auto *cardFadeAnim = new QPropertyAnimation(cardOpacity, "opacity", group);
    cardFadeAnim->setDuration(kPageLaunchDurationMs);
    cardFadeAnim->setStartValue(1.0);
    cardFadeAnim->setEndValue(0.0);
    cardFadeAnim->setEasingCurve(QEasingCurve::OutCubic);

    auto *homeFadeAnim = new QPropertyAnimation(homeOpacity, "opacity", group);
    homeFadeAnim->setDuration(kPageLaunchDurationMs);
    homeFadeAnim->setStartValue(0.0);
    homeFadeAnim->setEndValue(1.0);
    homeFadeAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(group, &QParallelAnimationGroup::finished, this,
            [this, group, transitionHost, homeOpacity]() {
        if (homeOpacity != nullptr) {
            homeOpacity->setOpacity(1.0);
            if (homePage_ != nullptr && homePage_->graphicsEffect() == homeOpacity) {
                homePage_->setGraphicsEffect(nullptr);
            }
        }
        transitionHost->hide();
        transitionHost->deleteLater();
        group->deleteLater();
        if (homePage_ != nullptr) {
            homePage_->focusDashboard();
        }
        AppLogger::info(QStringLiteral("Transition"), QStringLiteral("page->home finished"));
    });

    QPointer<QParallelAnimationGroup> guardGroup(group);
    QPointer<QWidget> guardHost(transitionHost);
    QPointer<QGraphicsOpacityEffect> guardOpacity(homeOpacity);
    QTimer::singleShot(kPageLaunchDurationMs + 400, this, [this, guardGroup, guardHost, guardOpacity]() {
        if (guardGroup == nullptr || guardGroup->state() != QAbstractAnimation::Running) {
            return;
        }
        AppLogger::warn(QStringLiteral("Transition"), QStringLiteral("page->home watchdog fallback triggered"));
        guardGroup->stop();
        if (guardOpacity != nullptr) {
            guardOpacity->setOpacity(1.0);
            if (homePage_ != nullptr && homePage_->graphicsEffect() == guardOpacity) {
                homePage_->setGraphicsEffect(nullptr);
            }
        }
        if (guardHost != nullptr) {
            guardHost->hide();
            guardHost->deleteLater();
        }
        if (homePage_ != nullptr) {
            homePage_->focusDashboard();
        }
        guardGroup->deleteLater();
    });

    QPointer<QWidget> cleanupHost(transitionHost);
    QTimer::singleShot(kPageLaunchDurationMs + 1200, this, [cleanupHost]() {
        if (cleanupHost == nullptr) {
            return;
        }
        cleanupHost->hide();
        cleanupHost->deleteLater();
    });

    group->start();
}

void VibeSpellerWindow::showCurrentWord() {
    if (currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        finishSession();
        return;
    }

    const WordItem &word = currentWords_.at(currentIndex_);
    const int totalTarget = qMax(1, sessionWordTargetCount_);
    const int displayIndex = qMin(records_.size() + 1, totalTarget);
    if (currentMode_ == SessionMode::Learning || currentMode_ == SessionMode::Review) {
        spellingPage_->setWord(word,
                               displayIndex,
                               totalTarget,
                               currentMode_ == SessionMode::Review);
        updateSpellingDebugInfo(word.id);
    } else if (currentMode_ == SessionMode::CountabilityLearning
               || currentMode_ == SessionMode::CountabilityReview) {
        countabilityPage_->setWord(word,
                                   displayIndex,
                                   totalTarget,
                                   currentMode_ == SessionMode::CountabilityReview);
    } else {
        polysemyPage_->setWord(word,
                               displayIndex,
                               totalTarget,
                               currentMode_ == SessionMode::PolysemyReview);
    }
}

void VibeSpellerWindow::updateSpellingDebugInfo(int wordId) {
    if (!debugMode_) {
        return;
    }

    WordDebugStats stats;
    if (!db_.fetchWordDebugStats(wordId, stats)) {
        spellingPage_->clearDebugInfo();
        return;
    }
    spellingPage_->setDebugInfo(stats.nextReview, stats.attemptCount, stats.correctCount);
}

void VibeSpellerWindow::persistCurrentSession() {
    if (currentWords_.isEmpty()) {
        return;
    }

    if (!db_.saveSessionProgress(modeKey(currentMode_), currentWords_, currentIndex_)) {
        showWarningPrompt(this,
                          QStringLiteral("会话保存失败"),
                          QStringLiteral("保存当前练习进度失败：%1").arg(db_.lastError()));
        AppLogger::error(QStringLiteral("Session"),
                         QStringLiteral("persist failed, mode=%1, index=%2, words=%3, error=%4")
                             .arg(sessionModeName(currentMode_))
                             .arg(currentIndex_)
                             .arg(currentWords_.size())
                             .arg(db_.lastError()));
        return;
    }
    AppLogger::info(QStringLiteral("Session"),
                    QStringLiteral("persist success, mode=%1, index=%2, words=%3")
                        .arg(sessionModeName(currentMode_))
                        .arg(currentIndex_)
                        .arg(currentWords_.size()));
}

void VibeSpellerWindow::clearSessionForMode(SessionMode mode) {
    if (!db_.clearSessionProgress(modeKey(mode))) {
        showWarningPrompt(this,
                          QStringLiteral("会话清理失败"),
                          QStringLiteral("清理练习进度失败：%1").arg(db_.lastError()));
        AppLogger::error(QStringLiteral("Session"),
                         QStringLiteral("clear failed, mode=%1, error=%2")
                             .arg(sessionModeName(mode))
                             .arg(db_.lastError()));
        return;
    }
    AppLogger::info(QStringLiteral("Session"),
                    QStringLiteral("clear success, mode=%1").arg(sessionModeName(mode)));
}

void VibeSpellerWindow::moveToNextWord() {
    ++currentIndex_;
    if (currentIndex_ >= currentWords_.size()) {
        finishSession();
        return;
    }

    persistCurrentSession();
    showCurrentWord();
}

void VibeSpellerWindow::moveToNextCountabilityWord() {
    markStudyUserActivity();
    ++currentIndex_;
    if (currentIndex_ >= currentWords_.size()) {
        finishSession();
        return;
    }
    persistCurrentSession();
    showCurrentWord();
}

void VibeSpellerWindow::finishSession() {
    AppLogger::step(QStringLiteral("Session"),
                    QStringLiteral("finish session, mode=%1, records=%2")
                        .arg(sessionModeName(currentMode_))
                        .arg(records_.size()));
    clearSessionForMode(currentMode_);
    const bool reviewMode = (currentMode_ == SessionMode::Review
                             || currentMode_ == SessionMode::CountabilityReview
                             || currentMode_ == SessionMode::PolysemyReview);
    summaryPage_->setSummary(records_, reviewMode);
    refreshHomeCounts();
    stack_->setCurrentWidget(summaryPage_);
    AppLogger::info(QStringLiteral("Session"), QStringLiteral("summary opened"));
}

void VibeSpellerWindow::continueNextGroup() {
    // 修复问题6：finishSession() 已经清除了会话断点，此处直接拉取新一批词即可，
    // 不需要经过 onStartXxx → tryResumeSession 的多余查询路径。
    QVector<WordItem> words;
    if (currentMode_ == SessionMode::Review) {
        words = db_.fetchReviewBatchForTraining(QStringLiteral("spelling"),
                                                QDateTime::currentDateTime(),
                                                kSessionBatchSize);
        if (words.isEmpty()) {
            const int tomorrowCount = db_.dueReviewCountForTraining(
                QStringLiteral("spelling"),
                QDateTime::currentDateTime().addDays(1));
            showInfoPrompt(this,
                           QStringLiteral("暂无复习任务"),
                           QStringLiteral("今天已经复习完，明天需要复习%1个").arg(tomorrowCount));
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
    } else if (currentMode_ == SessionMode::CountabilityLearning) {
        if (!ensureActiveBookForTraining(QStringLiteral("countability"), QStringLiteral("可数性辨析"))) {
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
        words = db_.fetchLearningBatchForTraining(QStringLiteral("countability"), kSessionBatchSize);
        if (words.isEmpty()) {
            showInfoPrompt(this,
                           QStringLiteral("暂无辨析任务"),
                           QStringLiteral("当前词书没有可进行可数性辨析的新词。"));
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
    } else if (currentMode_ == SessionMode::CountabilityReview) {
        words = db_.fetchReviewBatchForTraining(QStringLiteral("countability"),
                                                QDateTime::currentDateTime(),
                                                kSessionBatchSize);
        if (words.isEmpty()) {
            const int tomorrowCount = db_.dueReviewCountForTraining(
                QStringLiteral("countability"),
                QDateTime::currentDateTime().addDays(1));
            showInfoPrompt(this,
                           QStringLiteral("暂无复习任务"),
                           QStringLiteral("今天已经完成可数性复习，明天需要复习%1个").arg(tomorrowCount));
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
    } else if (currentMode_ == SessionMode::PolysemyLearning) {
        if (!ensureActiveBookForTraining(QStringLiteral("polysemy"), QStringLiteral("熟词生义学习"))) {
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
        words = db_.fetchLearningBatchForTraining(QStringLiteral("polysemy"), kSessionBatchSize);
        if (words.isEmpty()) {
            showInfoPrompt(this,
                           QStringLiteral("暂无学习任务"),
                           QStringLiteral("当前词书没有可进行熟词生义学习的新词。"));
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
    } else if (currentMode_ == SessionMode::PolysemyReview) {
        words = db_.fetchReviewBatchForTraining(QStringLiteral("polysemy"),
                                                QDateTime::currentDateTime(),
                                                kSessionBatchSize);
        if (words.isEmpty()) {
            const int tomorrowCount = db_.dueReviewCountForTraining(
                QStringLiteral("polysemy"),
                QDateTime::currentDateTime().addDays(1));
            showInfoPrompt(this,
                           QStringLiteral("暂无复习任务"),
                           QStringLiteral("今天已经完成熟词生义复习，明天需要复习%1个").arg(tomorrowCount));
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
    } else {
        if (!ensureActiveBookForTraining(QStringLiteral("spelling"), QStringLiteral("拼写学习"))) {
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
        words = db_.fetchLearningBatchForTraining(QStringLiteral("spelling"), kSessionBatchSize);
        if (words.isEmpty()) {
            showInfoPrompt(this,
                           QStringLiteral("暂无学习任务"),
                           QStringLiteral("当前词书没有可学习的新词。"));
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
    }
    startSession(currentMode_, std::move(words), 0);
}

QString VibeSpellerWindow::modeKey(SessionMode mode) const {
    switch (mode) {
    case SessionMode::Learning:
        return QStringLiteral("learning");
    case SessionMode::Review:
        return QStringLiteral("review");
    case SessionMode::CountabilityLearning:
        return QStringLiteral("countability_learning");
    case SessionMode::CountabilityReview:
        return QStringLiteral("countability_review");
    case SessionMode::PolysemyLearning:
        return QStringLiteral("polysemy_learning");
    case SessionMode::PolysemyReview:
        return QStringLiteral("polysemy_review");
    }
    return QStringLiteral("learning");
}

QString VibeSpellerWindow::trainingTypeForMode(SessionMode mode) const {
    switch (mode) {
    case SessionMode::Learning:
    case SessionMode::Review:
        return QStringLiteral("spelling");
    case SessionMode::CountabilityLearning:
    case SessionMode::CountabilityReview:
        return QStringLiteral("countability");
    case SessionMode::PolysemyLearning:
    case SessionMode::PolysemyReview:
        return QStringLiteral("polysemy");
    }
    return QStringLiteral("spelling");
}

QString VibeSpellerWindow::trainingDisplayName(const QString &trainingType) const {
    const QString type = trainingType.trimmed().toLower();
    if (type == QStringLiteral("countability")) {
        return QStringLiteral("可数性辨析");
    }
    if (type == QStringLiteral("polysemy")) {
        return QStringLiteral("熟词生义");
    }
    return QStringLiteral("拼写");
}

void VibeSpellerWindow::rememberHomeCardIndex() {
    if (homePage_ == nullptr) {
        return;
    }
    preservedHomeCardIndex_ = qBound(0, homePage_->currentCardIndex(), 2);
    if (!db_.setLastDashboardCardIndex(preservedHomeCardIndex_)) {
        AppLogger::warn(QStringLiteral("Home"),
                        QStringLiteral("remember home card index failed, idx=%1, error=%2")
                            .arg(preservedHomeCardIndex_)
                            .arg(db_.lastError()));
        return;
    }
    AppLogger::info(QStringLiteral("Home"),
                    QStringLiteral("remember home card index, idx=%1").arg(preservedHomeCardIndex_));
}

void VibeSpellerWindow::restoreHomeCardIndex(bool refreshCounts) {
    if (preservedHomeCardIndex_ < 0) {
        return;
    }
    const int index = qBound(0, preservedHomeCardIndex_, 2);
    if (!db_.setLastDashboardCardIndex(index)) {
        AppLogger::warn(QStringLiteral("Home"),
                        QStringLiteral("restore home card index failed, idx=%1, error=%2")
                            .arg(index)
                            .arg(db_.lastError()));
    } else {
        AppLogger::info(QStringLiteral("Home"),
                        QStringLiteral("restore home card index, idx=%1").arg(index));
    }
    if (refreshCounts) {
        refreshHomeCounts();
    }
    preservedHomeCardIndex_ = -1;
}

SessionMode VibeSpellerWindow::reviewModeForTraining(const QString &trainingType) const {
    const QString type = trainingType.trimmed().toLower();
    if (type == QStringLiteral("countability")) {
        return SessionMode::CountabilityReview;
    }
    if (type == QStringLiteral("polysemy")) {
        return SessionMode::PolysemyReview;
    }
    return SessionMode::Review;
}

SessionMode VibeSpellerWindow::learningModeForTraining(const QString &trainingType) const {
    const QString type = trainingType.trimmed().toLower();
    if (type == QStringLiteral("countability")) {
        return SessionMode::CountabilityLearning;
    }
    if (type == QStringLiteral("polysemy")) {
        return SessionMode::PolysemyLearning;
    }
    return SessionMode::Learning;
}

void VibeSpellerWindow::openWordBooksForTraining(const QString &trainingType) {
    const QString type = trainingType.trimmed().toLower().isEmpty()
                             ? QStringLiteral("spelling")
                             : trainingType.trimmed().toLower();
    if (stack_ != nullptr && stack_->currentWidget() == homePage_) {
        rememberHomeCardIndex();
    }
    pendingBookSelectionTrainingType_ = type;
    AppLogger::step(QStringLiteral("BookBinding"),
                    QStringLiteral("open word books, type=%1").arg(type));
    refreshWordBooks();
    stack_->setCurrentWidget(wordBooksPage_);
}

bool VibeSpellerWindow::ensureActiveBookForTraining(const QString &trainingType, const QString &title) {
    if (db_.activeBookIdForTraining(trainingType) > 0) {
        AppLogger::info(QStringLiteral("BookBinding"),
                        QStringLiteral("active book exists, type=%1, bookId=%2")
                            .arg(trainingType)
                            .arg(db_.activeBookIdForTraining(trainingType)));
        return true;
    }

    const bool goChoose = showQuestionPrompt(
        this,
        QStringLiteral("未绑定词书"),
        QStringLiteral("%1暂未绑定词书，是否现在去绑定？").arg(title + QStringLiteral(" ")));
    AppLogger::warn(QStringLiteral("BookBinding"),
                    QStringLiteral("no active book, type=%1, userChooseBindNow=%2")
                        .arg(trainingType)
                        .arg(goChoose ? QStringLiteral("yes") : QStringLiteral("no")));
    if (goChoose) {
        openWordBooksForTraining(trainingType);
    }
    return false;
}

SessionMode VibeSpellerWindow::modeForDashboardRequest(int modeIndex, bool isReview) const {
    const int index = qBound(0, modeIndex, 2);
    if (index == 1) {
        return isReview ? SessionMode::CountabilityReview : SessionMode::CountabilityLearning;
    }
    if (index == 2) {
        return isReview ? SessionMode::PolysemyReview : SessionMode::PolysemyLearning;
    }
    return isReview ? SessionMode::Review : SessionMode::Learning;
}

QString VibeSpellerWindow::resultLabel(SpellingResult result) const {
    switch (result) {
    case SpellingResult::Mastered:
        return QStringLiteral("熟悉（完全正确）");
    case SpellingResult::Blurry:
        return QStringLiteral("模糊（1-3 个字符误差）");
    case SpellingResult::Unfamiliar:
        return QStringLiteral("不熟悉（误差大于 3）");
    }
    return QStringLiteral("不熟悉");
}

QString VibeSpellerWindow::resultColor(SpellingResult result) const {
    switch (result) {
    case SpellingResult::Mastered:
        return QStringLiteral("#16a34a");
    case SpellingResult::Blurry:
        return QStringLiteral("#ea580c");
    case SpellingResult::Unfamiliar:
        return QStringLiteral("#dc2626");
    }
    return QStringLiteral("#6b7280");
}
