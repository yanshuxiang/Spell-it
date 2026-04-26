#include "gui_widgets.h"
#include "gui_widgets_internal.h"

#include <QEasingCurve>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextBrowser>

using namespace GuiWidgetsInternal;

CountabilityPageWidget::CountabilityPageWidget(QWidget *parent)
    : QWidget(parent) {
    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(28, 20, 28, 32);
    rootLayout_->setSpacing(0);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(10);

    exitButton_ = new HoverScaleButton(QStringLiteral("退出"), this);
    exitButton_->setHoverScaleEnabled(false);
    exitButton_->setFixedSize(96, 42);
    exitButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "  background: #f3f4f6;"
        "  color: #0f172a;"
        "  border-radius: 12px;"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "}"
        "HoverScaleButton:hover { background: #e8ebef; }"));

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

    rootLayout_->addLayout(header);

    // ── 内容区：英文单词 + 中文释义（优先级最高，内容完整显示）──
    contentHost_ = new QWidget(this);
    contentLayout_ = new QVBoxLayout(contentHost_);
    contentLayout_->setContentsMargins(0, 0, 0, 0);
    contentLayout_->setSpacing(6);

    wordLabel_ = new QLabel(QStringLiteral("word"), this);
    wordLabel_->setWordWrap(true);
    wordLabel_->setAlignment(Qt::AlignCenter);
    wordLabel_->setMargin(10);
    wordLabel_->setMinimumHeight(100);
    wordLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    wordLabel_->setStyleSheet(QStringLiteral("font-size: 42px; font-weight: 800; color: #0f172a;"));

    hintLabel_ = new QLabel(this);
    hintLabel_->setWordWrap(true);
    hintLabel_->setTextFormat(Qt::PlainText);
    hintLabel_->setAlignment(Qt::AlignCenter);
    hintLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    hintLabel_->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 600; color: #475569;"));

    contentLayout_->addWidget(wordLabel_);
    contentLayout_->addWidget(hintLabel_);

    // 初始居中布局（答题模式）
    topSpacer_ = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    bottomSpacer_ = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    rootLayout_->addSpacerItem(topSpacer_);
    rootLayout_->addWidget(contentHost_, 0, Qt::AlignCenter);
    rootLayout_->addSpacerItem(bottomSpacer_);

    // ── 详情区：正确答案条 + 可滚动用法说明 + 继续按钮 ──
    usageDetailsHost_ = new QWidget(this);
    auto *detailsLayout = new QVBoxLayout(usageDetailsHost_);
    detailsLayout->setContentsMargins(0, 8, 0, 0);
    detailsLayout->setSpacing(10);

    // 正确答案条（紧凑，固定高度）
    usageDetailCorrectLabel_ = new QLabel(this);
    usageDetailCorrectLabel_->setAlignment(Qt::AlignCenter);
    usageDetailCorrectLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    usageDetailCorrectLabel_->setStyleSheet(QStringLiteral(
        "font-size: 15px; font-weight: 700; color: #166534; "
        "background: rgba(134,239,172,0.15); border-radius: 10px; padding: 8px 16px;"));

    // 用法说明 Label（宽度撑满、高度自适应）
    usageDetailNotesLabel_ = new QLabel(this);
    usageDetailNotesLabel_->setWordWrap(true);
    usageDetailNotesLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    usageDetailNotesLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    usageDetailNotesLabel_->setStyleSheet(QStringLiteral(
        "font-size: 15px; color: #1e293b; padding: 18px;"));

    // 滚动区域包裹 notes，弹性填充剩余空间
    auto *notesScroll = new QScrollArea(this);
    notesScroll->setWidget(usageDetailNotesLabel_);
    notesScroll->setWidgetResizable(true);
    notesScroll->setFrameShape(QFrame::StyledPanel);
    notesScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    notesScroll->setStyleSheet(QStringLiteral(
        "QScrollArea {"
        "  background: #ffffff;"
        "  border: 1px solid #e2e8f0;"
        "  border-radius: 14px;"
        "}"
        "QScrollArea > QWidget > QWidget { background: #ffffff; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #cbd5e1; border-radius: 3px; }"));

    // 继续按钮（固定高度，始终可见）
    usageContinueButton_ = new HoverScaleButton(QStringLiteral("我已理解，继续"), this);
    usageContinueButton_->setFixedSize(240, 52);
    usageContinueButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "  background: #0f172a;"
        "  color: #ffffff;"
        "  border-radius: 14px;"
        "  font-size: 16px;"
        "  font-weight: 700;"
        "}"
        "HoverScaleButton:hover { background: #1e293b; }"));

    detailsLayout->addWidget(usageDetailCorrectLabel_);
    detailsLayout->addWidget(notesScroll, 1);   // stretch=1，弹性吸收剩余空间
    detailsLayout->addSpacing(4);
    detailsLayout->addWidget(usageContinueButton_, 0, Qt::AlignHCenter);
    usageDetailsHost_->hide();

    // 详情区 stretch=1，在竖向弹性分配
    rootLayout_->addWidget(usageDetailsHost_, 1);

    countableButton_ = new HoverScaleButton(QStringLiteral("可数"), this);
    uncountableButton_ = new HoverScaleButton(QStringLiteral("不可数"), this);
    bothButton_ = new HoverScaleButton(QStringLiteral("可数且不可数"), this);
    const QString answerButtonStyle = QStringLiteral(
        "HoverScaleButton {"
        "  background: #f8fafc;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 16px;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  color: #0f172a;"
        "  min-height: 60px;"
        "}"
        "HoverScaleButton:hover { background: #f1f5f9; border-color: #94a3b8; }"
        "HoverScaleButton:disabled { color: #94a3b8; background: #f8fafc; border-color: #e2e8f0; }");
    countableButton_->setStyleSheet(answerButtonStyle);
    uncountableButton_->setStyleSheet(answerButtonStyle);
    bothButton_->setStyleSheet(answerButtonStyle);

    rootLayout_->addWidget(countableButton_);
    rootLayout_->addSpacing(12);
    rootLayout_->addWidget(uncountableButton_);
    rootLayout_->addSpacing(12);
    rootLayout_->addWidget(bothButton_);

    connect(exitButton_, &HoverScaleButton::clicked, this, &CountabilityPageWidget::exitRequested);
    connect(countableButton_, &HoverScaleButton::clicked, this,
            [this]() { emit userActivity(); emit answerSubmitted(CountabilityAnswer::Countable); });
    connect(uncountableButton_, &HoverScaleButton::clicked, this,
            [this]() { emit userActivity(); emit answerSubmitted(CountabilityAnswer::Uncountable); });
    connect(bothButton_, &HoverScaleButton::clicked, this,
            [this]() { emit userActivity(); emit answerSubmitted(CountabilityAnswer::Both); });
    connect(usageContinueButton_, &HoverScaleButton::clicked, this, [this]() {
        emit userActivity();
        emit continueRequested();
    });
}

