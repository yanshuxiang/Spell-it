#include "gui_widgets.h"

#include <QComboBox>
#include <QDialog>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace {
enum class PromptType {
    Info,
    Warning,
    Error,
    Question,
};

QString findBestColumn(const QStringList &headers, const QStringList &keywords) {
    for (const QString &keyword : keywords) {
        for (const QString &header : headers) {
            if (header.toLower().contains(keyword)) {
                return header;
            }
        }
    }
    return QString();
}

int countByResult(const QVector<PracticeRecord> &records, SpellingResult target) {
    int count = 0;
    for (const PracticeRecord &record : records) {
        if (record.result == target) {
            ++count;
        }
    }
    return count;
}

QString briefResult(SpellingResult result) {
    switch (result) {
    case SpellingResult::Mastered:
        return QStringLiteral("熟悉");
    case SpellingResult::Blurry:
        return QStringLiteral("模糊");
    case SpellingResult::Unfamiliar:
        return QStringLiteral("不熟悉");
    }
    return QStringLiteral("不熟悉");
}

QString summaryRightText(const PracticeRecord &record, bool reviewMode) {
    switch (record.result) {
    case SpellingResult::Mastered:
        return reviewMode ? QStringLiteral("复习完成") : QStringLiteral("1天后复习");
    case SpellingResult::Blurry:
        return QStringLiteral("1天后复习");
    case SpellingResult::Unfamiliar:
        return record.skipped ? QStringLiteral("跳过") : QStringLiteral("1天后复习");
    }
    return QStringLiteral("1天后复习");
}

QColor summaryRightColor(const PracticeRecord &record, bool reviewMode) {
    if (record.result == SpellingResult::Mastered && reviewMode) {
        return QColor(QStringLiteral("#0f766e"));
    }
    return QColor(QStringLiteral("#6b7280"));
}

QString coverColorForBook(int bookId) {
    static const QStringList colors = {
        QStringLiteral("#ef4444"),
        QStringLiteral("#10b981"),
        QStringLiteral("#84cc16"),
        QStringLiteral("#f59e0b"),
        QStringLiteral("#3b82f6"),
        QStringLiteral("#8b5cf6")
    };
    if (bookId < 0) {
        return colors.first();
    }
    return colors.at(bookId % colors.size());
}

QIcon createBooksLineIcon() {
    QPixmap pix(28, 28);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor("#111827"));
    pen.setWidthF(1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    painter.drawRoundedRect(QRectF(7, 5, 14, 9), 2, 2);
    painter.drawLine(QPointF(7, 9.5), QPointF(21, 9.5));
    painter.drawRoundedRect(QRectF(5, 13, 14, 9), 2, 2);
    painter.drawLine(QPointF(5, 17.5), QPointF(19, 17.5));
    return QIcon(pix);
}

QIcon createArchiveLineIcon() {
    QPixmap pix(28, 28);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor("#111827"));
    pen.setWidthF(1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    painter.drawRoundedRect(QRectF(5, 8, 18, 14), 2.5, 2.5);
    painter.drawLine(QPointF(9, 8), QPointF(12, 5));
    painter.drawLine(QPointF(19, 8), QPointF(16, 5));
    painter.drawLine(QPointF(9, 15), QPointF(19, 15));
    return QIcon(pix);
}

QIcon createChartLineIcon() {
    QPixmap pix(28, 28);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor("#111827"));
    pen.setWidthF(1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    painter.drawRoundedRect(QRectF(5, 5, 18, 18), 3, 3);
    painter.drawLine(QPointF(8, 18), QPointF(12, 14));
    painter.drawLine(QPointF(12, 14), QPointF(15, 16));
    painter.drawLine(QPointF(15, 16), QPointF(20, 10));
    return QIcon(pix);
}

QWidget *createSummaryRow(const PracticeRecord &record, bool reviewMode) {
    auto *rowWidget = new QWidget();
    auto *rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(2, 8, 2, 8);
    rowLayout->setSpacing(16);
    rowWidget->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *leftLabel = new QLabel(record.word.word, rowWidget);
    leftLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #111827;"));

    auto *rightLabel = new QLabel(summaryRightText(record, reviewMode), rowWidget);
    rightLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rightLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: %1;").arg(summaryRightColor(record, reviewMode).name()));

    rowLayout->addWidget(leftLabel, 1);
    rowLayout->addWidget(rightLabel, 0, Qt::AlignRight);
    return rowWidget;
}

QColor promptAccent(PromptType type) {
    switch (type) {
    case PromptType::Info:
        return QColor(QStringLiteral("#2563eb"));
    case PromptType::Warning:
        return QColor(QStringLiteral("#d97706"));
    case PromptType::Error:
        return QColor(QStringLiteral("#dc2626"));
    case PromptType::Question:
        return QColor(QStringLiteral("#111827"));
    }
    return QColor(QStringLiteral("#111827"));
}

QString promptSymbol(PromptType type) {
    switch (type) {
    case PromptType::Info:
        return QStringLiteral("i");
    case PromptType::Warning:
        return QStringLiteral("!");
    case PromptType::Error:
        return QStringLiteral("×");
    case PromptType::Question:
        return QStringLiteral("?");
    }
    return QStringLiteral("?");
}

