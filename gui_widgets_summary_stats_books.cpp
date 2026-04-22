#include "gui_widgets.h"
#include "gui_widgets_internal.h"

#include <QBrush>
#include <QBuffer>
#include <QColor>
#include <QFontMetrics>
#include <QFrame>
#include <QComboBox>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QMenu>
#include <QPaintEvent>
#include <QTabBar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QVBoxLayout>

using namespace GuiWidgetsInternal;

class RoundedProgressStrip final : public QWidget {
public:
    explicit RoundedProgressStrip(QWidget *parent = nullptr)
        : QWidget(parent) {}

    void setRange(int minimum, int maximum) {
        minimum_ = minimum;
        maximum_ = qMax(minimum_, maximum);
        value_ = qBound(minimum_, value_, maximum_);
        update();
    }

    void setValue(int value) {
        value_ = qBound(minimum_, value, maximum_);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect();
        if (r.width() <= 0 || r.height() <= 0) {
            return;
        }

        const qreal radius = r.height() / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(148, 163, 184, 56));
        painter.drawRoundedRect(r, radius, radius);

        const int span = qMax(1, maximum_ - minimum_);
        const qreal ratio = qBound(0.0, static_cast<qreal>(value_ - minimum_) / span, 1.0);
        if (ratio <= 0.0) {
            return;
        }

        // Keep a visible rounded-rectangle chunk even when progress is very small.
        const qreal fillWidth = qMax<qreal>(18.0, r.width() * ratio);
        QRectF fillRect = r;
        fillRect.setWidth(qMin(r.width(), fillWidth));
        painter.setBrush(QColor("#0ea5a4"));
        painter.drawRoundedRect(fillRect, radius, radius);
    }

private:
    int minimum_ = 0;
    int maximum_ = 100;
    int value_ = 0;
};

SummaryPageWidget::SummaryPageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(10);

    setStyleSheet(QStringLiteral("background: #ffffff;"));

    auto *topBar = new QHBoxLayout();
    topBar->setContentsMargins(0, 0, 0, 0);

    auto *backButton = new HoverScaleButton(QStringLiteral("‹"), this);
    backButton->setFixedSize(32, 32);
    backButton->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; background: transparent; color: #222222;"));

    auto *topTitle = new QLabel(QStringLiteral("小结"), this);
    topTitle->setAlignment(Qt::AlignCenter);
    topTitle->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: #222222; padding-bottom: 2px;"));

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
    accuracyLabel_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #222222; padding-top: 2px; padding-bottom: 4px;"));

    statsLabel_ = new QLabel(this);
    statsLabel_->setAlignment(Qt::AlignCenter);
    statsLabel_->setStyleSheet(QStringLiteral("font-size: 13px; color: #7b7b7b;"));

    auto *listFrame = new QFrame(this);
    listFrame->setStyleSheet(QStringLiteral("QFrame { background: #ffffff; border: none; }"));
    auto *listLayout = new QVBoxLayout(listFrame);
    listLayout->setContentsMargins(14, 10, 14, 10);
    listLayout->setSpacing(0);

    auto *wrongTitle = new QLabel(QStringLiteral("本组拼写"), listFrame);
    wrongTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: #222222; padding-bottom: 2px;"));

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

    backHomeButton_ = new HoverScaleButton(QStringLiteral("返回首页"), this);
    backHomeButton_->setMinimumHeight(60);
    backHomeButton_->setStyleSheet(QStringLiteral(
        "background: rgba(17, 24, 39, 0.08);"
        "color: #111827;"
        "font-size: 18px;"
        "font-weight: 700;"
        "border-radius: 26px;"));

    nextGroupButton_ = new HoverScaleButton(QStringLiteral("继续下一组"), this);
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

    connect(backButton, &HoverScaleButton::clicked, this, &SummaryPageWidget::backHomeClicked);
    connect(backHomeButton_, &HoverScaleButton::clicked, this, &SummaryPageWidget::backHomeClicked);
    connect(nextGroupButton_, &HoverScaleButton::clicked, this, &SummaryPageWidget::nextGroupClicked);
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
    backButton_ = new HoverScaleButton(QStringLiteral("返回"), this);
    backButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "  background: transparent;"
        "  font-size: 16px;"
        "  color: #4b5563;"
        "  padding: 8px;"
        "}"
        "HoverScaleButton:hover { color: #111827; }"));
    
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

    connect(backButton_, &HoverScaleButton::clicked, this, &StatisticsPageWidget::backClicked);
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

    const QColor spellingLearnColor("#3b82f6");    // Blue 500
    const QColor spellingReviewColor("#93c5fd");   // Blue 300
    const QColor cbLearnColor("#10b981");          // Emerald 500
    const QColor cbReviewColor("#a7f3d0");         // Emerald 200
    const QColor totalLineColor("#94a3b8");        // Slate 400 (Grey)
    const QColor textPrimary("#111827");
    const QColor textMuted("#64748b");
    const QColor gridColor("#f1f5f9");
    const QColor cardBg("#ffffff");
    const QColor cardBorder("#e2e8f0");

    const int marginX = 28;
    const int marginTop = 86;
    const int sectionGap = 18;
    const int marginBottom = 28;
    const int contentWidth = this->width() - marginX * 2;
    const int contentHeight = this->height() - marginTop - marginBottom;
    const int actualTopHeight = (contentHeight - sectionGap) / 2;

    QRect topRect(marginX, marginTop, contentWidth, actualTopHeight);
    QRect bottomRect(marginX, marginTop + actualTopHeight + sectionGap, contentWidth, contentHeight - actualTopHeight - sectionGap);

    painter.setPen(QPen(cardBorder, 1));
    painter.setBrush(cardBg);
    painter.drawRoundedRect(topRect, 18, 18);
    painter.drawRoundedRect(bottomRect, 18, 18);

    const QRect topCard = topRect.adjusted(14, 12, -14, -12);
    const QRect bottomCard = bottomRect.adjusted(14, 12, -14, -12);

    // --- TOP SECTION: Word Quantity (Side-by-side grouped bars) ---
    painter.setPen(QPen(QColor("#1e293b"), 1)); 
    painter.setFont(QFont(font().family(), 13, QFont::Bold));
    painter.drawText(QRect(topCard.left(), topCard.top(), topCard.width(), 24), Qt::AlignCenter, QStringLiteral("学习数量统计"));

    const QRect topPlot = topCard.adjusted(12, 88, -12, -56);
    int maxCount = 10;
    for (const auto &log : logs_) {
        maxCount = qMax(maxCount, log.learningCount + log.reviewCount);
        maxCount = qMax(maxCount, log.countabilityLearningCount + log.countabilityReviewCount);
    }
    maxCount = (maxCount * 12) / 10; // Extra headroom

    const int dayWidth = topPlot.width() / logs_.size();
    const int barWidth = qMax(6, qMin(12, dayWidth / 4));
    
    for (int i = 0; i < logs_.size(); ++i) {
        const auto &log = logs_[i];
        const int centerX = topPlot.left() + (i + 0.5) * dayWidth;
        const int baseY = topPlot.bottom();

        // Spelling Bar (Left)
        const int spTotal = log.learningCount + log.reviewCount;
        const int spH = static_cast<int>((static_cast<double>(spTotal) / maxCount) * topPlot.height());
        const int spLearnH = spTotal > 0 ? (log.learningCount * spH / spTotal) : 0;
        const int spReviewH = spH - spLearnH;
        QRect spRect(centerX - barWidth - 2, baseY - spH, barWidth, spH);
        
        if (spReviewH > 0) {
            painter.setBrush(spellingReviewColor);
            painter.drawRoundedRect(QRect(spRect.left(), baseY - spH, barWidth, spReviewH), 3, 3);
        }
        if (spLearnH > 0) {
            painter.setBrush(spellingLearnColor);
            painter.drawRoundedRect(QRect(spRect.left(), baseY - spLearnH, barWidth, spLearnH), 3, 3);
        }

        // Countability Bar (Right)
        const int cbTotal = log.countabilityLearningCount + log.countabilityReviewCount;
        const int cbH = static_cast<int>((static_cast<double>(cbTotal) / maxCount) * topPlot.height());
        const int cbLearnH = cbTotal > 0 ? (log.countabilityLearningCount * cbH / cbTotal) : 0;
        const int cbReviewH = cbH - cbLearnH;
        QRect cbRect(centerX + 2, baseY - cbH, barWidth, cbH);

        if (cbReviewH > 0) {
            painter.setBrush(cbReviewColor);
            painter.drawRoundedRect(QRect(cbRect.left(), baseY - cbH, barWidth, cbReviewH), 3, 3);
        }
        if (cbLearnH > 0) {
            painter.setBrush(cbLearnColor);
            painter.drawRoundedRect(QRect(cbRect.left(), baseY - cbLearnH, barWidth, cbLearnH), 3, 3);
        }

        // Hover areas
        const QString dateLabel = (i == logs_.size() - 1) ? QStringLiteral("今日") : log.date.right(5);
        HoverBarInfo spInfo;
        spInfo.rect = spRect.adjusted(-2, -5, 2, 5);
        spInfo.text = QStringLiteral("<b>%1·拼写练习</b><br/>新学: %2<br/>复习: %3<br/>共计: %4").arg(dateLabel).arg(log.learningCount).arg(log.reviewCount).arg(spTotal);
        hoverBars_.push_back(spInfo);

        HoverBarInfo cbInfo;
        cbInfo.rect = cbRect.adjusted(-2, -5, 2, 5);
        cbInfo.text = QStringLiteral("<b>%1·可数性练习</b><br/>新学: %2<br/>复习: %3<br/>共计: %4").arg(dateLabel).arg(log.countabilityLearningCount).arg(log.countabilityReviewCount).arg(cbTotal);
        hoverBars_.push_back(cbInfo);

        // Date labels
        painter.setPen(textMuted);
        painter.setFont(QFont(font().family(), 9));
        painter.drawText(QRect(centerX - dayWidth/2, topPlot.bottom() + 6, dayWidth, 18), Qt::AlignCenter, dateLabel);
    }

    // Legend for quantity - Vertically stacked, Left-aligned at top-left
    auto drawLegend = [&](int x, int y, QColor c1, QColor c2, QString label) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(c1); painter.drawEllipse(x, y, 9, 9);
        painter.setBrush(c2); painter.drawEllipse(x + 12, y, 9, 9);
        painter.setPen(textMuted); painter.setFont(QFont(font().family(), 10, QFont::DemiBold));
        painter.drawText(x + 28, y + 9, label);
    };
    
    int legX = topCard.left() + 8;
    int legY = topCard.top() + 36; 
    drawLegend(legX, legY, spellingLearnColor, spellingReviewColor, QStringLiteral("拼写练习"));
    drawLegend(legX, legY + 20, cbLearnColor, cbReviewColor, QStringLiteral("可数性练习"));

    // --- BOTTOM SECTION: Study Time (Line Chart with Filter) ---
    painter.setPen(textPrimary);
    painter.setFont(QFont(font().family(), 12, QFont::Bold));
    painter.drawText(QRect(bottomCard.left(), bottomCard.top(), bottomCard.width(), 24), Qt::AlignCenter, QStringLiteral("学习时长统计（分钟）"));

    const QRect linePlot = bottomCard.adjusted(12, 108, -12, -66);
    int maxSecs = 600;
    for (const auto &log : logs_) {
        maxSecs = qMax(maxSecs, log.spellingSeconds + log.countabilitySeconds);
    }
    maxSecs = (maxSecs * 11) / 10;

    auto getPoints = [&](int type) { // 0:total, 1:spelling, 2:cb
        QVector<QPointF> pts;
        for (int i = 0; i < logs_.size(); ++i) {
            int s = 0;
            if (type == 0) s = logs_[i].spellingSeconds + logs_[i].countabilitySeconds;
            else if (type == 1) s = logs_[i].spellingSeconds;
            else s = logs_[i].countabilitySeconds;
            double xr = (i + 0.5) / static_cast<double>(logs_.size());
            int x = linePlot.left() + static_cast<int>(xr * linePlot.width());
            int y = linePlot.bottom() - static_cast<int>(static_cast<double>(s) / maxSecs * linePlot.height());
            pts.push_back(QPointF(x, y));
        }
        return pts;
    };

    painter.setPen(QPen(gridColor, 1));
    for (int i = 0; i < logs_.size(); ++i) {
        int x = linePlot.left() + (i + 0.5) / logs_.size() * linePlot.width();
        painter.drawLine(x, linePlot.top(), x, linePlot.bottom());
    }

    // Always Spelling
    {
        QPen p(spellingReviewColor, 1.5);
        painter.setPen(p);
        QVector<QPointF> pts = getPoints(1);
        if (pts.size() >= 2) painter.drawPolyline(pts.data(), pts.size());
        for (auto &pt : pts) { painter.setBrush(spellingReviewColor); painter.setPen(Qt::NoPen); painter.drawEllipse(pt, 4, 4); }
    }
    // Always Countability
    {
        QPen p(cbReviewColor, 1.5);
        painter.setPen(p);
        QVector<QPointF> pts = getPoints(2);
        if (pts.size() >= 2) painter.drawPolyline(pts.data(), pts.size());
        for (auto &pt : pts) { painter.setBrush(cbReviewColor); painter.setPen(Qt::NoPen); painter.drawEllipse(pt, 4, 4); }
    }
    // Always Total
    {
        QPen p(totalLineColor, 1.5);
        painter.setPen(p);
        QVector<QPointF> pts = getPoints(0);
        if (pts.size() >= 2) painter.drawPolyline(pts.data(), pts.size());
        for (auto &pt : pts) { painter.setBrush(totalLineColor); painter.setPen(Qt::NoPen); painter.drawEllipse(pt, 5, 5); }
    }

    // Legends for time chart - Vertically stacked, Left-aligned at top-left
    auto drawTimeLegend = [&](int x, int y, QColor c, QString label) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(c); painter.drawEllipse(x, y, 9, 9);
        painter.setPen(textMuted); painter.setFont(QFont(font().family(), 10, QFont::DemiBold));
        painter.drawText(x + 14, y + 9, label);
    };
    
    int tLegX = bottomCard.left() + 8;
    int tLegY = bottomCard.top() + 36;
    drawTimeLegend(tLegX, tLegY, totalLineColor, QStringLiteral("总时长"));
    drawTimeLegend(tLegX, tLegY + 20, spellingReviewColor, QStringLiteral("拼写时长"));
    drawTimeLegend(tLegX, tLegY + 40, cbReviewColor, QStringLiteral("可数性时长"));

    // Quick stats at bottom
    const auto &today = logs_.last();
    int todaySp = today.spellingSeconds / 60;
    int todayCb = today.countabilitySeconds / 60;
    int totalSp = 0, totalCb = 0;
    for(auto &l : logs_) { totalSp += l.spellingSeconds; totalCb += l.countabilitySeconds; }

    painter.setPen(textMuted); painter.setFont(QFont(font().family(), 10, QFont::DemiBold));
    painter.drawText(QRect(bottomCard.left(), bottomCard.bottom() - 32, bottomCard.width() / 2, 16), Qt::AlignCenter, QStringLiteral("今日时长 (拼写|可数)"));
    painter.drawText(QRect(bottomCard.center().x(), bottomCard.bottom() - 32, bottomCard.width() / 2, 16), Qt::AlignCenter, QStringLiteral("7天汇总 (拼写|可数)"));
    painter.setPen(textPrimary); painter.setFont(QFont(font().family(), 16, QFont::Bold));
    painter.drawText(QRect(bottomCard.left(), bottomCard.bottom() - 14, bottomCard.width() / 2, 20), Qt::AlignCenter, QStringLiteral("%1 | %2 min").arg(todaySp).arg(todayCb));
    painter.drawText(QRect(bottomCard.center().x(), bottomCard.bottom() - 14, bottomCard.width() / 2, 20), Qt::AlignCenter, QStringLiteral("%1 | %2 min").arg(totalSp/60).arg(totalCb/60));
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

