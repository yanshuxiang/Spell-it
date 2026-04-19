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
#include <QComboBox>
#include <QPainter>
#include <QPen>
#include <QPushButton>
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

    auto *title = new QLabel(QStringLiteral("词书"), this);
    title->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 700; color: #0f172a; padding-bottom: 4px;"));

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
    if (currentTitleLabel_) {
        currentTitleLabel_->setVisible(!isManagementMode_);
        if (currentTrainingDisplayName_.isEmpty()) {
            currentTitleLabel_->setText(QStringLiteral("当前学习词书"));
        } else {
            currentTitleLabel_->setText(QStringLiteral("%1 · 当前绑定词书").arg(currentTrainingDisplayName_));
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
    QStringList tags;
    if (book.boundSpelling) {
        tags << QStringLiteral("[拼]");
    }
    if (book.boundCountability) {
        tags << QStringLiteral("[可]");
    }
    if (book.boundPolysemy) {
        tags << QStringLiteral("[生]");
    }
    return tags.join(QStringLiteral(" "));
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