int showStyledPrompt(QWidget *parent,
                     const QString &title,
                     const QString &message,
                     PromptType type,
                     const QStringList &buttons,
                     int defaultButtonIndex) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setMinimumWidth(380);
    dialog.setStyleSheet(QStringLiteral(
        "QDialog { background: #ffffff; }"
        "QLabel#PromptTitle { font-size: 16px; font-weight: 700; color: #111827; }"
        "QLabel#PromptBody { font-size: 13px; color: #374151; }"
        "QPushButton {"
        "  border: none;"
        "  border-radius: 12px;"
        "  background: #f3f4f6;"
        "  padding: 10px 18px;"
        "  min-width: 92px;"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover { background: #e5e7eb; }"
        "QPushButton:pressed { background: #d1d5db; }"));

    auto *root = new QVBoxLayout(&dialog);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(14);

    auto *row = new QHBoxLayout();
    row->setSpacing(14);

    auto *iconLabel = new QLabel(promptSymbol(type), &dialog);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(36, 36);
    iconLabel->setStyleSheet(QStringLiteral(
        "border-radius: 18px;"
        "font-size: 18px;"
        "font-weight: 700;"
        "background: %1;"
        "color: #ffffff;").arg(promptAccent(type).name()));

    auto *textColumn = new QVBoxLayout();
    auto *titleLabel = new QLabel(title, &dialog);
    titleLabel->setObjectName(QStringLiteral("PromptTitle"));
    auto *bodyLabel = new QLabel(message, &dialog);
    bodyLabel->setObjectName(QStringLiteral("PromptBody"));
    bodyLabel->setWordWrap(true);
    bodyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    textColumn->addWidget(titleLabel);
    textColumn->addWidget(bodyLabel);

    row->addWidget(iconLabel, 0, Qt::AlignTop);
    row->addLayout(textColumn, 1);
    root->addLayout(row);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(10);
    buttonRow->addStretch(1);

    QVector<QPushButton *> createdButtons;
    createdButtons.reserve(buttons.size());
    for (const QString &buttonText : buttons) {
        auto *button = new QPushButton(buttonText, &dialog);
        buttonRow->addWidget(button);
        createdButtons.push_back(button);
    }
    buttonRow->addStretch(1);
    root->addLayout(buttonRow);

    if (defaultButtonIndex >= 0 && defaultButtonIndex < createdButtons.size()) {
        createdButtons[defaultButtonIndex]->setDefault(true);
        createdButtons[defaultButtonIndex]->setFocus();
    }

    for (int index = 0; index < createdButtons.size(); ++index) {
        QObject::connect(createdButtons[index], &QPushButton::clicked, &dialog, [&dialog, index]() {
            dialog.done(index + 1);
        });
    }

    return dialog.exec();
}

bool showQuestionPrompt(QWidget *parent, const QString &title, const QString &message) {
    return showStyledPrompt(parent, title, message, PromptType::Question, {QStringLiteral("取消"), QStringLiteral("确定")}, 1) == 2;
}

void showInfoPrompt(QWidget *parent, const QString &title, const QString &message) {
    showStyledPrompt(parent, title, message, PromptType::Info, {QStringLiteral("确定")}, 0);
}

void showWarningPrompt(QWidget *parent, const QString &title, const QString &message) {
    showStyledPrompt(parent, title, message, PromptType::Warning, {QStringLiteral("知道了")}, 0);
}

void showErrorPrompt(QWidget *parent, const QString &title, const QString &message) {
    showStyledPrompt(parent, title, message, PromptType::Error, {QStringLiteral("知道了")}, 0);
}
} // namespace

HomePageWidget::HomePageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 40);
    root->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("Spell it"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 700; letter-spacing: 0.2px;"));

    learningCountLabel_ = new QLabel(this);
    learningCountLabel_->setAlignment(Qt::AlignCenter);
    learningCountLabel_->setStyleSheet(QStringLiteral("font-size: 10px; color: #6b7280;"));

    learningButton_ = new QPushButton(QStringLiteral("学习"), this);
    learningButton_->setMinimumHeight(78);
    learningButton_->setStyleSheet(QStringLiteral(
        "font-size: 16px; "
        "font-weight: 700; "
        "text-align: left; "
        "padding-left: 12px; "
        "border-radius: 12px;"));

    reviewCountLabel_ = new QLabel(this);
    reviewCountLabel_->setAlignment(Qt::AlignCenter);
    reviewCountLabel_->setStyleSheet(QStringLiteral("font-size: 10px; color: #6b7280;"));

    reviewButton_ = new QPushButton(QStringLiteral("复习"), this);
    reviewButton_->setMinimumHeight(78);
    reviewButton_->setStyleSheet(QStringLiteral(
        "font-size: 16px; "
        "font-weight: 700; "
        "text-align: left; "
        "padding-left: 12px; "
        "border-radius: 12px;"));

    auto *cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(10);
    cardsLayout->addWidget(learningButton_, 1);
    cardsLayout->addWidget(reviewButton_, 1);

    auto *navLayout = new QHBoxLayout();
    navLayout->setContentsMargins(0, 0, 0, 0);

    auto *navBtn1 = new QPushButton(this);
    auto *navBtn2 = new QPushButton(this);
    auto *navBtn3 = new QPushButton(this);
    navBtn1->setToolTip(QStringLiteral("词书"));
    navBtn2->setToolTip(QStringLiteral("导入"));
    navBtn3->setToolTip(QStringLiteral("统计"));
    navBtn1->setIcon(createBooksLineIcon());
    navBtn2->setIcon(createArchiveLineIcon());
    navBtn3->setIcon(createChartLineIcon());
    navBtn1->setIconSize(QSize(24, 24));
    navBtn2->setIconSize(QSize(24, 24));
    navBtn3->setIconSize(QSize(24, 24));
    navBtn1->setFixedSize(46, 46);
    navBtn2->setFixedSize(46, 46);
    navBtn3->setFixedSize(46, 46);

    const QString navBtnStyle = QStringLiteral(
        "QPushButton {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0;"
        "}"
        "QPushButton:hover { background: #f3f4f6; border-radius: 12px; }");

    navBtn1->setStyleSheet(navBtnStyle);
    navBtn2->setStyleSheet(navBtnStyle);
    navBtn3->setStyleSheet(navBtnStyle);

    navLayout->addStretch(1);
    navLayout->addWidget(navBtn1);
    navLayout->addStretch(2);
    navLayout->addWidget(navBtn2);
    navLayout->addStretch(2);
    navLayout->addWidget(navBtn3);
    navLayout->addStretch(1);

    root->addWidget(learningCountLabel_);
    root->addStretch(1);
    root->addWidget(title);
    root->addStretch(6);
    root->addWidget(reviewCountLabel_);
    root->addSpacing(4);
    root->addLayout(cardsLayout);
    root->addSpacing(6);
    root->addLayout(navLayout);

    connect(learningButton_, &QPushButton::clicked, this, &HomePageWidget::startLearningClicked);
    connect(reviewButton_, &QPushButton::clicked, this, &HomePageWidget::startReviewClicked);
    connect(navBtn1, &QPushButton::clicked, this, &HomePageWidget::booksClicked);
    connect(navBtn3, &QPushButton::clicked, this, &HomePageWidget::statsClicked);
}