class CalendarPageWidget::CalendarCellButton final : public QPushButton {
public:
    explicit CalendarCellButton(QWidget *parent = nullptr)
        : QPushButton(parent) {
        setFlat(true);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setDiameter(32);
    }

    void setDiameter(int diameter) {
        diameter_ = qBound(24, diameter, 40);
        setFixedSize(diameter_, diameter_);
    }

    void setCellData(const QDate &date, bool inCurrentMonth, bool isFuture, int studyMinutes, bool selected) {
        date_ = date;
        setText((date.isValid() && inCurrentMonth) ? QString::number(date.day()) : QString());
        const bool activeDate = date.isValid() && inCurrentMonth && !isFuture;
        setEnabled(activeDate);

        QString textColor = QStringLiteral("#111827");
        if (!inCurrentMonth) {
            textColor = QStringLiteral("#9ca3af");
        } else if (isFuture) {
            textColor = QStringLiteral("#9ca3af");
        }

        QString bgColor = QStringLiteral("transparent");
        if (activeDate && studyMinutes > 5) {
            if (studyMinutes <= 15) bgColor = QStringLiteral("#d1fae5");
            else if (studyMinutes <= 30) bgColor = QStringLiteral("#86efac");
            else if (studyMinutes <= 60) bgColor = QStringLiteral("#4ade80");
            else bgColor = QStringLiteral("#16a34a");
        }
        QString borderColor = QStringLiteral("transparent");
        QString borderWidth = QStringLiteral("0");
        if (selected && activeDate) {
            borderColor = QStringLiteral("#2563eb");
            borderWidth = QStringLiteral("1");
        }
        const int radius = diameter_ / 2;
        setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  border: %1px solid %2;"
            "  border-radius: %3px;"
            "  background: %4;"
            "  color: %5;"
            "  font-size: 14px;"
            "  font-weight: 600;"
            "  padding: 0px;"
            "}"
            "QPushButton:disabled {"
            "  color: %5;"
            "  background: transparent;"
            "  border: 0px solid transparent;"
            "}")
            .arg(borderWidth, borderColor, QString::number(radius), bgColor, textColor));
        setToolTip(studyMinutes > 0
                       ? QStringLiteral("%1：学习 %2 分钟").arg(date.toString(QStringLiteral("yyyy-MM-dd"))).arg(studyMinutes)
                       : QString());
    }

    QDate date() const { return date_; }

private:
    QDate date_;
    int diameter_ = 32;
};

