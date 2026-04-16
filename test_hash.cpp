#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QString>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QString payload = "price|B";
    QString hash = QString::fromLatin1(QCryptographicHash::hash(payload.toUtf8(), QCryptographicHash::Sha256).toHex());
    qDebug() << payload << "hash:" << hash;
    return 0;
}