void HomePageWidget::setCounts(int learningCount,
                               int reviewCount,
                               int todayLearningCount,
                               int todayReviewCount) {
    learningButton_->setText(QStringLiteral("学习\n%1").arg(learningCount));
    reviewButton_->setText(QStringLiteral("复习\n%1").arg(reviewCount));
    learningCountLabel_->setText(
        QStringLiteral("今日已学 %1 词 · 今日复习 %2 词").arg(todayLearningCount).arg(todayReviewCount));
    reviewCountLabel_->setText(QStringLiteral("长期主义的核心是无视中断"));
}

MappingPageWidget::MappingPageWidget(QWidget *parent)
    : QWidget(parent) {
    const int leftLabelWidth = 150;

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("CSV 列映射"), this);
    title->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: #111827;"));

    auto *fileKeyLabel = new QLabel(QStringLiteral("文件："), this);
    fileKeyLabel->setFixedWidth(leftLabelWidth);
    fileKeyLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fileKeyLabel->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 600; color: #4b5563;"));

    filePathLabel_ = new QLabel(this);
    filePathLabel_->setWordWrap(true);
    filePathLabel_->setStyleSheet(QStringLiteral("font-size: 15px; color: #6b7280;"));

    auto *pathRow = new QHBoxLayout();
    pathRow->setSpacing(10);
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->addWidget(fileKeyLabel);
    pathRow->addWidget(filePathLabel_, 1);

    wordCombo_ = new QComboBox(this);
    translationCombo_ = new QComboBox(this);
    phoneticCombo_ = new QComboBox(this);
    const QString comboStyle = QStringLiteral(
        "QComboBox {"
        "  min-height: 44px;"
        "  padding: 0 14px;"
        "  font-size: 16px;"
        "  border: 1px solid #d7dce3;"
        "  border-radius: 12px;"
        "  background: #ffffff;"
        "}"
        "QComboBox:focus { border: 1px solid #111827; }");
    wordCombo_->setStyleSheet(comboStyle);
    translationCombo_->setStyleSheet(comboStyle);
    phoneticCombo_->setStyleSheet(comboStyle);
    wordCombo_->setMinimumWidth(260);
    translationCombo_->setMinimumWidth(260);
    phoneticCombo_->setMinimumWidth(260);

    auto *mappingRows = new QVBoxLayout();
    mappingRows->setSpacing(12);
    mappingRows->setContentsMargins(0, 4, 0, 4);

    auto makeRow = [this, leftLabelWidth](const QString &text, QWidget *field) {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        row->setContentsMargins(0, 0, 0, 0);

        auto *label = new QLabel(text, this);
        label->setFixedWidth(leftLabelWidth);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        label->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: #111827;"));

        row->addWidget(label);
        row->addWidget(field, 1);
        return row;
    };
    mappingRows->addLayout(makeRow(QStringLiteral("单词列"), wordCombo_));
    mappingRows->addLayout(makeRow(QStringLiteral("释义列"), translationCombo_));
    mappingRows->addLayout(makeRow(QStringLiteral("音标列(可选)"), phoneticCombo_));

    auto *previewTitle = new QLabel(QStringLiteral("CSV 样例预览"), this);
    previewTitle->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600; color: #374151;"));

    previewTable_ = new QTableWidget(this);
    previewTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    previewTable_->setSelectionMode(QAbstractItemView::NoSelection);
    previewTable_->setAlternatingRowColors(true);
    previewTable_->setShowGrid(false);
    previewTable_->setWordWrap(false);
    previewTable_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    previewTable_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    previewTable_->horizontalHeader()->setStretchLastSection(false);
    previewTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    previewTable_->horizontalHeader()->setMinimumSectionSize(96);
    previewTable_->horizontalHeader()->setDefaultSectionSize(200);
    previewTable_->horizontalHeader()->setHighlightSections(false);
    previewTable_->horizontalHeader()->setFixedHeight(42);
    previewTable_->verticalHeader()->setDefaultSectionSize(44);
    previewTable_->verticalHeader()->setVisible(false);
    previewTable_->setStyleSheet(QStringLiteral(
        "QTableWidget {"
        "  border: 1px solid #e7ebf0;"
        "  border-radius: 14px;"
        "  background: #ffffff;"
        "  alternate-background-color: #fafbfc;"
        "  gridline-color: #eef2f6;"
        "  font-size: 14px;"
        "}"
        "QHeaderView::section {"
        "  background: #f7f8fa;"
        "  color: #374151;"
        "  border: none;"
        "  border-bottom: 1px solid #e7ebf0;"
        "  padding: 8px 10px;"
        "  font-size: 14px;"
        "  font-weight: 600;"
        "}"));

    auto *buttons = new QHBoxLayout();
    buttons->setSpacing(16);

    auto *cancelButton = new QPushButton(QStringLiteral("返回首页"), this);
    auto *importButton = new QPushButton(QStringLiteral("导入词库"), this);
    cancelButton->setFixedSize(200, 62);
    importButton->setFixedSize(200, 62);
    cancelButton->setStyleSheet(QStringLiteral(
        "font-size: 16px; font-weight: 600; border-radius: 20px;"
        "background: rgba(17,24,39,0.06); color: #111827;"));
    importButton->setStyleSheet(QStringLiteral(
        "font-size: 16px; font-weight: 700; border-radius: 20px;"
        "background: #111827; color: #ffffff;"));

    buttons->addStretch();
    buttons->addWidget(cancelButton);
    buttons->addWidget(importButton);
    buttons->addStretch();

    root->addWidget(title);
    root->addLayout(pathRow);
    root->addLayout(mappingRows);
    root->addSpacing(6);
    root->addWidget(previewTitle);
    root->addWidget(previewTable_, 1);
    root->addSpacing(8);
    root->addLayout(buttons);

    connect(cancelButton, &QPushButton::clicked, this, &MappingPageWidget::cancelled);
    connect(importButton, &QPushButton::clicked, this, [this]() {
        if (wordCombo_->currentIndex() < 0 || translationCombo_->currentIndex() < 0) {
            showWarningPrompt(this, QStringLiteral("映射不完整"), QStringLiteral("请先选择单词列和释义列。"));
            return;
        }

        if (wordCombo_->currentIndex() == translationCombo_->currentIndex()) {
            showWarningPrompt(this, QStringLiteral("映射冲突"), QStringLiteral("单词列和释义列不能相同。"));
            return;
        }

        const int phoneticColumn = phoneticCombo_->currentData().toInt();
        emit importConfirmed(wordCombo_->currentIndex(), translationCombo_->currentIndex(), phoneticColumn);
    });
}