void CountabilityPageWidget::setWord(const WordItem &word, int currentIndex, int totalCount, bool isReviewMode) {
    if (detailsTransitionGroup_ != nullptr) {
        detailsTransitionGroup_->stop();
        detailsTransitionGroup_->deleteLater();
        detailsTransitionGroup_ = nullptr;
    }
    usageDetailsHost_->setGraphicsEffect(nullptr);

    isDetailsMode_ = false;
    modeLabel_->setText(isReviewMode ? QStringLiteral("可数性复习") : QStringLiteral("可数性辨析"));
    progressLabel_->setText(QStringLiteral("%1 / %2").arg(currentIndex).arg(qMax(1, totalCount)));
    
    wordLabel_->setText(word.word.trimmed().isEmpty() ? QStringLiteral("-") : word.word.trimmed());
    wordLabel_->setAlignment(Qt::AlignCenter);
    wordLabel_->setMargin(10);
    wordLabel_->setMinimumHeight(100);
    wordLabel_->setStyleSheet(QStringLiteral("font-size: 42px; font-weight: 800; color: #0f172a;"));

    QString hint = word.translation.trimmed();
    if (hint.isEmpty()) {
        hint = QStringLiteral("请选择该词作为名词时的可数性");
    } else if (hint.size() > 120) {
        hint = hint.left(120) + QStringLiteral("…");
    }
    hintLabel_->setText(hint);
    hintLabel_->setAlignment(Qt::AlignCenter);
    hintLabel_->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 600; color: #475569;"));

    resetOptionStyles();
    setOptionsEnabled(true);

    usageDetailsHost_->hide();
    countableButton_->show();
    uncountableButton_->show();
    bothButton_->show();

    // 恢复居中布局
    topSpacer_->changeSize(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    bottomSpacer_->changeSize(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
    rootLayout_->setAlignment(contentHost_, Qt::AlignCenter);
    rootLayout_->invalidate();
    rootLayout_->activate();
}



void CountabilityPageWidget::setOptionsEnabled(bool enabled) {
    countableButton_->setEnabled(enabled);
    uncountableButton_->setEnabled(enabled);
    bothButton_->setEnabled(enabled);
}

HoverScaleButton *CountabilityPageWidget::buttonForAnswer(CountabilityAnswer answer) const {
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
        "HoverScaleButton {"
        "  background: #f8fafc;"
        "  border: 1px solid #d8e1ec;"
        "  border-radius: 14px;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  color: #0f172a;"
        "  min-height: 54px;"
        "}"
        "HoverScaleButton:hover { background: #eef3f9; }"
        "HoverScaleButton:disabled { color: #94a3b8; background: #f8fafc; border-color: #e2e8f0; }");
    countableButton_->setStyleSheet(baseStyle);
    uncountableButton_->setStyleSheet(baseStyle);
    bothButton_->setStyleSheet(baseStyle);
}

void CountabilityPageWidget::showAnswerFeedback(CountabilityAnswer selected,
                                                CountabilityAnswer correct,
                                                bool isCorrect) {
    resetOptionStyles();
    HoverScaleButton *selectedButton = buttonForAnswer(selected);
    HoverScaleButton *correctButton = buttonForAnswer(correct);
    const QString greenStyle = QStringLiteral(
        "HoverScaleButton {"
        "  background: rgba(134,239,172,0.28);"
        "  border: 1px solid #86efac;"
        "  border-radius: 14px;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  color: #166534;"
        "  min-height: 54px;"
        "}");
    const QString redStyle = QStringLiteral(
        "HoverScaleButton {"
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

void CountabilityPageWidget::showDetailedFeedback(const WordItem &word,
                                                   CountabilityAnswer correct,
                                                   CountabilityAnswer selected) {
    Q_UNUSED(selected);
    if (isDetailsMode_) return;
    if (detailsTransitionGroup_ != nullptr) {
        detailsTransitionGroup_->stop();
        detailsTransitionGroup_->deleteLater();
        detailsTransitionGroup_ = nullptr;
    }
    isDetailsMode_ = true;

    // ── 1. 填充内容 ──
    const bool isActuallyCorrect = (selected == correct);
    QString prefix = isActuallyCorrect ? QStringLiteral("回答正确！") : QStringLiteral("正确答案：");
    QString correctLabel;
    switch (correct) {
    case CountabilityAnswer::Countable:   correctLabel = QStringLiteral("可数 (C)");           break;
    case CountabilityAnswer::Uncountable: correctLabel = QStringLiteral("不可数 (U)");         break;
    case CountabilityAnswer::Both:        correctLabel = QStringLiteral("可数且不可数 (Both)"); break;
    }
    usageDetailCorrectLabel_->setText(prefix + correctLabel);
    QString qss = QStringLiteral("font-size: 16px; font-weight: 700; padding: 12px 20px; border-radius: 14px;");
    if (isActuallyCorrect) {
        qss += QStringLiteral("color: #166534; background-color: #f0fdf4; border: 1px solid #bcf0da;");
    } else {
        qss += QStringLiteral("color: #991b1b; background-color: #fef2f2; border: 1px solid #fecaca;");
    }
    usageDetailCorrectLabel_->setStyleSheet(qss);
    usageDetailCorrectLabel_->setAlignment(Qt::AlignCenter);
    usageDetailNotesLabel_->setTextFormat(Qt::RichText);

    QString rawNotes = word.countabilityNotes.trimmed();
    if (rawNotes.isEmpty()) {
        usageDetailNotesLabel_->setText(QStringLiteral("<p style='color:#64748b;'>本词暂无详细用法说明。</p>"));
    } else if (rawNotes.startsWith(QLatin1Char('{'))) {
        // 解析 JSON 格式的 Explanation
        QJsonDocument doc = QJsonDocument::fromJson(rawNotes.toUtf8());
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            QString html;
            html += QStringLiteral("<div style='line-height:1.4;'>");

            // 0. 核心考点 (exam_points) - 优先级最高，置顶显示
            QString examPoints = obj.value(QStringLiteral("exam_points")).toString().trimmed();
            if (!examPoints.isEmpty()) {
                html += QStringLiteral("<div style='background:rgba(37,99,235,0.08); border-left:4px solid #2563eb; border-radius:4px; padding:12px; margin-bottom:14px;'>");
                html += QStringLiteral("<b style='color:#1e40af;'>核心考点：</b><br/>%1").arg(examPoints);
                html += QStringLiteral("</div>");
            }

            // 1. 复数形式 (仅对可数或 Both 显式提供)
            QString countability = obj.value(QStringLiteral("countability")).toString().toUpper();
            QString plural = obj.value(QStringLiteral("plural")).toString().trimmed();
            bool hidePlural = (countability == QStringLiteral("U")) || 
                             (plural.toUpper() == QStringLiteral("NA")) || 
                             (plural.toUpper() == QStringLiteral("N/A")) ||
                             plural.isEmpty();
            
            if (!hidePlural) {
                html += QStringLiteral("<p style='margin-bottom:12px;'><b style='color:#0f172a;'>复数形式：</b><code style='color:#2563eb; background:#f1f5f9; padding:2px 6px; border-radius:4px;'>%1</code></p>").arg(plural);
            }

            // 2. 语义与可数性变化 (shifts)
            QJsonArray shifts = obj.value(QStringLiteral("shifts")).toArray();
            if (!shifts.isEmpty()) {
                html += QStringLiteral("<p><b style='color:#0f172a;'>用法解析：</b></p>");
                html += QStringLiteral("<ul style='margin-top:4px;'>");
                for (const QJsonValue &v : shifts) {
                    QJsonObject s = v.toObject();
                    QString sense = s.value(QStringLiteral("sense")).toString().trimmed();
                    QString type = s.value(QStringLiteral("type")).toString().trimmed();
                    QString example = s.value(QStringLiteral("example")).toString().trimmed();
                    html += QStringLiteral("<li style='margin-bottom:10px;'>");
                    if (!type.isEmpty()) {
                        html += QStringLiteral("<b style='color:#2563eb;'>[%1] </b>").arg(type);
                    }
                    html += sense;
                    if (!example.isEmpty()) {
                        html += QStringLiteral("<br/><i style='color:#64748b; font-size:14px;'>%1</i>").arg(example);
                    }
                    html += QStringLiteral("</li>");
                }
                html += QStringLiteral("</ul>");
            }

            // 3. 常用搭配 (collocations / classifiers)
            QJsonArray collocations = obj.value(QStringLiteral("collocations")).toArray();
            if (collocations.isEmpty()) {
                collocations = obj.value(QStringLiteral("classifiers")).toArray();
            }

            if (!collocations.isEmpty()) {
                html += QStringLiteral("<p><b style='color:#0f172a;'>常用搭配：</b></p>");
                html += QStringLiteral("<ul style='margin-top:4px;'>");
                for (const QJsonValue &v : collocations) {
                    if (v.isObject()) {
                        QJsonObject c = v.toObject();
                        QString phrase = c.value(QStringLiteral("phrase")).toString().trimmed();
                        QString example = c.value(QStringLiteral("example")).toString().trimmed();
                        html += QStringLiteral("<li style='margin-bottom:10px;'>");
                        html += QStringLiteral("<b style='color:#1e293b;'>%1</b>").arg(phrase);
                        if (!example.isEmpty()) {
                            html += QStringLiteral("<br/><i style='color:#64748b; font-size:14px;'>%1</i>").arg(example);
                        }
                        html += QStringLiteral("</li>");
                    } else {
                        html += QStringLiteral("<li style='margin-bottom:6px; color:#1e293b;'>%1</li>").arg(v.toString());
                    }
                }
                html += QStringLiteral("</ul>");
            }

            // 4. 注意事项 (chinese_trap)
            QString trap = obj.value(QStringLiteral("chinese_trap")).toString().trimmed();
            if (!trap.isEmpty()) {
                html += QStringLiteral("<div style='background:rgba(234,179,8,0.1); border-radius:10px; padding:14px; margin-top:14px;'>");
                html += QStringLiteral("<b style='color:#854d0e;'>注意：</b><br/>%1").arg(trap);
                html += QStringLiteral("</div>");
            }

            // 5. 主谓一致 (agreement) - 仅在有特殊规则时显示
            QJsonObject agr = obj.value(QStringLiteral("agreement")).toObject();
            QString rule = agr.value(QStringLiteral("rule")).toString().trimmed();
            if (!rule.isEmpty()) {
                html += QStringLiteral("<div style='margin-top:16px; padding-top:12px; border-top:1px dashed #e2e8f0;'>");
                html += QStringLiteral("<b style='color:#0f172a;'>特殊语法：</b><br/>%1").arg(rule);
                QString agrEx = agr.value(QStringLiteral("example")).toString().trimmed();
                if (!agrEx.isEmpty()) {
                    html += QStringLiteral("<p style='color:#64748b; font-size:14px; margin-top:4px;'><i>%1</i></p>").arg(agrEx);
                }
                html += QStringLiteral("</div>");
            }

            html += QStringLiteral("</div>");
            usageDetailNotesLabel_->setText(html);
        } else {
            // 解析失败按原样显示
            usageDetailNotesLabel_->setText(rawNotes);
        }
    } else {
        // 原有纯文本逻辑
        rawNotes.replace(QStringLiteral("[例]"),    QStringLiteral("\n\xf0\x9f\x92\xa1 例:"));
        rawNotes.replace(QStringLiteral("[例(U)]"), QStringLiteral("\n\xf0\x9f\x92\xa1 例(U):"));
        rawNotes.replace(QStringLiteral("[例(C)]"), QStringLiteral("\n\xf0\x9f\x92\xa1 例(C):"));
        usageDetailNotesLabel_->setText(rawNotes);
    }

    // ── 2. 记录动画起点（当前居中位置）──
    rootLayout_->activate();
    QPoint startPos = contentHost_->pos();

    // ── 3. 切换到「详情」布局 ──
    // 隐藏答题按钮
    countableButton_->hide();
    uncountableButton_->hide();
    bothButton_->hide();

    // 顶部/底部弹性归零，让 contentHost 贴顶显示
    topSpacer_->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    bottomSpacer_->changeSize(0, 12, QSizePolicy::Minimum, QSizePolicy::Fixed);

    // 中英文：左对齐，全宽展开，内容完整显示（不截断）
    wordLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    wordLabel_->setMargin(8);
    wordLabel_->setMinimumHeight(68);
    wordLabel_->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 800; color: #0f172a;"));
    wordLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    
    hintLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    hintLabel_->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600; color: #475569;"));
    hintLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    // 强制更新字体并重新计算高度
    wordLabel_->adjustSize();
    hintLabel_->adjustSize();

    // contentHost 撑满全宽，高度按内容计算最小值保证不被挤压
    contentHost_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    rootLayout_->setAlignment(contentHost_, Qt::AlignLeft | Qt::AlignTop);

    // 预先设置详情区不透明度为 0（动画起点）
    auto *opacityEffect = new QGraphicsOpacityEffect(usageDetailsHost_);
    usageDetailsHost_->setGraphicsEffect(opacityEffect);
    opacityEffect->setOpacity(0.0);
    usageDetailsHost_->show();
    rootLayout_->activate();

    // ── 4. 记录动画终点 ──
    QPoint endPos = contentHost_->pos();
    QPoint usageFinalPos = usageDetailsHost_->pos(); // 记录详情区的最终布局位置

    // ── 5. 执行丝滑动画 ──
    contentHost_->move(startPos);   // 先归位，确保动画从正确起点出发
    
    // 详情区：起始位置向下偏移 30 像素，实现从下往上弹出的效果
    usageDetailsHost_->move(usageFinalPos.x(), usageFinalPos.y() + 32);

    auto *group = new QParallelAnimationGroup(this);
    detailsTransitionGroup_ = group;

    // 1. 单词标题：从居中滑向左上角
    auto *moveAnim = new QPropertyAnimation(contentHost_, "pos", group);
    moveAnim->setDuration(480);
    moveAnim->setStartValue(startPos);
    moveAnim->setEndValue(endPos);
    moveAnim->setEasingCurve(QEasingCurve::OutQuint);

    // 2. 详情区域：向上回弹动画
    auto *usageMoveAnim = new QPropertyAnimation(usageDetailsHost_, "pos", group);
    usageMoveAnim->setDuration(550);
    usageMoveAnim->setStartValue(QPoint(usageFinalPos.x(), usageFinalPos.y() + 32));
    usageMoveAnim->setEndValue(usageFinalPos);
    usageMoveAnim->setEasingCurve(QEasingCurve::OutCubic);

    // 3. 详情区域：淡入动画
    auto *fadeAnim = new QPropertyAnimation(opacityEffect, "opacity", group);
    fadeAnim->setDuration(580);
    fadeAnim->setStartValue(0.0);
    fadeAnim->setEndValue(1.0);
    fadeAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(group, &QParallelAnimationGroup::finished, this, [group, opacityEffect, this]() {
        // 动画结束后清理，避免残留的 Effect 影响后续交互
        usageDetailsHost_->setGraphicsEffect(nullptr);
        if (detailsTransitionGroup_ == group) {
            detailsTransitionGroup_ = nullptr;
        }
        group->deleteLater();
    });
    group->start();
}

void CountabilityPageWidget::refreshBasePositions() {
    rootLayout_->activate();
}

namespace {
QString compactText(const QString &value) {
    QString out = value;
    out = out.simplified();
    return out.trimmed();
}

void appendUniqueLimited(QStringList &out, const QString &value, int maxCount) {
    if (out.size() >= maxCount) {
        return;
    }
    const QString text = compactText(value);
    if (text.isEmpty()) {
        return;
    }
    if (!out.contains(text)) {
        out.push_back(text);
    }
}

QString jsonScalarToText(const QJsonValue &value) {
    if (value.isString()) {
        return compactText(value.toString());
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return QString();
}

QString jsonValueToText(const QJsonValue &value, int depth = 0) {
    if (depth > 5) {
        return QString();
    }
    const QString scalar = jsonScalarToText(value);
    if (!scalar.isEmpty()) {
        return scalar;
    }
    if (value.isArray()) {
        QStringList parts;
        for (const QJsonValue &item : value.toArray()) {
            const QString t = jsonValueToText(item, depth + 1);
            if (!t.isEmpty()) {
                parts.push_back(t);
            }
            if (parts.size() >= 6) {
                break;
            }
        }
        return compactText(parts.join(QStringLiteral("；")));
    }
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        static const QStringList preferredKeys = {
            QStringLiteral("definition"),
            QStringLiteral("meaning"),
            QStringLiteral("note"),
            QStringLiteral("example"),
            QStringLiteral("sentence"),
            QStringLiteral("context"),
            QStringLiteral("source"),
            QStringLiteral("text"),
            QStringLiteral("content"),
            QStringLiteral("value")
        };
        QStringList parts;
        for (const QString &key : preferredKeys) {
            if (!obj.contains(key)) {
                continue;
            }
            const QString t = jsonValueToText(obj.value(key), depth + 1);
            if (!t.isEmpty()) {
                parts.push_back(t);
            }
        }
        if (parts.isEmpty()) {
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                const QString t = jsonValueToText(it.value(), depth + 1);
                if (!t.isEmpty()) {
                    parts.push_back(t);
                }
                if (parts.size() >= 6) {
                    break;
                }
            }
        }
        return compactText(parts.join(QStringLiteral("；")));
    }
    return QString();
}

bool keyMatches(const QString &keyLower, const QStringList &keywords) {
    for (const QString &kw : keywords) {
        if (keyLower.contains(kw)) {
            return true;
        }
    }
    return false;
}

void collectByKeys(const QJsonValue &value,
                   const QStringList &keywords,
                   QStringList &out,
                   int depth = 0,
                   int maxCount = 8) {
    if (depth > 6 || out.size() >= maxCount) {
        return;
    }
    if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            collectByKeys(item, keywords, out, depth + 1, maxCount);
            if (out.size() >= maxCount) {
                return;
            }
        }
        return;
    }
    if (!value.isObject()) {
        return;
    }
    const QJsonObject obj = value.toObject();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QString keyLower = it.key().toLower();
        if (keyMatches(keyLower, keywords)) {
            appendUniqueLimited(out, jsonValueToText(it.value()), maxCount);
        }
        collectByKeys(it.value(), keywords, out, depth + 1, maxCount);
        if (out.size() >= maxCount) {
            return;
        }
    }
}

