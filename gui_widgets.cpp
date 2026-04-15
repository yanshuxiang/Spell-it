#include "gui_widgets.h"
#include "audio_downloader.h"

#include <QCoreApplication>
#include <QComboBox>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QColor>
#include <QIcon>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QSet>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QRegularExpression>

#include <cmath>
#include <utility>

namespace {
enum class PromptType {
    Info,
    Warning,
    Error,
    Question,
};

constexpr int kStudyIdleCutoffSeconds = 120;
// 每组练习词数。调试时可改成 5 等更小值。
constexpr int kSessionBatchSize = 5;
// 正确切词动画时长（毫秒），默认 0.7 秒。
constexpr int kCorrectTransitionMs = 300;
// 错误抖动动画时长（毫秒），默认 0.2 秒。
constexpr int kWrongShakeMs = 200;
// 正确切词位移幅度（像素），调大可让左右切换更明显。
constexpr int kTransitionShiftPx = 300;
// 错误抖动位移幅度（像素），调大晃动更明显。
constexpr int kWrongShakeOffsetPx = 3;
constexpr qreal kBasePlaybackVolume = 1.0;
constexpr double kTargetMeanDb = -20.0;
constexpr double kMaxPeakDb = -1.0;
constexpr int kAnalyzeTimeoutMs = 8000;

QString defaultInputStyle() {
    return QStringLiteral(
        "border: none;"
        "border-bottom: 2px solid #e5e7eb;"
        "color: #6b7280;"
        "padding: 6px 8px;"
        "background: transparent;");
}

QGraphicsOpacityEffect *ensureOpacityEffect(QWidget *widget) {
    if (widget == nullptr) {
        return nullptr;
    }
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(widget->graphicsEffect());
    if (effect == nullptr) {
        effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
    }
    return effect;
}

QString safeAudioFileName(const QString &word) {
    QString name = word.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    name = name.trimmed();
    if (name.isEmpty()) {
        name = QStringLiteral("word");
    }
    return name;
}

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

int nextReviewDaysForSummary(const PracticeRecord &record) {
    const QVector<int> ladder = {1, 2, 4, 7, 15, 30};
    const int currentInterval = record.word.interval;

    if (record.skipped || record.result == SpellingResult::Unfamiliar) {
        return 1;
    }

    if (record.result == SpellingResult::Blurry) {
        if (currentInterval <= ladder.first()) {
            return ladder.first();
        }
        for (int i = 0; i < ladder.size(); ++i) {
            if (ladder[i] == currentInterval) {
                return ladder[qMax(0, i - 1)];
            }
            if (ladder[i] > currentInterval) {
                return ladder[qMax(0, i - 1)];
            }
        }
        return ladder[ladder.size() - 2];
    }

    // Mastered: move to the next ladder step.
    if (currentInterval <= 0) {
        return ladder.first();
    }
    for (int i = 0; i < ladder.size(); ++i) {
        if (ladder[i] == currentInterval) {
            if (i + 1 < ladder.size()) {
                return ladder[i + 1];
            }
            return ladder.last();
        }
        if (ladder[i] > currentInterval) {
            return ladder[i];
        }
    }
    return ladder.last();
}

QString summaryRightText(const PracticeRecord &record, bool reviewMode) {
    Q_UNUSED(reviewMode);
    return QStringLiteral("%1天后复习").arg(nextReviewDaysForSummary(record));
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

QString coverTextForBook(const QString &bookName) {
    QString text = bookName.trimmed();
    if (text.isEmpty()) {
        text = QStringLiteral("词书");
    }
    if (text.size() > 4) {
        text = text.left(4);
    }
    if (text.size() >= 3) {
        text.insert(text.size() / 2, QChar('\n'));
    }
    return text;
}

QIcon createBackLineIcon() {
    QPixmap pix(20, 20);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor("#374151"));
    pen.setWidthF(1.9);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(13.5, 4.5), QPointF(6.5, 10.0));
    painter.drawLine(QPointF(6.5, 10.0), QPointF(13.5, 15.5));
    return QIcon(pix);
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

QIcon createTrashLineIcon() {
    QPixmap pix(20, 20);
    pix.fill(Qt::transparent);

    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor("#475569"));
    pen.setWidthF(1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    painter.drawRoundedRect(QRectF(6.5, 6.5, 7.0, 9.5), 1.8, 1.8);
    painter.drawLine(QPointF(5.0, 6.5), QPointF(15.0, 6.5));
    painter.drawLine(QPointF(7.6, 4.8), QPointF(12.4, 4.8));
    painter.drawLine(QPointF(8.3, 8.2), QPointF(8.3, 14.6));
    painter.drawLine(QPointF(10.0, 8.2), QPointF(10.0, 14.6));
    painter.drawLine(QPointF(11.7, 8.2), QPointF(11.7, 14.6));
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

QString buildMiniWeekCalendarHtml(const QDateTime &nextReview) {
    if (!nextReview.isValid()) {
        return QStringLiteral(
            "<div style='font-size:12px;color:#64748b;'>"
            "尚未安排复习时间"
            "</div>");
    }

    const QDate today = QDate::currentDate();
    const QDate dueDate = nextReview.date();
    const int daysToDue = today.daysTo(dueDate);
    const QString relation = (daysToDue > 0)
                                 ? QStringLiteral("%1 天后").arg(daysToDue)
                                 : (daysToDue == 0 ? QStringLiteral("今天") : QStringLiteral("已过期"));

    QString html = QStringLiteral(
        "<div style='font-size:12px;color:#64748b;'>"
        "下次复习：<b style='color:#0f172a;'>%1</b>（%2）"
        "</div><div style='margin-top:6px;'>")
                       .arg(dueDate.toString(QStringLiteral("MM-dd ddd")))
                       .arg(relation);

    for (int offset = 0; offset < 7; ++offset) {
        const QDate day = today.addDays(offset);
        const bool isDueDay = (day == dueDate);
        const QString bg = isDueDay ? QStringLiteral("#dbeafe") : QStringLiteral("#f8fafc");
        const QString fg = isDueDay ? QStringLiteral("#1d4ed8") : QStringLiteral("#64748b");
        const QString border = isDueDay ? QStringLiteral("#93c5fd") : QStringLiteral("#e2e8f0");
        html += QStringLiteral(
                    "<span style='display:inline-block;width:34px;height:34px;line-height:16px;"
                    "text-align:center;border-radius:10px;border:1px solid %1;background:%2;"
                    "font-size:11px;color:%3;margin-right:6px;padding-top:3px;'>"
                    "%4<br><b>%5</b></span>")
                    .arg(border, bg, fg)
                    .arg(day.toString(QStringLiteral("dd")))
                    .arg(day.toString(QStringLiteral("ddd")));
    }
    html += QStringLiteral("</div>");
    return html;
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
    translationLabel_->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: #111827;"));

    inputEdit_ = new QLineEdit(this);
    inputEdit_->setPlaceholderText(QString());
    inputEdit_->setAlignment(Qt::AlignCenter);
    inputEdit_->setFixedWidth(310);
    QFont spellingInputFont = inputEdit_->font();
    spellingInputFont.setPixelSize(35);
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
    translationLabel_->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 700; color: #111827;"));
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
    shakeWordLabel->setStyleSheet(QStringLiteral("font-size: 35px; font-weight: 500; color: #ef4444;"));
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

        const bool hasTextInput = !keyEvent->text().isEmpty()
                                  && !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        if (hasTextInput) {
            resetInputOnNextType_ = false;
            applyInputDefaultStyle();
            const QString typed = keyEvent->text();
            inputEdit_->clear();
            inputEdit_->setText(typed);
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
    setMouseTracking(true);

    hoverTip_ = new QLabel(this);
    hoverTip_->setAttribute(Qt::WA_TransparentForMouseEvents);
    hoverTip_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hoverTip_->setMargin(0);
    hoverTip_->setStyleSheet(QStringLiteral(
        "background: rgba(15,23,42,0.92);"
        "color: #f8fafc;"
        "border: 1px solid rgba(203,213,225,0.25);"
        "border-radius: 10px;"
        "padding: 6px 8px;"
        "font-size: 12px;"
        "font-weight: 600;"));
    hoverTip_->hide();

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
    hoverBars_.clear();
    hoveredBarIndex_ = -1;
    if (hoverTip_) {
        hoverTip_->hide();
    }
    update();
}

void StatisticsPageWidget::paintEvent(QPaintEvent *event) {
    QWidget::paintEvent(event);
    hoverBars_.clear();

    if (logs_.isEmpty()) {
        if (hoverTip_) {
            hoverTip_->hide();
        }
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor learningColor("#d38945"); // keep orange hue, lower saturation
    const QColor reviewColor("#9c73cf");   // keep purple hue, lower saturation
    const QColor accentColor("#d38945");
    const QColor textPrimary("#111827");
    const QColor textMuted("#6b7280");
    const QColor gridColor("#e6e9ee");
    const QColor lineColor("#c6c9cf");
    const QColor cardBg("#fbfcfd");
    const QColor cardBorder("#e7ebf0");

    const int marginX = 28;
    const int marginTop = 86;
    const int marginBottom = 28;
    const int sectionGap = 18;
    const int contentWidth = this->width() - marginX * 2;
    const int contentHeight = this->height() - marginTop - marginBottom;
    const int topHeight = (contentHeight - sectionGap) / 2;

    QRect topRect(marginX, marginTop, contentWidth, topHeight);
    QRect bottomRect(marginX, marginTop + topHeight + sectionGap, contentWidth, contentHeight - topHeight - sectionGap);

    painter.setPen(QPen(cardBorder, 1));
    painter.setBrush(cardBg);
    painter.drawRoundedRect(topRect, 18, 18);
    painter.drawRoundedRect(bottomRect, 18, 18);

    const QRect topCard = topRect.adjusted(14, 12, -14, -12);
    const QRect bottomCard = bottomRect.adjusted(14, 12, -14, -12);

    // Top section: learning/review count bars.
    painter.setPen(textPrimary);
    painter.setFont(QFont(font().family(), 12, QFont::Bold));
    painter.drawText(QRect(topCard.left(), topCard.top(), topCard.width(), 20),
                     Qt::AlignCenter,
                     QStringLiteral("单词数量"));

    const QRect topPlot = topCard.adjusted(8, 34, -8, -66);
    int maxCount = 10;
    for (const auto &log : logs_) {
        maxCount = qMax(maxCount, log.learningCount + log.reviewCount);
    }

    const int barWidth = qMax(4, qMin(10, topPlot.width() / (logs_.size() * 4)));
    for (int i = 0; i < logs_.size(); ++i) {
        const DatabaseManager::DailyLog &log = logs_[i];
        const int total = log.learningCount + log.reviewCount;
        const double xRatio = (i + 0.5) / static_cast<double>(logs_.size());
        const int centerX = topPlot.left() + static_cast<int>(xRatio * topPlot.width());
        const int fullHeight = static_cast<int>((static_cast<double>(total) / maxCount) * topPlot.height());
        const int learningHeight = total > 0
                                       ? static_cast<int>((static_cast<double>(log.learningCount) / total) * fullHeight)
                                       : 0;
        const int reviewHeight = fullHeight - learningHeight;
        const int baseY = topPlot.bottom();

        if (reviewHeight > 0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(reviewColor);
            painter.drawRoundedRect(QRect(centerX - barWidth / 2, baseY - fullHeight, barWidth, reviewHeight), 4, 4);
        }
        if (learningHeight > 0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(learningColor);
            painter.drawRoundedRect(QRect(centerX - barWidth / 2, baseY - learningHeight, barWidth, learningHeight), 4, 4);
        }

        if (fullHeight > 0) {
            HoverBarInfo info;
            const int hoverWidth = qMax(16, barWidth + 10);
            const int hoverHeight = qMax(18, fullHeight);
            info.rect = QRect(centerX - hoverWidth / 2, baseY - hoverHeight, hoverWidth, hoverHeight);
            const QString dayText = (i == logs_.size() - 1) ? QStringLiteral("今日") : log.date;
            info.text = QStringLiteral("%1\n学习：%2 词\n复习：%3 词\n合计：%4 词")
                            .arg(dayText)
                            .arg(log.learningCount)
                            .arg(log.reviewCount)
                            .arg(total);
            hoverBars_.push_back(info);
        }

        painter.setPen(textMuted);
        painter.setFont(QFont(font().family(), 9));
        QString dateStr = (i == logs_.size() - 1) ? QStringLiteral("今日") : log.date.right(5);
        painter.drawText(QRect(centerX - 30, topPlot.bottom() + 6, 60, 18), Qt::AlignCenter, dateStr);
    }

    const DatabaseManager::DailyLog &today = logs_.last();
    painter.setPen(textMuted);
    painter.setFont(QFont(font().family(), 10, QFont::DemiBold));
    painter.drawText(QRect(topCard.left(), topCard.bottom() - 34, topCard.width() / 2, 16), Qt::AlignCenter, QStringLiteral("当日学习"));
    painter.drawText(QRect(topCard.center().x(), topCard.bottom() - 34, topCard.width() / 2, 16), Qt::AlignCenter, QStringLiteral("当日复习"));
    painter.setPen(textPrimary);
    painter.setFont(QFont(font().family(), 20, QFont::Bold));
    painter.drawText(QRect(topCard.left(), topCard.bottom() - 16, topCard.width() / 2, 22), Qt::AlignCenter, QString::number(today.learningCount));
    painter.drawText(QRect(topCard.center().x(), topCard.bottom() - 16, topCard.width() / 2, 22), Qt::AlignCenter, QString::number(today.reviewCount));

    // Top-right legend (muted colors).
    painter.setPen(Qt::NoPen);
    painter.setBrush(learningColor);
    painter.drawEllipse(QRect(topCard.right() - 112, topCard.top() + 4, 10, 10));
    painter.setBrush(reviewColor);
    painter.drawEllipse(QRect(topCard.right() - 50, topCard.top() + 4, 10, 10));
    painter.setPen(textMuted);
    painter.setFont(QFont(font().family(), 10, QFont::DemiBold));
    painter.drawText(QRect(topCard.right() - 96, topCard.top() + 2, 38, 14), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("学习"));
    painter.drawText(QRect(topCard.right() - 34, topCard.top() + 2, 38, 14), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("复习"));

    // Bottom section: study time line chart (minutes).
    painter.setPen(textPrimary);
    painter.setFont(QFont(font().family(), 12, QFont::Bold));
    painter.drawText(QRect(bottomCard.left(), bottomCard.top(), bottomCard.width(), 20),
                     Qt::AlignCenter,
                     QStringLiteral("学习时长（分钟）"));

    const QRect linePlot = bottomCard.adjusted(8, 34, -8, -76);
    int maxMinutes = 10;
    int weeklyMinutes = 0;
    QVector<QPointF> points;
    points.reserve(logs_.size());
    for (int i = 0; i < logs_.size(); ++i) {
        const int minutes = logs_[i].studyMinutes;
        maxMinutes = qMax(maxMinutes, minutes);
        weeklyMinutes += minutes;
    }

    painter.setPen(QPen(gridColor, 1));
    for (int i = 0; i < logs_.size(); ++i) {
        const double xRatio = (i + 0.5) / static_cast<double>(logs_.size());
        const int x = linePlot.left() + static_cast<int>(xRatio * linePlot.width());
        painter.drawLine(x, linePlot.top(), x, linePlot.bottom());
    }

    for (int i = 0; i < logs_.size(); ++i) {
        const int minutes = logs_[i].studyMinutes;
        const double xRatio = (i + 0.5) / static_cast<double>(logs_.size());
        const int x = linePlot.left() + static_cast<int>(xRatio * linePlot.width());
        const int y = linePlot.bottom() - static_cast<int>((static_cast<double>(minutes) / maxMinutes) * linePlot.height());
        points.push_back(QPointF(x, y));
    }

    painter.setPen(QPen(lineColor, 2.2));
    painter.setBrush(Qt::NoBrush);
    if (points.size() >= 2) {
        painter.drawPolyline(points.data(), points.size());
    }

    painter.setFont(QFont(font().family(), 9, QFont::DemiBold));
    for (int i = 0; i < points.size(); ++i) {
        const bool isToday = (i == points.size() - 1);
        const QColor pointColor = isToday ? accentColor : QColor("#cfd3d8");
        painter.setPen(Qt::NoPen);
        painter.setBrush(pointColor);
        painter.drawEllipse(points[i], 5, 5);

        painter.setPen(isToday ? accentColor : textMuted);
        painter.drawText(QRect(static_cast<int>(points[i].x()) - 22,
                               static_cast<int>(points[i].y()) - 22,
                               44,
                               16),
                         Qt::AlignCenter,
                         QString::number(logs_[i].studyMinutes));
    }

    painter.setPen(textMuted);
    painter.setFont(QFont(font().family(), 9));
    for (int i = 0; i < points.size(); ++i) {
        QString dateStr = (i == points.size() - 1) ? QStringLiteral("今日") : logs_[i].date.right(5);
        painter.drawText(QRect(static_cast<int>(points[i].x()) - 24, linePlot.bottom() + 8, 48, 16),
                         Qt::AlignCenter,
                         dateStr);
    }

    painter.setPen(textMuted);
    painter.setFont(QFont(font().family(), 10, QFont::DemiBold));
    painter.drawText(QRect(bottomCard.left(), bottomCard.bottom() - 46, bottomCard.width() / 2, 18),
                     Qt::AlignCenter,
                     QStringLiteral("当日学习时长"));
    painter.drawText(QRect(bottomCard.center().x(), bottomCard.bottom() - 46, bottomCard.width() / 2, 18),
                     Qt::AlignCenter,
                     QStringLiteral("近7天总时长"));

    painter.setPen(textPrimary);
    painter.setFont(QFont(font().family(), 18, QFont::Bold));
    painter.drawText(QRect(bottomCard.left(), bottomCard.bottom() - 24, bottomCard.width() / 2, 22),
                     Qt::AlignCenter,
                     QStringLiteral("%1 分钟").arg(today.studyMinutes));
    painter.drawText(QRect(bottomCard.center().x(), bottomCard.bottom() - 24, bottomCard.width() / 2, 22),
                     Qt::AlignCenter,
                     QStringLiteral("%1 分钟").arg(weeklyMinutes));
}

void StatisticsPageWidget::mouseMoveEvent(QMouseEvent *event) {
    int hitIndex = -1;
    for (int i = 0; i < hoverBars_.size(); ++i) {
        if (hoverBars_[i].rect.contains(event->pos())) {
            hitIndex = i;
            break;
        }
    }

    if (hitIndex >= 0) {
        hoveredBarIndex_ = hitIndex;
        if (hoverTip_) {
            const HoverBarInfo &info = hoverBars_[hitIndex];
            hoverTip_->setText(info.text);
            hoverTip_->adjustSize();
            QPoint popupPos = event->pos() - QPoint(hoverTip_->width() + 10, hoverTip_->height() + 10);
            // Keep tooltip inside current page.
            popupPos.setX(qBound(6, popupPos.x(), width() - hoverTip_->width() - 6));
            popupPos.setY(qBound(6, popupPos.y(), height() - hoverTip_->height() - 6));
            hoverTip_->move(popupPos);
            hoverTip_->show();
        }
    } else {
        hoveredBarIndex_ = -1;
        if (hoverTip_) {
            hoverTip_->hide();
        }
    }

    QWidget::mouseMoveEvent(event);
}

void StatisticsPageWidget::leaveEvent(QEvent *event) {
    hoveredBarIndex_ = -1;
    if (hoverTip_) {
        hoverTip_->hide();
    }
    QWidget::leaveEvent(event);
}

WordBooksPageWidget::WordBooksPageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 18, 24, 22);
    root->setSpacing(10);

    auto *header = new QHBoxLayout();
    header->setSpacing(14);
    backButton_ = new QPushButton(this);
    backButton_->setFixedSize(46, 46);
    backButton_->setIcon(createBackLineIcon());
    backButton_->setIconSize(QSize(20, 20));
    backButton_->setStyleSheet(QStringLiteral(
        "border-radius: 14px;"
        "background: #f3f4f6; color: #374151;"
        "QPushButton:hover { background: #e9ebef; }"));

    auto *title = new QLabel(QStringLiteral("词书"), this);
    title->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 700; color: #0f172a;"));

    metaLabel_ = new QLabel(QStringLiteral("管理词书与当前学习进度"), this);
    metaLabel_->setStyleSheet(QStringLiteral("font-size: 15px; color: #6b7280;"));

    auto *titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(4);
    titleCol->addWidget(title);
    titleCol->addWidget(metaLabel_);

    audioStatusHost_ = new QWidget(this);
    audioStatusHost_->setFixedSize(240, 54);
    audioStatusHost_->setStyleSheet(QStringLiteral(
        "background: transparent;"
        "border: none;"));
    auto *audioStatusLayout = new QVBoxLayout(audioStatusHost_);
    audioStatusLayout->setContentsMargins(0, 0, 0, 0);
    audioStatusLayout->setSpacing(3);

    audioStatusLabel_ = new QLabel(QStringLiteral("音频未下载"), audioStatusHost_);
    audioStatusLabel_->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: #475569;"
        "background: transparent; border: none;"));