PhraseClusterPageWidget::PhraseClusterPageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 14);
    root->setSpacing(10);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(14);

    backButton_ = new HoverScaleButton(this);
    backButton_->setFixedSize(46, 46);
    backButton_->setIcon(createBackLineIcon());
    backButton_->setIconSize(QSize(20, 20));
    backButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "background: #f3f4f6;"
        "color: #475569;"
        "border: none;"
        "border-radius: 14px;"
        "}"
        "HoverScaleButton:hover { background: #eceff3; }"));

    titleLabel_ = new QLabel(QStringLiteral("词群翻译训练"), this);
    titleLabel_->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 700; color: #0f172a;"));
    titleMetaLabel_ = new QLabel(QStringLiteral(""), this);
    titleMetaLabel_->setStyleSheet(QStringLiteral("font-size: 15px; color: #6b7280;"));
    titleMetaLabel_->setVisible(false);

    auto *titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(4);
    titleCol->addWidget(titleLabel_);
    titleCol->addWidget(titleMetaLabel_);

    header->addWidget(backButton_, 0, Qt::AlignTop);
    header->addLayout(titleCol, 1);
    root->addLayout(header);

    controlPanel_ = new QFrame(this);
    controlPanel_->setStyleSheet(QStringLiteral(
        "QFrame { background: #ffffff; border: 1px solid #d8dee6; border-radius: 14px; }"));
    auto *controlLayout = new QVBoxLayout(controlPanel_);
    controlLayout->setContentsMargins(12, 10, 12, 10);
    controlLayout->setSpacing(8);

    modeTabs_ = new QTabBar(controlPanel_);
    modeTabs_->setDocumentMode(true);
    modeTabs_->setDrawBase(false);
    modeTabs_->setExpanding(false);
    modeTabs_->addTab(QStringLiteral("学习"));
    modeTabs_->addTab(QStringLiteral("复习"));
    modeTabs_->setStyleSheet(QStringLiteral(
        "QTabBar::tab {"
        "  border: 1px solid #cbd5e1; border-right: none; min-width: 70px;"
        "  padding: 8px 10px; background: #f8fafc; color: #64748b; font-size: 14px; font-weight: 600;}"
        "QTabBar::tab:first { border-top-left-radius: 10px; border-bottom-left-radius: 10px; }"
        "QTabBar::tab:last { border-right: 1px solid #cbd5e1; border-top-right-radius: 10px; border-bottom-right-radius: 10px; }"
        "QTabBar::tab:selected { background: #ffffff; color: #0f172a; border-color: #94a3b8; }"));

    bookCombo_ = new QComboBox(controlPanel_);
    bookCombo_->setMinimumWidth(180);
    bookCombo_->setStyleSheet(QStringLiteral(
        "QComboBox { min-height: 36px; padding: 0 10px; border: 1px solid #d1d9e5; border-radius: 10px; background: #ffffff; font-size: 14px; }"));

    sessionSizeCombo_ = new QComboBox(controlPanel_);
    sessionSizeCombo_->addItem(QStringLiteral("每组 5 题"), 5);
    sessionSizeCombo_->addItem(QStringLiteral("每组 10 题"), 10);
    sessionSizeCombo_->addItem(QStringLiteral("每组 15 题"), 15);
    sessionSizeCombo_->addItem(QStringLiteral("每组 20 题"), 20);
    sessionSizeCombo_->setCurrentIndex(0);
    sessionSizeCombo_->setStyleSheet(QStringLiteral(
        "QComboBox { min-height: 36px; padding: 0 10px; border: 1px solid #d1d9e5; border-radius: 10px; background: #ffffff; font-size: 14px; }"));

    manageButton_ = new HoverScaleButton(QStringLiteral("管理词群词书"), controlPanel_);
    manageButton_->setFixedHeight(36);
    manageButton_->setMinimumWidth(132);
    manageButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "font-size: 14px; font-weight: 700; color: #334155; border-radius: 10px;"
        "background: #eef2f7; border: 1px solid #d4dbe6; }"
        "HoverScaleButton:hover { background: #e6ebf1; }"));

    auto *controlTopRow = new QHBoxLayout();
    controlTopRow->setContentsMargins(0, 0, 0, 0);
    controlTopRow->setSpacing(8);
    controlTopRow->addWidget(modeTabs_, 0, Qt::AlignLeft);
    controlTopRow->addWidget(bookCombo_, 1);
    controlTopRow->addWidget(sessionSizeCombo_, 0);
    controlTopRow->addWidget(manageButton_, 0);

    managementPanel_ = new QFrame(controlPanel_);
    managementPanel_->setStyleSheet(QStringLiteral(
        "QFrame { background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 10px; }"));
    managementButtonsLayout_ = new QHBoxLayout(managementPanel_);
    managementButtonsLayout_->setContentsMargins(8, 8, 8, 8);
    managementButtonsLayout_->setSpacing(8);

    addBookButton_ = new HoverScaleButton(QStringLiteral("新增词书"), managementPanel_);
    deleteBookButton_ = new HoverScaleButton(QStringLiteral("删除词书"), managementPanel_);
    importJsonButton_ = new HoverScaleButton(QStringLiteral("导入 JSON"), managementPanel_);
    importCsvButton_ = new HoverScaleButton(QStringLiteral("导入 CSV"), managementPanel_);
    const QString actionStyle = QStringLiteral(
        "HoverScaleButton {"
        "font-size: 13px; font-weight: 700; color: #334155; padding: 6px 10px;"
        "border-radius: 8px; background: #ffffff; border: 1px solid #d1d9e5; }"
        "HoverScaleButton:hover { background: #f3f7fc; }");
    addBookButton_->setStyleSheet(actionStyle);
    deleteBookButton_->setStyleSheet(actionStyle);
    importJsonButton_->setStyleSheet(actionStyle);
    importCsvButton_->setStyleSheet(actionStyle);
    addBookButton_->setMinimumWidth(96);
    deleteBookButton_->setMinimumWidth(96);
    importJsonButton_->setMinimumWidth(104);
    importCsvButton_->setMinimumWidth(104);
    managementPanel_->setVisible(false);
    refreshManagementButtonsLayout();

    controlLayout->addLayout(controlTopRow);
    controlLayout->addWidget(managementPanel_);
    root->addWidget(controlPanel_);

    trainPanel_ = new QFrame(this);
    trainPanel_->setStyleSheet(QStringLiteral(
        "QFrame { background: transparent; border: none; }"));
    auto *trainLayout = new QVBoxLayout(trainPanel_);
    trainLayout->setContentsMargins(12, 6, 12, 18);
    trainLayout->setSpacing(8);

    progressLabel_ = new QLabel(QStringLiteral("准备开始"), trainPanel_);
    progressLabel_->setAlignment(Qt::AlignCenter);
    progressLabel_->setStyleSheet(QStringLiteral("font-size: 14px; color: #4b5563; font-weight: 600;"));

    clusterLabel_ = new QLabel(QStringLiteral("暂无词群"), trainPanel_);
    clusterLabel_->setAlignment(Qt::AlignCenter);
    clusterLabel_->setWordWrap(true);
    clusterLabel_->setMinimumHeight(150);
    clusterLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    clusterLabel_->setMargin(8);
    clusterLabel_->setStyleSheet(QStringLiteral("font-size: 35px; font-weight: 700; color: #111827;"));

    metaLabel_ = new QLabel(QStringLiteral("来源：—"), trainPanel_);
    metaLabel_->setAlignment(Qt::AlignCenter);
    metaLabel_->setWordWrap(true);
    metaLabel_->setStyleSheet(QStringLiteral("font-size: 14px; color: #4b5563;"));

    exampleLabel_ = new QLabel(trainPanel_);
    exampleLabel_->setAlignment(Qt::AlignCenter);
    exampleLabel_->setWordWrap(true);
    exampleLabel_->setStyleSheet(QStringLiteral(
        "font-size: 16px; color: #6b7280; line-height: 1.5; padding: 4px 8px;"));

    answerEdit_ = new QLineEdit(trainPanel_);
    answerEdit_->setPlaceholderText(QStringLiteral("输入英文表达（严格匹配）"));
    answerEdit_->setAlignment(Qt::AlignCenter);
    answerEdit_->setFixedWidth(360);
    QFont phraseInputFont = answerEdit_->font();
    phraseInputFont.setPixelSize(30);
    answerEdit_->setFont(phraseInputFont);
    const int phraseInputHeight = qMax(64, QFontMetrics(phraseInputFont).height() + 26);
    answerEdit_->setFixedHeight(phraseInputHeight);
    answerEdit_->setFrame(false);
    answerEdit_->setTextMargins(0, 0, 0, 0);
    answerEdit_->setStyleSheet(QStringLiteral(
        "QLineEdit {"
        "border: none;"
        "border-bottom: 2px solid #e5e7eb;"
        "color: #111827;"
        "padding: 6px 8px;"
        "background: transparent;"
        "}"
        "QLineEdit:focus { border-bottom: 2px solid #334155; }"));

    feedbackLabel_ = new QLabel(trainPanel_);
    feedbackLabel_->setAlignment(Qt::AlignCenter);
    feedbackLabel_->setWordWrap(true);
    feedbackLabel_->setMinimumHeight(56);
    feedbackLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    feedbackLabel_->setStyleSheet(QStringLiteral(
        "font-size: 14px; color: #6b7280;"));

    submitButton_ = new HoverScaleButton(QStringLiteral("提交"), trainPanel_);
    skipButton_ = new HoverScaleButton(QStringLiteral("跳过"), trainPanel_);
    nextButton_ = new HoverScaleButton(QStringLiteral("下一题"), trainPanel_);
    submitButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "font-size: 16px; font-weight: 700; color: #ffffff; background: #0f172a; border-radius: 10px; padding: 8px 14px; }"
        "HoverScaleButton:hover { background: #111f38; }"));
    skipButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "font-size: 16px; font-weight: 700; color: #334155; background: #eef2f7; border: 1px solid #d1d9e5; border-radius: 10px; padding: 8px 14px; }"
        "HoverScaleButton:hover { background: #e6ecf3; }"));
    nextButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "font-size: 16px; font-weight: 700; color: #334155; background: #eef2f7; border: 1px solid #d1d9e5; border-radius: 10px; padding: 8px 14px; }"
        "HoverScaleButton:hover { background: #e6ecf3; }"));
    submitButton_->setVisible(false);
    skipButton_->setVisible(false);
    nextButton_->setVisible(false);

    trainLayout->addWidget(progressLabel_);
    trainLayout->addStretch(1);
    trainLayout->addWidget(clusterLabel_);
    trainLayout->addSpacing(8);
    trainLayout->addWidget(metaLabel_);
    trainLayout->addWidget(exampleLabel_);
    trainLayout->addSpacing(12);
    auto *answerRow = new QHBoxLayout();
    answerRow->setContentsMargins(0, 0, 0, 0);
    answerRow->addStretch(1);
    answerRow->addWidget(answerEdit_, 0, Qt::AlignHCenter);
    answerRow->addStretch(1);
    trainLayout->addLayout(answerRow);
    trainLayout->addSpacing(12);
    trainLayout->addWidget(feedbackLabel_);
    trainLayout->addStretch(2);

    root->addWidget(trainPanel_, 1);

    bookManageView_ = new QWidget(this);
    auto *manageRoot = new QVBoxLayout(bookManageView_);
    manageRoot->setContentsMargins(0, 0, 0, 0);
    manageRoot->setSpacing(10);

    manageCurrentTitle_ = new QLabel(QStringLiteral("词群 · 当前绑定词书"), bookManageView_);
    manageCurrentTitle_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #334155;"));
    manageCurrentHost_ = new QWidget(bookManageView_);
    manageCurrentLayout_ = new QVBoxLayout(manageCurrentHost_);
    manageCurrentLayout_->setContentsMargins(0, 0, 0, 0);
    manageCurrentLayout_->setSpacing(10);

    manageOtherTitle_ = new QLabel(QStringLiteral("其他词群词书"), bookManageView_);
    manageOtherTitle_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 700; color: #334155;"));
    manageOtherList_ = new QListWidget(bookManageView_);
    manageOtherList_->setSpacing(8);
    manageOtherList_->setSelectionMode(QAbstractItemView::NoSelection);
    manageOtherList_->setFrameShape(QFrame::NoFrame);
    manageOtherList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    manageOtherList_->setStyleSheet(QStringLiteral(
        "QListWidget { border: none; background: transparent; outline: none; }"
        "QListWidget::item { border: none; padding: 0; margin: 0; }"));

    manageAddButton_ = new HoverScaleButton(QStringLiteral("添加词群词书"), bookManageView_);
    manageAddButton_->setFixedHeight(60);
    manageAddButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "font-size: 22px; font-weight: 700; border-radius: 18px;"
        "background: #0f1b3d; color: #ffffff; }"
        "HoverScaleButton:hover { background: #13224b; }"));

    manageRoot->addWidget(manageCurrentTitle_);
    manageRoot->addWidget(manageCurrentHost_, 3);
    manageRoot->addWidget(manageOtherTitle_);
    manageRoot->addWidget(manageOtherList_, 4);
    manageRoot->addWidget(manageAddButton_);

    root->addWidget(bookManageView_, 1);
    bookManageView_->setVisible(false);

    connect(backButton_, &HoverScaleButton::clicked, this, &PhraseClusterPageWidget::backClicked);
    connect(manageButton_, &HoverScaleButton::clicked, this, [this]() {
        setManagementOnlyView(true);
    });
    connect(modeTabs_, &QTabBar::currentChanged, this, [this](int idx) {
        reviewMode_ = (idx == 1);
        reloadSession();
    });
    connect(bookCombo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (db_ == nullptr || idx < 0) {
            return;
        }
        const int bookId = bookCombo_->itemData(idx).toInt();
        if (bookId <= 0) {
            return;
        }
        if (!db_->setActivePhraseBook(bookId)) {
            showWarningPrompt(this, QStringLiteral("切换失败"), db_->lastError());
            return;
        }
        refreshBooks();
        reloadSession();
    });
    connect(sessionSizeCombo_, &QComboBox::currentIndexChanged, this, [this](int idx) {
        Q_UNUSED(idx);
        sessionSize_ = sessionSizeCombo_->currentData().toInt();
        if (sessionSize_ <= 0) {
            sessionSize_ = 5;
        }
        reloadSession();
    });

    connect(addBookButton_, &HoverScaleButton::clicked, this, [this]() {
        if (db_ == nullptr) {
            return;
        }
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("新增词群词书"), QStringLiteral("词书名称："), QLineEdit::Normal,
            QStringLiteral("词群词书"), &ok);
        if (!ok) {
            return;
        }
        int newBookId = -1;
        if (!db_->createPhraseBook(name, &newBookId)) {
            showWarningPrompt(this, QStringLiteral("创建失败"), db_->lastError());
            return;
        }
        refreshBooks();
        reloadSession();
    });
    connect(manageAddButton_, &HoverScaleButton::clicked, this, [this]() {
        if (db_ == nullptr) {
            return;
        }
        QMenu menu(this);
        QAction *createAction = menu.addAction(QStringLiteral("新增空词书"));
        QAction *importJsonAction = menu.addAction(QStringLiteral("导入 JSON 到新词书"));
        QAction *importCsvAction = menu.addAction(QStringLiteral("导入 CSV 到新词书"));
        const QPoint pos = manageAddButton_->mapToGlobal(QPoint(manageAddButton_->width() / 2, 0));
        QAction *selected = menu.exec(pos);
        if (selected == nullptr) {
            return;
        }

        bool ok = false;
        QString defaultName = QStringLiteral("词群词书");
        QString sourcePath;
        if (selected == importJsonAction) {
            sourcePath = QFileDialog::getOpenFileName(
                this, QStringLiteral("导入词群 JSON"), QDir::homePath(),
                QStringLiteral("JSON Files (*.json *.jsonl);;All Files (*)"));
            if (sourcePath.isEmpty()) {
                return;
            }
            defaultName = QFileInfo(sourcePath).completeBaseName();
        } else if (selected == importCsvAction) {
            sourcePath = QFileDialog::getOpenFileName(
                this, QStringLiteral("导入词群 CSV"), QDir::homePath(),
                QStringLiteral("CSV Files (*.csv);;All Files (*)"));
            if (sourcePath.isEmpty()) {
                return;
            }
            defaultName = QFileInfo(sourcePath).completeBaseName();
        }

        const QString name = QInputDialog::getText(
            this, QStringLiteral("新增词群词书"), QStringLiteral("词书名称："), QLineEdit::Normal,
            defaultName, &ok);
        if (!ok || name.trimmed().isEmpty()) {
            return;
        }

        int newBookId = -1;
        if (!db_->createPhraseBook(name, &newBookId)) {
            showWarningPrompt(this, QStringLiteral("创建失败"), db_->lastError());
            return;
        }

        if (!sourcePath.isEmpty() && newBookId > 0) {
            int imported = 0;
            bool okImport = false;
            if (selected == importJsonAction) {
                okImport = db_->importPhraseBookFromJson(sourcePath, newBookId, imported);
            } else if (selected == importCsvAction) {
                okImport = db_->importPhraseBookFromCsv(sourcePath, newBookId, imported);
            }
            if (!okImport) {
                showWarningPrompt(this, QStringLiteral("导入失败"), db_->lastError());
            } else {
                showInfoPrompt(this, QStringLiteral("导入完成"), QStringLiteral("成功导入 %1 条词群关联").arg(imported));
            }
        }
        refreshBooks();
    });
    connect(deleteBookButton_, &HoverScaleButton::clicked, this, [this]() {
        if (db_ == nullptr) {
            return;
        }
        const int idx = bookCombo_->currentIndex();
        if (idx < 0) {
            return;
        }
        const int bookId = bookCombo_->itemData(idx).toInt();
        if (bookId <= 0) {
            return;
        }
        const bool confirmed = showQuestionPrompt(this,
                                                  QStringLiteral("删除词群词书"),
                                                  QStringLiteral("确认删除当前词群词书吗？"));
        if (!confirmed) {
            return;
        }
        if (!db_->deletePhraseBook(bookId)) {
            showWarningPrompt(this, QStringLiteral("删除失败"), db_->lastError());
            return;
        }
        refreshBooks();
        reloadSession();
    });
    connect(importJsonButton_, &HoverScaleButton::clicked, this, [this]() {
        if (db_ == nullptr) {
            return;
        }
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("导入词群 JSON"), QDir::homePath(),
            QStringLiteral("JSON Files (*.json *.jsonl);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }
        int imported = 0;
        const int targetBookId = db_->activePhraseBookId();
        if (!db_->importPhraseBookFromJson(path, targetBookId, imported)) {
            showWarningPrompt(this, QStringLiteral("导入失败"), db_->lastError());
            return;
        }
        showInfoPrompt(this, QStringLiteral("导入完成"), QStringLiteral("成功导入 %1 条词群关联").arg(imported));
        refreshBooks();
        reloadSession();
    });
    connect(importCsvButton_, &HoverScaleButton::clicked, this, [this]() {
        if (db_ == nullptr) {
            return;
        }
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("导入词群 CSV"), QDir::homePath(),
            QStringLiteral("CSV Files (*.csv);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }
        int imported = 0;
        const int targetBookId = db_->activePhraseBookId();
        if (!db_->importPhraseBookFromCsv(path, targetBookId, imported)) {
            showWarningPrompt(this, QStringLiteral("导入失败"), db_->lastError());
            return;
        }
        showInfoPrompt(this, QStringLiteral("导入完成"), QStringLiteral("成功导入 %1 条词群关联").arg(imported));
        refreshBooks();
        reloadSession();
    });

    const auto goNext = [this]() {
        if (!currentAnswered_) {
            feedbackLabel_->setText(QStringLiteral("请先回车提交当前题目。"));
            feedbackLabel_->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600; color: #b45309;"));
            return;
        }
        ++currentIndex_;
        if (currentIndex_ >= currentBatch_.size()) {
            showInfoPrompt(this,
                           QStringLiteral("本组完成"),
                           QStringLiteral("共 %1 题，正确 %2，错误 %3")
                               .arg(currentBatch_.size())
                               .arg(correctCount_)
                               .arg(wrongCount_));
            reloadSession();
            return;
        }
        showCurrentPhrase();
    };

    const auto submitCurrent = [this]() {
        if (db_ == nullptr || currentIndex_ < 0 || currentIndex_ >= currentBatch_.size()) {
            return;
        }
        if (currentAnswered_) {
            return;
        }
        const QString userInput = answerEdit_->text().trimmed();
        if (userInput.isEmpty()) {
            feedbackLabel_->setText(QStringLiteral("请输入答案后再提交。"));
            feedbackLabel_->setStyleSheet(QStringLiteral(
                "font-size: 14px; font-weight: 600; color: #b45309;"));
            return;
        }
        const PhraseItem item = currentBatch_.at(currentIndex_);
        const QString matched = tryMatchAnswer(item, userInput);
        const bool correct = !matched.isEmpty();
        if (!db_->applyPhraseReviewResult(item.id, correct, false, QDateTime::currentDateTime())) {
            showWarningPrompt(this, QStringLiteral("保存失败"), db_->lastError());
            return;
        }
        PhraseLearningEvent event;
        event.eventTime = QDateTime::currentDateTime();
        event.phraseId = item.id;
        event.mode = reviewMode_ ? QStringLiteral("review") : QStringLiteral("learning");
        event.correct = correct;
        event.skipped = false;
        event.userInput = userInput;
        event.matchedAnswer = matched;
        db_->recordPhraseLearningEvent(event);

        if (correct) {
            ++correctCount_;
            feedbackLabel_->setText(
                QStringLiteral("回答正确\n标准表达：%1").arg(item.keywordsEn.join(QStringLiteral(" / "))));
            feedbackLabel_->setStyleSheet(QStringLiteral(
                "font-size: 14px; font-weight: 700; color: #16a34a;"));
        } else {
            ++wrongCount_;
            feedbackLabel_->setText(
                QStringLiteral("回答不正确\n标准表达：%1").arg(item.keywordsEn.join(QStringLiteral(" / "))));
            feedbackLabel_->setStyleSheet(QStringLiteral(
                "font-size: 14px; font-weight: 700; color: #ef4444;"));
        }
        currentAnswered_ = true;
        nextButton_->setEnabled(true);
    };
    connect(submitButton_, &HoverScaleButton::clicked, this, submitCurrent);
    connect(nextButton_, &HoverScaleButton::clicked, this, goNext);
    connect(answerEdit_, &QLineEdit::returnPressed, this, [this, submitCurrent, goNext]() {
        if (currentAnswered_) {
            goNext();
            return;
        }
        submitCurrent();
    });

    connect(skipButton_, &HoverScaleButton::clicked, this, [this]() {
        if (db_ == nullptr || currentIndex_ < 0 || currentIndex_ >= currentBatch_.size()) {
            return;
        }
        if (currentAnswered_) {
            return;
        }
        const PhraseItem item = currentBatch_.at(currentIndex_);
        if (!db_->applyPhraseReviewResult(item.id, false, true, QDateTime::currentDateTime())) {
            showWarningPrompt(this, QStringLiteral("保存失败"), db_->lastError());
            return;
        }
        PhraseLearningEvent event;
        event.eventTime = QDateTime::currentDateTime();
        event.phraseId = item.id;
        event.mode = reviewMode_ ? QStringLiteral("review") : QStringLiteral("learning");
        event.correct = false;
        event.skipped = true;
        event.userInput = answerEdit_->text().trimmed();
        event.matchedAnswer = QString();
        db_->recordPhraseLearningEvent(event);
        ++wrongCount_;
        feedbackLabel_->setText(
            QStringLiteral("已跳过\n标准表达：%1").arg(item.keywordsEn.join(QStringLiteral(" / "))));
        feedbackLabel_->setStyleSheet(QStringLiteral(
            "font-size: 14px; font-weight: 700; color: #b45309;"));
        currentAnswered_ = true;
        nextButton_->setEnabled(true);
    });

    sessionSize_ = 5;
    nextButton_->setEnabled(false);
    refreshBooks();
    reloadSession();
    setManagementOnlyView(false);
}

void PhraseClusterPageWidget::setDatabaseManager(DatabaseManager *db) {
    db_ = db;
    refreshBooks();
    reloadSession();
}

void PhraseClusterPageWidget::openMode(bool reviewMode) {
    setManagementOnlyView(false);
    reviewMode_ = reviewMode;
    if (modeTabs_ != nullptr) {
        const QSignalBlocker blocker(modeTabs_);
        modeTabs_->setCurrentIndex(reviewMode ? 1 : 0);
    }
    if (managementPanel_ != nullptr) {
        managementPanel_->setVisible(false);
    }
    reloadSession();
}

void PhraseClusterPageWidget::openManagementPanel() {
    setManagementOnlyView(true);
    refreshBooks();
}

void PhraseClusterPageWidget::setManagementOnlyView(bool managementOnly) {
    managementOnlyView_ = managementOnly;
    if (titleLabel_ != nullptr) {
        titleLabel_->setText(managementOnly ? QStringLiteral("词群词书管理") : QStringLiteral("词群翻译训练"));
    }
    if (titleMetaLabel_ != nullptr) {
        titleMetaLabel_->setVisible(managementOnly);
    }
    Q_UNUSED(modeTabs_);
    Q_UNUSED(sessionSizeCombo_);
    Q_UNUSED(manageButton_);
    if (managementPanel_ != nullptr) {
        managementPanel_->setVisible(false);
    }
    refreshManagementButtonsLayout();
    if (controlPanel_ != nullptr) {
        controlPanel_->setVisible(false);
    }
    if (trainPanel_ != nullptr) {
        trainPanel_->setVisible(!managementOnly);
    }
    if (bookManageView_ != nullptr) {
        bookManageView_->setVisible(managementOnly);
    }
}

void PhraseClusterPageWidget::refreshManagementButtonsLayout() {
    if (managementButtonsLayout_ == nullptr || managementPanel_ == nullptr) {
        return;
    }
    while (QLayoutItem *item = managementButtonsLayout_->takeAt(0)) {
        delete item;
    }
    if (addBookButton_ == nullptr || deleteBookButton_ == nullptr || importJsonButton_ == nullptr || importCsvButton_ == nullptr) {
        return;
    }

    addBookButton_->setVisible(!managementOnlyView_);
    deleteBookButton_->setVisible(!managementOnlyView_);

    if (managementOnlyView_) {
        importJsonButton_->setMinimumWidth(180);
        importCsvButton_->setMinimumWidth(180);
        importJsonButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        importCsvButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        managementButtonsLayout_->addWidget(importJsonButton_, 1);
        managementButtonsLayout_->addSpacing(12);
        managementButtonsLayout_->addWidget(importCsvButton_, 1);
    } else {
        importJsonButton_->setMinimumWidth(104);
        importCsvButton_->setMinimumWidth(104);
        addBookButton_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        deleteBookButton_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        importJsonButton_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        importCsvButton_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        managementButtonsLayout_->addWidget(addBookButton_);
        managementButtonsLayout_->addWidget(deleteBookButton_);
        managementButtonsLayout_->addStretch(1);
        managementButtonsLayout_->addWidget(importJsonButton_);
        managementButtonsLayout_->addWidget(importCsvButton_);
    }
    managementPanel_->updateGeometry();
}

void PhraseClusterPageWidget::refreshBooks() {
    if (db_ == nullptr || bookCombo_ == nullptr) {
        return;
    }
    const QVector<PhraseBookItem> books = db_->fetchPhraseBooks();
    const int activeBookId = db_->activePhraseBookId();
    rebuildPhraseBookManagement(books, activeBookId);
    {
        const QSignalBlocker blocker(bookCombo_);
        bookCombo_->clear();
        int currentIndex = -1;
        for (const PhraseBookItem &book : books) {
            const QString text = QStringLiteral("%1（%2）").arg(book.name).arg(book.itemCount);
            bookCombo_->addItem(text, book.id);
            if (book.id == activeBookId) {
                currentIndex = bookCombo_->count() - 1;
            }
        }
        if (currentIndex >= 0) {
            bookCombo_->setCurrentIndex(currentIndex);
        }
    }
    const bool hasBooks = bookCombo_->count() > 0;
    deleteBookButton_->setEnabled(hasBooks);
    importJsonButton_->setEnabled(true);
    importCsvButton_->setEnabled(true);
}

void PhraseClusterPageWidget::rebuildPhraseBookManagement(const QVector<PhraseBookItem> &books, int activeBookId) {
    if (manageCurrentLayout_ == nullptr || manageOtherList_ == nullptr) {
        return;
    }
    int totalItems = 0;
    for (const PhraseBookItem &book : books) {
        totalItems += book.itemCount;
    }
    if (titleMetaLabel_ != nullptr) {
        titleMetaLabel_->setText(QStringLiteral("共 %1 本词群词书 · %2 词群").arg(books.size()).arg(totalItems));
    }

    while (QLayoutItem *item = manageCurrentLayout_->takeAt(0)) {
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    manageOtherList_->clear();

    const auto createBookRow = [this](QWidget *parent, const PhraseBookItem &book, bool current) -> QWidget * {
        auto *row = new QWidget(parent);
        row->setObjectName(QStringLiteral("phraseBookRow"));
        row->setStyleSheet(QStringLiteral(
            "QWidget#phraseBookRow {"
            "  background: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 18px;"
            "}").arg(current ? QStringLiteral("#f8fafc") : QStringLiteral("#ffffff"),
                     current ? QStringLiteral("#dbe4ef") : QStringLiteral("#e9eef5")));

        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(12);

        const QColor baseColor(coverColorForBook(book.id));
        const QString coverTop = baseColor.lighter(106).name();
        const QString coverBottom = baseColor.darker(102).name();
        auto *cover = new QLabel(coverTextForBook(book.name), row);
        cover->setAlignment(Qt::AlignCenter);
        cover->setFixedSize(68, 84);
        cover->setWordWrap(true);
        cover->setStyleSheet(QStringLiteral(
            "font-size: 12px; line-height: 1.1; font-weight: 700; color: rgba(255,255,255,0.95);"
            "border-radius: 14px;"
            "background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %1, stop:1 %2);")
                                 .arg(coverTop, coverBottom));
        layout->addWidget(cover);

        auto *title = new QLabel(book.name, row);
        title->setStyleSheet(QStringLiteral(
            "font-size: 20px; font-weight: 700; color: #0f172a; background: transparent; border: none;"));
        title->setWordWrap(true);
        auto *count = new QLabel(QStringLiteral("%1 词群").arg(book.itemCount), row);
        count->setStyleSheet(QStringLiteral("font-size: 15px; color: #94a3b8; background: transparent; border: none;"));
        auto *status = new QLabel(current ? QStringLiteral("[学习中]") : QStringLiteral("[未学习]"), row);
        status->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 700; color: #475569;"));

        auto *textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(5);
        textLayout->addWidget(title);
        textLayout->addWidget(count);
        textLayout->addWidget(status);
        textLayout->addStretch();
        layout->addLayout(textLayout, 1);

        auto *rightLayout = new QVBoxLayout();
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(6);
        rightLayout->setAlignment(Qt::AlignTop | Qt::AlignRight);

        if (!current) {
            auto *activateBtn = new HoverScaleButton(QStringLiteral("绑定"), row);
            activateBtn->setFixedSize(88, 34);
            activateBtn->setStyleSheet(QStringLiteral(
                "HoverScaleButton {"
                "  font-size: 13px; font-weight: 700; border-radius: 17px;"
                "  padding: 0 12px; border: 1px solid rgba(15,23,42,0.14);"
                "  background: #f8fafc; color: #0f172a;"
                "}"
                "HoverScaleButton:hover { background: #eef2f7; }"));
            connect(activateBtn, &HoverScaleButton::clicked, this, [this, book]() {
                if (db_ == nullptr) {
                    return;
                }
                if (!db_->setActivePhraseBook(book.id)) {
                    showWarningPrompt(this, QStringLiteral("切换失败"), db_->lastError());
                    return;
                }
                refreshBooks();
                reloadSession();
            });
            rightLayout->addWidget(activateBtn, 0, Qt::AlignRight);
        }

        auto *deleteBtn = new HoverScaleButton(QStringLiteral("🗑"), row);
        deleteBtn->setObjectName(QStringLiteral("phraseBookDeleteEmojiButton"));
        deleteBtn->setFixedSize(26, 26);
        deleteBtn->setToolTip(QStringLiteral("删除词书"));
        deleteBtn->setStyleSheet(QStringLiteral(
            "#phraseBookDeleteEmojiButton {"
            "  border: none; border-radius: 8px; padding: 0;"
            "  background: transparent; font-size: 12px; color: #64748b;"
            "}"
            "#phraseBookDeleteEmojiButton:hover { background: rgba(148,163,184,0.12); color: #475569; }"));
        connect(deleteBtn, &HoverScaleButton::clicked, this, [this, book]() {
            if (db_ == nullptr) {
                return;
            }
            const bool confirmed = showQuestionPrompt(
                this, QStringLiteral("删除词群词书"), QStringLiteral("确认删除“%1”吗？").arg(book.name));
            if (!confirmed) {
                return;
            }
            if (!db_->deletePhraseBook(book.id)) {
                showWarningPrompt(this, QStringLiteral("删除失败"), db_->lastError());
                return;
            }
            refreshBooks();
            reloadSession();
        });
        rightLayout->addWidget(deleteBtn, 0, Qt::AlignRight);

        layout->addLayout(rightLayout);
        return row;
    };

    bool hasCurrent = false;
    for (const PhraseBookItem &book : books) {
        if (book.id == activeBookId || book.isActive) {
            manageCurrentLayout_->addWidget(createBookRow(manageCurrentHost_, book, true));
            hasCurrent = true;
        }
    }
    if (!hasCurrent) {
        auto *emptyCurrent = new QWidget(manageCurrentHost_);
        emptyCurrent->setStyleSheet(QStringLiteral(
            "background: #f8fafc; border: 1px dashed #d8e1ec; border-radius: 18px;"));
        auto *emptyCurrentLayout = new QVBoxLayout(emptyCurrent);
        emptyCurrentLayout->setContentsMargins(16, 12, 16, 12);
        auto *emptyTitle = new QLabel(QStringLiteral("暂无当前绑定词群词书"), emptyCurrent);
        emptyTitle->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: #334155;"));
        auto *emptyDesc = new QLabel(QStringLiteral("请在下方选择词群词书并绑定到学习"), emptyCurrent);
        emptyDesc->setStyleSheet(QStringLiteral("font-size: 14px; color: #64748b;"));
        emptyCurrentLayout->addWidget(emptyTitle);
        emptyCurrentLayout->addWidget(emptyDesc);
        manageCurrentLayout_->addWidget(emptyCurrent);
    }
    manageCurrentLayout_->addStretch(1);

    int otherCount = 0;
    for (const PhraseBookItem &book : books) {
        if (book.id == activeBookId || book.isActive) {
            continue;
        }
        auto *itemWidget = createBookRow(manageOtherList_, book, false);
        auto *item = new QListWidgetItem(manageOtherList_);
        item->setSizeHint(itemWidget->sizeHint());
        manageOtherList_->addItem(item);
        manageOtherList_->setItemWidget(item, itemWidget);
        ++otherCount;
    }
    if (manageOtherTitle_ != nullptr) {
        manageOtherTitle_->setText(QStringLiteral("其他词群词书"));
    }
    if (otherCount <= 0) {
        auto *emptyWidget = new QWidget(manageOtherList_);
        emptyWidget->setStyleSheet(QStringLiteral(
            "background: #f8fafc; border: 1px dashed #d8e1ec; border-radius: 18px;"));
        auto *emptyLayout = new QVBoxLayout(emptyWidget);
        emptyLayout->setContentsMargins(16, 12, 16, 12);
        auto *emptyTitle = new QLabel(QStringLiteral("无其他词群词书"), emptyWidget);
        emptyTitle->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: #334155;"));
        auto *emptyDesc = new QLabel(QStringLiteral("点击底部“添加词群词书”创建或导入"), emptyWidget);
        emptyDesc->setStyleSheet(QStringLiteral("font-size: 14px; color: #64748b;"));
        emptyLayout->addWidget(emptyTitle);
        emptyLayout->addWidget(emptyDesc);
        auto *item = new QListWidgetItem(manageOtherList_);
        item->setSizeHint(emptyWidget->sizeHint());
        manageOtherList_->addItem(item);
        manageOtherList_->setItemWidget(item, emptyWidget);
    }
}

void PhraseClusterPageWidget::reloadSession() {
    currentBatch_.clear();
    currentIndex_ = -1;
    currentAnswered_ = false;
    correctCount_ = 0;
    wrongCount_ = 0;
    nextButton_->setEnabled(false);
    feedbackLabel_->setText(QStringLiteral("请选择模式并开始训练。"));
    feedbackLabel_->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600; color: #6b7280;"));
    clusterLabel_->setStyleSheet(QStringLiteral("font-size: 34px; font-weight: 700; color: #111827;"));

    if (db_ == nullptr) {
        progressLabel_->setText(QStringLiteral("数据库未连接"));
        clusterLabel_->setText(QStringLiteral("暂无词群"));
        metaLabel_->setText(QStringLiteral("来源：—"));
        exampleLabel_->setText(QStringLiteral("—"));
        answerEdit_->clear();
        answerEdit_->setEnabled(false);
        submitButton_->setEnabled(false);
        skipButton_->setEnabled(false);
        return;
    }

    if (sessionSize_ <= 0) {
        sessionSize_ = 5;
    }
    if (reviewMode_) {
        currentBatch_ = db_->fetchPhraseReviewBatch(QDateTime::currentDateTime(), sessionSize_);
    } else {
        currentBatch_ = db_->fetchPhraseLearningBatch(sessionSize_);
    }

    if (currentBatch_.isEmpty()) {
        progressLabel_->setText(reviewMode_ ? QStringLiteral("暂无复习任务") : QStringLiteral("暂无学习任务"));
        clusterLabel_->setText(reviewMode_
                                   ? QStringLiteral("当前没有到期词群")
                                   : QStringLiteral("当前词群词书暂无新词群"));
        metaLabel_->setText(QStringLiteral("请先导入词群数据或切换词群词书。"));
        exampleLabel_->setText(QStringLiteral("导入 JSON/CSV 后可在此开始训练。"));
        answerEdit_->clear();
        answerEdit_->setEnabled(false);
        submitButton_->setEnabled(false);
        skipButton_->setEnabled(false);
        return;
    }

    currentIndex_ = 0;
    answerEdit_->setEnabled(true);
    submitButton_->setEnabled(true);
    skipButton_->setEnabled(true);
    showCurrentPhrase();
}

void PhraseClusterPageWidget::showCurrentPhrase() {
    if (currentIndex_ < 0 || currentIndex_ >= currentBatch_.size()) {
        return;
    }
    currentAnswered_ = false;
    nextButton_->setEnabled(false);
    answerEdit_->clear();
    answerEdit_->setFocus();
    clusterLabel_->setStyleSheet(QStringLiteral("font-size: 45px; font-weight: 700; color: #111827;"));
    feedbackLabel_->setText(QStringLiteral("请输入英文表达并提交。"));
    feedbackLabel_->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600; color: #6b7280;"));

    const PhraseItem item = currentBatch_.at(currentIndex_);
    progressLabel_->setText(
        QStringLiteral("%1模式 · %2 / %3")
            .arg(reviewMode_ ? QStringLiteral("复习") : QStringLiteral("学习"))
            .arg(currentIndex_ + 1, 2, 10, QLatin1Char('0'))
            .arg(currentBatch_.size()));
    clusterLabel_->setText(item.clusterZh);
    const QString source = item.examLabels.isEmpty()
                               ? QStringLiteral("来源：未标注套题")
                               : QStringLiteral("来源：%1").arg(item.examLabels.join(QStringLiteral(" · ")));
    metaLabel_->setText(source);
    if (!item.examplesCn.isEmpty()) {
        exampleLabel_->setText(QStringLiteral("例句：%1").arg(item.examplesCn.first()));
    } else {
        exampleLabel_->setText(QStringLiteral("例句：—"));
    }
}

QString PhraseClusterPageWidget::normalizedAnswer(const QString &text) const {
    return text.trimmed().toLower().simplified();
}

QString PhraseClusterPageWidget::tryMatchAnswer(const PhraseItem &item, const QString &input) const {
    const QString normalizedInput = normalizedAnswer(input);
    if (normalizedInput.isEmpty()) {
        return QString();
    }
    for (const QString &answer : item.keywordsEn) {
        if (normalizedAnswer(answer) == normalizedInput) {
            return answer;
        }
    }
    return QString();
}

CalendarPageWidget::CalendarPageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 14);
    root->setSpacing(8);

    auto *backRow = new QHBoxLayout();
    backRow->setContentsMargins(0, 0, 0, 0);
    backButton_ = new HoverScaleButton(QStringLiteral("返回"), this);
    backButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton { background: transparent; color: #4b5563; font-size: 18px; font-weight: 700; padding: 8px 6px; }"
        "HoverScaleButton:hover { color: #111827; }"));
    backRow->addWidget(backButton_, 0, Qt::AlignLeft);
    backRow->addStretch(1);
    root->addLayout(backRow);

    auto *centerRow = new QHBoxLayout();
    centerRow->setContentsMargins(0, 0, 0, 0);
    centerRow->setSpacing(0);

    auto *centerHost = new QWidget(this);
    centerHost->setMinimumWidth(300);
    auto *centerHostLayout = new QHBoxLayout(centerHost);
    centerHostLayout->setContentsMargins(0, 0, 0, 0);
    centerHostLayout->setSpacing(16);

    prevButton_ = new HoverScaleButton(QStringLiteral("<"), this);
    nextButton_ = new HoverScaleButton(QStringLiteral(">"), this);
    prevButton_->setFixedSize(34, 34);
    nextButton_->setFixedSize(34, 34);
    prevButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton { background: #f3f4f6; border-radius: 17px; color: #6b7280; font-size: 20px; font-weight: 700; }"
        "HoverScaleButton:hover { color: #111827; background: #eceff3; }"));
    nextButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton { background: #f3f4f6; border-radius: 17px; color: #6b7280; font-size: 20px; font-weight: 700; }"
        "HoverScaleButton:hover { color: #111827; background: #eceff3; }"));
    titleLabel_ = new QLabel(this);
    titleLabel_->setAlignment(Qt::AlignCenter);
    titleLabel_->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: #111827;"));

    centerHostLayout->addStretch(1);
    centerHostLayout->addWidget(prevButton_, 0, Qt::AlignCenter);
    centerHostLayout->addWidget(titleLabel_, 0, Qt::AlignCenter);
    centerHostLayout->addWidget(nextButton_, 0, Qt::AlignCenter);
    centerHostLayout->addStretch(1);

    centerRow->addWidget(centerHost, 1, Qt::AlignHCenter);
    root->addLayout(centerRow);

    auto *divider = new QFrame(this);
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background: #e5e7eb; border: none;"));
    root->addWidget(divider);

    calendarPanel_ = new QWidget(this);
    calendarPanel_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *calendarPanelLayout = new QVBoxLayout(calendarPanel_);
    calendarPanelLayout->setContentsMargins(0, 0, 0, 0);
    calendarPanelLayout->setSpacing(8);

    auto *weekRow = new QGridLayout();
    weekRow->setContentsMargins(0, 0, 0, 0);
    weekRow->setHorizontalSpacing(0);
    weekRow->setVerticalSpacing(0);
    const QStringList weekNames = {QStringLiteral("MON"), QStringLiteral("TUE"), QStringLiteral("WED"),
                                   QStringLiteral("THU"), QStringLiteral("FRI"), QStringLiteral("SAT"), QStringLiteral("SUN")};
    for (int i = 0; i < weekNames.size(); ++i) {
        auto *label = new QLabel(weekNames.at(i), calendarPanel_);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("font-size: 12px; color: #9ca3af; font-weight: 700; letter-spacing: 1px;"));
        weekRow->addWidget(label, 0, i);
    }
    calendarPanelLayout->addLayout(weekRow);

    calendarGrid_ = new QGridLayout();
    calendarGrid_->setContentsMargins(0, 0, 0, 0);
    calendarGrid_->setHorizontalSpacing(6);
    calendarGrid_->setVerticalSpacing(4);

    cellDates_.resize(42);
    for (int i = 0; i < 42; ++i) {
        auto *btn = new CalendarCellButton(calendarPanel_);
        dayButtons_.push_back(btn);
        const int row = i / 7;
        const int col = i % 7;
        calendarGrid_->addWidget(btn, row, col, Qt::AlignCenter);
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            if (i < 0 || i >= cellDates_.size()) {
                return;
            }
            const QDate date = cellDates_.at(i);
            if (!date.isValid() || date > QDate::currentDate()) {
                return;
            }
            selectedDate_ = date;
            setMonth(currentMonth_, studyMinutesByDate_);
            emit daySelected(date);
        });
    }
    calendarPanelLayout->addLayout(calendarGrid_, 1);
    root->addWidget(calendarPanel_, 0, Qt::AlignHCenter);

    drawerFrame_ = new QFrame(this);
    drawerFrame_->setObjectName(QStringLiteral("calendarDrawerFrame"));
    drawerFrame_->setStyleSheet(QStringLiteral(
        "QFrame#calendarDrawerFrame { background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 16px; }"));
    auto *drawerLayout = new QVBoxLayout(drawerFrame_);
    drawerLayout->setContentsMargins(12, 10, 12, 10);
    drawerLayout->setSpacing(8);

    auto *metaTopRow = new QHBoxLayout();
    metaTopRow->setContentsMargins(0, 0, 0, 0);
    metaTopRow->setSpacing(0);
    selectedDateLabel_ = new QLabel(QStringLiteral("请选择日期"), drawerFrame_);
    selectedDateLabel_->setVisible(false);
    eventCountLabel_ = new QLabel(QStringLiteral("0 次作答"), drawerFrame_);
    eventCountLabel_->setVisible(false);

    trainingFilterTabs_ = new QTabBar(drawerFrame_);
    trainingFilterTabs_->setDocumentMode(true);
    trainingFilterTabs_->setDrawBase(false);
    trainingFilterTabs_->setExpanding(true);
    trainingFilterTabs_->setElideMode(Qt::ElideNone);
    trainingFilterTabs_->setUsesScrollButtons(false);
    trainingFilterTabs_->setFixedHeight(42);
    trainingFilterTabs_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    trainingFilterTabs_->setStyleSheet(QStringLiteral(
        "QTabBar::tab {"
        "  border: 1px solid #cbd5e1;"
        "  border-right: none;"
        "  padding: 8px 0px;"
        "  min-width: 96px;"
        "  margin: 0px;"
        "  background: #f3f6fa;"
        "  color: #64748b;"
        "  font-size: 16px;"
        "  font-weight: 600;"
        "}"
        "QTabBar::tab:first {"
        "  border-top-left-radius: 11px;"
        "  border-bottom-left-radius: 11px;"
        "}"
        "QTabBar::tab:last {"
        "  border-right: 1px solid #cbd5e1;"
        "  border-top-right-radius: 11px;"
        "  border-bottom-right-radius: 11px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #ffffff;"
        "  border-color: #94a3b8;"
        "  color: #0f172a;"
        "  font-weight: 700;"
        "}"
        "QTabBar::tab:hover:!selected {"
        "  background: #eef2f7;"
        "  border-color: #a8b3c3;"
        "  color: #334155;"
        "}"));
    const int tabAll = trainingFilterTabs_->addTab(QStringLiteral("全部"));
    const int tabSpelling = trainingFilterTabs_->addTab(QStringLiteral("拼写"));
    const int tabCountability = trainingFilterTabs_->addTab(QStringLiteral("可数性"));
    const int tabPolysemy = trainingFilterTabs_->addTab(QStringLiteral("熟词生义"));
    trainingFilterTabs_->setTabData(tabAll, QStringLiteral("all"));
    trainingFilterTabs_->setTabData(tabSpelling, QStringLiteral("spelling"));
    trainingFilterTabs_->setTabData(tabCountability, QStringLiteral("countability"));
    trainingFilterTabs_->setTabData(tabPolysemy, QStringLiteral("polysemy"));
    trainingFilterTabs_->setCurrentIndex(tabAll);
    metaTopRow->addWidget(trainingFilterTabs_, 1);

    dailyList_ = new QListWidget(drawerFrame_);
    dailyList_->setStyleSheet(QStringLiteral(
        "QListWidget { background: #ffffff; border: 1px solid #e2e8f0; border-radius: 12px; }"
        "QListWidget::item { padding: 10px 8px; border-bottom: 1px solid #f1f5f9; }"
        "QListWidget::item:selected { background: #eef2ff; color: #1f2937; }"));
    emptyLabel_ = new QLabel(QStringLiteral("该日暂无单词明细"), drawerFrame_);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setStyleSheet(QStringLiteral("font-size: 13px; color: #64748b; padding: 12px;"));

    drawerLayout->addLayout(metaTopRow);
    drawerLayout->addWidget(dailyList_, 1);
    drawerLayout->addWidget(emptyLabel_);
    root->addWidget(drawerFrame_, 1);

    connect(backButton_, &HoverScaleButton::clicked, this, &CalendarPageWidget::backClicked);
    connect(prevButton_, &HoverScaleButton::clicked, this, [this]() {
        const QDate next = currentMonth_.isValid() ? currentMonth_.addMonths(-1) : QDate::currentDate();
        emit monthChanged(QDate(next.year(), next.month(), 1));
    });
    connect(nextButton_, &HoverScaleButton::clicked, this, [this]() {
        const QDate next = currentMonth_.isValid() ? currentMonth_.addMonths(1) : QDate::currentDate();
        emit monthChanged(QDate(next.year(), next.month(), 1));
    });
    connect(trainingFilterTabs_, &QTabBar::currentChanged, this, [this](int) {
        emit trainingFilterChanged(selectedTrainingFilter());
    });
    connect(dailyList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item == nullptr) {
            return;
        }
        bool ok = false;
        const int wordId = item->data(Qt::UserRole).toInt(&ok);
        if (ok && wordId > 0) {
            emit wordDetailRequested(wordId);
        }
    });

    updateCalendarGeometry();
}