QString renderSectionHtml(const QString &title, const QStringList &items) {
    if (items.isEmpty()) {
        return QString();
    }
    QString html = QStringLiteral(
        "<div style='margin-top:10px;'>"
        "<div style='font-size:14px;font-weight:800;color:#1e293b;margin-bottom:6px;'>%1</div>"
        "<ul style='margin:0 0 0 18px;padding:0;color:#334155;'>")
        .arg(title.toHtmlEscaped());
    for (const QString &item : items) {
        html += QStringLiteral("<li style='margin:4px 0;'>%1</li>").arg(item.toHtmlEscaped());
    }
    html += QStringLiteral("</ul></div>");
    return html;
}

struct PolysemySenseRow {
    QString meaning;
    QString example;
    QString source;
};

QStringList textsFromRawString(const QString &raw);

QStringList textsFromJsonValue(const QJsonValue &value) {
    if (value.isUndefined() || value.isNull()) {
        return {};
    }
    if (value.isString()) {
        return textsFromRawString(value.toString());
    }
    if (value.isDouble()) {
        return {QString::number(value.toDouble())};
    }
    if (value.isBool()) {
        return {value.toBool() ? QStringLiteral("true") : QStringLiteral("false")};
    }
    if (value.isArray()) {
        QStringList out;
        for (const QJsonValue &item : value.toArray()) {
            const QStringList sub = textsFromJsonValue(item);
            for (const QString &s : sub) {
                appendUniqueLimited(out, s, 64);
            }
        }
        return out;
    }
    if (value.isObject()) {
        QStringList out;
        appendUniqueLimited(out, jsonValueToText(value), 64);
        return out;
    }
    return {};
}