void MappingPageWidget::setCsvData(const QString &csvPath,
                                   const QStringList &headers,
                                   const QVector<QStringList> &previewRows) {
    filePathLabel_->setText(csvPath);

    wordCombo_->clear();
    translationCombo_->clear();
    phoneticCombo_->clear();

    wordCombo_->addItems(headers);
    translationCombo_->addItems(headers);

    phoneticCombo_->addItem(QStringLiteral("不使用音标列"), -1);
    for (int i = 0; i < headers.size(); ++i) {
        phoneticCombo_->addItem(headers.at(i), i);
    }

    const QString bestWord = findBestColumn(headers, {QStringLiteral("word"), QStringLiteral("单词")});
    if (!bestWord.isEmpty()) {
        wordCombo_->setCurrentText(bestWord);
    }

    const QString bestTranslation = findBestColumn(headers,
                                                   {QStringLiteral("translation"),
                                                    QStringLiteral("meaning"),
                                                    QStringLiteral("释义"),
                                                    QStringLiteral("中文")});
    if (!bestTranslation.isEmpty()) {
        translationCombo_->setCurrentText(bestTranslation);
    } else if (headers.size() > 1) {
        translationCombo_->setCurrentIndex(1);
    }

    const QString bestPhonetic = findBestColumn(headers,
                                                {QStringLiteral("phonetic"),
                                                 QStringLiteral("pronunciation"),
                                                 QStringLiteral("音标")});
    if (!bestPhonetic.isEmpty()) {
        const int idx = phoneticCombo_->findText(bestPhonetic);
        if (idx >= 0) {
            phoneticCombo_->setCurrentIndex(idx);
        }
    }

    previewTable_->clear();
    previewTable_->setColumnCount(headers.size());
    previewTable_->setHorizontalHeaderLabels(headers);
    previewTable_->setRowCount(previewRows.size());

    for (int row = 0; row < previewRows.size(); ++row) {
        const QStringList &values = previewRows.at(row);
        for (int col = 0; col < headers.size(); ++col) {
            auto *item = new QTableWidgetItem(col < values.size() ? values.at(col) : QString());
            item->setToolTip(item->text());
            previewTable_->setItem(row, col, item);
        }
    }

    for (int col = 0; col < headers.size(); ++col) {
        previewTable_->setColumnWidth(col, 200);
    }
}

SpellingPageWidget::SpellingPageWidget(QWidget *parent)
    : QWidget(parent) {
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

    top->addWidget(exitButton_);
    top->addStretch();
    top->addWidget(modeLabel_);
    top->addStretch();
    top->addWidget(progressLabel_);
    top->setContentsMargins(4, 2, 4, 8);

    translationLabel_ = new QLabel(this);
    translationLabel_->setAlignment(Qt::AlignCenter);
    translationLabel_->setWordWrap(true);
    translationLabel_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #111827;"));

    inputEdit_ = new QLineEdit(this);
    inputEdit_->setPlaceholderText(QStringLiteral("输入英文后按 Enter"));
    inputEdit_->setAlignment(Qt::AlignCenter);
    inputEdit_->setFixedWidth(310);
    inputEdit_->setMinimumHeight(42);
    inputEdit_->setStyleSheet(QStringLiteral(
        "border: none;"
        "border-bottom: 2px solid #e5e7eb;"
        "color: #6b7280;"
        "font-size: 16px;"
        "padding: 4px 8px;"
        "background: transparent;"));

    feedbackLabel_ = new QLabel(this);
    feedbackLabel_->setAlignment(Qt::AlignCenter);
    feedbackLabel_->setStyleSheet(QStringLiteral("font-size: 12px; color: #6b7280;"));

    skipButton_ = new QPushButton(QStringLiteral("跳过"), this);
    skipButton_->setFixedSize(104, 42);
    skipButton_->setStyleSheet(QStringLiteral("font-size: 12px; border-radius: 12px;"));

    auto *bottom = new QHBoxLayout();
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->addStretch(1);
    bottom->addWidget(skipButton_);
    bottom->addStretch(1);

    root->addLayout(top);
    root->addStretch(1);
    root->addWidget(translationLabel_);
    root->addSpacing(18);
    auto *inputRow = new QHBoxLayout();
    inputRow->addStretch(1);
    inputRow->addWidget(inputEdit_, 0, Qt::AlignHCenter);
    inputRow->addStretch(1);
    root->addLayout(inputRow);
    root->addWidget(feedbackLabel_);
    root->addStretch(2);
    root->addLayout(bottom);

    connect(inputEdit_, &QLineEdit::returnPressed, this, [this]() {
        emit submitted(inputEdit_->text());
    });
    connect(skipButton_, &QPushButton::clicked, this, &SpellingPageWidget::skipped);
    connect(exitButton_, &QPushButton::clicked, this, &SpellingPageWidget::exitRequested);
}