void CalendarPageWidget::setMonth(const QDate &monthAnchor, const QHash<QDate, int> &studyMinutesByDate) {
    currentMonth_ = QDate(monthAnchor.year(), monthAnchor.month(), 1);
    studyMinutesByDate_ = studyMinutesByDate;
    titleLabel_->setText(currentMonth_.toString(QStringLiteral("yyyy年MM月")));

    const QDate gridStart = currentMonth_.addDays(-(currentMonth_.dayOfWeek() - 1));
    const QDate today = QDate::currentDate();
    for (int i = 0; i < 42 && i < dayButtons_.size(); ++i) {
        const QDate d = gridStart.addDays(i);
        cellDates_[i] = d;
        const bool inCurrentMonth = (d.month() == currentMonth_.month() && d.year() == currentMonth_.year());
        const bool isFuture = d > today;
        const int minutes = studyMinutesByDate_.value(d, 0);
        dayButtons_[i]->setCellData(d, inCurrentMonth, isFuture, minutes, d == selectedDate_);
    }
}

void CalendarPageWidget::setDailySummaries(const QDate &date,
                                           int totalEvents,
                                           const QVector<DailyWordSummary> &summaries,
                                           const QDate &eventStartDate) {
    selectedDate_ = date;
    eventStartDate_ = eventStartDate;
    selectedDateLabel_->setText(date.isValid()
                                    ? date.toString(QStringLiteral("yyyy-MM-dd"))
                                    : QStringLiteral("请选择日期"));
    eventCountLabel_->setText(QStringLiteral("共 %1 次作答").arg(totalEvents));

    auto resultText = [](SpellingResult result) -> QString {
        switch (result) {
        case SpellingResult::Mastered: return QStringLiteral("熟悉");
        case SpellingResult::Blurry: return QStringLiteral("模糊");
        case SpellingResult::Unfamiliar: default: return QStringLiteral("不熟悉");
        }
    };
    auto trainingTypeText = [](const QString &trainingType) -> QString {
        const QString type = trainingType.trimmed().toLower();
        if (type == QStringLiteral("spelling")) {
            return QStringLiteral("拼写");
        }
        if (type == QStringLiteral("countability")) {
            return QStringLiteral("可数性");
        }
        if (type == QStringLiteral("polysemy")) {
            return QStringLiteral("熟词生义");
        }
        return trainingType;
    };

    dailyList_->clear();
    for (const DailyWordSummary &summary : summaries) {
        QStringList parts;
        parts << summary.word
              << QStringLiteral("次数 %1").arg(summary.attempts)
              << QStringLiteral("类型 %1").arg(trainingTypeText(summary.trainingType))
              << QStringLiteral("最后 %1").arg(resultText(summary.lastResult));
        if (summary.lastTime.isValid()) {
            parts << summary.lastTime.toString(QStringLiteral("HH:mm"));
        }
        auto *item = new QListWidgetItem(parts.join(QStringLiteral("  ·  ")), dailyList_);
        item->setData(Qt::UserRole, summary.wordId);
    }

    const bool hasData = !summaries.isEmpty();
    dailyList_->setVisible(hasData);
    emptyLabel_->setVisible(!hasData);
    if (!hasData) {
        QString text = QStringLiteral("该日暂无单词明细");
        if (eventStartDate_.isValid()) {
            text += QStringLiteral("（明细记录自 %1 起）").arg(eventStartDate_.toString(QStringLiteral("yyyy-MM-dd")));
        }
        emptyLabel_->setText(text);
    }

    setMonth(currentMonth_, studyMinutesByDate_);
}