    audioProgressBar_ = new QProgressBar(audioStatusHost_);
    audioProgressBar_->setTextVisible(false);
    audioProgressBar_->setRange(0, 100);
    audioProgressBar_->setValue(0);
    audioProgressBar_->setFixedHeight(8);
    audioProgressBar_->setStyleSheet(QStringLiteral(
        "QProgressBar {"
        "  border: none;"
        "  border-radius: 4px;"
        "  background: rgba(148,163,184,0.22);"
        "}"
        "QProgressBar::chunk {"
        "  border: none;"
        "  border-radius: 4px;"
        "  min-width: 8px;"
        "  background: #0ea5a4;"
        "}"));

    audioStopButton_ = new QPushButton(QStringLiteral("⏸"), audioStatusHost_);
    audioStopButton_->setObjectName(QStringLiteral("audioPauseEmojiButton"));
    audioStopButton_->setFixedSize(18, 18);
    audioStopButton_->setToolTip(QStringLiteral("暂停下载"));
    audioStopButton_->setStyleSheet(QStringLiteral(
        "#audioPauseEmojiButton {"
        "  border: none;"
        "  background: transparent;"
        "  padding: 0;"
        "  font-size: 12px;"
        "  color: #64748b;"
        "}"
        "#audioPauseEmojiButton:hover { color: #334155; }"
        "#audioPauseEmojiButton:disabled { color: #cbd5e1; }"));
    audioStopButton_->setEnabled(false);

