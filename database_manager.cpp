#include "database_manager.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <QUuid>
#include <QtGlobal>

namespace {
constexpr const char *kDateTimeFormat = "yyyy-MM-dd HH:mm:ss";

QVector<int> reviewLadder() {
    return {1, 2, 4, 7, 15, 30};
}

QString joinWordIds(const QVector<WordItem> &words) {
    QStringList idList;
    idList.reserve(words.size());
    for (const WordItem &word : words) {
        idList.push_back(QString::number(word.id));
    }
    return idList.join(',');
}

QVector<int> splitWordIds(const QString &joined) {
    QVector<int> ids;
    const QStringList parts = joined.split(',', Qt::SkipEmptyParts);
    ids.reserve(parts.size());
    for (const QString &part : parts) {
        bool ok = false;
        const int id = part.toInt(&ok);
        if (ok && id > 0) {
            ids.push_back(id);
        }
    }
    return ids;
}

WordItem readWordFromQuery(const QSqlQuery &query) {
    WordItem item;
    item.id = query.value("id").toInt();
    item.word = query.value("word").toString();
    item.phonetic = query.value("phonetic").toString();
    item.translation = query.value("translation").toString();
    item.easeFactor = query.value("ease_factor").toDouble();
    item.interval = query.value("interval").toInt();
    item.status = query.value("status").toInt();

    const QString nextReviewText = query.value("next_review").toString();
    if (!nextReviewText.isEmpty()) {
        item.nextReview = QDateTime::fromString(nextReviewText, kDateTimeFormat);
    }
    return item;
}
} // namespace

DatabaseManager::DatabaseManager() {
    connectionName_ = QStringLiteral("vibespeller_conn_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

DatabaseManager::~DatabaseManager() {
    if (QSqlDatabase::contains(connectionName_)) {
        QSqlDatabase db = QSqlDatabase::database(connectionName_);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName_);
}

bool DatabaseManager::open(const QString &dbPath) {
    if (QSqlDatabase::contains(connectionName_)) {
        QSqlDatabase existing = QSqlDatabase::database(connectionName_);
        if (existing.isOpen()) {
            existing.close();
        }
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        lastError_ = db.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::initialize() {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    QSqlQuery query(database());
    const QString createSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS words ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "word TEXT NOT NULL UNIQUE,"
        "phonetic TEXT,"
        "translation TEXT NOT NULL,"
        "ease_factor REAL DEFAULT 2.5,"
        "interval INTEGER DEFAULT 0,"
        "next_review TEXT,"
        "status INTEGER DEFAULT 0"
        ")");

    if (!query.exec(createSql)) {
        lastError_ = query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_status ON words(status)"))) {
        lastError_ = query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_next_review ON words(next_review)"))) {
        lastError_ = query.lastError().text();
        return false;
    }

    const QString createSessionSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS session_progress ("
        "mode TEXT PRIMARY KEY,"
        "word_ids TEXT NOT NULL,"
        "current_index INTEGER NOT NULL DEFAULT 0,"
        "updated_at TEXT"
        ")");
    if (!query.exec(createSessionSql)) {
        lastError_ = query.lastError().text();
        return false;
    }

    return true;
}

bool DatabaseManager::readCsvPreview(const QString &csvPath,
                                     QStringList &headers,
                                     QVector<QStringList> &previewRows,
                                     int sampleCount) const {
    headers.clear();
    previewRows.clear();

    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lastError_ = QStringLiteral("无法打开 CSV 文件: %1").arg(csvPath);
        return false;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    if (in.atEnd()) {
        lastError_ = QStringLiteral("CSV 文件为空");
        return false;
    }

    headers = parseCsvLine(in.readLine());
    if (!headers.isEmpty()) {
        headers[0].remove(QChar(0xFEFF)); // 去除 UTF-8 BOM
    }

    int count = 0;
    while (!in.atEnd() && count < sampleCount) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }
        previewRows.push_back(parseCsvLine(line));
        ++count;
    }

    if (headers.isEmpty()) {
        lastError_ = QStringLiteral("无法解析 CSV 表头");
        return false;
    }

    return true;
}

bool DatabaseManager::importFromCsv(const QString &csvPath,
                                    int wordColumn,
                                    int translationColumn,
                                    int phoneticColumn,
                                    int &importedCount) {
    importedCount = 0;

    if (!ensureDatabaseOpen()) {
        return false;
    }

    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        lastError_ = QStringLiteral("无法打开 CSV 文件: %1").arg(csvPath);
        return false;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    if (in.atEnd()) {
        lastError_ = QStringLiteral("CSV 文件为空");
        return false;
    }

    // 跳过首行表头
    parseCsvLine(in.readLine());

    QSqlDatabase db = database();
    if (!db.transaction()) {
        lastError_ = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO words "
        "(word, phonetic, translation, ease_factor, interval, next_review, status) "
        "VALUES (?, ?, ?, 2.5, 0, NULL, 0)"));

    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) {
            continue;
        }

        const QStringList columns = parseCsvLine(line);
        if (wordColumn >= columns.size() || translationColumn >= columns.size()) {
            continue;
        }

        const QString word = columns[wordColumn].trimmed();
        const QString translation = columns[translationColumn].trimmed();
        const QString phonetic = (phoneticColumn >= 0 && phoneticColumn < columns.size())
                                     ? columns[phoneticColumn].trimmed()
                                     : QString();

        if (word.isEmpty() || translation.isEmpty()) {
            continue;
        }

        query.bindValue(0, word);
        query.bindValue(1, phonetic);
        query.bindValue(2, translation);

        if (!query.exec()) {
            db.rollback();
            lastError_ = query.lastError().text();
            return false;
        }

        if (query.numRowsAffected() > 0) {
            ++importedCount;
        }
    }

    if (!db.commit()) {
        lastError_ = db.lastError().text();
        return false;
    }

    return true;
}

