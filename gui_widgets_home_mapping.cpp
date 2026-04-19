#include "gui_widgets.h"
#include "gui_widgets_internal.h"
#include "app_logger.h"

#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QQmlContext>
#include <QPushButton>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVariantList>
#include <QVariantMap>
#include <QVBoxLayout>

using namespace GuiWidgetsInternal;
namespace {
class DashboardBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList cards READ cards NOTIFY cardsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)

public:
    explicit DashboardBridge(QObject *parent = nullptr)
        : QObject(parent) {}

    QVariantList cards() const { return cards_; }
    int currentIndex() const { return currentIndex_; }

    void setCards(const QVariantList &cards) {
        cards_ = cards;
        emit cardsChanged();
    }

    void setCurrentIndex(int index) {
        const int safeIndex = qMax(0, index);
        if (safeIndex == currentIndex_) {
            return;
        }
        currentIndex_ = safeIndex;
        emit currentIndexChanged(currentIndex_);
    }

    Q_INVOKABLE void requestStart(int modeIndex, bool isReview, const QVariantMap &globalRectMap) {
        const QRect globalRect(globalRectMap.value(QStringLiteral("x")).toInt(),
                               globalRectMap.value(QStringLiteral("y")).toInt(),
                               globalRectMap.value(QStringLiteral("width")).toInt(),
                               globalRectMap.value(QStringLiteral("height")).toInt());
        emit startRequested(modeIndex, isReview, globalRect);
    }

    Q_INVOKABLE void requestChangeBook(int modeIndex) {
        emit changeBookRequested(modeIndex);
    }

    Q_INVOKABLE void requestStats() {
        emit statsRequested();
    }

signals:
    void cardsChanged();
    void currentIndexChanged(int index);
    void startRequested(int modeIndex, bool isReview, const QRect &globalRect);
    void changeBookRequested(int modeIndex);
    void statsRequested();

private:
    QVariantList cards_;
    int currentIndex_ = 0;
};
} // namespace

