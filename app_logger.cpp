#include "app_logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStringConverter>
#include <QTextStream>

namespace {
QMutex gLogMutex;
QString gLogDirPath;
QString gLogFilePath;
QFile *gLogFile = nullptr;
bool gInitialized = false;

QString fallbackLogDir() {
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        return appDir + QStringLiteral("/runtime_logs");
    }
    return QStringLiteral("./runtime_logs");
}

QString nowStamp() {
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

QString startupStamp() {
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

void writeLineLocked(const QString &line) {
    if (gLogFile == nullptr || !gLogFile->isOpen()) {
        return;
    }
    QTextStream out(gLogFile);
    out.setEncoding(QStringConverter::Utf8);
    out << line << '\n';
    out.flush();
    gLogFile->flush();
}
} // namespace

namespace AppLogger {

void initialize(const QString &logsRootDir) {
    QMutexLocker locker(&gLogMutex);
    if (gInitialized) {
        return;
    }

    gLogDirPath = logsRootDir.trimmed().isEmpty() ? fallbackLogDir() : logsRootDir.trimmed();
    QDir dir;
    if (!dir.mkpath(gLogDirPath)) {
        // 最后兜底到当前目录，避免日志系统直接失效。
        gLogDirPath = fallbackLogDir();
        dir.mkpath(gLogDirPath);
    }

    const QString fileName = QStringLiteral("vibespeller_%1.log").arg(startupStamp());
    gLogFilePath = QDir(gLogDirPath).filePath(fileName);

    gLogFile = new QFile(gLogFilePath);
    if (!gLogFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete gLogFile;
        gLogFile = nullptr;
        gLogFilePath.clear();
        gInitialized = true;
        return;
    }

    gInitialized = true;
    writeLineLocked(QStringLiteral("[%1] [INFO] [Logger] initialized, file=%2")
                        .arg(nowStamp(), gLogFilePath));
}

void shutdown() {
    QMutexLocker locker(&gLogMutex);
    if (!gInitialized) {
        return;
    }
    if (gLogFile != nullptr && gLogFile->isOpen()) {
        writeLineLocked(QStringLiteral("[%1] [INFO] [Logger] shutdown").arg(nowStamp()));
        gLogFile->close();
    }
    delete gLogFile;
    gLogFile = nullptr;
    gInitialized = false;
}

void log(const QString &level, const QString &category, const QString &message) {
    bool needInitialize = false;
    {
        QMutexLocker locker(&gLogMutex);
        needInitialize = !gInitialized;
    }
    if (needInitialize) {
        initialize();
    }

    QMutexLocker locker(&gLogMutex);
    const QString safeMessage = QString(message).replace('\n', QStringLiteral("\\n"));
    writeLineLocked(QStringLiteral("[%1] [%2] [%3] %4")
                        .arg(nowStamp(),
                             level.trimmed().isEmpty() ? QStringLiteral("INFO") : level.trimmed().toUpper(),
                             category.trimmed().isEmpty() ? QStringLiteral("General") : category.trimmed(),
                             safeMessage));
}

void info(const QString &category, const QString &message) {
    log(QStringLiteral("INFO"), category, message);
}

void warn(const QString &category, const QString &message) {
    log(QStringLiteral("WARN"), category, message);
}

void error(const QString &category, const QString &message) {
    log(QStringLiteral("ERROR"), category, message);
}

void step(const QString &category, const QString &message) {
    log(QStringLiteral("STEP"), category, message);
}

QString logDirectory() {
    QMutexLocker locker(&gLogMutex);
    return gLogDirPath;
}

QString currentLogFilePath() {
    QMutexLocker locker(&gLogMutex);
    return gLogFilePath;
}

} // namespace AppLogger
