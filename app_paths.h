#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <QString>

namespace AppPaths {

QString appDataDir();
QString logsDir();
QString databasePath();
QString audioDir();
QString audioManifestPath();
QString bundledDir(const QString &dirName);
QString bundledFile(const QString &dirName, const QString &fileName);
QString defaultBooksDir();

} // namespace AppPaths

#endif // APP_PATHS_H
