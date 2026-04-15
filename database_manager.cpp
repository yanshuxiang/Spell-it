#include "database_manager.h"

#include <QFile>
#include <QFileInfo>
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

QString trimmedBookNameFromFilePath(const QString &csvPath) {
    QString name = QFileInfo(csvPath).completeBaseName().trimmed();
    if (name.isEmpty()) {
        name = QStringLiteral("词书");
    }
    return name.left(8);
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

// 判断一段 CSV 文本中是否存在未闭合的引号，用于支持跨行字段。
bool csvRecordHasUnclosedQuote(const QString &text) {
    bool inQuotes = false;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) != QChar('"')) {
            continue;
        }

        if (inQuotes && i + 1 < text.size() && text.at(i + 1) == QChar('"')) {
            ++i; // 跳过转义双引号 ""
            continue;
        }
        inQuotes = !inQuotes;
    }
    return inQuotes;
}

// 读取一条完整 CSV 记录：若字段被引号包裹且包含换行，则会自动拼接后续行。
QString readCsvRecord(QTextStream &in) {
    if (in.atEnd()) {
        return QString();
    }

    QString record = in.readLine();
    while (csvRecordHasUnclosedQuote(record) && !in.atEnd()) {
        record += QChar('\n');
        record += in.readLine();
    }
    return record;
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
        "status INTEGER DEFAULT 0,"
        "skip_forever INTEGER DEFAULT 0"
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

    const QString createBooksSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS word_books ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL UNIQUE,"
        "is_active INTEGER NOT NULL DEFAULT 0,"
        "created_at TEXT"
        ")");
    if (!query.exec(createBooksSql)) {
        lastError_ = query.lastError().text();
        return false;
    }

    bool hasBookIdColumn = false;
    bool hasSkipForeverColumn = false;
    QSqlQuery tableInfo(database());
    if (!tableInfo.exec(QStringLiteral("PRAGMA table_info(words)"))) {
        lastError_ = tableInfo.lastError().text();
        return false;
    }
    while (tableInfo.next()) {
        if (tableInfo.value(1).toString() == QStringLiteral("book_id")) {
            hasBookIdColumn = true;
        }
        if (tableInfo.value(1).toString() == QStringLiteral("skip_forever")) {
            hasSkipForeverColumn = true;
        }
    }
    if (!hasBookIdColumn) {
        if (!query.exec(QStringLiteral("ALTER TABLE words ADD COLUMN book_id INTEGER"))) {
            lastError_ = query.lastError().text();
            return false;
        }
    }
    if (!hasSkipForeverColumn) {
        if (!query.exec(QStringLiteral("ALTER TABLE words ADD COLUMN skip_forever INTEGER DEFAULT 0"))) {
            lastError_ = query.lastError().text();
            return false;
        }
        if (!query.exec(QStringLiteral("UPDATE words SET skip_forever = 0 WHERE skip_forever IS NULL"))) {
            lastError_ = query.lastError().text();
            return false;
        }
    }

    if (!query.exec(QStringLiteral(
            "INSERT INTO word_books(name, is_active, created_at) "
            "SELECT '默认词书', 1, datetime('now','localtime') "
            "WHERE NOT EXISTS (SELECT 1 FROM word_books)"))) {
        lastError_ = query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral(
            "UPDATE word_books "
            "SET is_active = 1 "
            "WHERE id = (SELECT id FROM word_books ORDER BY is_active DESC, id ASC LIMIT 1) "
            "AND NOT EXISTS (SELECT 1 FROM word_books WHERE is_active = 1)"))) {
        lastError_ = query.lastError().text();
        return false;
    }

    const int activeBookId = activeWordBookIdInternal();
    if (activeBookId > 0) {
        QSqlQuery fillBookId(database());
        fillBookId.prepare(QStringLiteral(
            "UPDATE words SET book_id = ? "
            "WHERE book_id IS NULL OR book_id <= 0"));
        fillBookId.bindValue(0, activeBookId);
        if (!fillBookId.exec()) {
            lastError_ = fillBookId.lastError().text();
            return false;
        }
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_book ON words(book_id)"))) {
        lastError_ = query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_skip_forever ON words(skip_forever)"))) {
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

    const QString createLogsSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS learning_logs ("
        "log_date TEXT PRIMARY KEY,"
        "learning_count INTEGER DEFAULT 0,"
        "review_count INTEGER DEFAULT 0,"
        "study_seconds INTEGER DEFAULT 0"
        ")");
    if (!query.exec(createLogsSql)) {
        lastError_ = query.lastError().text();
        return false;
    }

    bool hasStudySecondsColumn = false;
    QSqlQuery logsInfo(database());
    if (!logsInfo.exec(QStringLiteral("PRAGMA table_info(learning_logs)"))) {
        lastError_ = logsInfo.lastError().text();
        return false;
    }
    while (logsInfo.next()) {
        if (logsInfo.value(1).toString() == QStringLiteral("study_seconds")) {
            hasStudySecondsColumn = true;
            break;
        }
    }
    if (!hasStudySecondsColumn) {
        if (!query.exec(QStringLiteral("ALTER TABLE learning_logs ADD COLUMN study_seconds INTEGER DEFAULT 0"))) {
            lastError_ = query.lastError().text();
            return false;
        }
    }

    const QString createStatsSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS word_spelling_stats ("
        "word_id INTEGER PRIMARY KEY,"
        "attempt_count INTEGER NOT NULL DEFAULT 0,"
        "correct_count INTEGER NOT NULL DEFAULT 0,"
        "updated_at TEXT"
        ")");
    if (!query.exec(createStatsSql)) {
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

    headers = parseCsvLine(readCsvRecord(in));
    if (!headers.isEmpty()) {
        headers[0].remove(QChar(0xFEFF)); // 去除 UTF-8 BOM
    }

    int count = 0;
    while (count < sampleCount) {
        const QString record = readCsvRecord(in);
        if (record.isNull()) {
            break;
        }
        if (record.trimmed().isEmpty()) {
            continue;
        }
        previewRows.push_back(parseCsvLine(record));
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
    parseCsvLine(readCsvRecord(in));

    QSqlDatabase db = database();
    if (!db.transaction()) {
        lastError_ = db.lastError().text();
        return false;
    }

    const QString baseName = trimmedBookNameFromFilePath(csvPath);
    QString bookName = baseName;
    int suffix = 2;
    while (true) {
        QSqlQuery existsQuery(db);
        existsQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM word_books WHERE name = ?"));
        existsQuery.bindValue(0, bookName);
        if (!existsQuery.exec() || !existsQuery.next()) {
            db.rollback();
            lastError_ = existsQuery.lastError().text();
            return false;
        }
        if (existsQuery.value(0).toInt() == 0) {
            break;
        }
        const QString suffixText = QString::number(suffix++);
        int keepCount = 8 - suffixText.size();
        if (keepCount < 1) {
            keepCount = 1;
        }
        bookName = baseName.left(keepCount) + suffixText;
    }

    QSqlQuery updateActiveQuery(db);
    if (!updateActiveQuery.exec(QStringLiteral("UPDATE word_books SET is_active = 0"))) {
        db.rollback();
        lastError_ = updateActiveQuery.lastError().text();
        return false;
    }

    QSqlQuery createBookQuery(db);
    createBookQuery.prepare(QStringLiteral(
        "INSERT INTO word_books(name, is_active, created_at) VALUES(?, 1, datetime('now','localtime'))"));
    createBookQuery.bindValue(0, bookName);
    if (!createBookQuery.exec()) {
        db.rollback();
        lastError_ = createBookQuery.lastError().text();
        return false;
    }
    const int bookId = createBookQuery.lastInsertId().toInt();

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO words "
        "(word, phonetic, translation, ease_factor, interval, next_review, status, skip_forever, book_id) "
        "VALUES (?, ?, ?, 2.5, 0, NULL, 0, 0, ?)"));

    while (true) {
        const QString record = readCsvRecord(in);
        if (record.isNull()) {
            break;
        }
        if (record.trimmed().isEmpty()) {
            continue;
        }

        const QStringList columns = parseCsvLine(record);
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
        query.bindValue(3, bookId);

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

QVector<WordBookItem> DatabaseManager::fetchWordBooks() const {
    QVector<WordBookItem> books;
    if (!ensureDatabaseOpen()) {
        return books;
    }

    QSqlQuery query(database());
    if (!query.exec(QStringLiteral(
            "SELECT b.id, b.name, b.is_active, "
            "COUNT(w.id) AS word_count, "
            "SUM(CASE WHEN w.status != 0 THEN 1 ELSE 0 END) AS learned_count "
            "FROM word_books b "
            "LEFT JOIN words w ON w.book_id = b.id "
            "GROUP BY b.id, b.name, b.is_active "
            "ORDER BY b.is_active DESC, b.id ASC"))) {
        lastError_ = query.lastError().text();
        return books;
    }

    while (query.next()) {
        WordBookItem item;
        item.id = query.value(0).toInt();
        item.name = query.value(1).toString();
        item.isActive = query.value(2).toInt() == 1;
        item.wordCount = query.value(3).toInt();
        item.learnedCount = query.value(4).toInt();
        books.push_back(item);
    }

    return books;
}

bool DatabaseManager::setActiveWordBook(int bookId) {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    QSqlDatabase db = database();
    if (!db.transaction()) {
        lastError_ = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM word_books WHERE id = ?"));
    query.bindValue(0, bookId);
    if (!query.exec() || !query.next()) {
        db.rollback();
        lastError_ = query.lastError().text();
        return false;
    }
    if (query.value(0).toInt() <= 0) {
        db.rollback();
        lastError_ = QStringLiteral("词书不存在");
        return false;
    }

    if (!query.exec(QStringLiteral("UPDATE word_books SET is_active = 0"))) {
        db.rollback();
        lastError_ = query.lastError().text();
        return false;
    }

    query.prepare(QStringLiteral("UPDATE word_books SET is_active = 1 WHERE id = ?"));
    query.bindValue(0, bookId);
    if (!query.exec()) {
        db.rollback();
        lastError_ = query.lastError().text();
        return false;
    }

    if (!db.commit()) {
        lastError_ = db.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::deleteWordBook(int bookId) {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    QSqlDatabase db = database();
    if (!db.transaction()) {
        lastError_ = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM word_books WHERE id = ?"));
    query.bindValue(0, bookId);
    if (!query.exec() || !query.next()) {
        db.rollback();
        lastError_ = query.lastError().text();
        return false;
    }
    if (query.value(0).toInt() <= 0) {
        db.rollback();
        lastError_ = QStringLiteral("词书不存在");
        return false;
    }

    query.prepare(QStringLiteral("DELETE FROM words WHERE book_id = ?"));
    query.bindValue(0, bookId);
    if (!query.exec()) {
        db.rollback();
        lastError_ = query.lastError().text();
        return false;
    }

    query.prepare(QStringLiteral("DELETE FROM word_books WHERE id = ?"));
    query.bindValue(0, bookId);
    if (!query.exec()) {
        db.rollback();
        lastError_ = query.lastError().text();
        return false;
    }

    int fallbackActiveId = -1;
    if (!query.exec(QStringLiteral("SELECT id FROM word_books WHERE is_active = 1 ORDER BY id ASC LIMIT 1"))) {
        db.rollback();
        lastError_ = query.lastError().text();
        return false;
    }
    if (query.next()) {
        fallbackActiveId = query.value(0).toInt();
    }
    if (fallbackActiveId <= 0) {
        if (!query.exec(QStringLiteral("SELECT id FROM word_books ORDER BY id ASC LIMIT 1"))) {
            db.rollback();
            lastError_ = query.lastError().text();
            return false;
        }
        if (query.next()) {
            fallbackActiveId = query.value(0).toInt();
        }
    }
    if (fallbackActiveId > 0) {
        if (!query.exec(QStringLiteral("UPDATE word_books SET is_active = 0"))) {
            db.rollback();
            lastError_ = query.lastError().text();
            return false;
        }
        query.prepare(QStringLiteral("UPDATE word_books SET is_active = 1 WHERE id = ?"));
        query.bindValue(0, fallbackActiveId);
        if (!query.exec()) {
            db.rollback();
            lastError_ = query.lastError().text();
            return false;
        }
    }

    if (!db.commit()) {
        lastError_ = db.lastError().text();
        return false;
    }
    return true;
}

int DatabaseManager::activeWordBookId() const {
    return activeWordBookIdInternal();
}

int DatabaseManager::unlearnedCount() const {
    if (!ensureDatabaseOpen()) {
        return 0;
    }

    const int activeBookId = activeWordBookIdInternal();
    if (activeBookId <= 0) {
        return 0;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM words WHERE status = 0 AND skip_forever = 0 AND book_id = ?"));
    query.bindValue(0, activeBookId);
    if (!query.exec()) {
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

    const int activeBookId = activeWordBookIdInternal();
    if (activeBookId <= 0) {
        return 0;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM words "
        "WHERE status != 0 AND skip_forever = 0 AND next_review IS NOT NULL "
        "AND date(next_review) <= date(?) AND book_id = ?"));
    query.bindValue(0, now.toString(kDateTimeFormat));
    query.bindValue(1, activeBookId);

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

    const int activeBookId = activeWordBookIdInternal();
    if (activeBookId <= 0) {
        return items;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT id, word, phonetic, translation, ease_factor, interval, next_review, status, skip_forever "
        "FROM words WHERE status = 0 AND skip_forever = 0 AND book_id = ? ORDER BY id LIMIT ?"));
    query.bindValue(0, activeBookId);
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

QVector<WordItem> DatabaseManager::fetchReviewBatch(const QDateTime &now, int limit) const {
    QVector<WordItem> items;
    if (!ensureDatabaseOpen()) {
        return items;
    }

    const int activeBookId = activeWordBookIdInternal();
    if (activeBookId <= 0) {
        return items;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT id, word, phonetic, translation, ease_factor, interval, next_review, status, skip_forever "
        "FROM words "
        "WHERE status != 0 AND skip_forever = 0 AND next_review IS NOT NULL "
        "AND date(next_review) <= date(?) AND book_id = ? "
        "ORDER BY next_review ASC, id ASC LIMIT ?"));
    query.bindValue(0, now.toString(kDateTimeFormat));
    query.bindValue(1, activeBookId);
    query.bindValue(2, qMax(1, limit));

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return items;
    }

    while (query.next()) {
        items.push_back(readWordFromQuery(query));
    }

    return items;
}

QVector<WordItem> DatabaseManager::fetchWordsForBook(int bookId) const {
    QVector<WordItem> items;
    if (!ensureDatabaseOpen()) {
        return items;
    }

    int targetBookId = bookId;
    if (targetBookId <= 0) {
        targetBookId = activeWordBookIdInternal();
    }
    if (targetBookId <= 0) {
        return items;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT id, word, phonetic, translation, ease_factor, interval, next_review, status, skip_forever "
        "FROM words WHERE skip_forever = 0 AND book_id = ? ORDER BY id"));
    query.bindValue(0, targetBookId);

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

bool DatabaseManager::recordSpellingAttempt(int wordId, bool correct) {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    if (wordId <= 0) {
        lastError_ = QStringLiteral("无效的单词 ID");
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "INSERT INTO word_spelling_stats(word_id, attempt_count, correct_count, updated_at) "
        "VALUES(?, 1, ?, ?) "
        "ON CONFLICT(word_id) DO UPDATE SET "
        "attempt_count = attempt_count + 1, "
        "correct_count = correct_count + excluded.correct_count, "
        "updated_at = excluded.updated_at"));
    query.bindValue(0, wordId);
    query.bindValue(1, correct ? 1 : 0);
    query.bindValue(2, QDateTime::currentDateTime().toString(kDateTimeFormat));
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::fetchWordDebugStats(int wordId, WordDebugStats &stats) const {
    stats = WordDebugStats();
    if (!ensureDatabaseOpen()) {
        return false;
    }
    if (wordId <= 0) {
        lastError_ = QStringLiteral("无效的单词 ID");
        return false;
    }

    QSqlQuery wordQuery(database());
    wordQuery.prepare(QStringLiteral("SELECT next_review FROM words WHERE id = ?"));
    wordQuery.bindValue(0, wordId);
    if (!wordQuery.exec()) {
        lastError_ = wordQuery.lastError().text();
        return false;
    }
    if (!wordQuery.next()) {
        lastError_ = QStringLiteral("找不到对应单词");
        return false;
    }
    const QString nextReviewText = wordQuery.value(0).toString();
    if (!nextReviewText.isEmpty()) {
        stats.nextReview = QDateTime::fromString(nextReviewText, kDateTimeFormat);
        if (!stats.nextReview.isValid()) {
            stats.nextReview = QDateTime::fromString(nextReviewText, Qt::ISODate);
        }
    }

    QSqlQuery statsQuery(database());
    statsQuery.prepare(QStringLiteral(
        "SELECT attempt_count, correct_count "
        "FROM word_spelling_stats WHERE word_id = ?"));
    statsQuery.bindValue(0, wordId);
    if (!statsQuery.exec()) {
        lastError_ = statsQuery.lastError().text();
        return false;
    }
    if (statsQuery.next()) {
        stats.attemptCount = statsQuery.value(0).toInt();
        stats.correctCount = statsQuery.value(1).toInt();
    }
    return true;
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

bool DatabaseManager::incrementDailyCount(bool isLearning, const QDate &date) {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    const QString dateStr = date.toString(Qt::ISODate);
    QSqlQuery query(database());

    query.prepare(QStringLiteral("INSERT OR IGNORE INTO learning_logs (log_date) VALUES (?)"));
    query.bindValue(0, dateStr);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }

    if (isLearning) {
        query.prepare(QStringLiteral("UPDATE learning_logs SET learning_count = learning_count + 1 WHERE log_date = ?"));
    } else {
        query.prepare(QStringLiteral("UPDATE learning_logs SET review_count = review_count + 1 WHERE log_date = ?"));
    }
    query.bindValue(0, dateStr);

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::addDailyStudySeconds(int seconds, const QDate &date) {
    if (!ensureDatabaseOpen()) {
        return false;
    }
    if (seconds <= 0) {
        return true;
    }

    const QString dateStr = date.toString(Qt::ISODate);
    QSqlQuery query(database());

    query.prepare(QStringLiteral("INSERT OR IGNORE INTO learning_logs (log_date) VALUES (?)"));
    query.bindValue(0, dateStr);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }

    query.prepare(QStringLiteral("UPDATE learning_logs SET study_seconds = study_seconds + ? WHERE log_date = ?"));
    query.bindValue(0, seconds);
    query.bindValue(1, dateStr);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}

QVector<DatabaseManager::DailyLog> DatabaseManager::fetchWeeklyLogs(const QDate &endDate) const {
    QVector<DailyLog> logs;
    if (!ensureDatabaseOpen()) {
        return logs;
    }

    const QDate startDate = endDate.addDays(-6);
    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT log_date, learning_count, review_count, study_seconds "
        "FROM learning_logs "
        "WHERE log_date >= ? AND log_date <= ? "
        "ORDER BY log_date ASC"));
    query.bindValue(0, startDate.toString(Qt::ISODate));
    query.bindValue(1, endDate.toString(Qt::ISODate));

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return logs;
    }

    QMap<QString, DailyLog> logMap;
    while (query.next()) {
        DailyLog log;
        log.date = query.value(0).toString();
        log.learningCount = query.value(1).toInt();
        log.reviewCount = query.value(2).toInt();
        const int seconds = query.value(3).toInt();
        log.studyMinutes = seconds > 0 ? (seconds + 59) / 60 : 0;
        logMap.insert(log.date, log);
    }

    for (int i = 0; i < 7; ++i) {
        const QDate d = startDate.addDays(i);
        const QString dStr = d.toString(Qt::ISODate);
        if (logMap.contains(dStr)) {
            logs.push_back(logMap.value(dStr));
        } else {
            logs.push_back({dStr, 0, 0, 0});
        }
    }

    return logs;
}

bool DatabaseManager::reconcileFirstDayDailyLog(const QDate &date) {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    const QString dateStr = date.toString(Qt::ISODate);
    QSqlQuery query(database());

    query.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(learning_count + review_count), 0) "
        "FROM learning_logs WHERE log_date < ?"));
    query.bindValue(0, dateStr);
    if (!query.exec() || !query.next()) {
        lastError_ = query.lastError().text();
        return false;
    }

    // 仅在首日使用修正，避免影响已有多日数据。
    const int historicalTotal = query.value(0).toInt();
    if (historicalTotal > 0) {
        return true;
    }

    query.prepare(QStringLiteral("INSERT OR IGNORE INTO learning_logs (log_date) VALUES (?)"));
    query.bindValue(0, dateStr);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }

    query.prepare(QStringLiteral("SELECT learning_count, review_count FROM learning_logs WHERE log_date = ?"));
    query.bindValue(0, dateStr);
    if (!query.exec() || !query.next()) {
        lastError_ = query.lastError().text();
        return false;
    }

    const int todayLearning = query.value(0).toInt();
    const int todayReview = query.value(1).toInt();
    if (todayReview > 0) {
        return true;
    }

    query.prepare(QStringLiteral("SELECT COUNT(*) FROM words WHERE status != 0"));
    if (!query.exec() || !query.next()) {
        lastError_ = query.lastError().text();
        return false;
    }

    const int reviewedTotal = query.value(0).toInt();
    if (reviewedTotal <= todayLearning) {
        return true;
    }

    query.prepare(QStringLiteral("UPDATE learning_logs SET learning_count = ? WHERE log_date = ?"));
    query.bindValue(0, reviewedTotal);
    query.bindValue(1, dateStr);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }

    return true;
}

