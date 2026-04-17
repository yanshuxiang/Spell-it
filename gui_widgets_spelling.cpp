#include "gui_widgets.h"
#include "gui_widgets_internal.h"

#include <QDateTime>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

using namespace GuiWidgetsInternal;
SpellingPageWidget::SpellingPageWidget(QWidget *parent)
    : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 34);
    root->setSpacing(8);

    auto *top = new QHBoxLayout();
    exitButton_ = new QPushButton(QStringLiteral("退出"), this);
    exitButton_->setFixedSize(104, 42);
    exitButton_->setStyleSheet(QStringLiteral("font-size: 12px; border-radius: 12px;"));

    modeLabel_ = new QLabel(this);
    modeLabel_->setStyleSheet(QStringLiteral("font-size: 12px; color: #4b5563;"));

    progressLabel_ = new QLabel(this);
    progressLabel_->setStyleSheet(QStringLiteral("font-size: 12px; color: #4b5563;"));

    skipForeverButton_ = new QPushButton(this);
    skipForeverButton_->setFixedSize(40, 40);
    skipForeverButton_->setIcon(createTrashLineIcon());
    skipForeverButton_->setIconSize(QSize(25, 25));
    skipForeverButton_->setCursor(Qt::PointingHandCursor);
    skipForeverButton_->setToolTip(QString());
    skipForeverButton_->setMouseTracking(true);
    skipForeverButton_->setAttribute(Qt::WA_Hover, true);
    skipForeverButton_->installEventFilter(this);
    skipForeverButton_->setStyleSheet(QStringLiteral(
        "border: none;"
        "background: transparent;"
        "padding: 0;"
        "margin: 0;"));

    skipForeverTip_ = new QLabel(QStringLiteral("双击标熟"), this);
    skipForeverTip_->setAttribute(Qt::WA_TransparentForMouseEvents);
    skipForeverTip_->setVisible(false);
    skipForeverTip_->setStyleSheet(QStringLiteral(
        "background: rgba(15, 23, 42, 0.92);"
        "color: #ffffff;"
        "border-radius: 10px;"
        "padding: 5px 9px;"
        "font-size: 12px;"
        "font-weight: 600;"));

    top->addWidget(exitButton_);
    top->addStretch();
    top->addWidget(modeLabel_);
    top->addStretch();
    top->addWidget(skipForeverButton_, 0, Qt::AlignVCenter);
    top->addSpacing(4);
    top->addWidget(progressLabel_);
    top->setContentsMargins(4, 2, 4, 8);

    translationLabel_ = new QLabel(this);
    translationLabel_->setAlignment(Qt::AlignCenter);
    translationLabel_->setWordWrap(true);
    translationLabel_->setFixedHeight(170);
    translationLabel_->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: #111827;"));

    inputEdit_ = new QLineEdit(this);
    inputEdit_->setPlaceholderText(QString());
    inputEdit_->setAlignment(Qt::AlignCenter);
    inputEdit_->setFixedWidth(310);
    QFont spellingInputFont = inputEdit_->font();
    spellingInputFont.setPixelSize(30);
    inputEdit_->setFont(spellingInputFont);
    const int inputHeight = qMax(64, QFontMetrics(spellingInputFont).height() + 26);
    inputEdit_->setFixedHeight(inputHeight);
    inputEdit_->setFrame(false);
    inputEdit_->setTextMargins(0, 0, 0, 0);
    inputEdit_->setStyleSheet(defaultInputStyle());
    inputEdit_->installEventFilter(this);

    feedbackLabel_ = new QLabel(this);
    feedbackLabel_->setAlignment(Qt::AlignCenter);
    feedbackLabel_->setWordWrap(true);
    feedbackLabel_->setFixedHeight(56);
    feedbackLabel_->setStyleSheet(QStringLiteral("font-size: 14px; color: #6b7280;"));

    debugHost_ = new QWidget(this);
    debugHost_->setVisible(false);
    auto *debugLayout = new QVBoxLayout(debugHost_);
    debugLayout->setContentsMargins(0, 0, 0, 0);
    debugLayout->setSpacing(8);

    debugScheduleLabel_ = new QLabel(debugHost_);
    debugScheduleLabel_->setTextFormat(Qt::RichText);
    debugScheduleLabel_->setWordWrap(true);
    debugScheduleLabel_->setStyleSheet(QStringLiteral(
        "background: #f8fafc;"
        "border: 1px solid #e2e8f0;"
        "border-radius: 12px;"
        "padding: 10px 12px;"
        "font-size: 12px;"
        "color: #334155;"));

    debugAccuracyLabel_ = new QLabel(debugHost_);
    debugAccuracyLabel_->setWordWrap(true);
    debugAccuracyLabel_->setStyleSheet(QStringLiteral(
        "background: #f8fafc;"
        "border: 1px solid #e2e8f0;"
        "border-radius: 12px;"
        "padding: 10px 12px;"
        "font-size: 13px;"
        "font-weight: 600;"
        "color: #0f172a;"));

    debugLayout->addWidget(debugScheduleLabel_);
    debugLayout->addWidget(debugAccuracyLabel_);

    root->addLayout(top);
    root->addStretch(1);
    root->addWidget(translationLabel_);
    root->addSpacing(18);
    auto *inputRow = new QHBoxLayout();
    inputRow->addStretch(1);
    inputRow->addWidget(inputEdit_, 0, Qt::AlignHCenter);
    inputRow->addStretch(1);
    root->addLayout(inputRow);
    root->addSpacing(16);
    root->addWidget(feedbackLabel_);
    root->addWidget(debugHost_);
    root->addStretch(2);
    root->addSpacing(10);

    connect(inputEdit_, &QLineEdit::textEdited, this, &SpellingPageWidget::userActivity);
    connect(inputEdit_, &QLineEdit::returnPressed, this, [this]() {
        emit userActivity();
        emit submitted(inputEdit_->text());
    });
    connect(exitButton_, &QPushButton::clicked, this, [this]() {
        emit userActivity();
        emit exitRequested();
    });
    connect(skipForeverButton_, &QPushButton::clicked, this, [this]() {
        // 单击不执行动作，避免误触；永久跳过只通过双击触发。
    });
}