int DatabaseManager::unlearnedCount() const {
    if (!ensureDatabaseOpen()) {
        return 0;
    }

    QSqlQuery query(database());
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM words WHERE status = 0"))) {
        lastError_ = query.lastError().text();
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

int DatabaseManager::dueReviewCount(const QDateTime &now) const {
    if (!ensureDatabaseOpen()) {
        return 0;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM words "
        "WHERE status != 0 AND next_review IS NOT NULL AND next_review <= ?"));
    query.bindValue(0, now.toString(kDateTimeFormat));

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return 0;
    }

    if (query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

QVector<WordItem> DatabaseManager::fetchLearningBatch(int limit) const {
    QVector<WordItem> items;
    if (!ensureDatabaseOpen()) {
        return items;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT id, word, phonetic, translation, ease_factor, interval, next_review, status "
        "FROM words WHERE status = 0 ORDER BY id LIMIT ?"));
    query.bindValue(0, qMax(1, limit));

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return items;
    }

    while (query.next()) {
        items.push_back(readWordFromQuery(query));
    }

    return items;
}

QVector<WordItem> DatabaseManager::fetchReviewBatch(const QDateTime &now, int limit) const {
    QVector<WordItem> items;
    if (!ensureDatabaseOpen()) {
        return items;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT id, word, phonetic, translation, ease_factor, interval, next_review, status "
        "FROM words "
        "WHERE status != 0 AND next_review IS NOT NULL AND next_review <= ? "
        "ORDER BY next_review ASC, id ASC LIMIT ?"));
    query.bindValue(0, now.toString(kDateTimeFormat));
    query.bindValue(1, qMax(1, limit));

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return items;
    }

    while (query.next()) {
        items.push_back(readWordFromQuery(query));
    }

    return items;
}

