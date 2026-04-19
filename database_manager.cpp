#include "database_manager.h"

#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>
#include <QUuid>
#include <QtGlobal>

namespace {
constexpr const char *kDateTimeFormat = "yyyy-MM-dd HH:mm:ss";
constexpr const char *kTrainingTypeSpelling = "spelling";
constexpr const char *kTrainingTypeCountability = "countability";
constexpr const char *kTrainingTypePolysemy = "polysemy";
constexpr const char *kSettingCsvPromptHandled = "csv_prompt_handled";
constexpr const char *kDefaultTrainingTypes[] = {
    kTrainingTypeSpelling,
    kTrainingTypeCountability,
    kTrainingTypePolysemy,
};

bool isKnownTrainingType(const QString &trainingType) {
    const QString type = trainingType.trimmed().toLower();
    return type == QString::fromLatin1(kTrainingTypeSpelling)
        || type == QString::fromLatin1(kTrainingTypeCountability)
        || type == QString::fromLatin1(kTrainingTypePolysemy);
}

bool isCountabilityLabelValid(const QString &label) {
    const QString normalized = label.trimmed().toUpper();
    return normalized == QStringLiteral("C")
        || normalized == QStringLiteral("U")
        || normalized == QStringLiteral("B");
}

QString learningEligibilityClauseForType(const QString &trainingType) {
    if (trainingType == QString::fromLatin1(kTrainingTypeCountability)) {
        return QStringLiteral(
            " AND upper(trim(COALESCE(w.countability_label, ''))) IN ('C', 'U', 'B') ");
    }
    if (trainingType == QString::fromLatin1(kTrainingTypePolysemy)) {
        return QStringLiteral(
            " AND length(trim(COALESCE(w.polysemy_json, ''))) > 0 ");
    }
    return QString();
}

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

QString countabilityHashFor(const QString &word, const QString &label) {
    const QString normalizedWord = word.trimmed().toLower().simplified();
    const QString normalizedLabel = label.trimmed().toUpper();
    const QString payload = normalizedWord + QStringLiteral("|") + normalizedLabel;
    return QString::fromLatin1(
        QCryptographicHash::hash(payload.toUtf8(), QCryptographicHash::Sha256).toHex());
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
    item.partOfSpeech = query.value("part_of_speech").toString();
    if (query.record().indexOf("countability_label") >= 0) {
        item.countabilityLabel = query.value("countability_label").toString().trimmed().toUpper();
    }
    if (query.record().indexOf("countability_plural") >= 0) {
        item.countabilityPlural = query.value("countability_plural").toString().trimmed();
    }
    if (query.record().indexOf("countability_notes") >= 0) {
        item.countabilityNotes = query.value("countability_notes").toString().trimmed();
    }
    if (query.record().indexOf("polysemy_json") >= 0) {
        item.polysemyJson = query.value("polysemy_json").toString().trimmed();
    }
    item.easeFactor = query.value("ease_factor").toDouble();
    item.interval = query.value("interval").toInt();
    item.status = query.value("status").toInt();
    item.skipForever = query.value("skip_forever").toInt() == 1;

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
        "part_of_speech TEXT,"
        "countability_label TEXT,"
        "countability_plural TEXT,"
        "countability_notes TEXT,"
        "polysemy_json TEXT,"
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
    bool hasPartOfSpeechColumn = false;
    bool hasCountabilityLabelColumn = false;
    bool hasCountabilityPluralColumn = false;
    bool hasCountabilityNotesColumn = false;
    bool hasPolysemyJsonColumn = false;
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
        if (tableInfo.value(1).toString() == QStringLiteral("part_of_speech")) {
            hasPartOfSpeechColumn = true;
        }
        if (tableInfo.value(1).toString() == QStringLiteral("countability_label")) {
            hasCountabilityLabelColumn = true;
        }
        if (tableInfo.value(1).toString() == QStringLiteral("countability_plural")) {
            hasCountabilityPluralColumn = true;
        }
        if (tableInfo.value(1).toString() == QStringLiteral("countability_notes")) {
            hasCountabilityNotesColumn = true;
        }
        if (tableInfo.value(1).toString() == QStringLiteral("polysemy_json")) {
            hasPolysemyJsonColumn = true;
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
    }
    if (!hasPartOfSpeechColumn) {
        if (!query.exec(QStringLiteral("ALTER TABLE words ADD COLUMN part_of_speech TEXT"))) {
            lastError_ = query.lastError().text();
            return false;
        }
    }
    if (!hasCountabilityLabelColumn) {
        if (!query.exec(QStringLiteral("ALTER TABLE words ADD COLUMN countability_label TEXT"))) {
            lastError_ = query.lastError().text();
            return false;
        }
    }
    if (!hasCountabilityPluralColumn) {
        if (!query.exec(QStringLiteral("ALTER TABLE words ADD COLUMN countability_plural TEXT"))) {
            lastError_ = query.lastError().text();
            return false;
        }
    }
    if (!hasCountabilityNotesColumn) {
        if (!query.exec(QStringLiteral("ALTER TABLE words ADD COLUMN countability_notes TEXT"))) {
            lastError_ = query.lastError().text();
            return false;
        }
    }
    if (!hasPolysemyJsonColumn) {
        if (!query.exec(QStringLiteral("ALTER TABLE words ADD COLUMN polysemy_json TEXT"))) {
            lastError_ = query.lastError().text();
            return false;
        }
    }
    // 确保至少有一个活跃词书（在多词书场景下由用户切换）
    if (!query.exec(QStringLiteral(
            "UPDATE word_books "
            "SET is_active = 1 "
            "WHERE id = (SELECT id FROM word_books ORDER BY is_active DESC, id ASC LIMIT 1) "
            "AND NOT EXISTS (SELECT 1 FROM word_books WHERE is_active = 1)"))) {
        lastError_ = query.lastError().text();
        return false;
    }
    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_skip_forever ON words(skip_forever)"))) {
        lastError_ = query.lastError().text();
        return false;
    }
    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_words_polysemy ON words(polysemy_json)"))) {
        lastError_ = query.lastError().text();
        return false;
    }

    const QString createBookWordsSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS book_words ("
        "book_id INTEGER NOT NULL,"
        "word_id INTEGER NOT NULL,"
        "PRIMARY KEY(book_id, word_id)"
        ")");
    if (!query.exec(createBookWordsSql)) {
        lastError_ = query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_book_words_book ON book_words(book_id)"))) {
        lastError_ = query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_book_words_word ON book_words(word_id)"))) {
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
        "cb_learning_count INTEGER DEFAULT 0,"
        "cb_review_count INTEGER DEFAULT 0,"
        "study_seconds INTEGER DEFAULT 0,"
        "spelling_seconds INTEGER DEFAULT 0,"
        "cb_seconds INTEGER DEFAULT 0"
        ")");
    if (!query.exec(createLogsSql)) {
        lastError_ = query.lastError().text();
        return false;
    }

    QStringList needed = {
        QStringLiteral("cb_learning_count"),
        QStringLiteral("cb_review_count"),
        QStringLiteral("spelling_seconds"),
        QStringLiteral("cb_seconds")
    };
    for (const QString &col : needed) {
        bool exists = false;
        QSqlQuery ci(database());
        ci.exec(QStringLiteral("PRAGMA table_info(learning_logs)"));
        while (ci.next()) {
            if (ci.value(1).toString() == col) { exists = true; break; }
        }
        if (!exists) {
            query.exec(QStringLiteral("ALTER TABLE learning_logs ADD COLUMN %1 INTEGER DEFAULT 0").arg(col));
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

    const QString createTrainingProgressSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS training_progress ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "word_id INTEGER NOT NULL,"
        "training_type TEXT NOT NULL,"
        "ease_factor REAL DEFAULT 2.5,"
        "interval INTEGER DEFAULT 0,"
        "next_review TEXT,"
        "status INTEGER DEFAULT 0,"
        "correct_count INTEGER DEFAULT 0,"
        "wrong_count INTEGER DEFAULT 0,"
        "updated_at TEXT,"
        "UNIQUE(word_id, training_type)"
        ")");
    if (!query.exec(createTrainingProgressSql)) {
        lastError_ = query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_training_progress_type_review "
            "ON training_progress(training_type, next_review)"))) {
        lastError_ = query.lastError().text();
        return false;
    }

    const QString createTrainingConfigsSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS training_configs ("
        "training_type TEXT PRIMARY KEY,"
        "active_book_id INTEGER,"
        "last_card_index INTEGER DEFAULT 0,"
        "updated_at TEXT"
        ")");
    if (!query.exec(createTrainingConfigsSql)) {
        lastError_ = query.lastError().text();
        return false;
    }

    const QString createAppSettingsSql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS app_settings ("
        "setting_key TEXT PRIMARY KEY,"
        "setting_value TEXT,"
        "updated_at TEXT"
        ")");
    if (!query.exec(createAppSettingsSql)) {
        lastError_ = query.lastError().text();
        return false;
    }

    QSqlQuery seedTraining(database());
    seedTraining.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO training_configs(training_type, active_book_id, last_card_index, updated_at) "
        "VALUES(?, NULL, 0, datetime('now','localtime'))"));
    for (const char *trainingType : kDefaultTrainingTypes) {
        seedTraining.bindValue(0, QString::fromLatin1(trainingType));
        if (!seedTraining.exec()) {
            lastError_ = seedTraining.lastError().text();
            return false;
        }
    }

    int legacyActiveBookId = -1;
    QSqlQuery legacyActiveQuery(database());
    if (legacyActiveQuery.exec(QStringLiteral(
            "SELECT id FROM word_books WHERE is_active = 1 ORDER BY id ASC LIMIT 1"))
        && legacyActiveQuery.next()) {
        legacyActiveBookId = legacyActiveQuery.value(0).toInt();
    }
    if (legacyActiveBookId <= 0) {
        QSqlQuery fallbackBookQuery(database());
        if (fallbackBookQuery.exec(QStringLiteral("SELECT id FROM word_books ORDER BY id ASC LIMIT 1"))
            && fallbackBookQuery.next()) {
            legacyActiveBookId = fallbackBookQuery.value(0).toInt();
        }
    }
    if (legacyActiveBookId > 0) {
        QSqlQuery bindSpelling(database());
        bindSpelling.prepare(QStringLiteral(
            "UPDATE training_configs "
            "SET active_book_id = ?, updated_at = datetime('now','localtime') "
            "WHERE training_type = ? AND active_book_id IS NULL"));
        bindSpelling.bindValue(0, legacyActiveBookId);
        bindSpelling.bindValue(1, QString::fromLatin1(kTrainingTypeSpelling));
        if (!bindSpelling.exec()) {
            lastError_ = bindSpelling.lastError().text();
            return false;
        }
    }

    // 兼容旧库：把既有拼写调度回填到训练进度表（spelling）。
    QSqlQuery migrateSpellingProgress(database());
    migrateSpellingProgress.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO training_progress(word_id, training_type, ease_factor, interval, next_review, status, updated_at) "
        "SELECT id, ?, COALESCE(ease_factor, 2.5), COALESCE(interval, 0), next_review, COALESCE(status, 0), datetime('now','localtime') "
        "FROM words "
        "WHERE id IS NOT NULL"));
    migrateSpellingProgress.bindValue(0, QString::fromLatin1(kTrainingTypeSpelling));
    if (!migrateSpellingProgress.exec()) {
        lastError_ = migrateSpellingProgress.lastError().text();
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
                                    int countabilityColumn,
                                    int pluralColumn,
                                    int polysemyColumn,
                                    int notesColumn,
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

    int existingBookCount = 0;
    QSqlQuery countBooksQuery(db);
    if (!countBooksQuery.exec(QStringLiteral("SELECT COUNT(*) FROM word_books")) || !countBooksQuery.next()) {
        db.rollback();
        lastError_ = countBooksQuery.lastError().text();
        return false;
    }
    existingBookCount = countBooksQuery.value(0).toInt();
    const int shouldActive = existingBookCount <= 0 ? 1 : 0;

    QSqlQuery createBookQuery(db);
    createBookQuery.prepare(QStringLiteral(
        "INSERT INTO word_books(name, is_active, created_at) VALUES(?, ?, datetime('now','localtime'))"));
    createBookQuery.bindValue(0, bookName);
    createBookQuery.bindValue(1, shouldActive);
    if (!createBookQuery.exec()) {
        db.rollback();
        lastError_ = createBookQuery.lastError().text();
        return false;
    }
    const int bookId = createBookQuery.lastInsertId().toInt();

    QSqlQuery findWordQuery(db);
    findWordQuery.prepare(QStringLiteral("SELECT id FROM words WHERE word = ?"));

    QSqlQuery insertQuery(db);
    insertQuery.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO words (word, translation, phonetic, "
        "countability_label, countability_plural, countability_notes, polysemy_json) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));

    QSqlQuery updateExistingWordQuery(db);
    updateExistingWordQuery.prepare(QStringLiteral(
        "UPDATE words SET "
        "phonetic = CASE WHEN length(trim(COALESCE(phonetic, ''))) = 0 AND length(trim(?)) > 0 THEN ? ELSE phonetic END, "
        "countability_label = CASE WHEN length(trim(COALESCE(countability_label, ''))) = 0 AND length(trim(?)) > 0 THEN ? ELSE countability_label END, "
        "countability_plural = CASE WHEN length(trim(COALESCE(countability_plural, ''))) = 0 AND length(trim(?)) > 0 THEN ? ELSE countability_plural END, "
        "countability_notes = CASE WHEN length(trim(COALESCE(countability_notes, ''))) = 0 AND length(trim(?)) > 0 THEN ? ELSE countability_notes END, "
        "polysemy_json = CASE WHEN length(trim(COALESCE(polysemy_json, ''))) = 0 AND length(trim(?)) > 0 THEN ? ELSE polysemy_json END "
        "WHERE id = ?"));

    QSqlQuery linkWordQuery(db);
    linkWordQuery.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO book_words(book_id, word_id) VALUES(?, ?)"));

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
        if (word.isEmpty() || translation.isEmpty()) {
            continue;
        }

        QString phonetic;
        if (phoneticColumn >= 0 && phoneticColumn < columns.size()) {
            phonetic = columns[phoneticColumn].trimmed();
        }

        QString countabilityLabel;
        if (countabilityColumn >= 0 && countabilityColumn < columns.size()) {
            countabilityLabel = columns[countabilityColumn].trimmed().toUpper();
            if (countabilityLabel == QStringLiteral("COUNTABLE")) {
                countabilityLabel = QStringLiteral("C");
            } else if (countabilityLabel == QStringLiteral("UNCOUNTABLE")) {
                countabilityLabel = QStringLiteral("U");
            } else if (countabilityLabel == QStringLiteral("BOTH")) {
                countabilityLabel = QStringLiteral("B");
            }
            if (countabilityLabel != QStringLiteral("C") &&
                countabilityLabel != QStringLiteral("U") &&
                countabilityLabel != QStringLiteral("B")) {
                countabilityLabel.clear();
            }
        }

        QString countabilityPlural;
        if (pluralColumn >= 0 && pluralColumn < columns.size()) {
            countabilityPlural = columns[pluralColumn].trimmed();
        }

        QString countabilityNotes;
        if (notesColumn >= 0 && notesColumn < columns.size()) {
            countabilityNotes = columns[notesColumn].trimmed();
        }

        QString polysemyJson;
        if (polysemyColumn >= 0 && polysemyColumn < columns.size()) {
            polysemyJson = columns[polysemyColumn].trimmed();
        }

        insertQuery.bindValue(0, word);
        insertQuery.bindValue(1, translation);
        insertQuery.bindValue(2, phonetic);
        insertQuery.bindValue(3, countabilityLabel);
        insertQuery.bindValue(4, countabilityPlural);
        insertQuery.bindValue(5, countabilityNotes);
        insertQuery.bindValue(6, polysemyJson);

        if (!insertQuery.exec()) {
            db.rollback();
            lastError_ = insertQuery.lastError().text();
            return false;
        }

        int wordId = -1;
        if (insertQuery.numRowsAffected() > 0) {
            wordId = insertQuery.lastInsertId().toInt();
        } else {
            findWordQuery.bindValue(0, word);
            if (findWordQuery.exec() && findWordQuery.next()) {
                wordId = findWordQuery.value(0).toInt();
            }
        }

        if (wordId > 0) {
            if (insertQuery.numRowsAffected() == 0) {
                updateExistingWordQuery.bindValue(0, phonetic);
                updateExistingWordQuery.bindValue(1, phonetic);
                updateExistingWordQuery.bindValue(2, countabilityLabel);
                updateExistingWordQuery.bindValue(3, countabilityLabel);
                updateExistingWordQuery.bindValue(4, countabilityPlural);
                updateExistingWordQuery.bindValue(5, countabilityPlural);
                updateExistingWordQuery.bindValue(6, countabilityNotes);
                updateExistingWordQuery.bindValue(7, countabilityNotes);
                updateExistingWordQuery.bindValue(8, polysemyJson);
                updateExistingWordQuery.bindValue(9, polysemyJson);
                updateExistingWordQuery.bindValue(10, wordId);
                if (!updateExistingWordQuery.exec()) {
                    db.rollback();
                    lastError_ = updateExistingWordQuery.lastError().text();
                    return false;
                }
            }

            linkWordQuery.bindValue(0, bookId);
            linkWordQuery.bindValue(1, wordId);
            if (!linkWordQuery.exec()) {
                db.rollback();
                lastError_ = linkWordQuery.lastError().text();
                return false;
            }
            ++importedCount;
        }
    }

    QSqlQuery bindDefaultQuery(db);
    bindDefaultQuery.prepare(QStringLiteral(
        "UPDATE training_configs "
        "SET active_book_id = ?, updated_at = datetime('now','localtime') "
        "WHERE training_type = ? AND active_book_id IS NULL"));
    bindDefaultQuery.bindValue(0, bookId);
    bindDefaultQuery.bindValue(1, QString::fromLatin1(kTrainingTypeSpelling));
    if (!bindDefaultQuery.exec()) {
        db.rollback();
        lastError_ = bindDefaultQuery.lastError().text();
        return false;
    }

    if (!db.commit()) {
        lastError_ = db.lastError().text();
        return false;
    }

    file.close();
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
            "COUNT(bw.word_id) AS word_count, "
            "SUM(CASE WHEN COALESCE(tps.correct_count, 0) > 0 THEN 1 ELSE 0 END) AS learned_count, "
            "CASE WHEN EXISTS(SELECT 1 FROM training_configs tc WHERE tc.training_type = 'spelling' AND tc.active_book_id = b.id) THEN 1 ELSE 0 END AS bound_spelling, "
            "CASE WHEN EXISTS(SELECT 1 FROM training_configs tc WHERE tc.training_type = 'countability' AND tc.active_book_id = b.id) THEN 1 ELSE 0 END AS bound_countability, "
            "CASE WHEN EXISTS(SELECT 1 FROM training_configs tc WHERE tc.training_type = 'polysemy' AND tc.active_book_id = b.id) THEN 1 ELSE 0 END AS bound_polysemy "
            "FROM word_books b "
            "LEFT JOIN book_words bw ON bw.book_id = b.id "
            "LEFT JOIN training_progress tps ON tps.word_id = bw.word_id AND tps.training_type = 'spelling' "
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
        item.boundSpelling = query.value(5).toInt() == 1;
        item.boundCountability = query.value(6).toInt() == 1;
        item.boundPolysemy = query.value(7).toInt() == 1;
        books.push_back(item);
    }

    return books;
}