QString DatabaseManager::lastError() const {
    return lastError_;
}

int DatabaseManager::activeWordBookIdInternal() const {
    if (!ensureDatabaseOpen()) {
        return -1;
    }

    QSqlQuery query(database());
    if (!query.exec(QStringLiteral(
            "SELECT id FROM word_books ORDER BY is_active DESC, id ASC LIMIT 1"))) {
        lastError_ = query.lastError().text();
        return -1;
    }
    if (!query.next()) {
        return -1;
    }
    return query.value(0).toInt();
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

    auto pushField = [&values](QString field) {
        if (field.endsWith(QChar('\r'))) {
            field.chop(1);
        }
        values.push_back(field);
    };

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
            pushField(current);
            current.clear();
            continue;
        }

        current.append(ch);
    }

    pushField(current);
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
        "SELECT id, word, phonetic, translation, ease_factor, interval, next_review, status, skip_forever "
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

bool DatabaseManager::setWordSkipForever(int wordId, bool skipForever) {
    if (!ensureDatabaseOpen()) {
        return false;
    }

    if (wordId <= 0) {
        lastError_ = QStringLiteral("无效的单词 ID");
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral("UPDATE words SET skip_forever = ? WHERE id = ?"));
    query.bindValue(0, skipForever ? 1 : 0);
    query.bindValue(1, wordId);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}