QDate CalendarPageWidget::currentMonth() const {
    return currentMonth_;
}

QString CalendarPageWidget::selectedTrainingFilter() const {
    if (trainingFilterTabs_ == nullptr) {
        return QStringLiteral("all");
    }
    const int index = trainingFilterTabs_->currentIndex();
    if (index < 0) {
        return QStringLiteral("all");
    }
    const QString data = trainingFilterTabs_->tabData(index).toString().trimmed().toLower();
    return data.isEmpty() ? QStringLiteral("all") : data;
}

QDate CalendarPageWidget::selectedDate() const {
    return selectedDate_;
}

void CalendarPageWidget::setTrainingFilter(const QString &trainingType) {
    if (trainingFilterTabs_ == nullptr) {
        return;
    }
    const QString target = trainingType.trimmed().toLower();
    const QString desired = target.isEmpty() ? QStringLiteral("all") : target;
    for (int i = 0; i < trainingFilterTabs_->count(); ++i) {
        if (trainingFilterTabs_->tabData(i).toString().trimmed().toLower() == desired) {
            const QSignalBlocker blocker(trainingFilterTabs_);
            trainingFilterTabs_->setCurrentIndex(i);
            return;
        }
    }
    const QSignalBlocker blocker(trainingFilterTabs_);
    trainingFilterTabs_->setCurrentIndex(0);
}

