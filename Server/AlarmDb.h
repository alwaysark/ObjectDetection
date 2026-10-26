//
// AlarmDb —— 报警记录入库模块（SQLite）。
//
// 职责单一：报警的插入与统计查询。不认识网络，不认识界面——
// 调用方（ServerWindow）收到报警帧后喂进来，统计数字查询时拿走。
//
// 线程约定：所有接口只在 UI 线程调用。报警是 5 秒冷却级的低频事件，
// SQLite 单条插入微秒级，不值得再开线程。
//
// 表结构（server_alarms，exe 旁 server_alarms.db）：
//   id      自增主键
//   time    报警时间（"yyyy-MM-dd hh:mm:ss"，可排序、可被 strftime 解析）
//   label   类别名（多个逗号拼，如 "fire, smoke"）
//   score   最高置信度（0~1）
//   count   该次报警检出的目标总数
//   client  上报端标识（"ip:port"）
//
#ifndef FIRESMOKEDETECTION_SERVER_ALARMDB_H
#define FIRESMOKEDETECTION_SERVER_ALARMDB_H

#include <QObject>
#include <QSqlDatabase>
#include <QDateTime>
#include <QString>
#include <QMap>
#include <QVector>
#include <QPair>

class AlarmDb : public QObject {
    Q_OBJECT
public:
    explicit AlarmDb(QObject* parent = nullptr);
    ~AlarmDb() override;                       // 关库（RAII 收尾）

    // 打开（或创建）数据库文件并建表。失败返回 false 并把原因写入 *err
    bool open(const QString& dbPath, QString* err = nullptr);

    // 插入一条报警。失败静默返回 false（统计丢一条不致命，不能影响收报警主流程）
    bool addAlarm(const QDateTime& time, const QString& label,
                  double score, int count, const QString& client);

    // ---- 统计查询（界面标签用）----
    int totalCount() const;                    // 历史总报警数
    int todayCount() const;                    // 今天（本地时区）的报警数
    QMap<QString, int> countByLabel() const;   // 类别名 → 报警数（按次数降序）

    // ---- 曲线播种用（启动时把最近的历史画进曲线窗口）----
    // 最近 epochSec 之后的 (秒级时间戳, 最高分数值) 序列，按时间升序
    QVector<QPair<qint64, double>> scoresSince(qint64 epochSec) const;
    // 最近 epochSec 之后的 分钟桶(epoch分钟) → 报警数
    QMap<qint64, int> minuteCountsSince(qint64 epochSec) const;

private:
    QSqlDatabase m_db;      // 命名连接（不占默认连接，避免和潜在的其他用途冲突）
};

#endif //FIRESMOKEDETECTION_SERVER_ALARMDB_H
