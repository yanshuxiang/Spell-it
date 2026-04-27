#include "app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace {

QString sourceRoot() {
    return QStringLiteral(VIBESPELLER_SOURCE_DIR);
}

QString appDir() {
    const QString dir = QCoreApplication::applicationDirPath();
    return dir.isEmpty() ? QDir::currentPath() : dir;
}

QString ensureDir(const QString &path) {
    QDir dir;
    dir.mkpath(path);
    return path;
}

QString firstExistingDir(const QStringList &candidates, const QString &fallback) {
    for (const QString &candidate : candidates) {
        if (candidate.trimmed().isEmpty()) {
            continue;
        }
        const QFileInfo info(candidate);
        if (info.exists() && info.isDir()) {
            return info.absoluteFilePath();
        }
    }
    return fallback;
}

} // namespace

namespace AppPaths {

QString appDataDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.trimmed().isEmpty()) {
        dir = QDir(appDir()).filePath(QStringLiteral("VibeSpellerData"));
    }
    return ensureDir(dir);
}

QString logsDir() {
    return ensureDir(QDir(appDataDir()).filePath(QStringLiteral("runtime_logs")));
}

QString databasePath() {
    const QString target = QDir(appDataDir()).filePath(QStringLiteral("vibespeller.db"));
    if (!QFileInfo::exists(target)) {
        const QString seed = QDir(sourceRoot()).filePath(QStringLiteral("vibespeller.db"));
        if (QFileInfo::exists(seed)) {
            QFile::copy(seed, target);
        }
    }
    return target;
}

QString audioDir() {
    return ensureDir(QDir(appDataDir()).filePath(QStringLiteral("audio")));
}

QString audioManifestPath() {
    return QDir(audioDir()).filePath(QStringLiteral(".hash_manifest.json"));
}

QString bundledDir(const QString &dirName) {
    const QString cleanName = dirName.trimmed();
    return firstExistingDir({
        QDir(appDir()).filePath(cleanName),
        QDir(appDir()).filePath(QStringLiteral("../Resources/%1").arg(cleanName)),
        QDir(sourceRoot()).filePath(cleanName)
    }, QDir(appDir()).filePath(cleanName));
}

QString bundledFile(const QString &dirName, const QString &fileName) {
    return QDir(bundledDir(dirName)).filePath(fileName);
}

QString defaultBooksDir() {
    return bundledDir(QStringLiteral("default_books"));
}

} // namespace AppPaths
