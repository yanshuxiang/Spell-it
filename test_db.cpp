#include <QCoreApplication>
#include <QDebug>
#include "database_manager.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    DatabaseManager db;
    if (db.open("./vibespeller.db") && db.initialize()) {
        auto words = db.fetchWordsForCountabilityDownload();
        qDebug() << "Pending countability downloads:" << words.size();
    } else {
        qDebug() << "DB Open failed:" << db.lastError();
    }
    return 0;
}