bool DatabaseManager::setActiveWordBook(int bookId) {
    if (!setActiveBookIdForTraining(QString::fromLatin1(kTrainingTypeSpelling), bookId)) {
        return false;
    }
    if (!ensureDatabaseOpen()) {
        return false;
    }

    // 兼容旧逻辑：保留 word_books.is_active 作为“最近一次拼写绑定”的镜像。
    QSqlQuery query(database());
    if (!query.exec(QStringLiteral("UPDATE word_books SET is_active = 0"))) {
        lastError_ = query.lastError().text();
        return false;
    }
    query.prepare(QStringLiteral("UPDATE word_books SET is_active = 1 WHERE id = ?"));
    query.bindValue(0, bookId);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
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

    query.prepare(QStringLiteral("DELETE FROM book_words WHERE book_id = ?"));
    query.bindValue(0, bookId);
    if (!query.exec()) {
        db.rollback();
        lastError_ = query.lastError().text();
        return false;
    }

    // 兼容旧字段：若原 book_id 指向被删除词书，则切到剩余关联中的最小 book_id。
    query.prepare(QStringLiteral(
        "UPDATE words "
        "SET book_id = (SELECT MIN(bw.book_id) FROM book_words bw WHERE bw.word_id = words.id) "
        "WHERE book_id = ?"));
    query.bindValue(0, bookId);
    if (!query.exec()) {
        db.rollback();
        lastError_ = query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral(
            "DELETE FROM words "
            "WHERE id NOT IN (SELECT DISTINCT word_id FROM book_words) "
            "  AND id NOT IN (SELECT DISTINCT word_id FROM training_progress)"))) {
        db.rollback();
        lastError_ = query.lastError().text();
        return false;
    }

    if (!query.exec(QStringLiteral(
            "DELETE FROM word_spelling_stats "
            "WHERE word_id NOT IN (SELECT id FROM words)"))) {
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

    query.prepare(QStringLiteral(
        "UPDATE training_configs "
        "SET active_book_id = NULL, updated_at = datetime('now','localtime') "
        "WHERE active_book_id = ?"));
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
    return activeBookIdForTraining(QString::fromLatin1(kTrainingTypeSpelling));
}

