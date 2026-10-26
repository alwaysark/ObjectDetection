//
// AlarmDb 实现：建表、插入、统计查询。
//
#include "AlarmDb.h"
#include <QSqlQuery>
#include <QSqlError>

AlarmDb::AlarmDb(QObject* parent) : QObject(parent) {}

AlarmDb::~AlarmDb() {
    if (m_db.isOpen())
        m_db.close();
}

bool AlarmDb::open(const QString& dbPath, QString* err) {
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("alarm_server"));
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        if (err) *err = m_db.lastError().text();
        return false;
    }
    // 建表（存在则跳过）。时间存 "yyyy-MM-dd hh:mm:ss" 文本：字典序即时间序，
    // 且 SQLite 的 strftime/date 函数可以直接解析
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS alarms ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "time TEXT NOT NULL, label TEXT NOT NULL, score REAL NOT NULL,"
            "count INTEGER NOT NULL, client TEXT NOT NULL)"))) {
        if (err) *err = q.lastError().text();
        return false;
    }
    return true;
}

bool AlarmDb::addAlarm(const QDateTime& time, const QString& label,
                       double score, int count, const QString& client) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
            "INSERT INTO alarms (time,label,score,count,client) VALUES (?,?,?,?,?)"));
    q.addBindValue(time.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
    q.addBindValue(label);
    q.addBindValue(score);
    q.addBindValue(count);
    q.addBindValue(client);
    return q.exec();
}

int AlarmDb::totalCount() const {
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM alarms"));
    return q.next() ? q.value(0).toInt() : 0;
}

int AlarmDb::todayCount() const {
    QSqlQuery q(m_db);
    // date('now','localtime')：SQLite 的 now 是 UTC，显式转本地时区再和入库的本地时间比
    q.exec(QStringLiteral(
            "SELECT COUNT(*) FROM alarms WHERE date(time) = date('now','localtime')"));
    return q.next() ? q.value(0).toInt() : 0;
}

QMap<QString, int> AlarmDb::countByLabel() const {
    QMap<QString, int> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
            "SELECT label, COUNT(*) FROM alarms GROUP BY label ORDER BY COUNT(*) DESC"));
    while (q.next())
        out.insert(q.value(0).toString(), q.value(1).toInt());
    return out;
}

QVector<QPair<qint64, double>> AlarmDb::scoresSince(qint64 epochSec) const {
    QVector<QPair<qint64, double>> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
            "SELECT strftime('%s', time), score FROM alarms "
            "WHERE strftime('%s', time) >= ? ORDER BY time"));
    q.addBindValue(QString::number(epochSec));
    q.exec();
    while (q.next())
        out.append({q.value(0).toLongLong(), q.value(1).toDouble()});
    return out;
}

QMap<qint64, int> AlarmDb::minuteCountsSince(qint64 epochSec) const {
    QMap<qint64, int> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
            "SELECT strftime('%s', time)/60, COUNT(*) FROM alarms "
            "WHERE strftime('%s', time) >= ? GROUP BY 1"));
    q.addBindValue(QString::number(epochSec));
    q.exec();
    while (q.next())
        out.insert(q.value(0).toLongLong(), q.value(1).toInt());
    return out;
}