HomePageWidget::HomePageWidget(QWidget *parent)
    : QWidget(parent) {
    setStyleSheet(QStringLiteral("background: #e9edf2;"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    dashboardView_ = new QQuickWidget(this);
    dashboardView_->setResizeMode(QQuickWidget::SizeRootObjectToView);
    dashboardView_->setClearColor(QColor("#e9edf2"));
    dashboardView_->setStyleSheet(QStringLiteral("background: #e9edf2;"));
    dashboardView_->setAttribute(Qt::WA_AlwaysStackOnTop, false);
    dashboardView_->setFocusPolicy(Qt::StrongFocus);
    setFocusPolicy(Qt::StrongFocus);

    auto *bridge = new DashboardBridge(this);
    dashboardBridge_ = bridge;
    dashboardView_->rootContext()->setContextProperty(QStringLiteral("bridge"), bridge);
    const QString qmlPath = QStringLiteral(VIBESPELLER_SOURCE_DIR) + QStringLiteral("/qml/MainDashboard.qml");
    dashboardView_->setSource(QUrl::fromLocalFile(qmlPath));

    root->addWidget(dashboardView_, 1);

    connect(bridge, &DashboardBridge::startRequested, this, &HomePageWidget::handleStartRequest);
    connect(bridge, &DashboardBridge::changeBookRequested, this, &HomePageWidget::handleChangeBookRequest);
    connect(bridge, &DashboardBridge::currentIndexChanged, this, &HomePageWidget::handleCurrentIndexChanged);
    connect(bridge, &DashboardBridge::statsRequested, this, &HomePageWidget::handleStatsRequest);
}

void HomePageWidget::setDashboardCards(const QVector<DashboardCardState> &cards,
                                       int currentIndex,
                                       int todayLearningCount,
                                       int todayReviewCount,
                                       int todayStudyMinutes) {
    Q_UNUSED(todayLearningCount);
    Q_UNUSED(todayReviewCount);
    Q_UNUSED(todayStudyMinutes);
    cards_ = cards;
    currentCardIndex_ = qMax(0, currentIndex);
    updateCardModel();
}

void HomePageWidget::updateCardModel() {
    auto *bridge = qobject_cast<DashboardBridge *>(dashboardBridge_);
    if (bridge == nullptr) {
        return;
    }

    QVariantList model;
    model.reserve(cards_.size());
    for (const DashboardCardState &card : cards_) {
        QVariantMap item;
        item.insert(QStringLiteral("modeTitle"), card.modeTitle);
        item.insert(QStringLiteral("bookName"), card.bookName.isEmpty() ? QStringLiteral("未绑定词书") : card.bookName);
        item.insert(QStringLiteral("coverName"), card.coverName);
        item.insert(QStringLiteral("themeColor"), card.themeColor);
        item.insert(QStringLiteral("hasActiveBook"), card.hasActiveBook);
        item.insert(QStringLiteral("learningEnabled"), card.learningEnabled);
        item.insert(QStringLiteral("reviewEnabled"), card.reviewEnabled);
        item.insert(QStringLiteral("mastered"), card.masteredWords);
        item.insert(QStringLiteral("total"), card.totalWords);
        item.insert(QStringLiteral("unlearned"), card.unlearnedCount);
        item.insert(QStringLiteral("due"), card.dueReviewCount);
        const double progress = (card.totalWords > 0)
                                    ? (static_cast<double>(card.masteredWords) / static_cast<double>(card.totalWords))
                                    : 0.0;
        item.insert(QStringLiteral("progress"), progress);
        model.push_back(item);
    }

    bridge->setCards(model);
    bridge->setCurrentIndex(currentCardIndex_);
}

void HomePageWidget::handleStartRequest(int modeIndex, bool isReview, const QRect &globalRect) {
    if (modeIndex < 0 || modeIndex > 2) {
        AppLogger::warn(QStringLiteral("Home"),
                        QStringLiteral("ignore start request, invalid modeIndex=%1").arg(modeIndex));
        return;
    }
    AppLogger::step(QStringLiteral("Home"),
                    QStringLiteral("start request, modeIndex=%1, isReview=%2, rect=(%3,%4,%5,%6)")
                        .arg(modeIndex)
                        .arg(isReview ? QStringLiteral("true") : QStringLiteral("false"))
                        .arg(globalRect.x())
                        .arg(globalRect.y())
                        .arg(globalRect.width())
                        .arg(globalRect.height()));
    SessionMode mode = SessionMode::Learning;
    if (modeIndex == 1) {
        mode = isReview ? SessionMode::CountabilityReview : SessionMode::CountabilityLearning;
    } else if (modeIndex == 2) {
        mode = isReview ? SessionMode::PolysemyReview : SessionMode::PolysemyLearning;
    } else {
        mode = isReview ? SessionMode::Review : SessionMode::Learning;
    }

    QRect localRect;
    if (!globalRect.isEmpty()) {
        localRect = QRect(mapFromGlobal(globalRect.topLeft()), globalRect.size());
    }
    if (localRect.isEmpty() && dashboardView_ != nullptr) {
        const QSize fallbackSize(220, 64);
        const QPoint center = dashboardView_->geometry().center() - QPoint(fallbackSize.width() / 2, fallbackSize.height() / 2);
        localRect = QRect(center, fallbackSize);
    }
    launchRectByMode_.insert(static_cast<int>(mode), localRect);

    switch (mode) {
    case SessionMode::Learning:
        emit startLearningClicked();
        break;
    case SessionMode::Review:
        emit startReviewClicked();
        break;
    case SessionMode::CountabilityLearning:
        emit startCountabilityLearningClicked();
        break;
    case SessionMode::CountabilityReview:
        emit startCountabilityReviewClicked();
        break;
    case SessionMode::PolysemyLearning:
        emit startPolysemyLearningClicked();
        break;
    case SessionMode::PolysemyReview:
        emit startPolysemyReviewClicked();
        break;
    }
}

void HomePageWidget::handleChangeBookRequest(int modeIndex) {
    QString trainingType = QStringLiteral("spelling");
    if (modeIndex == 1) {
        trainingType = QStringLiteral("countability");
    } else if (modeIndex == 2) {
        trainingType = QStringLiteral("polysemy");
    }
    AppLogger::step(QStringLiteral("Home"),
                    QStringLiteral("change book request, modeIndex=%1, type=%2")
                        .arg(modeIndex)
                        .arg(trainingType));
    emit changeBookRequested(trainingType);
}

void HomePageWidget::handleCurrentIndexChanged(int index) {
    currentCardIndex_ = qMax(0, index);
    AppLogger::info(QStringLiteral("Home"),
                    QStringLiteral("dashboard current index changed, index=%1").arg(currentCardIndex_));
    emit dashboardIndexChanged(currentCardIndex_);
}

void HomePageWidget::handleStatsRequest() {
    emit statsClicked();
}

QRect HomePageWidget::launchRect(SessionMode mode) const {
    const QRect stored = launchRectByMode_.value(static_cast<int>(mode));
    if (!stored.isEmpty()) {
        return stored;
    }
    if (dashboardView_ == nullptr) {
        return QRect();
    }
    const QSize fallbackSize(220, 64);
    const QPoint center = dashboardView_->geometry().center() - QPoint(fallbackSize.width() / 2, fallbackSize.height() / 2);
    return QRect(center, fallbackSize);
}

int HomePageWidget::currentCardIndex() const {
    return currentCardIndex_;
}

void HomePageWidget::focusDashboard() {
    if (dashboardView_ == nullptr) {
        return;
    }
    dashboardView_->setFocus(Qt::OtherFocusReason);
    if (QQuickWindow *quickWindow = dashboardView_->quickWindow()) {
        if (QQuickItem *contentItem = quickWindow->contentItem()) {
            contentItem->forceActiveFocus(Qt::OtherFocusReason);
        }
    }
    if (QObject *rootObject = dashboardView_->rootObject()) {
        QMetaObject::invokeMethod(rootObject, "recoverInteraction", Qt::DirectConnection);
    }
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
    polysemyCombo_ = new QComboBox(this);

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
    polysemyCombo_->setStyleSheet(comboStyle);

    wordCombo_->setMinimumWidth(260);
    translationCombo_->setMinimumWidth(260);
    phoneticCombo_->setMinimumWidth(260);
    countabilityCombo_->setMinimumWidth(260);
    pluralCombo_->setMinimumWidth(260);
    notesCombo_->setMinimumWidth(260);
    polysemyCombo_->setMinimumWidth(260);

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
        label->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: #111827; padding-bottom: 4px;"));

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
    mappingRows->addLayout(makeRow(QStringLiteral("熟词生义"), polysemyCombo_));

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
        const int polysemyColumn = polysemyCombo_->currentData().toInt();

        emit importConfirmed(wordCombo_->currentIndex(), translationCombo_->currentIndex(), phoneticColumn,
                             countabilityColumn, pluralColumn, notesColumn, polysemyColumn);
    });
}