void SpellingPageWidget::setWord(const WordItem &word, int currentIndex, int totalCount, bool isReviewMode) {
    modeLabel_->setText(isReviewMode ? QStringLiteral("复习模式") : QStringLiteral("学习模式"));
    progressLabel_->setText(QStringLiteral("%1 / %2").arg(currentIndex).arg(totalCount));
    translationLabel_->setText(word.translation);
    inputEdit_->clear();
    clearFeedback();
    setInputEnabled(true);
    inputEdit_->setFocus();
}

void SpellingPageWidget::setInputEnabled(bool enabled) {
    inputEdit_->setEnabled(enabled);
    skipButton_->setEnabled(enabled);
}

void SpellingPageWidget::showFeedback(const QString &text, const QString &colorHex) {
    feedbackLabel_->setText(text);
    feedbackLabel_->setStyleSheet(QStringLiteral("font-size: 16px; color: %1;").arg(colorHex));
}

void SpellingPageWidget::clearFeedback() {
    feedbackLabel_->setText(QString());
    feedbackLabel_->setStyleSheet(QStringLiteral("font-size: 16px; color: #6b7280;"));
}

SummaryPageWidget::SummaryPageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(10);

    setStyleSheet(QStringLiteral("background: #ffffff;"));

    auto *topBar = new QHBoxLayout();
    topBar->setContentsMargins(0, 0, 0, 0);

    auto *backButton = new QPushButton(QStringLiteral("‹"), this);
    backButton->setFixedSize(32, 32);
    backButton->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; background: transparent; color: #222222;"));

    auto *topTitle = new QLabel(QStringLiteral("小结"), this);
    topTitle->setAlignment(Qt::AlignCenter);
    topTitle->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: #222222;"));

    auto *spacer = new QWidget(this);
    spacer->setFixedSize(32, 32);

    topBar->addWidget(backButton, 0, Qt::AlignLeft);
    topBar->addStretch(1);
    topBar->addWidget(topTitle, 0, Qt::AlignCenter);
    topBar->addStretch(1);
    topBar->addWidget(spacer, 0, Qt::AlignRight);

    topTipLabel_ = new QLabel(QStringLiteral("💡 快速回顾本组单词吧~"), this);
    topTipLabel_->setAlignment(Qt::AlignCenter);
    topTipLabel_->setStyleSheet(QStringLiteral("font-size: 14px; color: #7b7b7b;"));

    accuracyLabel_ = new QLabel(this);
    accuracyLabel_->setAlignment(Qt::AlignCenter);
    accuracyLabel_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #222222;"));

    statsLabel_ = new QLabel(this);
    statsLabel_->setAlignment(Qt::AlignCenter);
    statsLabel_->setStyleSheet(QStringLiteral("font-size: 13px; color: #7b7b7b;"));

    auto *listFrame = new QFrame(this);
    listFrame->setStyleSheet(QStringLiteral("QFrame { background: #ffffff; border: none; }"));
    auto *listLayout = new QVBoxLayout(listFrame);
    listLayout->setContentsMargins(14, 10, 14, 10);
    listLayout->setSpacing(0);

    auto *wrongTitle = new QLabel(QStringLiteral("本组拼写"), listFrame);
    wrongTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: #222222;"));

    wrongWordsList_ = new QListWidget(listFrame);
    wrongWordsList_->setFrameShape(QFrame::NoFrame);
    wrongWordsList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    wrongWordsList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    wrongWordsList_->setStyleSheet(QStringLiteral(
        "QListWidget {"
        "  background: transparent;"
        "  border: none;"
        "  font-size: 14px;"
        "  color: #6b7280;"
        "}"
        "QListWidget::item { background: transparent; border: none; }"));

    listLayout->addWidget(wrongTitle);
    listLayout->addWidget(wrongWordsList_, 1);

    footerLabel_ = new QLabel(this);
    footerLabel_->setAlignment(Qt::AlignCenter);
    footerLabel_->setStyleSheet(QStringLiteral("font-size: 14px; color: #7b7b7b;"));

    backHomeButton_ = new QPushButton(QStringLiteral("返回首页"), this);
    backHomeButton_->setMinimumHeight(60);
    backHomeButton_->setStyleSheet(QStringLiteral(
        "background: rgba(17, 24, 39, 0.08);"
        "color: #111827;"
        "font-size: 18px;"
        "font-weight: 700;"
        "border-radius: 26px;"));

    nextGroupButton_ = new QPushButton(QStringLiteral("继续下一组"), this);
    nextGroupButton_->setMinimumHeight(60);
    nextGroupButton_->setStyleSheet(QStringLiteral(
        "background: #111827;"
        "color: #ffffff;"
        "font-size: 18px;"
        "font-weight: 700;"
        "border-radius: 26px;"));

    root->addLayout(topBar);
    root->addSpacing(8);
    root->addWidget(topTipLabel_);
    root->addSpacing(8);
    root->addWidget(accuracyLabel_);
    root->addWidget(statsLabel_);
    root->addSpacing(8);
    root->addWidget(listFrame, 1);
    root->addSpacing(12);
    root->addWidget(footerLabel_);
    root->addSpacing(6);
    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(12);
    bottomRow->addWidget(backHomeButton_, 1);
    bottomRow->addWidget(nextGroupButton_, 1);
    root->addLayout(bottomRow);

    connect(backButton, &QPushButton::clicked, this, &SummaryPageWidget::backHomeClicked);
    connect(backHomeButton_, &QPushButton::clicked, this, &SummaryPageWidget::backHomeClicked);
    connect(nextGroupButton_, &QPushButton::clicked, this, &SummaryPageWidget::nextGroupClicked);
}