QStringList textsFromRawString(const QString &raw) {
    const QString text = compactText(raw);
    if (text.isEmpty()) {
        return {};
    }

    if ((text.startsWith(QLatin1Char('[')) && text.endsWith(QLatin1Char(']')))
        || (text.startsWith(QLatin1Char('{')) && text.endsWith(QLatin1Char('}')))) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError) {
            if (doc.isArray()) {
                return textsFromJsonValue(QJsonValue(doc.array()));
            }
            if (doc.isObject()) {
                return textsFromJsonValue(QJsonValue(doc.object()));
            }
        }
    }
    return {text};
}

void appendFromKeys(const QJsonObject &obj,
                    const QStringList &keys,
                    QStringList &out,
                    int maxCount = 16) {
    for (const QString &key : keys) {
        if (!obj.contains(key)) {
            continue;
        }
        const QStringList values = textsFromJsonValue(obj.value(key));
        for (const QString &v : values) {
            appendUniqueLimited(out, v, maxCount);
        }
    }
}

QString sourceTextFromObject(const QJsonObject &obj) {
    QStringList parts;
    const auto appendIf = [&parts, &obj](const QString &key) {
        const QString value = compactText(jsonValueToText(obj.value(key)));
        if (!value.isEmpty()) {
            parts.push_back(value);
        }
    };
    appendIf(QStringLiteral("book"));
    appendIf(QStringLiteral("test"));
    appendIf(QStringLiteral("passage"));
    appendIf(QStringLiteral("title"));
    appendIf(QStringLiteral("source_id"));
    if (parts.isEmpty()) {
        return QString();
    }
    return parts.join(QStringLiteral(" | "));
}

