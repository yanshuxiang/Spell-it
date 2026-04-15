#include "gui_widgets.h"
#include "gui_widgets_internal.h"

#include <QBrush>
#include <QColor>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

using namespace GuiWidgetsInternal;
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
            learnButton->setObjectName(QStringLiteral("bookLearnButton"));
            learnButton->setFixedSize(88, 34);
            learnButton->setCursor(Qt::PointingHandCursor);
            learnButton->setStyleSheet(QStringLiteral(
                "#bookLearnButton {"
                "  font-size: 13px;"
                "  font-weight: 700;"
                "  border-radius: 17px;"
                "  padding: 0 12px;"
                "  border: 1px solid rgba(15,23,42,0.14);"
                "  background: #f8fafc;"
                "  color: #0f172a;"
                "}"
                "#bookLearnButton:hover { background: #eef2f7; }"
                "#bookLearnButton:pressed { background: #e2e8f0; }"));
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
            downloadButton->setFixedSize(112, 34);
            downloadButton->setCursor(Qt::PointingHandCursor);
            downloadButton->setToolTip(QStringLiteral("下载音频"));
            downloadButton->setStyleSheet(QStringLiteral(
                "#bookDownloadButton {"
                "  font-size: 13px; font-weight: 700; border-radius: 17px;"
                "  padding: 0 12px;"
                "  border: 1px solid rgba(15,23,42,0.14);"
                "  background: #f8fafc; color: #0f172a;"
                "}"
                "#bookDownloadButton:hover { background: #eef2f7; }"
                "#bookDownloadButton:pressed { background: #e2e8f0; }"));
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