void SpellingPageWidget::setWord(const WordItem &word, int currentIndex, int totalCount, bool isReviewMode) {
    inTransition_ = false;
    resetInputOnNextType_ = false;
    modeLabel_->setText(isReviewMode ? QStringLiteral("复习模式") : QStringLiteral("学习模式"));
    progressLabel_->setText(QStringLiteral("%1 / %2").arg(currentIndex).arg(totalCount));
    translationLabel_->setText(word.translation);
    inputEdit_->clear();
    translationLabel_->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: #111827;"));
    applyInputDefaultStyle();
    clearFeedback();
    setAwaitingProceed(false);
    setInputEnabled(true);
    refreshAnimationBasePositions();
    inputEdit_->setFocus();
}

void SpellingPageWidget::setInputEnabled(bool enabled) {
    setAwaitingProceed(!enabled);
    inputEdit_->setEnabled(enabled);
    if (!enabled) {
        setFocus(Qt::OtherFocusReason);
    }
}

void SpellingPageWidget::applyInputDefaultStyle() {
    inputEdit_->setStyleSheet(defaultInputStyle());
}

void SpellingPageWidget::refreshAnimationBasePositions() {
    if (layout() != nullptr) {
        layout()->activate();
    }
    translationBasePos_ = translationLabel_->pos();
    inputBasePos_ = inputEdit_->pos();
}

void SpellingPageWidget::showFeedback(const QString &text, const QString &colorHex) {
    feedbackLabel_->setText(text);
    feedbackLabel_->setStyleSheet(
        QStringLiteral("font-size: 20px; font-weight: 700; color: %1; padding-top: 4px;").arg(colorHex));
}

void SpellingPageWidget::clearFeedback() {
    feedbackLabel_->setText(QString());
    feedbackLabel_->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600; color: #6b7280;"));
}

void SpellingPageWidget::setDebugMode(bool enabled) {
    debugMode_ = enabled;
    debugHost_->setVisible(enabled);
    if (!enabled) {
        clearDebugInfo();
    }
}

void SpellingPageWidget::setDebugInfo(const QDateTime &nextReview, int attemptCount, int correctCount) {
    if (!debugMode_) {
        return;
    }

    debugScheduleLabel_->setText(buildMiniWeekCalendarHtml(nextReview));
    double accuracy = 0.0;
    if (attemptCount > 0) {
        accuracy = (100.0 * static_cast<double>(correctCount)) / static_cast<double>(attemptCount);
    }
    debugAccuracyLabel_->setText(
        QStringLiteral("正确率：%1%  （正确 %2 / 总尝试 %3）")
            .arg(QString::number(accuracy, 'f', 1))
            .arg(correctCount)
            .arg(attemptCount));
}

void SpellingPageWidget::clearDebugInfo() {
    debugScheduleLabel_->setText(QString());
    debugAccuracyLabel_->setText(QString());
}