QString pickByIndex(const QStringList &values, int idx) {
    if (values.isEmpty()) {
        return QString();
    }
    if (idx >= 0 && idx < values.size()) {
        return values.at(idx);
    }
    if (values.size() == 1) {
        return values.first();
    }
    return QString();
}

void appendRowsFromObject(const QJsonObject &obj,
                          QVector<PolysemySenseRow> &rows,
                          int maxRows = 12) {
    if (rows.size() >= maxRows) {
        return;
    }

    QStringList meanings;
    QStringList examples;
    QStringList sources;
    appendFromKeys(obj,
                   {QStringLiteral("definition"),
                    QStringLiteral("meaning"),
                    QStringLiteral("note"),
                    QStringLiteral("gloss"),
                    QStringLiteral("explain"),
                    QStringLiteral("value"),
                    QStringLiteral("gem_meaning"),
                    QStringLiteral("exam_value")},
                   meanings);
    appendFromKeys(obj,
                   {QStringLiteral("example"),
                    QStringLiteral("examples"),
                    QStringLiteral("sentence"),
                    QStringLiteral("sentences"),
                    QStringLiteral("context"),
                    QStringLiteral("contexts"),
                    QStringLiteral("source_sentence")},
                   examples);
    appendFromKeys(obj,
                   {QStringLiteral("source"),
                    QStringLiteral("sources")},
                   sources);

    const QString composedSource = sourceTextFromObject(obj);
    if (!composedSource.isEmpty()) {
        appendUniqueLimited(sources, composedSource, 16);
    }

    const int rowCount = qMax(1, qMax(meanings.size(), qMax(examples.size(), sources.size())));
    for (int i = 0; i < rowCount && rows.size() < maxRows; ++i) {
        PolysemySenseRow row;
        row.meaning = pickByIndex(meanings, i);
        row.example = pickByIndex(examples, i);
        row.source = pickByIndex(sources, i);
        if (row.meaning.isEmpty() && row.example.isEmpty() && row.source.isEmpty()) {
            continue;
        }

        bool duplicated = false;
        for (const PolysemySenseRow &existing : rows) {
            if (existing.meaning == row.meaning
                && existing.example == row.example
                && existing.source == row.source) {
                duplicated = true;
                break;
            }
        }
        if (!duplicated) {
            rows.push_back(row);
        }
    }
}

