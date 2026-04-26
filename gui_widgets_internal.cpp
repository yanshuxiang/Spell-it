#include "gui_widgets_internal.h"
#include "app_logger.h"

#include <QColor>
#include <QDialog>
#include <QFormLayout>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QStyleOptionButton>
#include <QEnterEvent>

namespace GuiWidgetsInternal {

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
    // 使用 HSL 按 bookId 生成颜色，减少重复，同时保持低饱和、偏柔和。
    const int safeId = qMax(0, bookId);
    constexpr int kHueStep = 47; // 与 360 互质，前 360 个 id 不重复。
    const int hue = (safeId * kHueStep) % 360;
    const int saturation = 52; // 低饱和
    const int lightness = 118; // 偏柔和亮度
    return QColor::fromHsl(hue, saturation, lightness).name();
}

QString displayBookName(const QString &rawName, const QStringList &allBookNames) {
    QString name = rawName.trimmed();
    if (name.isEmpty()) {
        return QStringLiteral("词书");
    }

    // If the name looks like an auto-generated duplicate suffix ("xxx2", "xxx3"...),
    // and the base name exists, hide the suffix in UI while keeping internal ids intact.
    static const QRegularExpression kAutoSuffixPattern(QStringLiteral("^(.*?)([2-9]\\d*)$"));
    const QRegularExpressionMatch match = kAutoSuffixPattern.match(name);
    if (!match.hasMatch()) {
        return name;
    }

    const QString base = match.captured(1).trimmed();
    if (base.isEmpty()) {
        return name;
    }
    if (allBookNames.contains(base)) {
        return base;
    }
    return name;
}

QString coverTextForBook(const QString &bookName) {
    QString text = bookName.trimmed();
    if (text.isEmpty()) {
        text = QStringLiteral("词书");
    }

    // 中文封面：6 字以内不折叠，超过 6 字才折叠成两段，适配竖排书脊视觉。
    const bool hasCjk = text.contains(QRegularExpression(QStringLiteral("[\\u4e00-\\u9fff]")));
    if (hasCjk) {
        QString compact = text;
        compact.remove(QRegularExpression(QStringLiteral("\\s+")));
        if (compact.size() > 12) {
            compact = compact.left(12);
        }
        if (compact.size() > 6) {
            compact.insert(6, QChar('\n'));
        }
        return compact;
    }

    // 英文等非中文名称维持原有封面策略，避免长文本溢出。
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
    leftLabel->setMargin(4);
    leftLabel->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #111827; padding-bottom: 3px;"));

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
    const bool accepted = showStyledPrompt(parent, title, message, PromptType::Question, {QStringLiteral("取消"), QStringLiteral("确定")}, 1) == 2;
    AppLogger::info(QStringLiteral("Prompt"),
                    QStringLiteral("question title=%1 accepted=%2 message=%3")
                        .arg(title)
                        .arg(accepted ? QStringLiteral("true") : QStringLiteral("false"))
                        .arg(message));
    return accepted;
}

void showInfoPrompt(QWidget *parent, const QString &title, const QString &message) {
    AppLogger::info(QStringLiteral("Prompt"),
                    QStringLiteral("info title=%1 message=%2").arg(title, message));
    showStyledPrompt(parent, title, message, PromptType::Info, {QStringLiteral("确定")}, 0);
}

void showWarningPrompt(QWidget *parent, const QString &title, const QString &message) {
    AppLogger::warn(QStringLiteral("Prompt"),
                    QStringLiteral("warning title=%1 message=%2").arg(title, message));
    showStyledPrompt(parent, title, message, PromptType::Warning, {QStringLiteral("知道了")}, 0);
}

void showErrorPrompt(QWidget *parent, const QString &title, const QString &message) {
    AppLogger::error(QStringLiteral("Prompt"),
                     QStringLiteral("error title=%1 message=%2").arg(title, message));
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
} // namespace GuiWidgetsInternal

HoverScaleButton::HoverScaleButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent) {
    setMouseTracking(true);
}

HoverScaleButton::HoverScaleButton(QWidget *parent)
    : QPushButton(parent) {
    setMouseTracking(true);
}

void HoverScaleButton::enterEvent(QEnterEvent *event) {
    QPushButton::enterEvent(event);
    if (!hoverScaleEnabled_) {
        setScale(1.0);
        return;
    }
    auto *anim = new QPropertyAnimation(this, "scale");
    anim->setDuration(150);
    anim->setEndValue(1.06);
    anim->setEasingCurve(QEasingCurve::OutBack);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void HoverScaleButton::leaveEvent(QEvent *event) {
    QPushButton::leaveEvent(event);
    if (!hoverScaleEnabled_) {
        setScale(1.0);
        return;
    }
    auto *anim = new QPropertyAnimation(this, "scale");
    anim->setDuration(120);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void HoverScaleButton::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    painter.translate(rect().center());
    painter.scale(scale_, scale_);
    painter.translate(-rect().center());

    QStyleOptionButton option;
    initStyleOption(&option);
    style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
}
