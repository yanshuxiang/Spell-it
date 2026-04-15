#ifndef GUI_WIDGETS_INTERNAL_H
#define GUI_WIDGETS_INTERNAL_H

#include <QDateTime>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QWidget>

#include "gui_widgets.h"

namespace GuiWidgetsInternal {

enum class PromptType {
    Info,
    Warning,
    Error,
    Question,
};

inline constexpr int kStudyIdleCutoffSeconds = 120;
inline constexpr int kSessionBatchSize = 5;
inline constexpr int kCorrectTransitionMs = 300;
inline constexpr int kWrongShakeMs = 200;
inline constexpr int kTransitionShiftPx = 300;
inline constexpr int kWrongShakeOffsetPx = 3;
inline constexpr qreal kBasePlaybackVolume = 1.0;
inline constexpr double kTargetMeanDb = -20.0;
inline constexpr double kMaxPeakDb = -1.0;
inline constexpr int kAnalyzeTimeoutMs = 8000;

QString defaultInputStyle();
QGraphicsOpacityEffect *ensureOpacityEffect(QWidget *widget);
QString safeAudioFileName(const QString &word);
QString findBestColumn(const QStringList &headers, const QStringList &keywords);
int countByResult(const QVector<PracticeRecord> &records, SpellingResult target);
QString briefResult(SpellingResult result);
int nextReviewDaysForSummary(const PracticeRecord &record);
QString summaryRightText(const PracticeRecord &record, bool reviewMode);
QColor summaryRightColor(const PracticeRecord &record, bool reviewMode);
QString coverColorForBook(int bookId);
QString coverTextForBook(const QString &bookName);
QIcon createBackLineIcon();
QIcon createBooksLineIcon();
QIcon createArchiveLineIcon();
QIcon createChartLineIcon();
QIcon createTrashLineIcon();
QWidget *createSummaryRow(const PracticeRecord &record, bool reviewMode);
QString promptSymbol(PromptType type);
int showStyledPrompt(QWidget *parent,
                     const QString &title,
                     const QString &message,
                     PromptType type,
                     const QStringList &buttons,
                     int defaultButtonIndex);
bool showQuestionPrompt(QWidget *parent, const QString &title, const QString &message);
void showInfoPrompt(QWidget *parent, const QString &title, const QString &message);
void showWarningPrompt(QWidget *parent, const QString &title, const QString &message);
void showErrorPrompt(QWidget *parent, const QString &title, const QString &message);
QString buildMiniWeekCalendarHtml(const QDateTime &nextReview);

} // namespace GuiWidgetsInternal

#endif // GUI_WIDGETS_INTERNAL_H