void MappingPageWidget::setCsvData(const QString &csvPath,
                                   const QStringList &headers,
                                   const QVector<QStringList> &previewRows) {
    filePathLabel_->setText(csvPath);

    wordCombo_->clear();
    translationCombo_->clear();
    phoneticCombo_->clear();
    countabilityCombo_->clear();
    pluralCombo_->clear();
    notesCombo_->clear();
    polysemyCombo_->clear();

    wordCombo_->addItems(headers);
    translationCombo_->addItems(headers);

    phoneticCombo_->addItem(QStringLiteral("不使用"), -1);
    countabilityCombo_->addItem(QStringLiteral("不处理"), -1);
    pluralCombo_->addItem(QStringLiteral("不处理"), -1);
    notesCombo_->addItem(QStringLiteral("不处理"), -1);
    polysemyCombo_->addItem(QStringLiteral("不处理"), -1);

    for (int i = 0; i < headers.size(); ++i) {
        phoneticCombo_->addItem(headers.at(i), i);
        countabilityCombo_->addItem(headers.at(i), i);
        pluralCombo_->addItem(headers.at(i), i);
        notesCombo_->addItem(headers.at(i), i);
        polysemyCombo_->addItem(headers.at(i), i);
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

    const QString bestPolysemy = findBestColumn(headers,
                                                {QStringLiteral("polysemy_json"),
                                                 QStringLiteral("polysemy"),
                                                 QStringLiteral("json"),
                                                 QStringLiteral("熟词生义"),
                                                 QStringLiteral("生义")});
    if (!bestPolysemy.isEmpty()) {
        const int idx = polysemyCombo_->findText(bestPolysemy);
        if (idx >= 0) polysemyCombo_->setCurrentIndex(idx);
    } else {
        polysemyCombo_->setCurrentIndex(0);
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

#include "gui_widgets_home_mapping.moc"