void SpellingPageWidget::showSkipForeverTip(const QPoint &anchorPos) {
    if (!skipForeverTip_) {
        return;
    }

    skipForeverTip_->adjustSize();
    QPoint popupPos = anchorPos - QPoint(skipForeverTip_->width() + 10, skipForeverTip_->height() + 10);
    popupPos.setX(qBound(6, popupPos.x(), width() - skipForeverTip_->width() - 6));
    popupPos.setY(qBound(6, popupPos.y(), height() - skipForeverTip_->height() - 6));
    skipForeverTip_->move(popupPos);
    skipForeverTip_->show();
}

void SpellingPageWidget::hideSkipForeverTip() {
    if (skipForeverTip_) {
        skipForeverTip_->hide();
    }
}

void SpellingPageWidget::playCorrectTransition(const WordItem &currentWord,
                                               const WordItem &nextWord,
                                               int nextIndex,
                                               int totalCount,
                                               bool isReviewMode) {
    Q_UNUSED(currentWord);
    if (inTransition_) {
        return;
    }
    inTransition_ = true;

    setInputEnabled(false);
    setAwaitingProceed(false);

    refreshAnimationBasePositions();

    auto *currentTranslation = new QLabel(translationLabel_->text(), this);
    currentTranslation->setAlignment(Qt::AlignCenter);
    currentTranslation->setWordWrap(true);
    currentTranslation->setGeometry(translationLabel_->geometry());
    currentTranslation->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: #111827;"));
    auto *currentTransOpacity = new QGraphicsOpacityEffect(currentTranslation);
    currentTransOpacity->setOpacity(1.0);
    currentTranslation->setGraphicsEffect(currentTransOpacity);
    currentTranslation->show();

    auto *nextTranslation = new QLabel(nextWord.translation, this);
    nextTranslation->setAlignment(Qt::AlignCenter);
    nextTranslation->setWordWrap(true);
    nextTranslation->setGeometry(translationLabel_->geometry());
    nextTranslation->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: #111827;"));
    auto *nextTranslationOpacity = new QGraphicsOpacityEffect(nextTranslation);
    nextTranslationOpacity->setOpacity(0.0);
    nextTranslation->setGraphicsEffect(nextTranslationOpacity);
    nextTranslation->move(translationBasePos_ + QPoint(kTransitionShiftPx, 0));
    nextTranslation->show();

    modeLabel_->setText(isReviewMode ? QStringLiteral("复习模式") : QStringLiteral("学习模式"));
    progressLabel_->setText(QStringLiteral("%1 / %2").arg(nextIndex).arg(totalCount));
    resetInputOnNextType_ = false;
    clearFeedback();
    translationLabel_->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: #111827;"));
    applyInputDefaultStyle();
    inputEdit_->clear();
    // 保持布局内真实控件可见，避免 setVisible(false) 触发布局重排导致“瞬移”。
    // 动画只作用于叠加层，真实控件通过透明度临时隐藏。
    if (auto *translationEffect = ensureOpacityEffect(translationLabel_)) {
        translationEffect->setOpacity(0.0);
    }

    auto *group = new QParallelAnimationGroup(this);

    auto *outTransMove = new QPropertyAnimation(currentTranslation, "pos", group);
    outTransMove->setDuration(kCorrectTransitionMs);
    outTransMove->setStartValue(translationBasePos_);
    outTransMove->setEndValue(translationBasePos_ - QPoint(kTransitionShiftPx, 0));

    auto *outTransFade = new QPropertyAnimation(currentTransOpacity, "opacity", group);
    outTransFade->setDuration(kCorrectTransitionMs);
    outTransFade->setKeyValueAt(0.0, 1.0);
    outTransFade->setKeyValueAt(0.45, 0.0);
    outTransFade->setKeyValueAt(1.0, 0.0);

    auto *inTransMove = new QPropertyAnimation(nextTranslation, "pos", group);
    inTransMove->setDuration(kCorrectTransitionMs);
    inTransMove->setStartValue(translationBasePos_ + QPoint(kTransitionShiftPx, 0));
    inTransMove->setEndValue(translationBasePos_);

    auto *inTransFade = new QPropertyAnimation(nextTranslationOpacity, "opacity", group);
    inTransFade->setDuration(kCorrectTransitionMs);
    inTransFade->setKeyValueAt(0.0, 0.0);
    inTransFade->setKeyValueAt(0.35, 0.0);
    inTransFade->setKeyValueAt(1.0, 1.0);

    connect(group, &QParallelAnimationGroup::finished, this,
            [this, group, currentTranslation, nextTranslation, nextWord]() {
        currentTranslation->deleteLater();
        nextTranslation->deleteLater();
        translationLabel_->setText(nextWord.translation);
        if (auto *translationEffect = ensureOpacityEffect(translationLabel_)) {
            translationEffect->setOpacity(1.0);
        }
        applyInputDefaultStyle();
        inTransition_ = false;
        setInputEnabled(true);
        inputEdit_->setFocus();
        emit correctTransitionFinished();
        group->deleteLater();
    });

    group->start();
}

