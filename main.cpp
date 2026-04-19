#include <QApplication>
#include <QFont>

#include "app_logger.h"
#include "gui_widgets.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    const bool kDebugMode = false;
    app.setProperty("vibespeller_debug", kDebugMode);

    const QString logsRoot = QStringLiteral(VIBESPELLER_SOURCE_DIR) + QStringLiteral("/runtime_logs");
    AppLogger::initialize(logsRoot);
    AppLogger::info(QStringLiteral("Main"),
                    QStringLiteral("Application started, debug=%1, logs=%2")
                        .arg(kDebugMode ? QStringLiteral("true") : QStringLiteral("false"),
                             AppLogger::currentLogFilePath()));

    QFont uiFont;
    uiFont.setFamilies({QStringLiteral("PingFang SC"),
                        QStringLiteral("Microsoft YaHei"),
                        QStringLiteral("Noto Sans CJK SC"),
                        QStringLiteral("Helvetica Neue")});
    uiFont.setPointSize(8);
    app.setFont(uiFont);

    VibeSpellerWindow window;
    window.show();

    const int code = app.exec();
    AppLogger::info(QStringLiteral("Main"), QStringLiteral("Application exited, code=%1").arg(code));
    AppLogger::shutdown();
    return code;
}
