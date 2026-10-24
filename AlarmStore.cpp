//
// AlarmStore 实现（SQLite 告警记录库）。
//
#include "AlarmStore.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

AlarmStore::AlarmStore(QObject* parent) : QObject(parent) {
    // 用命名连接而不是默认连接：万一将来别处也要开库，互不干扰
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("alarm_conn"));
}

AlarmStore::~AlarmStore() {
    if (m_db.isOpen()) m_db.close();
}

bool AlarmStore::open(const QString& dbPath, QString* err) {
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        if (err) *err = m_db.lastError().text();
        return false;
    }

    // 建表（IF NOT EXISTS：老库直接复用）
    QSqlQuery q(m_db);
    const QString ddl = QStringLiteral(
            "CREATE TABLE IF NOT EXISTS alarms ("
            "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  time      TEXT    NOT NULL,"     // ISO 8601 字符串，天然可排序
            "  label     TEXT    NOT NULL,"
            "  score     REAL    NOT NULL,"
            "  count     INTEGER NOT NULL,"
            "  snapshot  TEXT"
            ")");
    if (!q.exec(ddl)) {
        if (err) *err = q.lastError().text();
        return false;
    }
    // time 列要按日期过滤/排序，建个索引
    q.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_alarms_time ON alarms(time)"));
    return true;
}

bool AlarmStore::append(const AlarmRecord& r) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
            "INSERT INTO alarms (time, label, score, count, snapshot) "
            "VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(r.time.toString(Qt::ISODateWithMs));
    q.addBindValue(r.label);
    q.addBindValue(r.score);
    q.addBindValue(r.count);
    q.addBindValue(r.snapshot);
    if (!q.exec()) {
        qDebug() << "AlarmStore insert failed:" << q.lastError().text();
        return false;
    }
    return true;
}

QSqlQueryModel* AlarmStore::queryByDay(const QDate& day) {
    if (!m_historyModel)
        m_historyModel = new QSqlQueryModel(this);
    QSqlQuery q(m_db);
    if (day.isValid()) {
        // ISO 字符串的字典序 == 时间序，按前缀匹配就能圈出"这一天"
        const QString d = day.toString(Qt::ISODate);          // yyyy-MM-dd
        q.prepare(QStringLiteral(
                "SELECT id, time, label, score, count, snapshot "
                "FROM alarms WHERE time LIKE ? ORDER BY time DESC"));
        q.addBindValue(d + "%");
    } else {
        q.prepare(QStringLiteral(
                "SELECT id, time, label, score, count, snapshot "
                "FROM alarms ORDER BY time DESC LIMIT 500"));
    }
    q.exec();
    m_historyModel->setQuery(std::move(q));   // 重复 setQuery 即刷新内容，不产生新模型
    m_historyModel->setHeaderData(0, Qt::Horizontal, tr("编号"));
    m_historyModel->setHeaderData(1, Qt::Horizontal, tr("时间"));
    m_historyModel->setHeaderData(2, Qt::Horizontal, tr("类别"));
    m_historyModel->setHeaderData(3, Qt::Horizontal, tr("置信度%"));
    m_historyModel->setHeaderData(4, Qt::Horizontal, tr("目标数"));
    m_historyModel->setHeaderData(5, Qt::Horizontal, tr("快照"));
    return m_historyModel;
}

int AlarmStore::totalCount() const {
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM alarms"));
    return q.next() ? q.value(0).toInt() : 0;
}