void CalendarPageWidget::syncLayoutForAnimation() {
    if (layout() != nullptr) {
        layout()->activate();
    }
    updateCalendarGeometry();
}

void CalendarPageWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateCalendarGeometry();
}

void CalendarPageWidget::updateCalendarGeometry() {
    if (calendarPanel_ == nullptr) {
        return;
    }

    const int targetPanelHeight = qBound(160, qRound(height() * 0.25), 280);
    const int maxAllowedWidth = qMax(220, width() - 32);
    const int targetPanelWidth = qMin(maxAllowedWidth, qMax(220, qRound(targetPanelHeight * 1.22)));
    calendarPanel_->setFixedSize(targetPanelWidth, targetPanelHeight);

    const int byWidth = (targetPanelWidth - (calendarGrid_ != nullptr ? calendarGrid_->horizontalSpacing() * 6 : 36)) / 7;
    const int dayAreaHeight = qMax(110, targetPanelHeight - 32);
    const int byHeight = (dayAreaHeight - (calendarGrid_ != nullptr ? calendarGrid_->verticalSpacing() * 5 : 20)) / 6;
    const int diameter = qBound(24, qMin(byWidth, byHeight), 40);
    for (CalendarCellButton *button : dayButtons_) {
        if (button != nullptr) {
            button->setDiameter(diameter);
        }
    }
}

WordDetailPageWidget::WordDetailPageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(8);

    auto *header = new QHBoxLayout();
    backButton_ = new HoverScaleButton(QStringLiteral("返回"), this);
    backButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton { background: transparent; color: #4b5563; font-size: 16px; padding: 8px; }"
        "HoverScaleButton:hover { color: #111827; }"));
    titleLabel_ = new QLabel(QStringLiteral("单词详情"), this);
    titleLabel_->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 700; color: #0f172a;"));
    header->addWidget(backButton_);
    header->addStretch(1);
    header->addWidget(titleLabel_);
    header->addStretch(1);
    root->addLayout(header);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto *host = new QWidget(scrollArea);
    auto *hostLayout = new QVBoxLayout(host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    contentLabel_ = new QTextBrowser(host);
    contentLabel_->setReadOnly(true);
    contentLabel_->setOpenLinks(false);
    contentLabel_->setFrameShape(QFrame::NoFrame);
    contentLabel_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentLabel_->setStyleSheet(QStringLiteral(
        "QTextBrowser {"
        "  background: transparent;"
        "  border: none;"
        "  font-size: 14px;"
        "  color: #1f2937;"
        "}"));
    hostLayout->addWidget(contentLabel_);
    scrollArea->setWidget(host);
    root->addWidget(scrollArea, 1);

    connect(contentLabel_, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.toString() == QStringLiteral("toggle-json")) {
            jsonExpanded_ = !jsonExpanded_;
            if (hasDetailData_) {
                renderDetailHtml();
            }
        }
    });
    connect(backButton_, &HoverScaleButton::clicked, this, &WordDetailPageWidget::backClicked);
}

void WordDetailPageWidget::setLoading(int wordId) {
    hasDetailData_ = false;
    jsonExpanded_ = false;
    Q_UNUSED(wordId);
    titleLabel_->setText(QStringLiteral("单词详情"));
    contentLabel_->setHtml(QStringLiteral(
        "<div style='padding:10px 0;color:#475569;'>正在加载单词详情...</div>"));
}

void WordDetailPageWidget::setError(const QString &message) {
    hasDetailData_ = false;
    jsonExpanded_ = false;
    titleLabel_->setText(QStringLiteral("单词详情"));
    contentLabel_->setHtml(QStringLiteral(
        "<div style='padding:10px 0;color:#374151;'>加载失败：%1</div>").arg(message.toHtmlEscaped()));
}

void WordDetailPageWidget::setDetail(const WordFullDetail &detail) {
    detail_ = detail;
    hasDetailData_ = true;
    jsonExpanded_ = false;
    titleLabel_->setText(QStringLiteral("单词详情"));
    renderDetailHtml();
}

