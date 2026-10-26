//
// AlarmReporter —— 报警外发客户端（TCP + 自定义二进制协议）。
//
// 协议帧结构（小端）：
//   ┌────────┬────────┬────────┬─────────┬──────────┐
//   │ 帧头    │ 帧长    │ 命令字  │ 数据域   │ CRC16    │
//   │ 2B     │ 2B     │ 1B     │ NB      │ 2B       │
//   │ AA 55  │ N+3    │ 1B     │ JSON    │ modbus   │
//   └────────┴────────┴────────┴─────────┴──────────┘
//   帧长 = 命令字 + 数据域 + CRC 的总字节数；CRC16-Modbus 覆盖 命令字+数据域
//
// 命令字：
//   0x01 客户端→平台  报警上报（数据域 = AlarmRecord 的 JSON）
//   0x02 双向         心跳（数据域空）
//   0x81 平台→客户端  应答（数据域 = "OK"）
//
// 可靠性机制：
//   - 粘包/断包：readyRead 只往 m_rxBuf 追加字节，按"帧头→帧长→够不够"循环切帧；
//   - 心跳保活：QTimer 每 30s 发一帧心跳，连续 2 个周期没收到应答判定断线；
//   - 断线重连：状态机 Offline→Connecting→Online，断线后 5s 自动重连（主动断开不重连）。
//
// 线程约定：全部在 UI 线程使用（QTcpSocket 异步非阻塞，不会卡界面）。
//
#ifndef FIRESMOKEDETECTION_ALARMREPORTER_H
#define FIRESMOKEDETECTION_ALARMREPORTER_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QDateTime>

// 一条报警记录的数据载体（报警发生时由界面组装、经 JSON 发往服务端）
struct AlarmRecord {
    QDateTime time;          // 报警时间
    QString   label;         // 类别名（多个用逗号拼，如 "fire, smoke"）
    float     score = 0;     // 最高置信度 0~1
    int       count = 0;     // 检出的目标总数
};

class AlarmReporter : public QObject {
    Q_OBJECT
public:
    explicit AlarmReporter(QObject* parent = nullptr);

    // 连接报警统计平台。connectToHost 是异步的，结果通过 connectedChanged 信号通知
    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();

    // 上报一条报警（组帧发送；未在线时静默丢弃——平台在不在都不影响本地检测）
    void reportAlarm(const AlarmRecord& r);

    bool isOnline() const { return m_state == Online; }

signals:
    void connectedChanged(bool online);       // 连接状态变化（界面显示"已连接平台"）
    void platformAck(const QString& msg);     // 平台应答（调试用）

private slots:
    void onReadyRead();                       // 收平台数据：拆帧 + 分发
    void onStateChanged(QAbstractSocket::SocketState s);
    void onHeartbeatTick();                   // 心跳定时器：发心跳 + 判超时

private:
    // 协议工具（static：不依赖实例状态，方便单测）
    static QByteArray buildFrame(quint8 cmd, const QByteArray& payload);
    static quint16 crc16Modbus(const QByteArray& data);   // CRC16-Modbus（多项式 0x8005 反射）

    // 从 m_rxBuf 里切出一帧；返回 false 表示数据不够（留着等下一次 readyRead）
    bool tryParseFrame(QByteArray& outFrame);

    enum State { Offline, Connecting, Online };
    State m_state = Offline;

    QTcpSocket m_socket;
    QTimer     m_heartbeatTimer;    // 30s 周期：发心跳 + 清点漏答（超时判定就在这里做）
    QTimer     m_reconnectTimer;    // 5s 后重连
    QString    m_host;
    quint16    m_port = 0;
    int        m_missedAcks = 0;    // 连续未收到应答的心跳次数
    bool       m_manualClose = false;  // 主动断开标志：抑制断开后的自动重连
    QByteArray m_rxBuf;             // 接收缓冲（拆帧用）
};

#endif //FIRESMOKEDETECTION_ALARMREPORTER_H