bool DatabaseManager::isValidTrainingType(const QString &trainingType) const {
    return isKnownTrainingType(trainingType);
}

bool DatabaseManager::upsertSetting(const QString &key, const QString &value) {
    if (!ensureDatabaseOpen()) {
        return false;
    }
    if (key.trimmed().isEmpty()) {
        lastError_ = QStringLiteral("设置键不能为空");
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "INSERT INTO app_settings(setting_key, setting_value, updated_at) "
        "VALUES(?, ?, datetime('now','localtime')) "
        "ON CONFLICT(setting_key) DO UPDATE SET "
        "setting_value = excluded.setting_value, "
        "updated_at = excluded.updated_at"));
    query.bindValue(0, key.trimmed());
    query.bindValue(1, value);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}

QString DatabaseManager::settingValue(const QString &key, const QString &defaultValue) const {
    if (!ensureDatabaseOpen()) {
        return defaultValue;
    }
    if (key.trimmed().isEmpty()) {
        return defaultValue;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT setting_value FROM app_settings WHERE setting_key = ?"));
    query.bindValue(0, key.trimmed());
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return defaultValue;
    }
    if (!query.next()) {
        return defaultValue;
    }
    return query.value(0).toString();
}

int DatabaseManager::activeBookIdForTraining(const QString &trainingType) const {
    if (!ensureDatabaseOpen()) {
        return -1;
    }
    const QString type = trainingType.trimmed().toLower();
    if (!isValidTrainingType(type)) {
        lastError_ = QStringLiteral("不支持的训练类型：%1").arg(trainingType);
        return -1;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT active_book_id "
        "FROM training_configs "
        "WHERE training_type = ?"));
    query.bindValue(0, type);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return -1;
    }
    if (!query.next() || query.value(0).isNull()) {
        return -1;
    }
    return query.value(0).toInt();
}