void WordDetailPageWidget::renderDetailHtml() {
    if (!hasDetailData_) {
        return;
    }

    const WordFullDetail &detail = detail_;

    auto resultText = [](SpellingResult result) -> QString {
        switch (result) {
        case SpellingResult::Mastered: return QStringLiteral("熟悉");
        case SpellingResult::Blurry: return QStringLiteral("模糊");
        case SpellingResult::Unfamiliar: default: return QStringLiteral("不熟悉");
        }
    };
    auto trainingTypeText = [](const QString &trainingType) -> QString {
        const QString type = trainingType.trimmed().toLower();
        if (type == QStringLiteral("spelling")) {
            return QStringLiteral("拼写");
        }
        if (type == QStringLiteral("countability")) {
            return QStringLiteral("可数性");
        }
        if (type == QStringLiteral("polysemy")) {
            return QStringLiteral("熟词生义");
        }
        return trainingType;
    };
    auto statusText = [](int status) -> QString {
        if (status == 0) return QStringLiteral("新词");
        if (status == 1) return QStringLiteral("学习中");
        if (status == 2) return QStringLiteral("已掌握");
        return QString::number(status);
    };

    auto esc = [](const QString &value) -> QString {
        return value.toHtmlEscaped();
    };
    auto hasValue = [](const QString &value) -> bool {
        const QString trimmed = value.trimmed();
        return !trimmed.isEmpty() && trimmed.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0;
    };
    auto cleanedValue = [hasValue](const QString &value) -> QString {
        return hasValue(value) ? value.trimmed() : QString();
    };
    auto looksLikeJson = [](const QString &value) -> bool {
        const QString text = value.trimmed();
        if (text.isEmpty()) {
            return false;
        }
        return (text.startsWith('{') && text.endsWith('}'))
            || (text.startsWith('[') && text.endsWith(']'));
    };
    auto prettyDateTime = [](const QDateTime &time) -> QString {
        return time.isValid() ? time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : QString();
    };
    auto renderJsonBlock = [cleanedValue, this](const QString &raw) -> QString {
        const QString text = cleanedValue(raw);
        if (text.isEmpty()) {
            return QString();
        }
        if (!jsonExpanded_) {
            return QStringLiteral(
                "<a class='action' href='toggle-json'>展开结构化 JSON</a>");
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            return QStringLiteral("<a class='action' href='toggle-json'>收起 JSON</a><pre class='json'>%1</pre><div class='muted'>JSON 解析失败：%2</div>")
                .arg(text.toHtmlEscaped(), parseError.errorString().toHtmlEscaped());
        }
        const QString pretty = QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).toHtmlEscaped();
        return QStringLiteral("<a class='action' href='toggle-json'>收起 JSON</a><pre class='json'>%1</pre>").arg(pretty);
    };

    auto kvRow = [esc](const QString &key, const QString &value) -> QString {
        return QStringLiteral("<tr><td class='k'>%1</td><td class='v'>%2</td></tr>")
            .arg(esc(key), esc(value));
    };

    auto renderTrendImage = [](const QVector<double> &values) -> QString {
        if (values.size() < 2) {
            return QString();
        }
        const int width = 700;
        const int height = 188;
        const int left = 18;
        const int right = 16;
        const int top = 14;
        const int bottom = 28;
        const int chartWidth = qMax(10, width - left - right);
        const int chartHeight = qMax(10, height - top - bottom);

        double minValue = values.first();
        double maxValue = values.first();
        for (double value : values) {
            minValue = qMin(minValue, value);
            maxValue = qMax(maxValue, value);
        }
        if (qFuzzyCompare(minValue + 1.0, maxValue + 1.0)) {
            minValue -= 0.2;
            maxValue += 0.2;
        }
        const double padding = (maxValue - minValue) * 0.12;
        minValue -= padding;
        maxValue += padding;
        if (maxValue <= minValue) {
            maxValue = minValue + 1.0;
        }

        QPixmap pixmap(width, height);
        pixmap.fill(Qt::white);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QColor gridColor(QStringLiteral("#e5e7eb"));
        const QColor lineColor(QStringLiteral("#111827"));
        const QPen gridPen(gridColor, 1.0);
        painter.setPen(gridPen);
        for (int i = 0; i < 3; ++i) {
            const qreal y = top + (chartHeight * i) / 2.0;
            painter.drawLine(QPointF(left, y), QPointF(left + chartWidth, y));
        }
        painter.drawLine(QPointF(left, top + chartHeight), QPointF(left + chartWidth, top + chartHeight));

        auto pointAt = [&](int index, double value) -> QPointF {
            const double xRatio = values.size() <= 1 ? 0.0 : static_cast<double>(index) / static_cast<double>(values.size() - 1);
            const double yRatio = (value - minValue) / (maxValue - minValue);
            const double x = left + chartWidth * xRatio;
            const double y = top + chartHeight * (1.0 - yRatio);
            return QPointF(x, y);
        };

        QPainterPath linePath;
        linePath.moveTo(pointAt(0, values.first()));
        for (int i = 1; i < values.size(); ++i) {
            linePath.lineTo(pointAt(i, values.at(i)));
        }
        painter.setPen(QPen(lineColor, 1.6));
        painter.drawPath(linePath);

        painter.setPen(QPen(lineColor, 1.2));
        painter.setBrush(Qt::white);
        for (int i = 0; i < values.size(); ++i) {
            painter.drawEllipse(pointAt(i, values.at(i)), 2.8, 2.8);
        }

        QByteArray pngBytes;
        QBuffer buffer(&pngBytes);
        if (!buffer.open(QIODevice::WriteOnly)) {
            return QString();
        }
        if (!pixmap.save(&buffer, "PNG")) {
            return QString();
        }
        return QStringLiteral("<img class='trend-image' src='data:image/png;base64,%1' alt='训练趋势图' />")
            .arg(QString::fromLatin1(pngBytes.toBase64()));
    };

    QString overviewItems;
    QString detailSections;

    const TrainingProgressDetail *primaryProgress = nullptr;
    for (const TrainingProgressDetail &progress : detail.progressByType) {
        if (progress.trainingType.trimmed().compare(QStringLiteral("spelling"), Qt::CaseInsensitive) == 0) {
            primaryProgress = &progress;
            break;
        }
    }
    if (primaryProgress == nullptr && !detail.progressByType.isEmpty()) {
        primaryProgress = &detail.progressByType.first();
    }

    if (primaryProgress != nullptr) {
        overviewItems += QStringLiteral(
            "<td class='metric-cell'>"
            "<div class='metric-k'>熟练系数（EF） <span class='hint' title='值越高通常表示越容易。答对 +0.1，模糊 -0.05，不熟 -0.2。'>?</span></div>"
            "<div class='metric-v'>%1</div>"
            "</td>")
            .arg(QString::number(primaryProgress->easeFactor, 'f', 2).toHtmlEscaped());
        overviewItems += QStringLiteral(
            "<td class='metric-cell'>"
            "<div class='metric-k'>当前间隔（天）</div>"
            "<div class='metric-v'>%1</div>"
            "</td>")
            .arg(QString::number(primaryProgress->interval).toHtmlEscaped());

        const QString nextReview = prettyDateTime(primaryProgress->nextReview);
        if (!nextReview.isEmpty()) {
            overviewItems += QStringLiteral(
                "<td class='metric-cell'>"
                "<div class='metric-k'>下次复习</div>"
                "<div class='metric-v metric-v-small'>%1</div>"
                "</td>")
                .arg(nextReview.toHtmlEscaped());
        }
    }

    QString trendSection;
    if (primaryProgress != nullptr) {
        QVector<WordEventItem> eventsDesc;
        const QString targetType = primaryProgress->trainingType.trimmed().toLower();
        for (const WordEventItem &event : detail.recentEvents) {
            if (event.trainingType.trimmed().toLower() == targetType) {
                eventsDesc.push_back(event);
            }
            if (eventsDesc.size() >= 30) {
                break;
            }
        }
        if (!eventsDesc.isEmpty()) {
            QVector<WordEventItem> eventsAsc;
            QVector<double> efAsc;
            eventsAsc.reserve(eventsDesc.size());
            efAsc.reserve(eventsDesc.size());

            double currentEf = primaryProgress->easeFactor;
            QVector<double> afterDesc;
            afterDesc.reserve(eventsDesc.size());
            for (const WordEventItem &event : eventsDesc) {
                afterDesc.push_back(currentEf);
                if (event.result == SpellingResult::Mastered) {
                    currentEf = qMax(1.3, currentEf - 0.1);
                } else if (event.result == SpellingResult::Blurry) {
                    currentEf = qMin(3.0, currentEf + 0.05);
                } else {
                    currentEf = qMin(3.0, currentEf + 0.2);
                }
            }
            for (int i = eventsDesc.size() - 1; i >= 0; --i) {
                eventsAsc.push_back(eventsDesc.at(i));
                efAsc.push_back(afterDesc.at(i));
            }

            QString trendRows;
            const int rowCount = qMin(6, eventsAsc.size());
            for (int i = eventsAsc.size() - rowCount; i < eventsAsc.size(); ++i) {
                const WordEventItem &event = eventsAsc.at(i);
                trendRows += QStringLiteral(
                    "<tr>"
                    "<td>%1</td>"
                    "<td>%2</td>"
                    "<td>%3</td>"
                    "<td>%4</td>"
                    "</tr>")
                    .arg(esc(prettyDateTime(event.eventTime)),
                         esc(resultText(event.result)),
                         esc(QString::number(efAsc.at(i), 'f', 2)),
                         esc(event.skipped ? QStringLiteral("是") : QStringLiteral("否")));
            }

            QString trendContent = QStringLiteral("<div class='section-title'>训练进度趋势</div>");
            if (efAsc.size() >= 2) {
                trendContent += renderTrendImage(efAsc);
                trendContent += QStringLiteral("<div class='muted'>类型：%1（最近 %2 次）</div>")
                    .arg(esc(trainingTypeText(primaryProgress->trainingType)),
                         esc(QString::number(efAsc.size())));
            } else {
                trendContent += QStringLiteral(
                    "<div class='muted'>当前熟练系数：%1，最近一次时间：%2</div>")
                    .arg(esc(QString::number(efAsc.first(), 'f', 2)),
                         esc(prettyDateTime(eventsAsc.last().eventTime)));
            }
            if (!trendRows.isEmpty()) {
                trendContent += QStringLiteral(
                "<table class='grid compact'>"
                "<thead><tr><th>时间</th><th>结果</th><th>熟练系数</th><th>跳过</th></tr></thead>"
                "<tbody>%1</tbody>"
                "</table>")
                    .arg(trendRows);
            }
            trendSection = QStringLiteral("<div class='section-card'>%1</div>").arg(trendContent);
        }
    }

    QString baseRows;
    if (hasValue(detail.word.word)) {
        baseRows += kvRow(QStringLiteral("单词"), cleanedValue(detail.word.word));
    }
    if (hasValue(detail.word.phonetic)) {
        baseRows += kvRow(QStringLiteral("音标"), cleanedValue(detail.word.phonetic));
    }
    if (hasValue(detail.word.translation)) {
        baseRows += kvRow(QStringLiteral("释义"), cleanedValue(detail.word.translation));
    }
    if (hasValue(detail.word.partOfSpeech)) {
        baseRows += kvRow(QStringLiteral("词性"), cleanedValue(detail.word.partOfSpeech));
    }
    if (hasValue(detail.word.countabilityLabel)) {
        baseRows += kvRow(QStringLiteral("可数性"), cleanedValue(detail.word.countabilityLabel));
    }
    if (hasValue(detail.word.countabilityPlural)) {
        baseRows += kvRow(QStringLiteral("复数"), cleanedValue(detail.word.countabilityPlural));
    }
    if (hasValue(detail.word.countabilityNotes)) {
        const QString notes = cleanedValue(detail.word.countabilityNotes);
        if (!looksLikeJson(notes)) {
            baseRows += kvRow(QStringLiteral("备注"), notes);
        }
    }
    if (detail.word.skipForever) {
        baseRows += kvRow(QStringLiteral("永久跳过"), QStringLiteral("是"));
    }
    const QString nextReviewText = prettyDateTime(detail.word.nextReview);
    if (!nextReviewText.isEmpty()) {
        baseRows += kvRow(QStringLiteral("下次复习"), nextReviewText);
    }
    QString jsonContent;
    const QString polysemyJsonBlock = renderJsonBlock(detail.word.polysemyJson);
    if (!polysemyJsonBlock.isEmpty()) {
        jsonContent += QStringLiteral("<div class='sub-title'>熟词生义 JSON</div>%1").arg(polysemyJsonBlock);
    }
    if (hasValue(detail.word.countabilityNotes)) {
        const QString notes = cleanedValue(detail.word.countabilityNotes);
        if (looksLikeJson(notes)) {
            const QString notesJsonBlock = renderJsonBlock(notes);
            if (!notesJsonBlock.isEmpty()) {
                jsonContent += QStringLiteral("<div class='sub-title'>备注 JSON</div>%1").arg(notesJsonBlock);
            }
        }
    }

    if (!baseRows.isEmpty() || !jsonContent.isEmpty()) {
        QString baseSection = QStringLiteral("<div class='section-card'><div class='section-title'>基础信息</div>");
        if (!baseRows.isEmpty()) {
            baseSection += QStringLiteral("<table class='kv'>%1</table>").arg(baseRows);
        }
        if (!jsonContent.isEmpty()) {
            baseSection += jsonContent;
        }
        baseSection += QStringLiteral("</div>");
        detailSections += baseSection;
    }

    if (!detail.progressByType.isEmpty()) {
        QString rows;
        for (const TrainingProgressDetail &p : detail.progressByType) {
            rows += QStringLiteral(
                "<tr>"
                "<td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td><td>%8</td>"
                "</tr>")
                .arg(esc(hasValue(p.trainingType) ? trainingTypeText(cleanedValue(p.trainingType)) : QStringLiteral("未知")),
                     esc(QString::number(p.easeFactor, 'f', 2)),
                     esc(QString::number(p.interval)),
                     esc(statusText(p.status)),
                     esc(QString::number(p.correctCount)),
                     esc(QString::number(p.wrongCount)),
                     esc(prettyDateTime(p.nextReview)),
                     esc(prettyDateTime(p.updatedAt)));
        }
        detailSections += QStringLiteral(
            "<div class='section-card'>"
            "<div class='section-title'>训练进度</div>"
            "<table class='grid'>"
            "<thead><tr><th>类型</th><th>熟练系数</th><th>间隔</th><th>状态</th><th>正确</th><th>错误</th><th>下次复习</th><th>更新时间</th></tr></thead>"
            "<tbody>%1</tbody>"
            "</table>"
            "</div>").arg(rows);
    }

    const bool hasSpellingStats = detail.spellingAttemptCount > 0
                               || detail.spellingCorrectCount > 0
                               || detail.spellingStatsUpdatedAt.isValid();
    if (hasSpellingStats) {
        QString statsRows;
        if (detail.spellingAttemptCount > 0) {
            statsRows += kvRow(QStringLiteral("作答次数"), QString::number(detail.spellingAttemptCount));
        }
        if (detail.spellingCorrectCount > 0) {
            statsRows += kvRow(QStringLiteral("答对次数"), QString::number(detail.spellingCorrectCount));
        }
        const QString updated = prettyDateTime(detail.spellingStatsUpdatedAt);
        if (!updated.isEmpty()) {
            statsRows += kvRow(QStringLiteral("更新时间"), updated);
        }
        if (!statsRows.isEmpty()) {
            detailSections += QStringLiteral(
                "<div class='section-card'>"
                "<div class='section-title'>拼写统计</div>"
                "<table class='kv'>%1</table>"
                "</div>").arg(statsRows);
        }
    }

    if (!detail.books.isEmpty()) {
        QString booksSection = QStringLiteral("<div class='section-card'><div class='section-title'>所属词书</div>");
        booksSection += QStringLiteral("<ul class='list'>");
        for (const WordBookItem &book : detail.books) {
            booksSection += QStringLiteral("<li>%1 <span class='muted'>（当前绑定：%2）</span></li>")
                .arg(esc(hasValue(book.name) ? cleanedValue(book.name) : QStringLiteral("未命名词书")),
                     esc(book.isActive ? QStringLiteral("是") : QStringLiteral("否")));
        }
        booksSection += QStringLiteral("</ul>");
        booksSection += QStringLiteral("</div>");
        detailSections += booksSection;
    }

    const bool hasEvents = detail.totalEventCount > 0
                        || detail.lastEventTime.isValid()
                        || !detail.recentEvents.isEmpty();
    if (hasEvents) {
        QString eventsSection = QStringLiteral("<div class='section-card'><div class='section-title'>学习事件</div>");
        QString eventMetaRows;
        if (detail.totalEventCount > 0) {
            eventMetaRows += kvRow(QStringLiteral("总作答"), QString::number(detail.totalEventCount));
        }
        const QString lastTime = prettyDateTime(detail.lastEventTime);
        if (!lastTime.isEmpty()) {
            eventMetaRows += kvRow(QStringLiteral("最近作答"), lastTime);
        }
        if (!eventMetaRows.isEmpty()) {
            eventsSection += QStringLiteral("<table class='kv'>%1</table>").arg(eventMetaRows);
        }
        if (!detail.recentEvents.isEmpty()) {
            QString eventRows;
            const int itemCount = qMin(12, detail.recentEvents.size());
            for (int i = 0; i < itemCount; ++i) {
                const WordEventItem &event = detail.recentEvents.at(i);
                const QString eventInput = hasValue(event.userInput) ? cleanedValue(event.userInput) : QStringLiteral("—");
                eventRows += QStringLiteral(
                    "<tr>"
                    "<td>%1</td>"
                    "<td>%2</td>"
                    "<td>%3</td>"
                    "<td>%4</td>"
                    "<td>%5</td>"
                    "</tr>")
                    .arg(esc(hasValue(event.trainingType) ? trainingTypeText(cleanedValue(event.trainingType)) : QStringLiteral("未知")),
                         esc(resultText(event.result)),
                         esc(event.skipped ? QStringLiteral("是") : QStringLiteral("否")),
                         esc(eventInput),
                         esc(prettyDateTime(event.eventTime)));
            }
            eventsSection += QStringLiteral(
                "<table class='grid compact'>"
                "<thead><tr><th>类型</th><th>结果</th><th>跳过</th><th>输入</th><th>时间</th></tr></thead>"
                "<tbody>%1</tbody>"
                "</table>").arg(eventRows);
        }
        eventsSection += QStringLiteral("</div>");
        detailSections += eventsSection;
    }

    QString body;
    if (!overviewItems.isEmpty()) {
        body += QStringLiteral(
            "<div class='section-card'>"
            "<div class='section-title'>概览</div>"
            "<table class='metric-table'><tr>%1</tr></table>"
            "</div>").arg(overviewItems);
    }
    if (!trendSection.isEmpty()) {
        body += trendSection;
    }
    body += detailSections;
    if (body.isEmpty()) {
        body = QStringLiteral(
            "<div class='section-card'><div class='section-title'>单词详情</div><div class='muted'>暂无可展示的数据</div></div>");
    }

    const QString html = QStringLiteral(
        "<style>"
        "body{font-family:'PingFang SC','Microsoft YaHei','Helvetica Neue',sans-serif;color:#1f2937;}"
        ".section-card{border:1px solid #d8dee6;border-radius:10px;padding:12px 14px;margin:0 0 14px 0;background:#ffffff;}"
        ".section-title{font-size:17px;font-weight:700;color:#0f172a;margin:0 0 10px 0;}"
        ".metric-table{width:100%;border-collapse:separate;border-spacing:8px;}"
        ".metric-cell{border:1px solid #e5e7eb;border-radius:8px;padding:10px 12px;vertical-align:top;}"
        ".metric-k{font-size:13px;font-weight:600;color:#64748b;margin-bottom:6px;}"
        ".metric-v{font-size:30px;font-weight:700;line-height:1.1;color:#0f172a;}"
        ".metric-v-small{font-size:22px;}"
        ".hint{display:inline-block;width:15px;height:15px;line-height:15px;text-align:center;border-radius:50%;border:1px solid #cbd5e1;color:#64748b;font-size:11px;}"
        ".sub-title{font-size:14px;font-weight:700;color:#334155;margin:10px 0 6px 0;}"
        ".muted{color:#64748b;font-size:13px;line-height:1.45;}"
        ".action{display:inline-block;margin:4px 0 8px 0;color:#1f2937;text-decoration:underline;font-size:13px;}"
        ".kv{width:100%;border-collapse:collapse;}"
        ".kv td{padding:6px 0;vertical-align:top;border-bottom:1px solid #f1f5f9;}"
        ".kv tr:last-child td{border-bottom:none;}"
        ".kv .k{width:116px;color:#64748b;font-weight:600;}"
        ".kv .v{color:#0f172a;word-break:break-word;overflow-wrap:anywhere;}"
        ".grid{width:100%;border-collapse:collapse;margin-top:8px;}"
        ".grid th,.grid td{border-bottom:1px solid #f1f5f9;padding:6px 8px;text-align:left;font-size:12px;word-break:break-word;overflow-wrap:anywhere;}"
        ".grid th{color:#64748b;font-weight:700;background:transparent;}"
        ".grid.compact th,.grid.compact td{font-size:11px;padding:5px 6px;}"
        ".list{margin:0;padding-left:18px;}"
        ".list li{margin:4px 0;}"
        ".trend-image{display:block;width:100%;height:auto;max-width:700px;border:1px solid #e5e7eb;margin:2px 0 8px 0;}"
        ".json{margin:0;border:1px solid #e5e7eb;background:#ffffff;color:#1f2937;padding:10px;font-size:12px;line-height:1.45;white-space:pre-wrap;}"
        "</style>"
        "%1")
        .arg(body);
    contentLabel_->setHtml(html);
}