void appendRowsFromValue(const QJsonValue &value,
                         QVector<PolysemySenseRow> &rows,
                         int depth = 0,
                         int maxRows = 12) {
    if (depth > 6 || rows.size() >= maxRows) {
        return;
    }
    if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            appendRowsFromValue(item, rows, depth + 1, maxRows);
            if (rows.size() >= maxRows) {
                return;
            }
        }
        return;
    }
    if (!value.isObject()) {
        return;
    }

    const QJsonObject obj = value.toObject();
    if (obj.contains(QStringLiteral("senses"))) {
        appendRowsFromValue(obj.value(QStringLiteral("senses")), rows, depth + 1, maxRows);
    }
    if (obj.contains(QStringLiteral("polysemy_data"))) {
        appendRowsFromValue(obj.value(QStringLiteral("polysemy_data")), rows, depth + 1, maxRows);
    }
    appendRowsFromObject(obj, rows, maxRows);
}
} // namespace

PolysemyPageWidget::PolysemyPageWidget(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 20, 28, 30);
    root->setSpacing(10);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(10);

    exitButton_ = new HoverScaleButton(QStringLiteral("退出"), this);
    exitButton_->setHoverScaleEnabled(false);
    exitButton_->setFixedSize(96, 42);
    exitButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "  background: #f3f4f6;"
        "  color: #0f172a;"
        "  border-radius: 12px;"
        "  font-size: 14px;"
        "  font-weight: 700;"
        "}"
        "HoverScaleButton:hover { background: #e8ebef; }"));

    modeLabel_ = new QLabel(QStringLiteral("熟词生义"), this);
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

    masteredButton_ = new HoverScaleButton(QStringLiteral("已掌握"), this);
    blurryButton_ = new HoverScaleButton(QStringLiteral("模糊"), this);
    unfamiliarButton_ = new HoverScaleButton(QStringLiteral("不熟悉"), this);
    masteredButton_->setHoverScaleEnabled(false);
    blurryButton_->setHoverScaleEnabled(false);
    unfamiliarButton_->setHoverScaleEnabled(false);
    const QString answerButtonStyle = QStringLiteral(
        "HoverScaleButton {"
        "  background: #edf3f9;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 16px;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  color: #0f172a;"
        "  min-height: 62px;"
        "  padding: 0 10px;"
        "}"
        "HoverScaleButton:hover { background: #dfe8f3; border-color: #94a3b8; }"
        "HoverScaleButton:disabled { color: #94a3b8; background: #f8fafc; border-color: #e2e8f0; }");
    masteredButton_->setStyleSheet(answerButtonStyle);
    blurryButton_->setStyleSheet(answerButtonStyle);
    unfamiliarButton_->setStyleSheet(answerButtonStyle);
    masteredButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    blurryButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    unfamiliarButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *answerRow = new QHBoxLayout();
    answerRow->setContentsMargins(0, 0, 0, 0);
    answerRow->setSpacing(12);
    answerRow->addWidget(masteredButton_, 1);
    answerRow->addWidget(blurryButton_, 1);
    answerRow->addWidget(unfamiliarButton_, 1);

    stageStack_ = new QStackedWidget(this);

    quizPage_ = new QWidget(this);
    auto *quizLayout = new QVBoxLayout(quizPage_);
    quizLayout->setContentsMargins(0, 0, 0, 0);
    quizLayout->setSpacing(12);

    translationLabel_ = new QLabel(QStringLiteral("-"), quizPage_);
    translationLabel_->setAlignment(Qt::AlignCenter);
    translationLabel_->setWordWrap(true);
    translationLabel_->setMinimumHeight(120);
    translationLabel_->setStyleSheet(QStringLiteral(
        "font-size: 29px;"
        "font-weight: 800;"
        "color: #0f172a;"
        "background: transparent;"
        "border: none;"
        "padding: 0;"));

    quizLayout->addStretch(2);
    quizLayout->addWidget(translationLabel_);
    quizLayout->addStretch(3);
    quizLayout->addLayout(answerRow);

    detailPage_ = new QWidget(this);
    auto *detailLayout = new QVBoxLayout(detailPage_);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(10);

    detailWordLabel_ = new QLabel(QStringLiteral("-"), detailPage_);
    detailWordLabel_->setAlignment(Qt::AlignCenter);
    detailWordLabel_->setWordWrap(true);
    detailWordLabel_->setStyleSheet(QStringLiteral("font-size: 40px; font-weight: 800; color: #0f172a;"));

    detailRatingLabel_ = new QLabel(QString(), detailPage_);
    detailRatingLabel_->setAlignment(Qt::AlignCenter);
    detailRatingLabel_->setStyleSheet(QStringLiteral(
        "font-size: 15px;"
        "font-weight: 700;"
        "color: #475569;"
        "padding: 10px 16px;"
        "border-radius: 12px;"
        "background: #f8fafc;"
        "border: 1px solid #e2e8f0;"));

    detailBrowser_ = new QTextBrowser(detailPage_);
    detailBrowser_->setOpenExternalLinks(false);
    detailBrowser_->setFrameShape(QFrame::NoFrame);
    detailBrowser_->setStyleSheet(QStringLiteral(
        "QTextBrowser {"
        "  background: #f8fafc;"
        "  border: 1px solid #e2e8f0;"
        "  border-radius: 14px;"
        "  color: #334155;"
        "  font-size: 15px;"
        "  line-height: 1.45;"
        "  padding: 10px;"
        "}"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #cbd5e1; border-radius: 3px; }"));

    continueButton_ = new HoverScaleButton(QStringLiteral("下一个单词"), detailPage_);
    continueButton_->setHoverScaleEnabled(false);
    continueButton_->setFixedSize(220, 54);
    continueButton_->setStyleSheet(QStringLiteral(
        "HoverScaleButton {"
        "  background: #0f172a;"
        "  color: #ffffff;"
        "  border-radius: 14px;"
        "  font-size: 17px;"
        "  font-weight: 700;"
        "}"
        "HoverScaleButton:hover { background: #1e293b; }"));

    detailLayout->addWidget(detailWordLabel_);
    detailLayout->addWidget(detailRatingLabel_);
    detailLayout->addWidget(detailBrowser_, 1);
    detailLayout->addWidget(continueButton_, 0, Qt::AlignHCenter);

    stageStack_->addWidget(quizPage_);
    stageStack_->addWidget(detailPage_);

    root->addLayout(header);
    root->addWidget(stageStack_, 1);

    connect(exitButton_, &HoverScaleButton::clicked, this, [this]() {
        emit userActivity();
        emit exitRequested();
    });
    connect(continueButton_, &HoverScaleButton::clicked, this, [this]() {
        emit userActivity();
        emit continueRequested();
    });
    connect(masteredButton_, &HoverScaleButton::clicked, this, [this]() {
        emit userActivity();
        emit ratingSubmitted(SpellingResult::Mastered);
    });
    connect(blurryButton_, &HoverScaleButton::clicked, this, [this]() {
        emit userActivity();
        emit ratingSubmitted(SpellingResult::Blurry);
    });
    connect(unfamiliarButton_, &HoverScaleButton::clicked, this, [this]() {
        emit userActivity();
        emit ratingSubmitted(SpellingResult::Unfamiliar);
    });
}

QString PolysemyPageWidget::ratingText(SpellingResult result) const {
    switch (result) {
    case SpellingResult::Mastered:
        return QStringLiteral("已掌握");
    case SpellingResult::Blurry:
        return QStringLiteral("模糊");
    case SpellingResult::Unfamiliar:
        return QStringLiteral("不熟悉");
    }
    return QStringLiteral("不熟悉");
}

QString PolysemyPageWidget::buildPolysemyDetailHtml(const WordItem &word) const {
    QString html = QStringLiteral("<div style='font-size:15px;color:#334155;'>");
    if (!word.translation.trimmed().isEmpty()) {
        html += QStringLiteral(
            "<div style='margin-bottom:8px;'>"
            "<span style='font-weight:800;color:#1e293b;'>基础释义（词典）：</span>%1"
            "</div>")
            .arg(word.translation.trimmed().toHtmlEscaped());
    }
    if (!word.partOfSpeech.trimmed().isEmpty()) {
        html += QStringLiteral(
            "<div style='margin-bottom:8px;'>"
            "<span style='font-weight:800;color:#1e293b;'>词性：</span>%1"
            "</div>")
            .arg(word.partOfSpeech.trimmed().toHtmlEscaped());
    }

    QVector<PolysemySenseRow> rows;
    QString fallbackRaw;

    const QString raw = word.polysemyJson.trimmed();
    if (!raw.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError) {
            const QJsonValue root = doc.isObject()
                                        ? QJsonValue(doc.object())
                                        : QJsonValue(doc.array());
            appendRowsFromValue(root, rows, 0, 12);
        } else {
            fallbackRaw = raw.left(420);
        }
    }

    if (!rows.isEmpty()) {
        html += QStringLiteral(
            "<div style='margin-top:10px;margin-bottom:6px;font-size:14px;font-weight:800;color:#1e293b;'>"
            "生义与例句对应"
            "</div>");
        for (int i = 0; i < rows.size(); ++i) {
            const PolysemySenseRow &row = rows.at(i);
            html += QStringLiteral(
                "<div style='margin:8px 0;padding:10px 12px;background:#ffffff;border:1px solid #e2e8f0;"
                "border-radius:10px;'>"
                "<div style='font-size:12px;color:#64748b;font-weight:700;margin-bottom:6px;'>条目 %1</div>")
                .arg(i + 1);
            if (!row.meaning.isEmpty()) {
                html += QStringLiteral(
                    "<div style='margin-bottom:6px;'><span style='font-weight:800;color:#1e293b;'>生义：</span>%1</div>")
                    .arg(row.meaning.toHtmlEscaped());
            }
            if (!row.example.isEmpty()) {
                html += QStringLiteral(
                    "<div style='margin-bottom:6px;'><span style='font-weight:800;color:#1e293b;'>例句：</span>%1</div>")
                    .arg(row.example.toHtmlEscaped());
            }
            if (!row.source.isEmpty()) {
                html += QStringLiteral(
                    "<div><span style='font-weight:800;color:#1e293b;'>来源：</span>%1</div>")
                    .arg(row.source.toHtmlEscaped());
            }
            html += QStringLiteral("</div>");
        }
    } else {
        if (!fallbackRaw.isEmpty()) {
            html += QStringLiteral(
                "<div style='margin-top:10px;'>"
                "<div style='font-size:14px;font-weight:800;color:#1e293b;margin-bottom:6px;'>原始数据</div>"
                "<div style='background:#ffffff;border:1px solid #e2e8f0;border-radius:10px;padding:10px;"
                "white-space:pre-wrap;'>%1</div></div>")
                .arg(fallbackRaw.toHtmlEscaped());
        } else {
            html += QStringLiteral(
                "<div style='margin-top:10px;color:#64748b;'>暂无来源与例句数据</div>");
        }
    }

    html += QStringLiteral("</div>");
    return html;
}

