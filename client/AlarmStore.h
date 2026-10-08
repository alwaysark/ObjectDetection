//
// AlarmStore —— 告警记录入库（SQLite）。
//
// 职责单一：告警记录的插入与查询。不认识检测线程，不认识界面——
// 调用方（MainWindow）把 AlarmRecord 组装好喂进来，查询时拿 QSqlQueryModel 走。
//
// 线程约定：所有接口只在 UI 线程调用。SQLite 单条插入是微秒级，
// 告警又是 5 秒冷却的低频事件，不值得再开线程。
//
// 表结构（alarms）：
//   id          自增主键
//   time        告警时间（ISO 字符串，可排序）
//   label       类别名（fire/smoke/...，多类拼接）
//   score       最高置信度百分数（0~100，方便直接展示）
//   count       该次告警检出的目标总数
//   snapshot    快照 png 路径（可空字符串）
//
#ifndef FIRESMOKEDETECTION_ALARMSTORE_H
#define FIRESMOKEDETECTION_ALARMSTORE_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQueryModel>
#include <QString>
#include <QDateTime>

// 一条告警记录的数据载体（入库前由调用方组装）
struct AlarmRecord {
    QDateTime time;          // 告警时间
    QString   label;         // 类别名（多个用逗号拼，如 "fire, smoke"）
    float     score = 0;     // 最高置信度 0~1
    int       count = 0;     // 检出的目标总数
    QString   snapshot;      // 快照 png 路径（可空）
};

class AlarmStore : public QObject {
    Q_OBJECT
public:
    explicit AlarmStore(QObject* parent = nullptr);
    ~AlarmStore() override;

    // 打开（或创建）数据库文件并建表。失败返回 false 并把原因写入 *err
    bool open(const QString& dbPath, QString* err = nullptr);

    // 插入一条告警记录（单条事务，失败静默返回 false——告警记录丢一条不能影响检测主流程）
    bool append(const AlarmRecord& r);

    // 查询某一天的告警（给 QTableView 的 model）。空日期 = 查全部。
    // 返回的是内部复用的同一个 model（重复 setQuery 刷新），调用方不得 delete
    QSqlQueryModel* queryByDay(const QDate& day);

    // 统计信息（界面状态栏/日志用）
    int totalCount() const;

private:
    QSqlDatabase m_db;       // 命名连接（默认连接留给可能出现的其他用途，避免冲突）
    QSqlQueryModel* m_historyModel = nullptr;   // 历史查询复用的模型（避免反复 new 泄漏式堆积）
};

#endif //FIRESMOKEDETECTION_ALARMSTORE_H