WordBooksPageWidget::WordBooksPageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 18, 24, 22);
    root->setSpacing(10);

    auto *header = new QHBoxLayout();
    header->setSpacing(14);
    backButton_ = new HoverScaleButton(this);
    backButton_->setFixedSize(46, 46);
    backButton_->setIcon(createBackLineIcon());
    backButton_->setIconSize(QSize(20, 20));
    backButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "background: #f3f4f6;"
        "color: #374151;"
        "border: none;"
        "border-radius: 14px;"
        "}"
        "HoverScaleButton:hover { background: #e9ebef; }"));

    titleLabel_ = new QLabel(QStringLiteral("词书"), this);
    titleLabel_->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 700; color: #0f172a; padding-bottom: 4px;"));

    metaLabel_ = new QLabel(QStringLiteral("管理词书与当前学习进度"), this);
    metaLabel_->setStyleSheet(QStringLiteral("font-size: 15px; color: #6b7280;"));

    auto *titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(4);
    titleCol->addWidget(titleLabel_);
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

    audioProgressBar_ = new RoundedProgressStrip(audioStatusHost_);
    audioProgressBar_->setRange(0, 100);
    audioProgressBar_->setValue(0);
    audioProgressBar_->setFixedHeight(10);

    audioStopButton_ = new HoverScaleButton(QStringLiteral("⏸"), audioStatusHost_);
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

    auto *statusColumn = new QVBoxLayout();
    statusColumn->setContentsMargins(0, 0, 0, 0);
    statusColumn->setSpacing(4);
    statusColumn->addWidget(audioStatusHost_, 0, Qt::AlignRight);

    header->addWidget(backButton_, 0, Qt::AlignTop);
    header->addLayout(titleCol, 1);
    header->addLayout(statusColumn, 0);

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

    addBookButton_ = new HoverScaleButton(QStringLiteral("添加词书"), this);
    addBookButton_->setFixedHeight(60);
    addBookButton_->setStyleSheet(QStringLiteral(
        "font-size: 22px; font-weight: 700; border-radius: 18px;"
        "background: #0f1b3d; color: #ffffff;"
        "HoverScaleButton:hover { background: #13224b; }"));

    root->addLayout(header);
    root->addWidget(currentTitleLabel_);
    root->addWidget(currentCardHost_);
    root->addWidget(otherTitleLabel_);
    root->addWidget(booksList_, 1);
    root->addWidget(addBookButton_);

    connect(backButton_, &HoverScaleButton::clicked, this, &WordBooksPageWidget::backClicked);
    connect(addBookButton_, &HoverScaleButton::clicked, this, &WordBooksPageWidget::addBookClicked);
    connect(audioStopButton_, &HoverScaleButton::clicked, this, &WordBooksPageWidget::audioDownloadStopRequested);
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

void WordBooksPageWidget::setWordBooks(const QVector<WordBookItem> &books,
                                       int activeBookId,
                                       const QString &trainingType,
                                       const QString &trainingDisplayName) {
    books_ = books;
    activeBookId_ = activeBookId;
    currentTrainingType_ = trainingType.trimmed().toLower();
    isManagementMode_ = (currentTrainingType_ == QStringLiteral("none"));
    currentTrainingDisplayName_ = trainingDisplayName.trimmed();
    int totalWords = 0;
    for (const WordBookItem &book : books_) {
        totalWords += book.wordCount;
    }
    if (metaLabel_) {
        metaLabel_->setText(QStringLiteral("共 %1 本词书 · %2 词").arg(books_.size()).arg(totalWords));
    }
    if (titleLabel_) {
        titleLabel_->setText(currentTrainingDisplayName_.isEmpty() ? QStringLiteral("词书") : currentTrainingDisplayName_);
    }
    if (currentTitleLabel_) {
        currentTitleLabel_->setVisible(!isManagementMode_);
        if (currentTrainingDisplayName_.isEmpty()) {
            currentTitleLabel_->setText(QStringLiteral("当前绑定"));
        } else {
            currentTitleLabel_->setText(QStringLiteral("%1 · 当前绑定").arg(currentTrainingDisplayName_));
        }
    }
    if (currentCardHost_) {
        currentCardHost_->setVisible(!isManagementMode_);
    }
    if (otherTitleLabel_) {
        otherTitleLabel_->setText(isManagementMode_ ? QStringLiteral("所有词库") : QStringLiteral("其他词书"));
    }
    rebuildList();
}

QString WordBooksPageWidget::bindingTagText(const WordBookItem &book) const {
    Q_UNUSED(book);
    return QString();
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

    WordBookItem activeBook;
    bool hasActiveBook = false;
    if (!isManagementMode_) {
        for (const WordBookItem &book : books_) {
            if (book.id == activeBookId_) {
                activeBook = book;
                hasActiveBook = true;
                break;
            }
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
        const QString coverTop = baseColor.lighter(106).name();
        const QString coverBottom = baseColor.darker(102).name();

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
        title->setMargin(6);
        title->setStyleSheet(QStringLiteral(
            "font-size: 20px; font-weight: 700; color: #0f172a;"
            "background: transparent; border: none;"));
        title->setWordWrap(true);

        auto *count = new QLabel(QStringLiteral("%1 词").arg(book.wordCount), row);
        count->setStyleSheet(QStringLiteral(
            "font-size: 15px; color: #94a3b8;"
            "background: transparent; border: none;"));
        auto *binding = new QLabel(bindingTagText(book), row);
        binding->setStyleSheet(QStringLiteral(
            "font-size: 12px;"
            "font-weight: 700;"
            "letter-spacing: 0.4px;"
            "color: #475569;"
            "background: transparent; border: none;"));
        if (binding->text().isEmpty()) {
            binding->hide();
        }

        auto *textLayout = new QVBoxLayout();
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(5);
        textLayout->addWidget(title);
        textLayout->addWidget(count);
        textLayout->addWidget(binding);

        textLayout->addStretch();

        auto *rightLayout = new QVBoxLayout();
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(4);
        rightLayout->setAlignment(Qt::AlignTop | Qt::AlignRight);

        if (!isCurrent) {
            auto *learnButton = new HoverScaleButton(QStringLiteral("绑定"), row);
            learnButton->setObjectName(QStringLiteral("bookLearnButton"));
            learnButton->setFixedSize(88, 34);
            learnButton->setCursor(Qt::PointingHandCursor);
            learnButton->setVisible(!isManagementMode_);
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
            connect(learnButton, &HoverScaleButton::clicked, this, [this, book]() {
                emit wordBookSelected(book.id);
            });

            auto *deleteButton = new HoverScaleButton(QStringLiteral("🗑"), row);
            deleteButton->setObjectName(QStringLiteral("bookDeleteEmojiButton"));
            deleteButton->setFixedSize(26, 26);
            deleteButton->setToolTip(QStringLiteral("删除词书"));
            deleteButton->setStyleSheet(QStringLiteral(
                "#bookDeleteEmojiButton {"
                "  border: none; border-radius: 8px; padding: 0;"
                "  background: transparent; font-size: 12px; color: #64748b;"
                "}"
                "#bookDeleteEmojiButton:hover { background: rgba(148,163,184,0.12); color: #475569; }"));
            connect(deleteButton, &HoverScaleButton::clicked, this, [this, book]() {
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
            auto *downloadButton = new HoverScaleButton(QStringLiteral("下载音频"), row);
            downloadButton->setObjectName(QStringLiteral("bookDownloadButton"));
            downloadButton->setFixedSize(92, 34);
            downloadButton->setCursor(Qt::PointingHandCursor);
            downloadButton->setVisible(!isManagementMode_);
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
            connect(downloadButton, &HoverScaleButton::clicked, this, [this, book]() {
                emit downloadAudioRequested(book.id);
            });

            auto *deleteButton = new HoverScaleButton(QStringLiteral("🗑"), row);
            deleteButton->setObjectName(QStringLiteral("bookDeleteEmojiButton"));
            deleteButton->setFixedSize(26, 26);
            deleteButton->setToolTip(QStringLiteral("删除词书"));
            deleteButton->setStyleSheet(QStringLiteral(
                "#bookDeleteEmojiButton {"
                "  border: none; border-radius: 8px; padding: 0;"
                "  background: transparent; font-size: 12px; color: #64748b;"
                "}"
                "#bookDeleteEmojiButton:hover { background: rgba(148,163,184,0.12); color: #475569; }"));
            connect(deleteButton, &HoverScaleButton::clicked, this, [this, book]() {
                emit wordBookDeleteRequested(book.id);
            });

            auto *downloadRow = new QHBoxLayout();
            downloadRow->setContentsMargins(0, 0, 0, 0);
            downloadRow->setSpacing(6);
            downloadRow->addStretch(1);
            if (!isManagementMode_) {
                downloadRow->addWidget(downloadButton);
            }

            auto *deleteRow = new QHBoxLayout();
            deleteRow->setContentsMargins(0, 0, 0, 0);
            deleteRow->setSpacing(0);
            deleteRow->addStretch(1);
            deleteRow->addWidget(deleteButton);

            rightLayout->addLayout(downloadRow);
            rightLayout->addLayout(deleteRow);
        }

        layout->addWidget(cover);
        layout->addLayout(textLayout, 1);
        layout->addLayout(rightLayout);
        return row;
    };

    if (hasActiveBook) {
        currentCardLayout_->addWidget(makeBookCard(currentCardHost_, activeBook, true));
    } else {
        auto *emptyBind = new QWidget(currentCardHost_);
        emptyBind->setStyleSheet(QStringLiteral(
            "background: #f8fafc; border: 1px dashed #d8e1ec; border-radius: 18px;"));
        auto *emptyLayout = new QVBoxLayout(emptyBind);
        emptyLayout->setContentsMargins(16, 12, 16, 12);
        auto *emptyTitle = new QLabel(QStringLiteral("当前模式未绑定词书"), emptyBind);
        emptyTitle->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: #334155;"));
        auto *emptyDesc = new QLabel(QStringLiteral("点击下方任意词书的“绑定”按钮即可设置"), emptyBind);
        emptyDesc->setStyleSheet(QStringLiteral("font-size: 14px; color: #64748b;"));
        emptyLayout->addWidget(emptyTitle);
        emptyLayout->addWidget(emptyDesc);
        currentCardLayout_->addWidget(emptyBind);
    }

    int otherCount = 0;
    for (const WordBookItem &book : books_) {
        if (hasActiveBook && book.id == activeBook.id) {
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
