#include "gui_widgets.h"
#include "gui_widgets_internal.h"

#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

using namespace GuiWidgetsInternal;
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

    countabilityLearnButton_ = new QPushButton(QStringLiteral("可数性辨析\n0"), this);
    countabilityLearnButton_->setMinimumHeight(78);
    countabilityLearnButton_->setStyleSheet(QStringLiteral(
        "font-size: 16px; "
        "font-weight: 700; "
        "text-align: left; "
        "padding-left: 12px; "
        "border-radius: 12px;"));

    countabilityReviewButton_ = new QPushButton(QStringLiteral("可数性复习\n0"), this);
    countabilityReviewButton_->setMinimumHeight(78);
    countabilityReviewButton_->setStyleSheet(QStringLiteral(
        "font-size: 16px; "
        "font-weight: 700; "
        "text-align: left; "
        "padding-left: 12px; "
        "border-radius: 12px;"));

    auto *countabilityCardsLayout = new QHBoxLayout();
    countabilityCardsLayout->setSpacing(10);
    countabilityCardsLayout->addWidget(countabilityLearnButton_, 1);
    countabilityCardsLayout->addWidget(countabilityReviewButton_, 1);

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
    root->addLayout(countabilityCardsLayout);
    root->addSpacing(8);
    root->addSpacing(4);
    root->addLayout(cardsLayout);
    root->addSpacing(10);
    root->addWidget(reviewCountLabel_);
    root->addSpacing(6);
    root->addLayout(navLayout);

    connect(learningButton_, &QPushButton::clicked, this, &HomePageWidget::startLearningClicked);
    connect(reviewButton_, &QPushButton::clicked, this, &HomePageWidget::startReviewClicked);
    connect(countabilityLearnButton_, &QPushButton::clicked, this, &HomePageWidget::startCountabilityLearningClicked);
    connect(countabilityReviewButton_, &QPushButton::clicked, this, &HomePageWidget::startCountabilityReviewClicked);
    connect(navBtn1, &QPushButton::clicked, this, &HomePageWidget::booksClicked);
    connect(navBtn3, &QPushButton::clicked, this, &HomePageWidget::statsClicked);
}

void HomePageWidget::setCounts(int learningCount,
                               int reviewCount,
                               int todayLearningCount,
                               int todayReviewCount,
                               int countabilityLearningCount,
                               int countabilityReviewCount) {
    learningButton_->setText(QStringLiteral("拼写学习\n%1").arg(learningCount));
    reviewButton_->setText(QStringLiteral("拼写复习\n%1").arg(reviewCount));
    countabilityLearnButton_->setText(QStringLiteral("可数性辨析\n%1").arg(countabilityLearningCount));
    countabilityReviewButton_->setText(QStringLiteral("可数性复习\n%1").arg(countabilityReviewCount));
    learningCountLabel_->setText(
        QStringLiteral("今日已学 %1 词 · 今日复习 %2 词").arg(todayLearningCount).arg(todayReviewCount));
    reviewCountLabel_->setText(QStringLiteral("长期主义的核心是无视中断"));
}

QRect HomePageWidget::launchRect(SessionMode mode) const {
    const QPushButton *button = nullptr;
    switch (mode) {
    case SessionMode::Learning:
        button = learningButton_;
        break;
    case SessionMode::Review:
        button = reviewButton_;
        break;
    case SessionMode::CountabilityLearning:
        button = countabilityLearnButton_;
        break;
    case SessionMode::CountabilityReview:
        button = countabilityReviewButton_;
        break;
    }

    if (button == nullptr) {
        return QRect();
    }
    return button->geometry();
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
    countabilityCombo_ = new QComboBox(this);
    pluralCombo_ = new QComboBox(this);
    notesCombo_ = new QComboBox(this);

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
    countabilityCombo_->setStyleSheet(comboStyle);
    pluralCombo_->setStyleSheet(comboStyle);
    notesCombo_->setStyleSheet(comboStyle);

    wordCombo_->setMinimumWidth(260);
    translationCombo_->setMinimumWidth(260);
    phoneticCombo_->setMinimumWidth(260);
    countabilityCombo_->setMinimumWidth(260);
    pluralCombo_->setMinimumWidth(260);
    notesCombo_->setMinimumWidth(260);

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
    mappingRows->addLayout(makeRow(QStringLiteral("音标列"), phoneticCombo_));
    mappingRows->addLayout(makeRow(QStringLiteral("可数性"), countabilityCombo_));
    mappingRows->addLayout(makeRow(QStringLiteral("复数形式"), pluralCombo_));
    mappingRows->addLayout(makeRow(QStringLiteral("用法备注"), notesCombo_));

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
        const int countabilityColumn = countabilityCombo_->currentData().toInt();
        const int pluralColumn = pluralCombo_->currentData().toInt();
        const int notesColumn = notesCombo_->currentData().toInt();

        emit importConfirmed(wordCombo_->currentIndex(), translationCombo_->currentIndex(), phoneticColumn,
                             countabilityColumn, pluralColumn, notesColumn);
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

    phoneticCombo_->addItem(QStringLiteral("不使用"), -1);
    countabilityCombo_->addItem(QStringLiteral("不处理"), -1);
    pluralCombo_->addItem(QStringLiteral("不处理"), -1);
    notesCombo_->addItem(QStringLiteral("不处理"), -1);

    for (int i = 0; i < headers.size(); ++i) {
        phoneticCombo_->addItem(headers.at(i), i);
        countabilityCombo_->addItem(headers.at(i), i);
        pluralCombo_->addItem(headers.at(i), i);
        notesCombo_->addItem(headers.at(i), i);
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
    if (bestPhonetic.isEmpty()) {
        phoneticCombo_->setCurrentIndex(0);
    } else {
        const int idx = phoneticCombo_->findText(bestPhonetic);
        if (idx >= 0) phoneticCombo_->setCurrentIndex(idx);
    }

    const QString bestCountability = findBestColumn(headers, {QStringLiteral("countability"), QStringLiteral("可数性")});
    if (!bestCountability.isEmpty()) {
        const int idx = countabilityCombo_->findText(bestCountability);
        if (idx >= 0) countabilityCombo_->setCurrentIndex(idx);
    } else {
        countabilityCombo_->setCurrentIndex(0);
    }

    const QString bestPlural = findBestColumn(headers, {QStringLiteral("plural"), QStringLiteral("复数")});
    if (!bestPlural.isEmpty()) {
        const int idx = pluralCombo_->findText(bestPlural);
        if (idx >= 0) pluralCombo_->setCurrentIndex(idx);
    } else {
        pluralCombo_->setCurrentIndex(0);
    }

    const QString bestNotes = findBestColumn(headers, {QStringLiteral("explanation"), QStringLiteral("notes"), QStringLiteral("备注")});
    if (!bestNotes.isEmpty()) {
        const int idx = notesCombo_->findText(bestNotes);
        if (idx >= 0) notesCombo_->setCurrentIndex(idx);
    } else {
        notesCombo_->setCurrentIndex(0);
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