void SpellingPageWidget::playWrongShake() {
    if (inTransition_) {
        return;
    }

    refreshAnimationBasePositions();
    resetInputOnNextType_ = true;

    translationLabel_->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: #111827;"));
    inputEdit_->setStyleSheet(QStringLiteral(
        "border: none;"
        "border-bottom: 2px solid #e5e7eb;"
        "color: transparent;"
        "padding: 6px 8px;"
        "background: transparent;"));

    auto *shakeWordLabel = new QLabel(inputEdit_->text(), this);
    shakeWordLabel->setAlignment(Qt::AlignCenter);
    shakeWordLabel->setGeometry(inputEdit_->geometry());
    shakeWordLabel->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 500; color: #ef4444;"));
    shakeWordLabel->show();

    auto *group = new QParallelAnimationGroup(this);
    auto *shakeInput = new QPropertyAnimation(shakeWordLabel, "pos", group);
    shakeInput->setDuration(kWrongShakeMs);
    shakeInput->setKeyValueAt(0.0, inputBasePos_);
    shakeInput->setKeyValueAt(0.2, inputBasePos_ + QPoint(-kWrongShakeOffsetPx, 0));
    shakeInput->setKeyValueAt(0.4, inputBasePos_ + QPoint(kWrongShakeOffsetPx, 0));
    shakeInput->setKeyValueAt(0.6, inputBasePos_ + QPoint(-(kWrongShakeOffsetPx * 3) / 4, 0));
    shakeInput->setKeyValueAt(0.8, inputBasePos_ + QPoint((kWrongShakeOffsetPx * 3) / 4, 0));
    shakeInput->setKeyValueAt(1.0, inputBasePos_);

    connect(group, &QParallelAnimationGroup::finished, this, [this, group, shakeWordLabel]() {
        shakeWordLabel->deleteLater();
        inputEdit_->setStyleSheet(QStringLiteral(
            "border: none;"
            "border-bottom: 2px solid #e5e7eb;"
            "color: #ef4444;"
            "padding: 6px 8px;"
            "background: transparent;"));
        group->deleteLater();
    });
    group->start();
}

void SpellingPageWidget::keyPressEvent(QKeyEvent *event) {
    if (awaitingProceed_
        && (event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Enter
            || event->key() == Qt::Key_Space)) {
        if (!proceedKeyArmed_) {
            event->accept();
            return;
        }
        proceedKeyArmed_ = false;
        emit userActivity();
        emit proceedRequested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool SpellingPageWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched == skipForeverButton_) {
        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::HoverEnter: {
            const QPoint anchorPos = skipForeverButton_->mapToParent(skipForeverButton_->rect().center());
            showSkipForeverTip(anchorPos);
            break;
        }
        case QEvent::MouseMove: {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPoint anchorPos = skipForeverButton_->mapToParent(mouseEvent->pos());
            showSkipForeverTip(anchorPos);
            break;
        }
        case QEvent::Leave:
        case QEvent::HoverLeave:
            hideSkipForeverTip();
            break;
        case QEvent::MouseButtonDblClick:
            emit userActivity();
            emit skipForeverRequested();
            hideSkipForeverTip();
            return true;
        default:
            break;
        }
    }

    if (watched == inputEdit_ && event->type() == QEvent::KeyPress && resetInputOnNextType_) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const bool isReturnKey = (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter);
        if (isReturnKey) {
            return QWidget::eventFilter(watched, event);
        }
        const bool isBackspaceKey = (keyEvent->key() == Qt::Key_Backspace);

        const bool hasTextInput = !keyEvent->text().isEmpty()
                                  && !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if (hasTextInput || isBackspaceKey) {
            resetInputOnNextType_ = false;
            applyInputDefaultStyle();
            const QString typed = keyEvent->text();
            inputEdit_->clear();
            if (!isBackspaceKey) {
                inputEdit_->setText(typed);
            }
            emit userActivity();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SpellingPageWidget::setAwaitingProceed(bool awaiting) {
    awaitingProceed_ = awaiting;
    if (!awaitingProceed_) {
        proceedKeyArmed_ = false;
        return;
    }

    // Arm on next event-loop turn so the same Enter used for submit
    // won't immediately trigger "next word".
    proceedKeyArmed_ = false;
    QTimer::singleShot(0, this, [this]() {
        if (awaitingProceed_) {
            proceedKeyArmed_ = true;
        }
    });
}
