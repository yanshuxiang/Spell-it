#include "gui_widgets.h"
#include "gui_widgets_internal.h"
#include "audio_downloader.h"

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
    stack_->addWidget(summaryPage_);
    stack_->addWidget(statisticsPage_);
    stack_->addWidget(wordBooksPage_);
    stack_->setCurrentWidget(homePage_);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    connect(stack_, &QStackedWidget::currentChanged, this, [this](int) {
        updateStudyTimeTracking();
    });
    studyIdleTimer_ = new QTimer(this);
    studyIdleTimer_->setInterval(5000);
    connect(studyIdleTimer_, &QTimer::timeout, this, &VibeSpellerWindow::updateStudyTimeTracking);
    studyIdleTimer_->start();

    connect(homePage_, &HomePageWidget::startLearningClicked, this, &VibeSpellerWindow::onStartLearning);
    connect(homePage_, &HomePageWidget::startReviewClicked, this, &VibeSpellerWindow::onStartReview);
    connect(homePage_, &HomePageWidget::startCountabilityLearningClicked, this, &VibeSpellerWindow::onStartCountabilityLearning);
    connect(homePage_, &HomePageWidget::startCountabilityReviewClicked, this, &VibeSpellerWindow::onStartCountabilityReview);
    connect(homePage_, &HomePageWidget::booksClicked, this, &VibeSpellerWindow::onOpenWordBooks);
    connect(homePage_, &HomePageWidget::statsClicked, this, [this]() {
        statisticsPage_->setLogs(db_.fetchWeeklyLogs());
        stack_->setCurrentWidget(statisticsPage_);
    });

    connect(statisticsPage_, &StatisticsPageWidget::backClicked, this, [this]() {
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
            [this](int wordCol, int transCol, int phoCol, int countCol, int plurCol, int noteCol) {
                if (pendingCsvPath_.isEmpty()) {
                    showWarningPrompt(this,
                                      QStringLiteral("导入失败"),
                                      QStringLiteral("没有可导入的 CSV 文件路径。"));
                    return;
                }

                int importedCount = 0;
                if (!db_.importFromCsv(pendingCsvPath_, wordCol, transCol, phoCol, countCol, plurCol, noteCol, importedCount)) {
                    showErrorPrompt(this,
                                    QStringLiteral("导入失败"),
                                    QStringLiteral("CSV 导入失败：%1").arg(db_.lastError()));
                    return;
                }

                showInfoPrompt(this,
                               QStringLiteral("导入完成"),
                               QStringLiteral("成功导入 %1 条新单词。\n重复单词会自动跳过。")
                                   .arg(importedCount));

                pendingCsvPath_.clear();
                refreshHomeCounts();
                refreshWordBooks();
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
    connect(countabilityPage_, &CountabilityPageWidget::exitRequested, this, &VibeSpellerWindow::onExitSession);
    connect(countabilityPage_, &CountabilityPageWidget::answerSubmitted, this, &VibeSpellerWindow::onCountabilityAnswer);
    connect(countabilityPage_, &CountabilityPageWidget::continueRequested, this, &VibeSpellerWindow::moveToNextCountabilityWord);

    connect(summaryPage_, &SummaryPageWidget::backHomeClicked, this, [this]() {
        refreshHomeCounts();
        stack_->setCurrentWidget(homePage_);
    });
    connect(summaryPage_, &SummaryPageWidget::nextGroupClicked, this, &VibeSpellerWindow::continueNextGroup);
    connect(wordBooksPage_, &WordBooksPageWidget::backClicked, this, [this]() {
        refreshHomeCounts();
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
    pendingHomeLaunchRect_ = homePage_->launchRect(SessionMode::Learning);
    if (tryResumeSession(SessionMode::Learning)) {
        return;
    }

    const QVector<WordItem> words = db_.fetchLearningBatch(kSessionBatchSize);
    if (words.isEmpty()) {
        const bool answer = showQuestionPrompt(this,
                                               QStringLiteral("暂无学习任务"),
                                               QStringLiteral("当前没有未学习单词。现在导入 CSV 吗？"));
        if (answer) {
            pickCsvAndShowMapping(false);
        }
        return;
    }

    startSession(SessionMode::Learning, words, 0);
}

void VibeSpellerWindow::onStartReview() {
    pendingHomeLaunchRect_ = homePage_->launchRect(SessionMode::Review);
    if (tryResumeSession(SessionMode::Review)) {
        return;
    }

    const QVector<WordItem> words = db_.fetchReviewBatch(QDateTime::currentDateTime(), kSessionBatchSize);
    if (words.isEmpty()) {
        const int tomorrowCount = db_.dueReviewCount(QDateTime::currentDateTime().addDays(1));
        showInfoPrompt(this,
                       QStringLiteral("暂无复习任务"),
                       QStringLiteral("今天已经复习完，明天需要复习%1个").arg(tomorrowCount));
        return;
    }

    startSession(SessionMode::Review, words, 0);
}

void VibeSpellerWindow::onStartCountabilityLearning() {
    pendingHomeLaunchRect_ = homePage_->launchRect(SessionMode::CountabilityLearning);
    if (tryResumeSession(SessionMode::CountabilityLearning)) {
        return;
    }

    const QVector<WordItem> words = db_.fetchCountabilityLearningBatch(kSessionBatchSize);
    if (words.isEmpty()) {
        showInfoPrompt(this,
                       QStringLiteral("暂无辨析任务"),
                       QStringLiteral("当前词书没有可进行可数性辨析的新词。"));
        return;
    }
    startSession(SessionMode::CountabilityLearning, words, 0);
}

void VibeSpellerWindow::onStartCountabilityReview() {
    pendingHomeLaunchRect_ = homePage_->launchRect(SessionMode::CountabilityReview);
    if (tryResumeSession(SessionMode::CountabilityReview)) {
        return;
    }

    const QVector<WordItem> words = db_.fetchCountabilityReviewBatch(QDateTime::currentDateTime(), kSessionBatchSize);
    if (words.isEmpty()) {
        const int tomorrowCount = db_.countabilityDueReviewCount(QDateTime::currentDateTime().addDays(1));
        showInfoPrompt(this,
                       QStringLiteral("暂无复习任务"),
                       QStringLiteral("今天已经完成可数性复习，明天需要复习%1个").arg(tomorrowCount));
        return;
    }
    startSession(SessionMode::CountabilityReview, words, 0);
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
        } else {
            db_.incrementDailyCount(currentMode_ == SessionMode::Learning);
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
        } else {
            db_.incrementDailyCount(currentMode_ == SessionMode::Learning);
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
            // 修复问题2：可数性训练的词数不写入拼写学习统计，避免 learning_count 被虚高。
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
            // 本词本轮有过错误，这是最后一次出现且答对：统计为不熟悉（与拼写模块逻辑一致）。
            PracticeRecord record;
            record.word = current;
            record.result = SpellingResult::Unfamiliar;
            record.userInput = firstWrongInputs_.value(current.id, countabilityAnswerText(answer));
            records_.push_back(record);
        }
        // 修复Bug#1：只有在「最终」出现时才清除追踪状态；若队列里还有同词（中间出现），
        // 保留 countabilityWrongCounts_ / firstWrongInputs_ 以便下次出现能正确判断历史。
        if (!hasSameWordAhead()) {
            countabilityWrongCounts_.remove(current.id);
            firstWrongInputs_.remove(current.id);
        }
        persistCurrentSession();
        delayedAdvance();
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

void VibeSpellerWindow::onProceedAfterFeedback() {
    if (currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        return;
    }
    moveToNextWord();
}

void VibeSpellerWindow::onExitSession() {
    if (currentWords_.isEmpty() || currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        stack_->setCurrentWidget(homePage_);
        return;
    }

    persistCurrentSession();
    refreshHomeCounts();
    QWidget *sourcePage = (currentMode_ == SessionMode::CountabilityLearning || currentMode_ == SessionMode::CountabilityReview)
                             ? (QWidget *)countabilityPage_
                             : (QWidget *)spellingPage_;
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
    refreshWordBooks();
    stack_->setCurrentWidget(wordBooksPage_);
}

void VibeSpellerWindow::onSelectWordBook(int bookId) {
    if (bookId <= 0) {
        return;
    }
    if (bookId == db_.activeWordBookId()) {
        return;
    }

    const bool confirmSwitch = showQuestionPrompt(
        this,
        QStringLiteral("切换词书"),
        QStringLiteral("是否切换词书？当前词书进度会保留，已学单词正常推送复习"));
    if (!confirmSwitch) {
        return;
    }

    if (!db_.setActiveWordBook(bookId)) {
        showWarningPrompt(this,
                          QStringLiteral("切换失败"),
                          QStringLiteral("切换当前词书失败：%1").arg(db_.lastError()));
        return;
    }

    clearSessionForMode(SessionMode::Learning);
    clearSessionForMode(SessionMode::CountabilityLearning);
    refreshHomeCounts();
    refreshWordBooks();
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
                                 || stack_->currentWidget() == countabilityPage_)
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
                              || stack_->currentWidget() == countabilityPage_)
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

    QDateTime cursor = start;
    while (cursor.date() < end.date()) {
        const QDateTime endOfDay(cursor.date(), QTime(23, 59, 59));
        const int seconds = static_cast<int>(cursor.secsTo(endOfDay) + 1);
        if (seconds > 0) {
            db_.addDailyStudySeconds(seconds, cursor.date());
        }
        cursor = endOfDay.addSecs(1);
    }

    const int finalSeconds = static_cast<int>(cursor.secsTo(end));
    if (finalSeconds > 0) {
        db_.addDailyStudySeconds(finalSeconds, cursor.date());
    }
}

void VibeSpellerWindow::initializeDatabase() {
    const QString dbPath = QStringLiteral(VIBESPELLER_SOURCE_DIR) + QStringLiteral("/vibespeller.db");

    if (!db_.open(dbPath)) {
        showErrorPrompt(this,
                        QStringLiteral("数据库错误"),
                        QStringLiteral("无法打开数据库：%1").arg(db_.lastError()));
        return;
    }

    if (!db_.initialize()) {
        showErrorPrompt(this,
                        QStringLiteral("数据库错误"),
                        QStringLiteral("无法初始化数据库：%1").arg(db_.lastError()));
    }
}

void VibeSpellerWindow::refreshHomeCounts() {
    db_.reconcileFirstDayDailyLog();

    // 首页展示“总任务量”：
    // 学习 = 当前词书剩余未学总数；复习 = 今天到期需复习总数。
    const int learning = db_.unlearnedCount();
    const int review = db_.dueReviewCount(QDateTime::currentDateTime());
    const int countabilityLearning = db_.countabilityUnlearnedCount();
    const int countabilityReview = db_.countabilityDueReviewCount(QDateTime::currentDateTime());

    int todayLearning = 0;
    int todayReview = 0;
    const QVector<DatabaseManager::DailyLog> logs = db_.fetchWeeklyLogs();
    if (!logs.isEmpty()) {
        todayLearning = logs.last().learningCount;
        todayReview = logs.last().reviewCount;
    }

    homePage_->setCounts(learning,
                         review,
                         todayLearning,
                         todayReview,
                         countabilityLearning,
                         countabilityReview);
}

void VibeSpellerWindow::refreshWordBooks() {
    wordBooksPage_->setWordBooks(db_.fetchWordBooks(), db_.activeWordBookId());
}

void VibeSpellerWindow::requestCsvImportIfNeeded() {
    const int totalWords = db_.unlearnedCount() + db_.dueReviewCount(QDateTime::currentDateTime());
    if (totalWords > 0) {
        return;
    }

    const bool answer = showQuestionPrompt(this,
                                           QStringLiteral("导入词库"),
                                           QStringLiteral("首次使用需要导入 CSV 词库。现在导入吗？"));
    if (answer) {
        pickCsvAndShowMapping(false);
    }
}

bool VibeSpellerWindow::pickCsvAndShowMapping(bool returnToWordBooks) {
    const QString csvPath = QFileDialog::getOpenFileName(this,
                                                          QStringLiteral("选择 CSV 词库"),
                                                          QDir::homePath(),
                                                          QStringLiteral("CSV Files (*.csv);;All Files (*)"));
    if (csvPath.isEmpty()) {
        return false;
    }

    QStringList headers;
    QVector<QStringList> previewRows;
    if (!db_.readCsvPreview(csvPath, headers, previewRows)) {
        showErrorPrompt(this,
                        QStringLiteral("读取失败"),
                        QStringLiteral("读取 CSV 失败：%1").arg(db_.lastError()));
        return false;
    }

    pendingCsvPath_ = csvPath;
    returnToWordBooksAfterImport_ = returnToWordBooks;
    mappingPage_->setCsvData(csvPath, headers, previewRows);
    stack_->setCurrentWidget(mappingPage_);
    return true;
}

bool VibeSpellerWindow::tryResumeSession(SessionMode mode) {
    QVector<WordItem> savedWords;
    int savedIndex = 0;
    if (!db_.loadSessionProgress(modeKey(mode), savedWords, savedIndex)) {
        return false;
    }

    startSession(mode, std::move(savedWords), savedIndex);
    return true;
}

void VibeSpellerWindow::startSession(SessionMode mode, QVector<WordItem> words, int startIndex) {
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
            candidates = db_.fetchReviewBatch(QDateTime::currentDateTime(), fetchLimit);
            break;
        case SessionMode::Learning:
            candidates = db_.fetchLearningBatch(fetchLimit);
            break;
        case SessionMode::CountabilityLearning:
            candidates = db_.fetchCountabilityLearningBatch(fetchLimit);
            break;
        case SessionMode::CountabilityReview:
            candidates = db_.fetchCountabilityReviewBatch(QDateTime::currentDateTime(), fetchLimit);
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
        clearSessionForMode(mode);
        return;
    }

    persistCurrentSession();
    showCurrentWord();
    QWidget *targetPage = (mode == SessionMode::CountabilityLearning || mode == SessionMode::CountabilityReview)
                             ? (QWidget *)countabilityPage_
                             : (QWidget *)spellingPage_;
    animateHomeToPageTransition(pendingHomeLaunchRect_, targetPage);
    pendingHomeLaunchRect_ = QRect();
}

void VibeSpellerWindow::animateHomeToPageTransition(const QRect &sourceRect, QWidget *targetPage) {
    const QRect stackRect = stack_->geometry();
    if (sourceRect.isEmpty() || stack_->currentWidget() != homePage_ || stackRect.isEmpty() || targetPage == nullptr) {
        stack_->setCurrentWidget(targetPage);
        targetPage->setFocus();
        return;
    }

    QRect startRect(homePage_->mapTo(this, sourceRect.topLeft()), sourceRect.size());
    startRect = startRect.intersected(rect());
    if (startRect.isEmpty()) {
        stack_->setCurrentWidget(targetPage);
        targetPage->setFocus();
        return;
    }

    const QRect endRect = stackRect.intersected(rect());
    if (endRect.isEmpty()) {
        stack_->setCurrentWidget(targetPage);
        targetPage->setFocus();
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
        return;
    }

    auto *transitionHost = new QWidget(this);
    transitionHost->setAttribute(Qt::WA_TransparentForMouseEvents);
    transitionHost->setAttribute(Qt::WA_NoSystemBackground);
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

    auto *homeOpacity = ensureOpacityEffect(homePage_);
    homeOpacity->setOpacity(1.0);
    auto *homeFadeAnim = new QPropertyAnimation(homeOpacity, "opacity", group);
    homeFadeAnim->setDuration(kPageLaunchDurationMs);
    homeFadeAnim->setStartValue(1.0);
    homeFadeAnim->setEndValue(0.0);
    homeFadeAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(group, &QParallelAnimationGroup::finished, this,
            [this, group, transitionHost, homeOpacity, targetPage]() {
        homeOpacity->setOpacity(1.0);
        stack_->setCurrentWidget(targetPage);
        targetPage->setFocus();
        transitionHost->deleteLater();
        group->deleteLater();
    });

    group->start();
}

void VibeSpellerWindow::animatePageToHomeTransition(QWidget *sourcePage, const QRect &targetRect) {
    const QRect stackRect = stack_->geometry();
    if (targetRect.isEmpty() || stack_->currentWidget() != sourcePage || stackRect.isEmpty() || sourcePage == nullptr) {
        stack_->setCurrentWidget(homePage_);
        return;
    }

    QRect endRect(homePage_->mapTo(this, targetRect.topLeft()), targetRect.size());
    endRect = endRect.intersected(rect());
    if (endRect.isEmpty()) {
        stack_->setCurrentWidget(homePage_);
        return;
    }

    const QRect startRect = stackRect.intersected(rect());
    if (startRect.isEmpty()) {
        stack_->setCurrentWidget(homePage_);
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
        return;
    }

    auto *homeOpacity = ensureOpacityEffect(homePage_);
    homeOpacity->setOpacity(0.0);
    stack_->setCurrentWidget(homePage_);

    auto *transitionHost = new QWidget(this);
    transitionHost->setAttribute(Qt::WA_TransparentForMouseEvents);
    transitionHost->setAttribute(Qt::WA_NoSystemBackground);
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
            [group, transitionHost, homeOpacity]() {
        homeOpacity->setOpacity(1.0);
        transitionHost->deleteLater();
        group->deleteLater();
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
    } else {
        countabilityPage_->setWord(word,
                                   displayIndex,
                                   totalTarget,
                                   currentMode_ == SessionMode::CountabilityReview);
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
    }
}

void VibeSpellerWindow::clearSessionForMode(SessionMode mode) {
    if (!db_.clearSessionProgress(modeKey(mode))) {
        showWarningPrompt(this,
                          QStringLiteral("会话清理失败"),
                          QStringLiteral("清理练习进度失败：%1").arg(db_.lastError()));
    }
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
    ++currentIndex_;
    if (currentIndex_ >= currentWords_.size()) {
        finishSession();
        return;
    }
    persistCurrentSession();
    showCurrentWord();
}

void VibeSpellerWindow::finishSession() {
    clearSessionForMode(currentMode_);
    const bool reviewMode = (currentMode_ == SessionMode::Review || currentMode_ == SessionMode::CountabilityReview);
    summaryPage_->setSummary(records_, reviewMode);
    refreshHomeCounts();
    stack_->setCurrentWidget(summaryPage_);
}

void VibeSpellerWindow::continueNextGroup() {
    // 修复问题6：finishSession() 已经清除了会话断点，此处直接拉取新一批词即可，
    // 不需要经过 onStartXxx → tryResumeSession 的多余查询路径。
    QVector<WordItem> words;
    if (currentMode_ == SessionMode::Review) {
        words = db_.fetchReviewBatch(QDateTime::currentDateTime(), kSessionBatchSize);
        if (words.isEmpty()) {
            const int tomorrowCount = db_.dueReviewCount(QDateTime::currentDateTime().addDays(1));
            showInfoPrompt(this,
                           QStringLiteral("暂无复习任务"),
                           QStringLiteral("今天已经复习完，明天需要复习%1个").arg(tomorrowCount));
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
    } else if (currentMode_ == SessionMode::CountabilityLearning) {
        words = db_.fetchCountabilityLearningBatch(kSessionBatchSize);
        if (words.isEmpty()) {
            showInfoPrompt(this,
                           QStringLiteral("暂无辨析任务"),
                           QStringLiteral("当前词书没有可进行可数性辨析的新词。"));
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
    } else if (currentMode_ == SessionMode::CountabilityReview) {
        words = db_.fetchCountabilityReviewBatch(QDateTime::currentDateTime(), kSessionBatchSize);
        if (words.isEmpty()) {
            const int tomorrowCount = db_.countabilityDueReviewCount(QDateTime::currentDateTime().addDays(1));
            showInfoPrompt(this,
                           QStringLiteral("暂无复习任务"),
                           QStringLiteral("今天已经完成可数性复习，明天需要复习%1个").arg(tomorrowCount));
            refreshHomeCounts();
            stack_->setCurrentWidget(homePage_);
            return;
        }
    } else {
        words = db_.fetchLearningBatch(kSessionBatchSize);
        if (words.isEmpty()) {
            const bool answer = showQuestionPrompt(this,
                                                   QStringLiteral("暂无学习任务"),
                                                   QStringLiteral("当前没有未学习单词。现在导入 CSV 吗？"));
            if (answer) {
                pickCsvAndShowMapping(false);
            } else {
                refreshHomeCounts();
                stack_->setCurrentWidget(homePage_);
            }
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
    }
    return QStringLiteral("learning");
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