void SummaryPageWidget::setSummary(const QVector<PracticeRecord> &records, bool reviewMode) {
    reviewMode_ = reviewMode;
    const int total = records.size();
    const int mastered = countByResult(records, SpellingResult::Mastered);
    const int blurry = countByResult(records, SpellingResult::Blurry);
    const int unfamiliar = countByResult(records, SpellingResult::Unfamiliar);

    const double accuracy = total > 0 ? (100.0 * mastered / static_cast<double>(total)) : 0.0;

    accuracyLabel_->setText(QStringLiteral("正确率：%1%").arg(QString::number(accuracy, 'f', 1)));
    statsLabel_->setText(QStringLiteral("熟悉 %1  |  模糊 %2  |  不熟悉 %3")
                             .arg(mastered)
                             .arg(blurry)
                             .arg(unfamiliar));

    wrongWordsList_->clear();
    wrongWordsList_->setUpdatesEnabled(false);
    for (const PracticeRecord &record : records) {
        auto *item = new QListWidgetItem(wrongWordsList_);
        item->setSizeHint(QSize(0, 44));
        item->setFlags(Qt::NoItemFlags);
        wrongWordsList_->addItem(item);
        wrongWordsList_->setItemWidget(item, createSummaryRow(record, reviewMode_));
    }
    wrongWordsList_->setUpdatesEnabled(true);

    if (wrongWordsList_->count() == 0) {
        footerLabel_->setText(QStringLiteral("本组没有错词，继续保持。"));
    } else {
        footerLabel_->setText(QStringLiteral("已完成 %1 词，继续保持节奏。").arg(total));
    }

    backHomeButton_->setText(QStringLiteral("返回首页"));
    nextGroupButton_->setText(QStringLiteral("继续下一组"));
}