QString DatabaseManager::activeBookNameForTraining(const QString &trainingType) const {
    const int bookId = activeBookIdForTraining(trainingType);
    if (bookId <= 0) {
        return QString();
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral("SELECT name FROM word_books WHERE id = ?"));
    query.bindValue(0, bookId);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return QString();
    }
    if (!query.next()) {
        return QString();
    }
    return query.value(0).toString();
}

bool DatabaseManager::setActiveBookIdForTraining(const QString &trainingType, int bookId) {
    if (!ensureDatabaseOpen()) {
        return false;
    }
    const QString type = trainingType.trimmed().toLower();
    if (!isValidTrainingType(type)) {
        lastError_ = QStringLiteral("不支持的训练类型：%1").arg(trainingType);
        return false;
    }
    if (bookId > 0) {
        QSqlQuery verify(database());
        verify.prepare(QStringLiteral("SELECT COUNT(*) FROM word_books WHERE id = ?"));
        verify.bindValue(0, bookId);
        if (!verify.exec() || !verify.next()) {
            lastError_ = verify.lastError().text();
            return false;
        }
        if (verify.value(0).toInt() <= 0) {
            lastError_ = QStringLiteral("词书不存在");
            return false;
        }
    }

    QSqlDatabase db = database();
    if (!db.transaction()) {
        lastError_ = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO training_configs(training_type, active_book_id, last_card_index, updated_at) "
        "VALUES(?, ?, 0, datetime('now','localtime')) "
        "ON CONFLICT(training_type) DO UPDATE SET "
        "active_book_id = excluded.active_book_id, "
        "updated_at = excluded.updated_at"));
    query.bindValue(0, type);
    if (bookId > 0) {
        query.bindValue(1, bookId);
    } else {
        query.bindValue(1, QVariant());
    }
    if (!query.exec()) {
        db.rollback();
        lastError_ = query.lastError().text();
        return false;
    }

    if (type == QString::fromLatin1(kTrainingTypeSpelling)) {
        if (!query.exec(QStringLiteral("UPDATE word_books SET is_active = 0"))) {
            db.rollback();
            lastError_ = query.lastError().text();
            return false;
        }
        if (bookId > 0) {
            query.prepare(QStringLiteral("UPDATE word_books SET is_active = 1 WHERE id = ?"));
            query.bindValue(0, bookId);
            if (!query.exec()) {
                db.rollback();
                lastError_ = query.lastError().text();
                return false;
            }
        }
    }

    if (!db.commit()) {
        lastError_ = db.lastError().text();
        return false;
    }
    return true;
}

