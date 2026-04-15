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
#include <QHash>
#include <QKeySequence>
#include <QProcess>
#include <QRegularExpression>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

using namespace GuiWidgetsInternal;
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
    summaryPage_ = new SummaryPageWidget(this);
    statisticsPage_ = new StatisticsPageWidget(this);
    wordBooksPage_ = new WordBooksPageWidget(this);
    debugMode_ = qApp->property("vibespeller_debug").toBool();
    spellingPage_->setDebugMode(debugMode_);
    pronunciationProcess_ = new QProcess(this);

    stack_->addWidget(homePage_);
    stack_->addWidget(mappingPage_);
    stack_->addWidget(spellingPage_);
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
            [this](int wordColumn, int translationColumn, int phoneticColumn) {
                if (pendingCsvPath_.isEmpty()) {
                    showWarningPrompt(this,
                                      QStringLiteral("导入失败"),
                                      QStringLiteral("没有可导入的 CSV 文件路径。"));
                    return;
                }

                int importedCount = 0;
                if (!db_.importFromCsv(pendingCsvPath_, wordColumn, translationColumn, phoneticColumn, importedCount)) {
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

    // 启动后若词库为空，提示导入 CSV。
    QTimer::singleShot(0, this, &VibeSpellerWindow::requestCsvImportIfNeeded);
}

void VibeSpellerWindow::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        updateStudyTimeTracking();
    }
}

void VibeSpellerWindow::closeEvent(QCloseEvent *event) {
    flushStudyTimeTracking();
    QWidget::closeEvent(event);
}

void VibeSpellerWindow::onStartLearning() {
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
    stack_->setCurrentWidget(homePage_);
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
    clearSessionForMode(SessionMode::Review);
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
    audioDownloadCancelRequested_ = false;
    wordBooksPage_->setAudioDownloadStatus(QStringLiteral("准备下载..."), 0, words.size(), true);

    AudioDownloader downloader;
    QString errorText;
    const AudioDownloader::Result result = downloader.downloadBookAudio(
        words,
        bookId,
        [this](int current, int total, const QString &word) {
            const int displayIndex = qMin(total, qMax(1, current + 1));
            const QString status = QStringLiteral("下载中：%1（%2/%3）")
                                       .arg(word)
                                       .arg(displayIndex)
                                       .arg(total);
            wordBooksPage_->setAudioDownloadStatus(status, current, total, true);
            QCoreApplication::processEvents();
        },
        [this]() {
            return audioDownloadCancelRequested_;
        },
        errorText);

    audioDownloadRunning_ = false;

    if (!errorText.isEmpty()) {
        const int processed = result.resumeStartIndex + result.downloaded + result.reused + result.noMp3 + result.failed;
        wordBooksPage_->setAudioDownloadStatus(
            QStringLiteral("下载异常：%1").arg(errorText),
            qMin(processed, result.totalWords),
            result.totalWords,
            false);
        return;
    }

    const int processed = result.resumeStartIndex + result.downloaded + result.reused + result.noMp3 + result.failed;
    const QString finalText = result.cancelled
                                  ? QStringLiteral("已暂停（%1/%2），下次会从上一个单词重下")
                                        .arg(qMin(processed, result.totalWords))
                                        .arg(result.totalWords)
                                  : QStringLiteral("完成：新增%1 已有%2 无MP3%3 失败%4")
                                        .arg(result.downloaded)
                                        .arg(result.reused)
                                        .arg(result.noMp3)
                                        .arg(result.failed);
    wordBooksPage_->setAudioDownloadStatus(finalText,
                                           qMin(processed, result.totalWords),
                                           result.totalWords,
                                           false);
}

void VibeSpellerWindow::onAudioDownloadStopRequested() {
    if (!audioDownloadRunning_) {
        return;
    }
    audioDownloadCancelRequested_ = true;
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
                             && stack_->currentWidget() == spellingPage_
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
                          && stack_->currentWidget() == spellingPage_
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

    int todayLearning = 0;
    int todayReview = 0;
    const QVector<DatabaseManager::DailyLog> logs = db_.fetchWeeklyLogs();
    if (!logs.isEmpty()) {
        todayLearning = logs.last().learningCount;
        todayReview = logs.last().reviewCount;
    }

    homePage_->setCounts(learning, review, todayLearning, todayReview);
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
        const QVector<WordItem> candidates = (mode == SessionMode::Review)
                                                 ? db_.fetchReviewBatch(QDateTime::currentDateTime(), fetchLimit)
                                                 : db_.fetchLearningBatch(fetchLimit);
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

    stack_->setCurrentWidget(spellingPage_);
    persistCurrentSession();
    showCurrentWord();
}

void VibeSpellerWindow::showCurrentWord() {
    if (currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        finishSession();
        return;
    }

    const WordItem &word = currentWords_.at(currentIndex_);
    const int totalTarget = qMax(1, sessionWordTargetCount_);
    const int displayIndex = qMin(records_.size() + 1, totalTarget);
    spellingPage_->setWord(word,
                           displayIndex,
                           totalTarget,
                           currentMode_ == SessionMode::Review);
    updateSpellingDebugInfo(word.id);
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

void VibeSpellerWindow::finishSession() {
    clearSessionForMode(currentMode_);
    summaryPage_->setSummary(records_, currentMode_ == SessionMode::Review);
    refreshHomeCounts();
    stack_->setCurrentWidget(summaryPage_);
}

void VibeSpellerWindow::continueNextGroup() {
    if (currentMode_ == SessionMode::Review) {
        onStartReview();
    } else {
        onStartLearning();
    }
}

QString VibeSpellerWindow::modeKey(SessionMode mode) const {
    switch (mode) {
    case SessionMode::Learning:
        return QStringLiteral("learning");
    case SessionMode::Review:
        return QStringLiteral("review");
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