bool DatabaseManager::saveSessionProgress(const QString &mode, const QVector<WordItem> &words, int currentIndex) {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    if (mode.trimmed().isEmpty()) {
        lastError_ = QStringLiteral("会话模式不能为空");
        return false;
    }

    if (words.isEmpty() || currentIndex >= words.size()) {
        return clearSessionProgress(mode);
    }

    const int safeIndex = qMax(0, currentIndex);
    const QString wordIds = joinWordIds(words);

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "INSERT INTO session_progress(mode, word_ids, current_index, updated_at) "
        "VALUES(?, ?, ?, ?) "
        "ON CONFLICT(mode) DO UPDATE SET "
        "word_ids = excluded.word_ids, "
        "current_index = excluded.current_index, "
        "updated_at = excluded.updated_at"));
    query.bindValue(0, mode);
    query.bindValue(1, wordIds);
    query.bindValue(2, safeIndex);
    query.bindValue(3, QDateTime::currentDateTime().toString(kDateTimeFormat));

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::loadSessionProgress(const QString &mode, QVector<WordItem> &words, int &currentIndex) {
    words.clear();
    currentIndex = 0;

    if (!ensureDatabaseOpen()) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT word_ids, current_index FROM session_progress WHERE mode = ?"));
    query.bindValue(0, mode);

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    if (!query.next()) {
        return false;
    }

    const QVector<int> ids = splitWordIds(query.value(0).toString());
    currentIndex = query.value(1).toInt();

    words.reserve(ids.size());
    for (int id : ids) {
        WordItem word;
        if (queryWordById(id, word)) {
            words.push_back(word);
        }
    }

    if (words.isEmpty()) {
        clearSessionProgress(mode);
        currentIndex = 0;
        return false;
    }

    if (currentIndex < 0) {
        currentIndex = 0;
    }
    if (currentIndex >= words.size()) {
        clearSessionProgress(mode);
        words.clear();
        currentIndex = 0;
        return false;
    }

    return true;
}

bool DatabaseManager::clearSessionProgress(const QString &mode) {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral("DELETE FROM session_progress WHERE mode = ?"));
    query.bindValue(0, mode);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::hasSessionProgress(const QString &mode) const {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM session_progress WHERE mode = ?"));
    query.bindValue(0, mode);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    if (!query.next()) {
        return false;
    }
    return query.value(0).toInt() > 0;
}

SpellingResult DatabaseManager::evaluateSpelling(const QString &input, const QString &target) const {
    const QString normalizedInput = normalizedToken(input);
    const QString normalizedTarget = normalizedToken(target);

    if (normalizedTarget.isEmpty()) {
        return normalizedInput.isEmpty() ? SpellingResult::Mastered : SpellingResult::Unfamiliar;
    }

    if (normalizedInput == normalizedTarget) {
        return SpellingResult::Mastered;
    }

    if (normalizedInput.isEmpty()) {
        return SpellingResult::Unfamiliar;
    }

    const int distance = levenshteinDistance(normalizedInput, normalizedTarget);
    if (distance >= 1 && distance <= 3) {
        return SpellingResult::Blurry;
    }

    return SpellingResult::Unfamiliar;
}

bool DatabaseManager::applyReviewResult(int wordId,
                                        SpellingResult result,
                                        bool skipped,
                                        const QDateTime &now) {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    WordItem current;
    if (!queryWordById(wordId, current)) {
        return false;
    }

    SpellingResult effectiveResult = result;
    if (skipped) {
        effectiveResult = SpellingResult::Unfamiliar;
    }

    int newInterval = 0;
    double newEaseFactor = current.easeFactor;

    switch (effectiveResult) {
    case SpellingResult::Mastered:
        newInterval = nextIntervalForMastered(current.interval);
        newEaseFactor = qMin(3.0, current.easeFactor + 0.1);
        break;
    case SpellingResult::Blurry:
        newInterval = nextIntervalForBlurry(current.interval);
        newEaseFactor = qMax(1.3, current.easeFactor - 0.05);
        break;
    case SpellingResult::Unfamiliar:
        newInterval = nextIntervalForUnfamiliar();
        newEaseFactor = qMax(1.3, current.easeFactor - 0.2);
        break;
    }

    const QDateTime nextReviewTime = now.addDays(newInterval);
    const int newStatus = (newInterval >= 30) ? 2 : 1;

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "UPDATE words SET ease_factor = ?, interval = ?, next_review = ?, status = ? WHERE id = ?"));
    query.bindValue(0, newEaseFactor);
    query.bindValue(1, newInterval);
    query.bindValue(2, nextReviewTime.toString(kDateTimeFormat));
    query.bindValue(3, newStatus);
    query.bindValue(4, wordId);

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }

    return true;
}