int DatabaseManager::lastDashboardCardIndex() const {
    if (!ensureDatabaseOpen()) {
        return 0;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT last_card_index "
        "FROM training_configs "
        "WHERE training_type = ?"));
    query.bindValue(0, QString::fromLatin1(kTrainingTypeSpelling));
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return 0;
    }
    if (!query.next()) {
        return 0;
    }
    return qMax(0, query.value(0).toInt());
}

bool DatabaseManager::setLastDashboardCardIndex(int index) {
    if (!ensureDatabaseOpen()) {
        return false;
    }
    const int safeIndex = qMax(0, index);
    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "UPDATE training_configs "
        "SET last_card_index = ?, updated_at = datetime('now','localtime') "
        "WHERE training_type IN (?, ?, ?)"));
    query.bindValue(0, safeIndex);
    query.bindValue(1, QString::fromLatin1(kTrainingTypeSpelling));
    query.bindValue(2, QString::fromLatin1(kTrainingTypeCountability));
    query.bindValue(3, QString::fromLatin1(kTrainingTypePolysemy));
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::isCsvPromptHandled() const {
    return settingValue(QString::fromLatin1(kSettingCsvPromptHandled), QStringLiteral("0")) == QStringLiteral("1");
}

bool DatabaseManager::markCsvPromptHandled() {
    return upsertSetting(QString::fromLatin1(kSettingCsvPromptHandled), QStringLiteral("1"));
}

