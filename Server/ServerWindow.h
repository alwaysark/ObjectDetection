//
// ServerWindow —— 报警统计平台主窗口：TCP 收报警 → SQLite 落库 → 曲线/统计/日志。
//
// 布局：顶部统计标签行（总报警数 / 今日 / 分类计数 / 在线客户端），
// 中间 QCustomPlot 双曲线（左轴：每分钟报警数；右轴：报警最高置信度%），
// 底部连接/报警日志。
//
// 每个客户端连接对应一个 Agent（在 .cpp 里）：拆帧、回 ACK、上报报警 JSON。
// 窗口只管"收到报警以后干什么"：入库、更新曲线与统计、记日志。
//
#ifndef FIRESMOKEDETECTION_SERVER_SERVERWINDOW_H
#define FIRESMOKEDETECTION_SERVER_SERVERWINDOW_H

#include <QMainWindow>
#include <QTcpServer>

class QCustomPlot;
class QLabel;
class QTextBrowser;
class AlarmDb;
class QJsonObject;

class ServerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ServerWindow(QWidget* parent = nullptr);
    ~ServerWindow() override;

private slots:
    void onNewConnection();    // 新客户端接入：挂 Agent、在线数+1
    void onAlarmReceived(const QJsonObject& obj, const QString& client);  // 报警落地
    void onClientGone();       // 客户端断开：在线数-1

private:
    void refreshStats();       // 查库刷新统计标签
    void rebuildPlot();        // 用内存数据重画两条曲线（滚动窗口 60 分钟）
    void log(const QString& text, const QString& color = QString());  // 带时间戳写日志
    void pruneOldData();       // 丢掉曲线窗口之外的历史点

    QTcpServer   m_server;
    AlarmDb     *m_db = nullptr;
    QCustomPlot *m_plot = nullptr;
    QLabel      *m_totalLabel = nullptr;    // 历史总报警数
    QLabel      *m_todayLabel = nullptr;    // 今日报警数
    QLabel      *m_byLabelLabel = nullptr;  // 分类计数（fire x / smoke y）
    QLabel      *m_onlineLabel = nullptr;   // 在线客户端数
    QTextBrowser* m_log = nullptr;

    // 曲线数据（内存持有，报警到来时增量更新；启动时从库里播种最近一小时）
    QMap<qint64, int> m_minuteBuckets;          // 分钟epoch → 该分钟报警数
    QVector<QPair<qint64, double>> m_scorePts;  // (秒epoch, 最高分%)
    int m_onlineClients = 0;

    static constexpr int kCurveWindowMin = 60;  // 曲线滚动窗口：60 分钟
    static constexpr quint16 kListenPort = 9000;// 监听端口（客户端默认连 127.0.0.1:9000）
};

#endif //FIRESMOKEDETECTION_SERVER_SERVERWINDOW_H