QString DatabaseManager::lastError() const {
    return lastError_;
}

bool DatabaseManager::ensureDatabaseOpen() const {
    if (!QSqlDatabase::contains(connectionName_)) {
        lastError_ = QStringLiteral("数据库连接不存在");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName_);
    if (!db.isOpen()) {
        lastError_ = QStringLiteral("数据库未打开");
        return false;
    }

    return true;
}

QSqlDatabase DatabaseManager::database() const {
    return QSqlDatabase::database(connectionName_);
}

QStringList DatabaseManager::parseCsvLine(const QString &line) {
    QStringList values;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);

        if (ch == '"') {
            // 双引号内部的两个连续双引号表示转义。
            if (inQuotes && i + 1 < line.size() && line.at(i + 1) == '"') {
                current.append('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }

        if (ch == ',' && !inQuotes) {
            values.push_back(current);
            current.clear();
            continue;
        }

        current.append(ch);
    }

    values.push_back(current);
    return values;
}

QString DatabaseManager::normalizedToken(const QString &text) {
    return text.trimmed().toLower().simplified();
}

int DatabaseManager::levenshteinDistance(const QString &a, const QString &b) const {
    const int n = a.size();
    const int m = b.size();

    if (n == 0) {
        return m;
    }
    if (m == 0) {
        return n;
    }

    QVector<int> prev(m + 1);
    QVector<int> curr(m + 1);

    for (int j = 0; j <= m; ++j) {
        prev[j] = j;
    }

    for (int i = 1; i <= n; ++i) {
        curr[0] = i;
        for (int j = 1; j <= m; ++j) {
            const int cost = (a.at(i - 1) == b.at(j - 1)) ? 0 : 1;
            curr[j] = qMin(qMin(curr[j - 1] + 1, prev[j] + 1), prev[j - 1] + cost);
        }
        prev = curr;
    }

    return prev[m];
}

int DatabaseManager::nextIntervalForMastered(int currentInterval) const {
    const QVector<int> ladder = reviewLadder();

    if (currentInterval <= 0) {
        return ladder.first();
    }

    for (int i = 0; i < ladder.size(); ++i) {
        if (ladder[i] == currentInterval) {
            if (i + 1 < ladder.size()) {
                return ladder[i + 1];
            }
            return ladder.last();
        }

        if (ladder[i] > currentInterval) {
            return ladder[i];
        }
    }

    return ladder.last();
}

int DatabaseManager::nextIntervalForBlurry(int currentInterval) const {
    const QVector<int> ladder = reviewLadder();

    if (currentInterval <= ladder.first()) {
        return ladder.first();
    }

    for (int i = 0; i < ladder.size(); ++i) {
        if (ladder[i] == currentInterval) {
            return ladder[qMax(0, i - 1)];
        }

        if (ladder[i] > currentInterval) {
            return ladder[qMax(0, i - 1)];
        }
    }

    return ladder[ladder.size() - 2];
}

int DatabaseManager::nextIntervalForUnfamiliar() const {
    return reviewLadder().first();
}

bool DatabaseManager::queryWordById(int wordId, WordItem &item) const {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT id, word, phonetic, translation, ease_factor, interval, next_review, status "
        "FROM words WHERE id = ?"));
    query.bindValue(0, wordId);

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }

    if (!query.next()) {
        lastError_ = QStringLiteral("未找到指定单词 id=%1").arg(wordId);
        return false;
    }

    item = readWordFromQuery(query);
    return true;
}
