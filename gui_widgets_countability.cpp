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
#include <QSizePolicy>
#include <QSpacerItem>
#include <QVBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

using namespace GuiWidgetsInternal;

CountabilityPageWidget::CountabilityPageWidget(QWidget *parent)
    : QWidget(parent) {
    rootLayout_ = new QVBoxLayout(this);
    rootLayout_->setContentsMargins(28, 20, 28, 32);
    rootLayout_->setSpacing(0);

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
    usageContinueButton_ = new QPushButton(QStringLiteral("我已理解，继续"), this);
    usageContinueButton_->setFixedSize(240, 52);
    usageContinueButton_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: #0f172a;"
        "  color: #ffffff;"
        "  border-radius: 14px;"
        "  font-size: 16px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover { background: #1e293b; }"));

    detailsLayout->addWidget(usageDetailCorrectLabel_);
    detailsLayout->addWidget(notesScroll, 1);   // stretch=1，弹性吸收剩余空间
    detailsLayout->addSpacing(4);
    detailsLayout->addWidget(usageContinueButton_, 0, Qt::AlignHCenter);
    usageDetailsHost_->hide();

    // 详情区 stretch=1，在竖向弹性分配
    rootLayout_->addWidget(usageDetailsHost_, 1);

    countableButton_ = new QPushButton(QStringLiteral("可数"), this);
    uncountableButton_ = new QPushButton(QStringLiteral("不可数"), this);
    bothButton_ = new QPushButton(QStringLiteral("可数且不可数"), this);
    const QString answerButtonStyle = QStringLiteral(
        "QPushButton {"
        "  background: #f8fafc;"
        "  border: 1px solid #cbd5e1;"
        "  border-radius: 16px;"
        "  font-size: 18px;"
        "  font-weight: 700;"
        "  color: #0f172a;"
        "  min-height: 60px;"
        "}"
        "QPushButton:hover { background: #f1f5f9; border-color: #94a3b8; }"
        "QPushButton:disabled { color: #94a3b8; background: #f8fafc; border-color: #e2e8f0; }");
    countableButton_->setStyleSheet(answerButtonStyle);
    uncountableButton_->setStyleSheet(answerButtonStyle);
    bothButton_->setStyleSheet(answerButtonStyle);

    rootLayout_->addWidget(countableButton_);
    rootLayout_->addSpacing(12);
    rootLayout_->addWidget(uncountableButton_);
    rootLayout_->addSpacing(12);
    rootLayout_->addWidget(bothButton_);

    connect(exitButton_, &QPushButton::clicked, this, &CountabilityPageWidget::exitRequested);
    connect(countableButton_, &QPushButton::clicked, this,
            [this]() { emit userActivity(); emit answerSubmitted(CountabilityAnswer::Countable); });
    connect(uncountableButton_, &QPushButton::clicked, this,
            [this]() { emit userActivity(); emit answerSubmitted(CountabilityAnswer::Uncountable); });
    connect(bothButton_, &QPushButton::clicked, this,
            [this]() { emit userActivity(); emit answerSubmitted(CountabilityAnswer::Both); });
    connect(usageContinueButton_, &QPushButton::clicked, this, [this]() {
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
