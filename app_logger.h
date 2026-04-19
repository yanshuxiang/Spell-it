#ifndef APP_LOGGER_H
#define APP_LOGGER_H

#include <QString>

namespace AppLogger {

void initialize(const QString &logsRootDir = QString());
void shutdown();

void log(const QString &level, const QString &category, const QString &message);
void info(const QString &category, const QString &message);
void warn(const QString &category, const QString &message);
void error(const QString &category, const QString &message);
void step(const QString &category, const QString &message);

QString logDirectory();
QString currentLogFilePath();

} // namespace AppLogger

#endif // APP_LOGGER_H