int DatabaseManager::totalWordCountForTraining(const QString &trainingType) const {
    if (!ensureDatabaseOpen()) {
        return 0;
    }
    const QString type = trainingType.trimmed().toLower();
    if (!isValidTrainingType(type)) {
        return 0;
    }

    const int activeBookId = activeBookIdForTraining(type);
    if (activeBookId <= 0) {
        return 0;
    }

    QString sql = QStringLiteral(
        "SELECT COUNT(*) "
        "FROM book_words bw "
        "JOIN words w ON w.id = bw.word_id "
        "WHERE bw.book_id = ?");
    sql += learningEligibilityClauseForType(type);

    QSqlQuery query(database());
    query.prepare(sql);
    query.bindValue(0, activeBookId);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return 0;
    }
    if (!query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

int DatabaseManager::masteredWordCountForTraining(const QString &trainingType) const {
    if (!ensureDatabaseOpen()) {
        return 0;
    }
    const QString type = trainingType.trimmed().toLower();
    if (!isValidTrainingType(type)) {
        return 0;
    }

    const int activeBookId = activeBookIdForTraining(type);
    if (activeBookId <= 0) {
        return 0;
    }

    QString sql = QStringLiteral(
        "SELECT COUNT(*) "
        "FROM book_words bw "
        "JOIN words w ON w.id = bw.word_id "
        "JOIN training_progress tp ON tp.word_id = w.id AND tp.training_type = ? "
        "WHERE bw.book_id = ? "
        "  AND COALESCE(tp.status, 0) = 2");
    sql += learningEligibilityClauseForType(type);

    QSqlQuery query(database());
    query.prepare(sql);
    query.bindValue(0, type);
    query.bindValue(1, activeBookId);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return 0;
    }
    if (!query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

int DatabaseManager::unlearnedCountForTraining(const QString &trainingType) const {
    if (!ensureDatabaseOpen()) {
        return 0;
    }
    const QString type = trainingType.trimmed().toLower();
    if (!isValidTrainingType(type)) {
        return 0;
    }

    const int activeBookId = activeBookIdForTraining(type);
    if (activeBookId <= 0) {
        return 0;
    }

    QString sql = QStringLiteral(
        "SELECT COUNT(*) "
        "FROM book_words bw "
        "JOIN words w ON w.id = bw.word_id "
        "LEFT JOIN training_progress tp ON tp.word_id = w.id AND tp.training_type = ? "
        "WHERE bw.book_id = ? "
        "  AND w.skip_forever = 0 "
        "  AND (tp.id IS NULL OR COALESCE(tp.status, 0) = 0)");
    sql += learningEligibilityClauseForType(type);

    QSqlQuery query(database());
    query.prepare(sql);
    query.bindValue(0, type);
    query.bindValue(1, activeBookId);
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return 0;
    }
    if (!query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

int DatabaseManager::dueReviewCountForTraining(const QString &trainingType, const QDateTime &now) const {
    if (!ensureDatabaseOpen()) {
        return 0;
    }
    const QString type = trainingType.trimmed().toLower();
    if (!isValidTrainingType(type)) {
        return 0;
    }

    QString sql = QStringLiteral(
        "SELECT COUNT(*) "
        "FROM training_progress tp "
        "JOIN words w ON w.id = tp.word_id "
        "WHERE tp.training_type = ? "
        "  AND tp.status != 0 "
        "  AND w.skip_forever = 0 "
        "  AND tp.next_review IS NOT NULL "
        "  AND date(tp.next_review) <= date(?)");
    sql += learningEligibilityClauseForType(type);

    QSqlQuery query(database());
    query.prepare(sql);
    query.bindValue(0, type);
    query.bindValue(1, now.toString(kDateTimeFormat));
    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return 0;
    }
    if (!query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

QVector<WordItem> DatabaseManager::fetchLearningBatchForTraining(const QString &trainingType, int limit) const {
    QVector<WordItem> items;
    if (!ensureDatabaseOpen()) {
        return items;
    }
    const QString type = trainingType.trimmed().toLower();
    if (!isValidTrainingType(type)) {
        return items;
    }
    const int activeBookId = activeBookIdForTraining(type);
    if (activeBookId <= 0) {
        return items;
    }

    QString sql = QStringLiteral(
        "SELECT w.id, w.word, w.phonetic, w.translation, w.part_of_speech, "
        "       w.countability_label, w.countability_plural, w.countability_notes, w.polysemy_json, "
        "       COALESCE(tp.ease_factor, 2.5) AS ease_factor, "
        "       COALESCE(tp.interval, 0) AS interval, "
        "       tp.next_review AS next_review, "
        "       COALESCE(tp.status, 0) AS status, "
        "       w.skip_forever "
        "FROM book_words bw "
        "JOIN words w ON w.id = bw.word_id "
        "LEFT JOIN training_progress tp "
        "  ON tp.word_id = w.id AND tp.training_type = ? "
        "WHERE bw.book_id = ? "
        "  AND w.skip_forever = 0 "
        "  AND (tp.id IS NULL OR COALESCE(tp.status, 0) = 0)");
    sql += learningEligibilityClauseForType(type);
    sql += QStringLiteral(" ORDER BY w.id LIMIT ?");

    QSqlQuery query(database());
    query.prepare(sql);
    query.bindValue(0, type);
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

QVector<WordItem> DatabaseManager::fetchReviewBatchForTraining(const QString &trainingType,
                                                               const QDateTime &now,
                                                               int limit) const {
    QVector<WordItem> items;
    if (!ensureDatabaseOpen()) {
        return items;
    }
    const QString type = trainingType.trimmed().toLower();
    if (!isValidTrainingType(type)) {
        return items;
    }

    QString sql = QStringLiteral(
        "SELECT w.id, w.word, w.phonetic, w.translation, w.part_of_speech, "
        "       w.countability_label, w.countability_plural, w.countability_notes, w.polysemy_json, "
        "       tp.ease_factor AS ease_factor, "
        "       tp.interval AS interval, "
        "       tp.next_review AS next_review, "
        "       tp.status AS status, "
        "       w.skip_forever "
        "FROM training_progress tp "
        "JOIN words w ON w.id = tp.word_id "
        "WHERE tp.training_type = ? "
        "  AND tp.status != 0 "
        "  AND w.skip_forever = 0 "
        "  AND tp.next_review IS NOT NULL "
        "  AND date(tp.next_review) <= date(?)");
    sql += learningEligibilityClauseForType(type);
    sql += QStringLiteral(" ORDER BY tp.next_review ASC, w.id ASC LIMIT ?");

    QSqlQuery query(database());
    query.prepare(sql);
    query.bindValue(0, type);
    query.bindValue(1, now.toString(kDateTimeFormat));
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

int DatabaseManager::unlearnedCount() const {
    return unlearnedCountForTraining(QString::fromLatin1(kTrainingTypeSpelling));
}

int DatabaseManager::dueReviewCount(const QDateTime &now) const {
    return dueReviewCountForTraining(QString::fromLatin1(kTrainingTypeSpelling), now);
}

QVector<WordItem> DatabaseManager::fetchLearningBatch(int limit) const {
    return fetchLearningBatchForTraining(QString::fromLatin1(kTrainingTypeSpelling), limit);
}

QVector<WordItem> DatabaseManager::fetchReviewBatch(const QDateTime &now, int limit) const {
    return fetchReviewBatchForTraining(QString::fromLatin1(kTrainingTypeSpelling), now, limit);
}

int DatabaseManager::countabilityUnlearnedCount() const {
    return unlearnedCountForTraining(QString::fromLatin1(kTrainingTypeCountability));
}

int DatabaseManager::countabilityDueReviewCount(const QDateTime &now) const {
    return dueReviewCountForTraining(QString::fromLatin1(kTrainingTypeCountability), now);
}

QVector<WordItem> DatabaseManager::fetchCountabilityLearningBatch(int limit) const {
    return fetchLearningBatchForTraining(QString::fromLatin1(kTrainingTypeCountability), limit);
}

QVector<WordItem> DatabaseManager::fetchCountabilityReviewBatch(const QDateTime &now, int limit) const {
    return fetchReviewBatchForTraining(QString::fromLatin1(kTrainingTypeCountability), now, limit);
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
        "SELECT w.id, w.word, w.phonetic, w.translation, w.part_of_speech, "
        "       w.countability_label, w.countability_plural, w.countability_notes, w.polysemy_json, "
        "       w.ease_factor, w.interval, w.next_review, w.status, w.skip_forever "
        "FROM book_words bw "
        "JOIN words w ON w.id = bw.word_id "
        "WHERE bw.book_id = ? AND w.skip_forever = 0 "
        "ORDER BY w.id"));
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
    wordQuery.prepare(QStringLiteral(
        "SELECT next_review "
        "FROM training_progress "
        "WHERE word_id = ? AND training_type = ?"));
    wordQuery.bindValue(0, wordId);
    wordQuery.bindValue(1, QString::fromLatin1(kTrainingTypeSpelling));
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
    const bool ok = applyTrainingReviewResult(
        wordId,
        QString::fromLatin1(kTrainingTypeSpelling),
        result,
        skipped,
        now);
    if (!ok) {
        return false;
    }

    // 兼容旧逻辑：继续回写 words 表，确保已有统计/展示不受影响。
    WordItem current;
    if (!queryWordById(wordId, current)) {
        return false;
    }
    current.easeFactor = qMax(1.3, current.easeFactor);
    current.interval = qMax(0, current.interval);

    QSqlQuery syncQuery(database());
    syncQuery.prepare(QStringLiteral(
        "SELECT ease_factor, interval, next_review, status "
        "FROM training_progress "
        "WHERE word_id = ? AND training_type = ?"));
    syncQuery.bindValue(0, wordId);
    syncQuery.bindValue(1, QString::fromLatin1(kTrainingTypeSpelling));
    if (!syncQuery.exec() || !syncQuery.next()) {
        lastError_ = syncQuery.lastError().text();
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "UPDATE words SET ease_factor = ?, interval = ?, next_review = ?, status = ? WHERE id = ?"));
    query.bindValue(0, syncQuery.value(0));
    query.bindValue(1, syncQuery.value(1));
    query.bindValue(2, syncQuery.value(2));
    query.bindValue(3, syncQuery.value(3));
    query.bindValue(4, wordId);

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::applyCountabilityResult(int wordId,
                                              bool correct,
                                              const QDateTime &now) {
    return applyTrainingReviewResult(wordId,
                                     QString::fromLatin1(kTrainingTypeCountability),
                                     correct ? SpellingResult::Mastered : SpellingResult::Unfamiliar,
                                     !correct,
                                     now);
}

bool DatabaseManager::applyPolysemyResult(int wordId,
                                          SpellingResult result,
                                          bool skipped,
                                          const QDateTime &now) {
    return applyTrainingReviewResult(wordId,
                                     QString::fromLatin1(kTrainingTypePolysemy),
                                     result,
                                     skipped,
                                     now);
}

bool DatabaseManager::applyTrainingReviewResult(int wordId,
                                                const QString &trainingType,
                                                SpellingResult result,
                                                bool skipped,
                                                const QDateTime &now) {
    if (!ensureDatabaseOpen()) {
        return false;
    }
    if (wordId <= 0) {
        lastError_ = QStringLiteral("无效的单词 ID");
        return false;
    }
    const QString type = trainingType.trimmed().toLower();
    if (!isValidTrainingType(type)) {
        lastError_ = QStringLiteral("不支持的训练类型：%1").arg(trainingType);
        return false;
    }

    // 在更新前查一下状态，判断是“学习”还是“复习”
    bool isLearningRow = true;
    QSqlQuery statusQuery(database());
    statusQuery.prepare(QStringLiteral("SELECT status FROM training_progress WHERE word_id = ? AND training_type = ?"));
    statusQuery.bindValue(0, wordId);
    statusQuery.bindValue(1, type);
    if (statusQuery.exec() && statusQuery.next()) {
        const int oldStatus = statusQuery.value(0).toInt();
        if (oldStatus != 0) { // status 0 = New/Unlearned in some conventions, check current logic.
            isLearningRow = false;
        }
    }

    const bool isCountability = (type == QString::fromLatin1(kTrainingTypeCountability));

    QSqlQuery progressQuery(database());
    progressQuery.prepare(QStringLiteral(
        "SELECT ease_factor, interval, next_review, status "
        "FROM training_progress "
        "WHERE word_id = ? AND training_type = ?"));
    progressQuery.bindValue(0, wordId);
    progressQuery.bindValue(1, type);
    if (!progressQuery.exec()) {
        lastError_ = progressQuery.lastError().text();
        return false;
    }

    double currentEaseFactor = 2.5;
    int currentInterval = 0;
    if (progressQuery.next()) {
        currentEaseFactor = progressQuery.value(0).toDouble();
        currentInterval = progressQuery.value(1).toInt();
    } else if (type == QString::fromLatin1(kTrainingTypeSpelling)) {
        WordItem word;
        if (!queryWordById(wordId, word)) {
            return false;
        }
        currentEaseFactor = word.easeFactor;
        currentInterval = word.interval;
    }

    SpellingResult effectiveResult = result;
    if (skipped) {
        effectiveResult = SpellingResult::Unfamiliar;
    }

    int newInterval = 0;
    double newEaseFactor = qMax(1.3, currentEaseFactor);

    switch (effectiveResult) {
    case SpellingResult::Mastered:
        newInterval = nextIntervalForMastered(currentInterval);
        newEaseFactor = qMin(3.0, currentEaseFactor + 0.1);
        break;
    case SpellingResult::Blurry:
        newInterval = nextIntervalForBlurry(currentInterval);
        newEaseFactor = qMax(1.3, currentEaseFactor - 0.05);
        break;
    case SpellingResult::Unfamiliar:
        newInterval = nextIntervalForUnfamiliar();
        newEaseFactor = qMax(1.3, currentEaseFactor - 0.2);
        break;
    }

    const QDateTime nextReviewTime = now.addDays(newInterval);
    const int newStatus = (newInterval >= 30) ? 2 : 1;

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "INSERT INTO training_progress("
        "word_id, training_type, ease_factor, interval, next_review, status, correct_count, wrong_count, updated_at"
        ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(word_id, training_type) DO UPDATE SET "
        "ease_factor = excluded.ease_factor, "
        "interval = excluded.interval, "
        "next_review = excluded.next_review, "
        "status = excluded.status, "
        "correct_count = training_progress.correct_count + excluded.correct_count, "
        "wrong_count = training_progress.wrong_count + excluded.wrong_count, "
        "updated_at = excluded.updated_at"));
    query.bindValue(0, wordId);
    query.bindValue(1, type);
    query.bindValue(2, newEaseFactor);
    query.bindValue(3, newInterval);
    query.bindValue(4, nextReviewTime.toString(kDateTimeFormat));
    query.bindValue(5, newStatus);
    query.bindValue(6, effectiveResult == SpellingResult::Mastered ? 1 : 0);
    query.bindValue(7, effectiveResult == SpellingResult::Mastered ? 0 : 1);
    query.bindValue(8, now.toString(kDateTimeFormat));

    if (!query.exec()) {
        lastError_ = query.lastError().text();
        return false;
    }
    incrementDailyCount(isLearningRow, isCountability, now.date());
    return true;
}

bool DatabaseManager::incrementDailyCount(bool isLearning, bool isCountability, const QDate &date) {
    if (!ensureDatabaseOpen()) return false;
    const QString dateStr = date.toString(Qt::ISODate);
    const QString col = isCountability ? (isLearning ? "cb_learning_count" : "cb_review_count")
                                      : (isLearning ? "learning_count" : "review_count");

    QSqlQuery check(database());
    check.prepare(QStringLiteral("SELECT 1 FROM learning_logs WHERE log_date = ?"));
    check.bindValue(0, dateStr);
    if (!check.exec() || !check.next()) {
        QSqlQuery ins(database());
        ins.prepare(QStringLiteral("INSERT INTO learning_logs (log_date, %1) VALUES (?, 1)").arg(col));
        ins.bindValue(0, dateStr);
        return ins.exec();
    }

    QSqlQuery upd(database());
    upd.prepare(QStringLiteral("UPDATE learning_logs SET %1 = %1 + 1 WHERE log_date = ?").arg(col));
    upd.bindValue(0, dateStr);
    return upd.exec();
}

bool DatabaseManager::addDailyStudySeconds(int seconds, bool isCountability, const QDate &date) {
    if (!ensureDatabaseOpen() || seconds <= 0) return false;
    const QString dateStr = date.toString(Qt::ISODate);
    const QString specificCol = isCountability ? "cb_seconds" : "spelling_seconds";

    QSqlQuery check(database());
    check.prepare(QStringLiteral("SELECT 1 FROM learning_logs WHERE log_date = ?"));
    check.bindValue(0, dateStr);
    if (!check.exec() || !check.next()) {
        QSqlQuery ins(database());
        ins.prepare(QStringLiteral("INSERT INTO learning_logs (log_date, study_seconds, %1) VALUES (?, ?, ?)").arg(specificCol));
        ins.bindValue(0, dateStr);
        ins.bindValue(1, seconds);
        ins.bindValue(2, seconds);
        return ins.exec();
    }

    QSqlQuery upd(database());
    upd.prepare(QStringLiteral("UPDATE learning_logs SET study_seconds = study_seconds + ?, %1 = %1 + ? WHERE log_date = ?").arg(specificCol));
    upd.bindValue(0, seconds);
    upd.bindValue(1, seconds);
    upd.bindValue(2, dateStr);
    return upd.exec();
}

QVector<DatabaseManager::DailyLog> DatabaseManager::fetchWeeklyLogs(const QDate &endDate) const {
    QVector<DailyLog> logs;
    if (!ensureDatabaseOpen()) {
        return logs;
    }

    const QDate startDate = endDate.addDays(-6);
    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT log_date, learning_count, review_count, "
        "       cb_learning_count, cb_review_count, study_seconds, "
        "       spelling_seconds, cb_seconds "
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
        log.countabilityLearningCount = query.value(3).toInt();
        log.countabilityReviewCount = query.value(4).toInt();
        
        const int totalSeconds = query.value(5).toInt();
        log.studyMinutes = totalSeconds > 0 ? (totalSeconds + 59) / 60 : 0;
        log.spellingSeconds = query.value(6).toInt();
        log.countabilitySeconds = query.value(7).toInt();
        
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

    query.prepare(QStringLiteral(
        "SELECT COUNT(*) "
        "FROM training_progress "
        "WHERE training_type = ? AND status != 0"));
    query.bindValue(0, QString::fromLatin1(kTrainingTypeSpelling));
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
        "SELECT id, word, phonetic, translation, part_of_speech, "
        "       countability_label, countability_plural, countability_notes, polysemy_json, "
        "       ease_factor, interval, next_review, status, skip_forever "
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

bool DatabaseManager::isCountabilityCandidate(const WordItem &word) const {
    if (word.skipForever) {
        return false;
    }

    const QString pos = word.partOfSpeech.toLower();
    const QString translation = word.translation.toLower();
    if (pos.contains(QStringLiteral("noun"))) {
        return true;
    }
    if (translation.contains(QStringLiteral("可数"))
        || translation.contains(QStringLiteral("不可数"))
        || translation.startsWith(QStringLiteral("n."))
        || translation.contains(QStringLiteral(" n."))) {
        return true;
    }
    return false;
}

bool DatabaseManager::hasTrainingProgress(int wordId, const QString &trainingType) const {
    if (!ensureDatabaseOpen()) {
        return false;
    }
    if (wordId <= 0 || trainingType.trimmed().isEmpty()) {
        return false;
    }

    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM training_progress WHERE word_id = ? AND training_type = ?"));
    query.bindValue(0, wordId);
    query.bindValue(1, trainingType.trimmed());
    if (!query.exec() || !query.next()) {
        return false;
    }
    return query.value(0).toInt() > 0;
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