    auto *progressRow = new QHBoxLayout();
    progressRow->setContentsMargins(0, 0, 0, 0);
    progressRow->setSpacing(6);
    progressRow->addWidget(audioProgressBar_, 1);
    progressRow->addWidget(audioStopButton_, 0, Qt::AlignVCenter);

    audioStatusLayout->addWidget(audioStatusLabel_);
    audioStatusLayout->addLayout(progressRow);

    header->addWidget(backButton_, 0, Qt::AlignTop);
    header->addLayout(titleCol, 1);
    header->addWidget(audioStatusHost_, 0, Qt::AlignTop | Qt::AlignRight);

    currentTitleLabel_ = new QLabel(QStringLiteral("当前学习词书"), this);
    currentTitleLabel_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #334155;"));

    currentCardHost_ = new QWidget(this);
    currentCardLayout_ = new QVBoxLayout(currentCardHost_);
    currentCardLayout_->setContentsMargins(0, 0, 0, 0);
    currentCardLayout_->setSpacing(0);

    otherTitleLabel_ = new QLabel(QStringLiteral("其他词书"), this);
    otherTitleLabel_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #334155;"));

    booksList_ = new QListWidget(this);
    booksList_->setSpacing(10);
    booksList_->setSelectionMode(QAbstractItemView::NoSelection);
    booksList_->setFrameShape(QFrame::NoFrame);
    booksList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    booksList_->setStyleSheet(QStringLiteral(
        "QListWidget { border: none; background: transparent; outline: none; }"
        "QListWidget::item { border: none; padding: 0; margin: 0; }"
        "QScrollBar:vertical { background: transparent; width: 8px; margin: 4px 0; }"
        "QScrollBar::handle:vertical { background: rgba(148,163,184,0.35); border-radius: 4px; min-height: 30px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));

    addBookButton_ = new QPushButton(QStringLiteral("添加词书"), this);
    addBookButton_->setFixedHeight(60);
    addBookButton_->setStyleSheet(QStringLiteral(
        "font-size: 22px; font-weight: 700; border-radius: 18px;"
        "background: #0f1b3d; color: #ffffff;"
        "QPushButton:hover { background: #13224b; }"));

    root->addLayout(header);
    root->addWidget(currentTitleLabel_);
    root->addWidget(currentCardHost_);
    root->addWidget(otherTitleLabel_);
    root->addWidget(booksList_, 1);
    root->addWidget(addBookButton_);

    connect(backButton_, &QPushButton::clicked, this, &WordBooksPageWidget::backClicked);
    connect(addBookButton_, &QPushButton::clicked, this, &WordBooksPageWidget::addBookClicked);
    connect(audioStopButton_, &QPushButton::clicked, this, &WordBooksPageWidget::audioDownloadStopRequested);
    setAudioDownloadStatus(QStringLiteral("音频未下载"), 0, 0, false);
}

void WordBooksPageWidget::setAudioDownloadStatus(const QString &text, int current, int total, bool running) {
    if (audioStatusHost_) {
        audioStatusHost_->setVisible(running);
    }
    if (!running) {
        if (audioStatusLabel_) {
            audioStatusLabel_->setText(QString());
        }
        if (audioProgressBar_) {
            audioProgressBar_->setRange(0, 100);
            audioProgressBar_->setValue(0);
        }
        if (audioStopButton_) {
            audioStopButton_->setEnabled(false);
        }
        return;
    }

    if (audioStatusLabel_) {
        audioStatusLabel_->setText(text);
    }
    if (audioProgressBar_) {
        if (total < 0) {
            audioProgressBar_->setRange(0, 0);
        } else if (total == 0) {
            audioProgressBar_->setRange(0, 100);
            audioProgressBar_->setValue(0);
        } else if (total > 0) {
            audioProgressBar_->setRange(0, total);
            audioProgressBar_->setValue(qBound(0, current, total));
        }
    }
    if (audioStopButton_) {
        audioStopButton_->setEnabled(running);
    }
}

void WordBooksPageWidget::setWordBooks(const QVector<WordBookItem> &books, int activeBookId) {
    books_ = books;
    activeBookId_ = activeBookId;
    int totalWords = 0;
    for (const WordBookItem &book : books_) {
        totalWords += book.wordCount;
    }
    if (metaLabel_) {
        metaLabel_->setText(QStringLiteral("共 %1 本词书 · %2 词").arg(books_.size()).arg(totalWords));
    }
    rebuildList();
}

void WordBooksPageWidget::rebuildList() {
    while (QLayoutItem *item = currentCardLayout_->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    booksList_->clear();
    booksList_->setUpdatesEnabled(false);

    if (books_.isEmpty()) {
        auto *emptyCurrent = new QWidget(currentCardHost_);
        emptyCurrent->setStyleSheet(QStringLiteral(
            "background: #f8fafc; border: 1px dashed #d8e1ec; border-radius: 18px;"));
        auto *emptyCurrentLayout = new QVBoxLayout(emptyCurrent);
        emptyCurrentLayout->setContentsMargins(16, 12, 16, 12);
        auto *emptyTitle = new QLabel(QStringLiteral("还没有词书"), emptyCurrent);
        emptyTitle->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: #334155;"));
        auto *emptyDesc = new QLabel(QStringLiteral("点击底部“添加词书”导入 CSV 开始练习"), emptyCurrent);
        emptyDesc->setStyleSheet(QStringLiteral("font-size: 14px; color: #64748b;"));
        emptyCurrentLayout->addWidget(emptyTitle);
        emptyCurrentLayout->addWidget(emptyDesc);
        currentCardLayout_->addWidget(emptyCurrent);
        otherTitleLabel_->hide();
        booksList_->hide();
        booksList_->setUpdatesEnabled(true);
        return;
    }

    otherTitleLabel_->show();
    booksList_->show();

    WordBookItem activeBook = books_.first();
    for (const WordBookItem &book : books_) {
        if (book.id == activeBookId_) {
            activeBook = book;
            break;
        }
    }

    const auto makeBookCard = [this](QWidget *parent, const WordBookItem &book, bool isCurrent) -> QWidget * {
        auto *row = new QWidget(parent);
        row->setObjectName(QStringLiteral("bookRow"));
        row->setStyleSheet(QStringLiteral(
            "QWidget#bookRow {"
            "  background: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 18px;"
            "}").arg(isCurrent ? QStringLiteral("#f8fafc") : QStringLiteral("#ffffff"),
                     isCurrent ? QStringLiteral("#dbe4ef") : QStringLiteral("#e9eef5")));

        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(12);

        const QColor baseColor(coverColorForBook(book.id));
        const QString coverTop = baseColor.lighter(118).name();
        const QString coverBottom = baseColor.darker(105).name();

        auto *cover = new QLabel(coverTextForBook(book.name), row);
        cover->setAlignment(Qt::AlignCenter);
        cover->setFixedSize(68, 84);
        cover->setWordWrap(true);
        cover->setStyleSheet(QStringLiteral(
            "font-size: 12px; line-height: 1.1; font-weight: 700; color: rgba(255,255,255,0.95);"
            "border-radius: 14px;"
            "background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %1, stop:1 %2);")
                                 .arg(coverTop, coverBottom));

        auto *title = new QLabel(book.name, row);
        title->setStyleSheet(QStringLiteral(
            "font-size: 20px; font-weight: 700; color: #0f172a;"
            "background: transparent; border: none;"));
        title->setWordWrap(true);

        auto *count = new QLabel(QStringLiteral("%1 词").arg(book.wordCount), row);
        count->setStyleSheet(QStringLiteral(
            "font-size: 15px; color: #94a3b8;"
            "background: transparent; border: none;"));

        auto *textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(5);
        textLayout->addWidget(title);
        textLayout->addWidget(count);

        if (isCurrent) {
            auto *progressBar = new QProgressBar(row);
            progressBar->setTextVisible(false);
            progressBar->setRange(0, qMax(1, book.wordCount));
            progressBar->setValue(qBound(0, book.learnedCount, qMax(1, book.wordCount)));
            progressBar->setFixedHeight(8);
            progressBar->setStyleSheet(QStringLiteral(
                "QProgressBar {"
                "  border: none;"
                "  border-radius: 4px;"
                "  background: rgba(148,163,184,0.22);"
                "}"
                "QProgressBar::chunk {"
                "  border: none;"
                "  border-radius: 4px;"
                "  min-width: 8px;"
                "  background: #0ea5a4;"
                "}"));

            auto *progressText = new QHBoxLayout();
            progressText->setContentsMargins(0, 0, 0, 0);
            progressText->setSpacing(4);
            auto *learnedLabel = new QLabel(QStringLiteral("已学习 %1").arg(book.learnedCount), row);
            learnedLabel->setStyleSheet(QStringLiteral(
                "font-size: 13px; color: #475569; background: transparent; border: none;"));
            auto *totalLabel = new QLabel(QStringLiteral("总词数 %1").arg(book.wordCount), row);
            totalLabel->setStyleSheet(QStringLiteral(
                "font-size: 13px; color: #64748b; background: transparent; border: none;"));
            progressText->addWidget(learnedLabel, 0, Qt::AlignLeft);
            progressText->addStretch();
            progressText->addWidget(totalLabel, 0, Qt::AlignRight);

            textLayout->addSpacing(2);
            textLayout->addWidget(progressBar);
            textLayout->addLayout(progressText);
        }

        textLayout->addStretch();

        auto *rightLayout = new QVBoxLayout();
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(4);
        rightLayout->setAlignment(Qt::AlignTop | Qt::AlignRight);

        if (!isCurrent) {
            auto *learnButton = new QPushButton(QStringLiteral("学习"), row);
            learnButton->setFixedSize(74, 30);
            learnButton->setStyleSheet(QStringLiteral(
                "font-size: 13px; font-weight: 700; border-radius: 10px;"
                "background: #0f1b3d; color: #ffffff;"
                "QPushButton:hover { background: #13224b; }"));
            connect(learnButton, &QPushButton::clicked, this, [this, book]() {
                emit wordBookSelected(book.id);
            });

            auto *deleteButton = new QPushButton(QStringLiteral("🗑"), row);
            deleteButton->setObjectName(QStringLiteral("bookDeleteEmojiButton"));
            deleteButton->setFixedSize(26, 26);
            deleteButton->setToolTip(QStringLiteral("删除词书"));
            deleteButton->setStyleSheet(QStringLiteral(
                "#bookDeleteEmojiButton {"
                "  border: none; border-radius: 8px; padding: 0;"
                "  background: transparent; font-size: 12px; color: #64748b;"
                "}"
                "#bookDeleteEmojiButton:hover { background: rgba(148,163,184,0.12); color: #475569; }"));
            connect(deleteButton, &QPushButton::clicked, this, [this, book]() {
                emit wordBookDeleteRequested(book.id);
            });

            auto *opsRow = new QHBoxLayout();
            opsRow->setContentsMargins(0, 0, 0, 0);
            opsRow->setSpacing(6);
            opsRow->addStretch(1);
            opsRow->addWidget(learnButton);
            opsRow->addWidget(deleteButton);
            rightLayout->addLayout(opsRow);
        } else {
            auto *downloadButton = new QPushButton(QStringLiteral("下载音频"), row);
            downloadButton->setObjectName(QStringLiteral("bookDownloadButton"));
            downloadButton->setFixedSize(88, 28);
            downloadButton->setToolTip(QStringLiteral("下载音频"));
            downloadButton->setStyleSheet(QStringLiteral(
                "#bookDownloadButton {"
                "  font-size: 12px; font-weight: 700; border-radius: 8px;"
                "  padding: 0;"
                "  border: 1px solid rgba(15,23,42,0.20);"
                "  background: transparent; color: #334155;"
                "}"
                "#bookDownloadButton:hover { background: rgba(15,23,42,0.05); }"));
            connect(downloadButton, &QPushButton::clicked, this, [this, book]() {
                emit downloadAudioRequested(book.id);
            });

            auto *deleteButton = new QPushButton(QStringLiteral("🗑"), row);
            deleteButton->setObjectName(QStringLiteral("bookDeleteEmojiButton"));
            deleteButton->setFixedSize(26, 26);
            deleteButton->setToolTip(QStringLiteral("删除词书"));
            deleteButton->setStyleSheet(QStringLiteral(
                "#bookDeleteEmojiButton {"
                "  border: none; border-radius: 8px; padding: 0;"
                "  background: transparent; font-size: 12px; color: #64748b;"
                "}"
                "#bookDeleteEmojiButton:hover { background: rgba(148,163,184,0.12); color: #475569; }"));
            connect(deleteButton, &QPushButton::clicked, this, [this, book]() {
                emit wordBookDeleteRequested(book.id);
            });

            auto *opsRow = new QHBoxLayout();
            opsRow->setContentsMargins(0, 0, 0, 0);
            opsRow->setSpacing(6);
            opsRow->addStretch(1);
            opsRow->addWidget(downloadButton);
            opsRow->addWidget(deleteButton);
            rightLayout->addLayout(opsRow);
        }

        layout->addWidget(cover);
        layout->addLayout(textLayout, 1);
        layout->addLayout(rightLayout);
        return row;
    };

    currentCardLayout_->addWidget(makeBookCard(currentCardHost_, activeBook, true));

    int otherCount = 0;
    for (const WordBookItem &book : books_) {
        if (book.id == activeBook.id) {
            continue;
        }
        ++otherCount;
        auto *item = new QListWidgetItem(booksList_);
        item->setData(Qt::UserRole, book.id);
        item->setSizeHint(QSize(0, 114));
        booksList_->addItem(item);
        booksList_->setItemWidget(item, makeBookCard(booksList_, book, false));
    }

    if (otherCount == 0) {
        auto *item = new QListWidgetItem(booksList_);
        item->setSizeHint(QSize(0, 78));
        booksList_->addItem(item);

        auto *emptyWidget = new QWidget(booksList_);
        emptyWidget->setStyleSheet(QStringLiteral(
            "background: #f8fafc; border: 1px dashed #d8e1ec; border-radius: 14px;"));
        auto *emptyLayout = new QHBoxLayout(emptyWidget);
        emptyLayout->setContentsMargins(14, 10, 14, 10);
        auto *emptyTitle = new QLabel(QStringLiteral("无其他词书"), emptyWidget);
        emptyTitle->setStyleSheet(QStringLiteral(
            "font-size: 14px; font-weight: 600; color: #64748b;"
            "background: transparent; border: none;"));
        emptyLayout->addWidget(emptyTitle);
        emptyLayout->addStretch();
        booksList_->setItemWidget(item, emptyWidget);
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
