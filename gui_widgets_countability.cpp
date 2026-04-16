#include "gui_widgets.h"
#include "gui_widgets_internal.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

using namespace GuiWidgetsInternal;

CountabilityPageWidget::CountabilityPageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 20);
    root->setSpacing(0);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(10);

    exitButton_ = new QPushButton(QStringLiteral("退出"), this);
    exitButton_->setFixedSize(96, 42);
    exitButton_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #f3f4f6;"
        "  color: #0f172a;"
        "  border-radius: 12px;"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background: #e8ebef; }"));

    modeLabel_ = new QLabel(QStringLiteral("可数性辨析"), this);
    modeLabel_->setAlignment(Qt::AlignCenter);
    modeLabel_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #334155;"));

    progressLabel_ = new QLabel(QStringLiteral("0 / 0"), this);
    progressLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progressLabel_->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 700; color: #475569;"));

    header->addWidget(exitButton_, 0, Qt::AlignLeft);
    header->addStretch(1);
    header->addWidget(modeLabel_, 0, Qt::AlignCenter);
    header->addStretch(1);
    header->addWidget(progressLabel_, 0, Qt::AlignRight);

    wordLabel_ = new QLabel(QStringLiteral("word"), this);
    wordLabel_->setAlignment(Qt::AlignCenter);
    wordLabel_->setMinimumHeight(64);
    wordLabel_->setStyleSheet(QStringLiteral("font-size: 56px; font-weight: 700; color: #0f172a;"));

    hintLabel_ = new QLabel(this);
    hintLabel_->setWordWrap(true);
    hintLabel_->setTextFormat(Qt::PlainText);
    hintLabel_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    hintLabel_->setMinimumHeight(120);
    hintLabel_->setMaximumHeight(170);
    hintLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    hintLabel_->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 600; color: #374151;"));


    countableButton_ = new QPushButton(QStringLiteral("可数"), this);
    uncountableButton_ = new QPushButton(QStringLiteral("不可数"), this);
    bothButton_ = new QPushButton(QStringLiteral("可数且不可数"), this);
    const QString answerButtonStyle = QStringLiteral(
        "QPushButton {"
        "  background: #f8fafc;"
        "  border: 1px solid #d8e1ec;"
        "  border-radius: 14px;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  color: #0f172a;"
        "  min-height: 54px;"
        "}"
        "QPushButton:hover { background: #eef3f9; }"
        "QPushButton:disabled { color: #94a3b8; background: #f8fafc; border-color: #e2e8f0; }");
    countableButton_->setStyleSheet(answerButtonStyle);
    uncountableButton_->setStyleSheet(answerButtonStyle);
    bothButton_->setStyleSheet(answerButtonStyle);

    auto *centerHost = new QWidget(this);
    auto *centerLayout = new QVBoxLayout(centerHost);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(14);
    centerLayout->addWidget(wordLabel_, 0, Qt::AlignHCenter);
    centerLayout->addWidget(hintLabel_, 0, Qt::AlignHCenter);

    root->addLayout(header);
    root->addStretch(3);
    root->addWidget(centerHost, 0, Qt::AlignHCenter);
    root->addStretch(3);
    root->addWidget(countableButton_);
    root->addSpacing(10);
    root->addWidget(uncountableButton_);
    root->addSpacing(10);
    root->addWidget(bothButton_);

    connect(exitButton_, &QPushButton::clicked, this, &CountabilityPageWidget::exitRequested);
    connect(countableButton_, &QPushButton::clicked, this,
            [this]() { emit answerSubmitted(CountabilityAnswer::Countable); });
    connect(uncountableButton_, &QPushButton::clicked, this,
            [this]() { emit answerSubmitted(CountabilityAnswer::Uncountable); });
    connect(bothButton_, &QPushButton::clicked, this,
            [this]() { emit answerSubmitted(CountabilityAnswer::Both); });
}

void CountabilityPageWidget::setWord(const WordItem &word, int currentIndex, int totalCount, bool isReviewMode) {
    modeLabel_->setText(isReviewMode ? QStringLiteral("可数性复习") : QStringLiteral("可数性辨析"));
    progressLabel_->setText(QStringLiteral("%1 / %2").arg(currentIndex).arg(qMax(1, totalCount)));
    wordLabel_->setText(word.word.trimmed().isEmpty() ? QStringLiteral("-") : word.word.trimmed());

    QString hint = word.translation.trimmed();
    if (hint.isEmpty()) {
        hint = QStringLiteral("请选择该词作为名词时的可数性");
    } else if (hint.size() > 120) {
        hint = hint.left(120) + QStringLiteral("…");
    }
    hintLabel_->setText(QStringLiteral("释义提示：%1").arg(hint));
    resetOptionStyles();
    setOptionsEnabled(true);
}



void CountabilityPageWidget::setOptionsEnabled(bool enabled) {
    countableButton_->setEnabled(enabled);
    uncountableButton_->setEnabled(enabled);
    bothButton_->setEnabled(enabled);
}

QPushButton *CountabilityPageWidget::buttonForAnswer(CountabilityAnswer answer) const {
    switch (answer) {
    case CountabilityAnswer::Countable:
        return countableButton_;
    case CountabilityAnswer::Uncountable:
        return uncountableButton_;
    case CountabilityAnswer::Both:
        return bothButton_;
    }
    return countableButton_;
}

void CountabilityPageWidget::resetOptionStyles() {
    const QString baseStyle = QStringLiteral(
        "QPushButton {"
        "  background: #f8fafc;"
        "  border: 1px solid #d8e1ec;"
        "  border-radius: 14px;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  color: #0f172a;"
        "  min-height: 54px;"
        "}"
        "QPushButton:hover { background: #eef3f9; }"
        "QPushButton:disabled { color: #94a3b8; background: #f8fafc; border-color: #e2e8f0; }");
    countableButton_->setStyleSheet(baseStyle);
    uncountableButton_->setStyleSheet(baseStyle);
    bothButton_->setStyleSheet(baseStyle);
}

void CountabilityPageWidget::showAnswerFeedback(CountabilityAnswer selected,
                                                CountabilityAnswer correct,
                                                bool isCorrect) {
    resetOptionStyles();
    QPushButton *selectedButton = buttonForAnswer(selected);
    QPushButton *correctButton = buttonForAnswer(correct);
    const QString greenStyle = QStringLiteral(
        "QPushButton {"
        "  background: rgba(134,239,172,0.28);"
        "  border: 1px solid #86efac;"
        "  border-radius: 14px;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  color: #166534;"
        "  min-height: 54px;"
        "}");
    const QString redStyle = QStringLiteral(
        "QPushButton {"
        "  background: rgba(252,165,165,0.28);"
        "  border: 1px solid #fca5a5;"
        "  border-radius: 14px;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  color: #991b1b;"
        "  min-height: 54px;"
        "}");

    if (isCorrect) {
        if (selectedButton != nullptr) {
            selectedButton->setStyleSheet(greenStyle);
        }
    } else {
        if (selectedButton != nullptr) {
            selectedButton->setStyleSheet(redStyle);
        }
        if (correctButton != nullptr) {
            correctButton->setStyleSheet(greenStyle);
        }
    }
}