void PolysemyPageWidget::setWord(const WordItem &word, int currentIndex, int totalCount, bool isReviewMode) {
    modeLabel_->setText(isReviewMode ? QStringLiteral("熟词生义复习") : QStringLiteral("熟词生义学习"));
    progressLabel_->setText(QStringLiteral("%1 / %2").arg(currentIndex).arg(qMax(1, totalCount)));
    const QString englishWord = word.word.trimmed().isEmpty()
                                    ? QStringLiteral("-")
                                    : word.word.trimmed();
    translationLabel_->setText(englishWord);
    detailWordLabel_->setText(word.word.trimmed().isEmpty() ? QStringLiteral("-") : word.word.trimmed());
    detailRatingLabel_->clear();
    detailBrowser_->clear();
    resetRevealState();
    setOptionsEnabled(true);
}

void PolysemyPageWidget::showDetail(const WordItem &word, SpellingResult selectedResult) {
    detailWordLabel_->setText(word.word.trimmed().isEmpty() ? QStringLiteral("-") : word.word.trimmed());
    detailRatingLabel_->setText(QStringLiteral("你的选择：%1").arg(ratingText(selectedResult)));
    QString ratingStyle = QStringLiteral(
        "font-size: 16px;"
        "font-weight: 800;"
        "padding: 10px 16px;"
        "border-radius: 12px;");
    if (selectedResult == SpellingResult::Mastered) {
        ratingStyle += QStringLiteral("color: #166534; background-color: #f0fdf4; border: 1px solid #bcf0da;");
    } else if (selectedResult == SpellingResult::Blurry) {
        ratingStyle += QStringLiteral("color: #9a3412; background-color: #fff7ed; border: 1px solid #fed7aa;");
    } else {
        ratingStyle += QStringLiteral("color: #991b1b; background-color: #fef2f2; border: 1px solid #fecaca;");
    }
    detailRatingLabel_->setStyleSheet(ratingStyle);
    detailBrowser_->setHtml(buildPolysemyDetailHtml(word));
    detailBrowser_->verticalScrollBar()->setValue(0);
    stageStack_->setCurrentWidget(detailPage_);
    revealed_ = true;
}

void PolysemyPageWidget::setOptionsEnabled(bool enabled) {
    masteredButton_->setEnabled(enabled);
    blurryButton_->setEnabled(enabled);
    unfamiliarButton_->setEnabled(enabled);
}

void PolysemyPageWidget::resetRevealState() {
    revealed_ = false;
    if (stageStack_ != nullptr && quizPage_ != nullptr) {
        stageStack_->setCurrentWidget(quizPage_);
    }
}