StatisticsPageWidget::StatisticsPageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 18);
    root->setSpacing(16);

    auto *header = new QHBoxLayout();
    backButton_ = new QPushButton(QStringLiteral("返回"), this);
    backButton_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: transparent;"
        "  font-size: 16px;"
        "  color: #4b5563;"
        "  padding: 8px;"
        "}"
        "QPushButton:hover { color: #111827; }"));
    
    auto *title = new QLabel(QStringLiteral("学习统计"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700;"));

    header->addWidget(backButton_);
    header->addStretch(1);
    header->addWidget(title);
    header->addStretch(1);
    header->addSpacing(backButton_->sizeHint().width()); // balance title

    root->addLayout(header);
    root->addStretch(1);

    connect(backButton_, &QPushButton::clicked, this, &StatisticsPageWidget::backClicked);
}

void StatisticsPageWidget::setLogs(const QVector<DatabaseManager::DailyLog> &logs) {
    logs_ = logs;
    update();
}

void StatisticsPageWidget::paintEvent(QPaintEvent *event) {
    QWidget::paintEvent(event);
    if (logs_.isEmpty()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int marginX = 40;
    const int marginBottom = 120;
    const int marginTop = 80;
    const int width = this->width() - 2 * marginX;
    const int height = this->height() - marginTop - marginBottom;

    int maxCount = 10; // minimum scale
    for (const auto &log : logs_) {
        maxCount = qMax(maxCount, log.learningCount + log.reviewCount);
    }

    const int barSpacing = width / (logs_.size() + 1);
    const int barWidth = qMax(12, qMin(24, barSpacing / 2));

    for (int i = 0; i < logs_.size(); ++i) {
        const auto &log = logs_[i];
        const int total = log.learningCount + log.reviewCount;
        
        const int centerX = marginX + (i + 1) * barSpacing;
        const int barHeight = static_cast<int>((static_cast<double>(total) / maxCount) * height);
        
        const int learningHeight = total > 0 ? static_cast<int>((static_cast<double>(log.learningCount) / total) * barHeight) : 0;
        const int reviewHeight = barHeight - learningHeight;

        // Draw Review Part (top, purple)
        if (reviewHeight > 0) {
            QRect reviewRect(centerX - barWidth / 2, marginTop + height - barHeight, barWidth, reviewHeight);
            painter.setBrush(QColor("#a855f7")); // Purple
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(reviewRect, barWidth / 2, barWidth / 2);
        }

        // Draw Learning Part (bottom, orange)
        if (learningHeight > 0) {
            QRect learningRect(centerX - barWidth / 2, marginTop + height - learningHeight, barWidth, learningHeight);
            painter.setBrush(QColor("#f97316")); // Orange
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(learningRect, barWidth / 2, barWidth / 2);
        }

        // Draw Date Label
        painter.setPen(QColor("#6b7280"));
        painter.setFont(QFont(font().family(), 9));
        QString dateStr = log.date.right(5); // MM-DD
        if (i == logs_.size() - 1) {
            dateStr = QStringLiteral("今日");
        }
        painter.drawText(QRect(centerX - 30, marginTop + height + 10, 60, 20), Qt::AlignCenter, dateStr);

        // Draw Total Label (only for today or if > 0)
        if (i == logs_.size() - 1 || total > 0) {
            painter.drawText(QRect(centerX - 30, marginTop + height - barHeight - 25, 60, 20), Qt::AlignCenter, QString::number(total));
        }
    }

    // Draw Legend
    int legendY = marginTop - 30;
    int legendX = this->width() - marginX - 100;
    
    painter.setBrush(QColor("#f97316"));
    painter.drawEllipse(legendX, legendY, 8, 8);
    painter.setPen(QColor("#4b5563"));
    painter.drawText(legendX + 14, legendY + 9, QStringLiteral("学习"));

    painter.setBrush(QColor("#a855f7"));
    painter.drawEllipse(legendX + 50, legendY, 8, 8);
    painter.drawText(legendX + 64, legendY + 9, QStringLiteral("复习"));

    // Draw today's summary at bottom
    if (!logs_.isEmpty()) {
        const auto &today = logs_.last();
        painter.setPen(QColor("#111827"));
        painter.setFont(QFont(font().family(), 12, QFont::Bold));
        
        painter.drawText(QRect(marginX, marginTop + height + 50, width / 2, 20), Qt::AlignCenter, QStringLiteral("当日学习"));
        painter.drawText(QRect(marginX + width / 2, marginTop + height + 50, width / 2, 20), Qt::AlignCenter, QStringLiteral("当日复习"));

        painter.setFont(QFont(font().family(), 24, QFont::Bold));
        painter.drawText(QRect(marginX, marginTop + height + 80, width / 2, 30), Qt::AlignCenter, QString::number(today.learningCount));
        painter.drawText(QRect(marginX + width / 2, marginTop + height + 80, width / 2, 30), Qt::AlignCenter, QString::number(today.reviewCount));
    }
}

WordBooksPageWidget::WordBooksPageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 20);
    root->setSpacing(12);

    auto *header = new QHBoxLayout();
    backButton_ = new QPushButton(QStringLiteral("返回"), this);
    backButton_->setFixedHeight(42);
    backButton_->setStyleSheet(QStringLiteral(
        "font-size: 14px; border-radius: 12px; background: rgba(17,24,39,0.06);"));

    auto *title = new QLabel(QStringLiteral("词书"), this);
    title->setStyleSheet(QStringLiteral("font-size: 28px; font-weight: 700;"));

    header->addWidget(backButton_);
    header->addSpacing(12);
    header->addWidget(title);
    header->addStretch(1);

    booksList_ = new QListWidget(this);
    booksList_->setSpacing(10);
    booksList_->setFrameShape(QFrame::NoFrame);
    booksList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    booksList_->setStyleSheet(QStringLiteral(
        "QListWidget { border: none; background: transparent; }"
        "QListWidget::item { border: none; }"));

    addBookButton_ = new QPushButton(QStringLiteral("添加词书"), this);
    addBookButton_->setFixedHeight(60);
    addBookButton_->setStyleSheet(QStringLiteral(
        "font-size: 18px; font-weight: 700; border-radius: 18px;"
        "background: #111827; color: #ffffff;"));

    root->addLayout(header);
    root->addWidget(booksList_, 1);
    root->addWidget(addBookButton_);

    connect(backButton_, &QPushButton::clicked, this, &WordBooksPageWidget::backClicked);
    connect(addBookButton_, &QPushButton::clicked, this, &WordBooksPageWidget::addBookClicked);
    connect(booksList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        const int bookId = item->data(Qt::UserRole).toInt();
        emit wordBookSelected(bookId);
    });
}

void WordBooksPageWidget::setWordBooks(const QVector<WordBookItem> &books, int activeBookId) {
    books_ = books;
    activeBookId_ = activeBookId;
    rebuildList();
}

void WordBooksPageWidget::rebuildList() {
    booksList_->clear();
    booksList_->setUpdatesEnabled(false);

    for (const WordBookItem &book : books_) {
        auto *item = new QListWidgetItem(booksList_);
        item->setData(Qt::UserRole, book.id);
        item->setSizeHint(QSize(0, 110));
        booksList_->addItem(item);

        auto *row = new QWidget(booksList_);
        row->setObjectName(QStringLiteral("bookRow"));
        row->setStyleSheet(book.id == activeBookId_
                               ? QStringLiteral(
                                     "QWidget#bookRow {"
                                     "  background: #fff7ed;"
                                     "  border: 1px solid #fdba74;"
                                     "  border-radius: 18px;"
                                     "}")
                               : QStringLiteral(
                                     "QWidget#bookRow {"
                                     "  background: #ffffff;"
                                     "  border: 1px solid #eef2f7;"
                                     "  border-radius: 18px;"
                                     "}"));

        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(12);

        auto *cover = new QLabel(QStringLiteral("BOOK"), row);
        cover->setAlignment(Qt::AlignCenter);
        cover->setFixedSize(64, 82);
        cover->setStyleSheet(QStringLiteral(
            "font-size: 12px; font-weight: 700; color: #ffffff; border-radius: 12px; background: %1;")
                                .arg(coverColorForBook(book.id)));

        auto *title = new QLabel(book.name, row);
        title->setStyleSheet(
            QStringLiteral("font-size: 20px; font-weight: 700; color: #111827; border: none; background: transparent;"));
        title->setWordWrap(true);

        auto *count = new QLabel(QStringLiteral("%1 词").arg(book.wordCount), row);
        count->setStyleSheet(QStringLiteral("font-size: 15px; color: #9ca3af; border: none; background: transparent;"));

        auto *textLayout = new QVBoxLayout();
        textLayout->setSpacing(6);
        textLayout->addWidget(title);
        textLayout->addWidget(count);
        textLayout->addStretch(1);

        auto *status = new QLabel(book.id == activeBookId_ ? QStringLiteral("正在学习") : QString(), row);
        status->setStyleSheet(
            QStringLiteral("font-size: 18px; font-weight: 700; color: #f97316; border: none; background: transparent;"));
        status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        auto *deleteButton = new QPushButton(QStringLiteral("删除"), row);
        deleteButton->setFixedSize(92, 38);
        deleteButton->setStyleSheet(QStringLiteral(
            "font-size: 14px; font-weight: 600; border-radius: 10px;"
            "padding: 0 12px;"
            "background: rgba(239,68,68,0.10); color: #dc2626;"));
        connect(deleteButton, &QPushButton::clicked, this, [this, book]() {
            emit wordBookDeleteRequested(book.id);
        });

        auto *rightLayout = new QVBoxLayout();
        rightLayout->setSpacing(8);
        rightLayout->addWidget(status, 0, Qt::AlignRight);
        rightLayout->addWidget(deleteButton, 0, Qt::AlignRight);
        rightLayout->addStretch(1);

        layout->addWidget(cover);
        layout->addLayout(textLayout, 1);
        layout->addLayout(rightLayout);

        booksList_->setItemWidget(item, row);
    }

    booksList_->setUpdatesEnabled(true);
}

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
    connect(spellingPage_, &SpellingPageWidget::skipped, this, &VibeSpellerWindow::onSkipWord);
    connect(spellingPage_, &SpellingPageWidget::exitRequested, this, &VibeSpellerWindow::onExitSession);

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

