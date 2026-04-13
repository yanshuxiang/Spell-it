#include <QApplication>
#include <QFont>

#include "gui_widgets.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QFont uiFont;
    uiFont.setFamilies({QStringLiteral("PingFang SC"),
                        QStringLiteral("Microsoft YaHei"),
                        QStringLiteral("Noto Sans CJK SC"),
                        QStringLiteral("Helvetica Neue")});
    uiFont.setPointSize(8);
    app.setFont(uiFont);

    VibeSpellerWindow window;
    window.show();

    return app.exec();
}