void VibeSpellerWindow::onStartLearning() {
    if (tryResumeSession(SessionMode::Learning)) {
        return;
    }

    const QVector<WordItem> words = db_.fetchLearningBatch(20);
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

    const QVector<WordItem> words = db_.fetchReviewBatch(QDateTime::currentDateTime(), 20);
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
    const SpellingResult result = db_.evaluateSpelling(text, current.word);

    if (!db_.applyReviewResult(current.id, result, false, QDateTime::currentDateTime())) {
        showWarningPrompt(this,
                          QStringLiteral("更新失败"),
                          QStringLiteral("保存复习结果失败：%1").arg(db_.lastError()));
    } else {
        db_.incrementDailyCount(currentMode_ == SessionMode::Learning);
    }

    PracticeRecord record;
    record.word = current;
    record.result = result;
    record.userInput = text;
    record.skipped = false;
    records_.push_back(record);

    spellingPage_->setInputEnabled(false);
    spellingPage_->showFeedback(resultLabel(result), resultColor(result));
    db_.saveSessionProgress(modeKey(currentMode_), currentWords_, currentIndex_ + 1);

    QTimer::singleShot(620, this, &VibeSpellerWindow::moveToNextWord);
}

void VibeSpellerWindow::onSkipWord() {
    if (currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        return;
    }

    const WordItem current = currentWords_.at(currentIndex_);
    const SpellingResult result = SpellingResult::Unfamiliar;

    if (!db_.applyReviewResult(current.id, result, true, QDateTime::currentDateTime())) {
        showWarningPrompt(this,
                          QStringLiteral("更新失败"),
                          QStringLiteral("保存复习结果失败：%1").arg(db_.lastError()));
    } else {
        // 跳过也代表完成了一次练习尝试，应计入当日学习/复习统计。
        db_.incrementDailyCount(currentMode_ == SessionMode::Learning);
    }

    PracticeRecord record;
    record.word = current;
    record.result = result;
    record.userInput.clear();
    record.skipped = true;
    records_.push_back(record);

    spellingPage_->setInputEnabled(false);
    spellingPage_->showFeedback(QStringLiteral("不熟悉（已跳过）"), QStringLiteral("#dc2626"));
    db_.saveSessionProgress(modeKey(currentMode_), currentWords_, currentIndex_ + 1);

    QTimer::singleShot(260, this, &VibeSpellerWindow::moveToNextWord);
}

void VibeSpellerWindow::onExitSession() {
    if (currentWords_.isEmpty() || currentIndex_ < 0 || currentIndex_ >= currentWords_.size()) {
        stack_->setCurrentWidget(homePage_);
        return;
    }

    const bool answer = showQuestionPrompt(this,
                                           QStringLiteral("退出练习"),
                                           QStringLiteral("退出后会保存当前进度，下次继续这组 20 词。是否退出？"));
    if (!answer) {
        return;
    }

    persistCurrentSession();
    refreshHomeCounts();
    stack_->setCurrentWidget(homePage_);
}

void VibeSpellerWindow::onOpenWordBooks() {
    refreshWordBooks();
    stack_->setCurrentWidget(wordBooksPage_);
}

void VibeSpellerWindow::onSelectWordBook(int bookId) {
    if (bookId <= 0) {
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

    int learning = qMin(20, db_.unlearnedCount());
    int review = qMin(20, db_.dueReviewCount(QDateTime::currentDateTime()));

    int todayLearning = 0;
    int todayReview = 0;
    const QVector<DatabaseManager::DailyLog> logs = db_.fetchWeeklyLogs();
    if (!logs.isEmpty()) {
        todayLearning = logs.last().learningCount;
        todayReview = logs.last().reviewCount;
    }

    QVector<WordItem> savedWords;
    int savedIndex = 0;
    if (db_.loadSessionProgress(modeKey(SessionMode::Learning), savedWords, savedIndex)) {
        learning = qMax(0, savedWords.size() - savedIndex);
    }
    if (db_.loadSessionProgress(modeKey(SessionMode::Review), savedWords, savedIndex)) {
        review = qMax(0, savedWords.size() - savedIndex);
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
    currentIndex_ = qMax(0, startIndex);
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
    spellingPage_->setWord(word,
                           currentIndex_ + 1,
                           currentWords_.size(),
                           currentMode_ == SessionMode::Review);
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
