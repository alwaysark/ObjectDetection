//
// AlarmReporter 实现：组帧、拆帧、心跳、重连。
//
#include "AlarmReporter.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

// ---- 协议常量（帧结构见 .h）----
static const quint8  FRAME_MAGIC0 = 0xAA;
static const quint8  FRAME_MAGIC1 = 0x55;
static const quint8  CMD_ALARM    = 0x01;
static const quint8  CMD_HEARTBEAT= 0x02;
static const quint8  CMD_ACK      = 0x81;
static const int     HEADER_SIZE  = 5;    // 帧头2 + 帧长2 + 命令字1

AlarmReporter::AlarmReporter(QObject* parent) : QObject(parent) {
    connect(&m_socket, &QTcpSocket::readyRead, this, &AlarmReporter::onReadyRead);
    connect(&m_socket, &QTcpSocket::stateChanged, this, &AlarmReporter::onStateChanged);

    // 心跳节拍：每 30s 发一帧并清点"漏答"次数（连续 2 次没应答就判死，见 onHeartbeatTick）
    m_heartbeatTimer.setInterval(30 * 1000);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &AlarmReporter::onHeartbeatTick);

    // 断线后延迟 5s 再重连（立即重连在服务端重启的瞬间会撞上拒绝）
    m_reconnectTimer.setSingleShot(true);
    m_reconnectTimer.setInterval(5 * 1000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_state == Offline && !m_host.isEmpty())
            connectToServer(m_host, m_port);
    });
}

void AlarmReporter::connectToServer(const QString& host, quint16 port) {
    m_host = host;
    m_port = port;
    m_manualClose = false;
    m_state = Connecting;
    m_socket.abort();                       // 清掉半开连接
    m_socket.connectToHost(host, port);
    m_heartbeatTimer.start();
}

void AlarmReporter::disconnectFromServer() {
    m_manualClose = true;                   // 主动断开：抑制 onStateChanged 里的自动重连
    m_reconnectTimer.stop();
    m_heartbeatTimer.stop();
    m_socket.disconnectFromHost();
    m_socket.abort();                       // 确保立刻落回 UnconnectedState
}

void AlarmReporter::onStateChanged(QAbstractSocket::SocketState s) {
    if (s == QAbstractSocket::ConnectedState) {
        m_state = Online;
        m_missedAcks = 0;
        emit connectedChanged(true);
    } else if (s == QAbstractSocket::UnconnectedState) {
        if (m_state == Online || m_state == Connecting)
            emit connectedChanged(false);
        m_state = Offline;
        m_heartbeatTimer.stop();
        if (!m_manualClose && !m_host.isEmpty() && !m_reconnectTimer.isActive())
            m_reconnectTimer.start();       // 自动重连（主动断开不重连）
    }
}

// ---- 报警上报：AlarmRecord → JSON → 帧 ----
void AlarmReporter::reportAlarm(const AlarmRecord& r) {
    if (m_state != Online) return;          // 未在线就丢弃（本地检测不受影响）

    QJsonObject obj;
    obj[QStringLiteral("time")]  = r.time.toString(Qt::ISODateWithMs);
    obj[QStringLiteral("label")] = r.label;
    obj[QStringLiteral("score")] = r.score;
    obj[QStringLiteral("count")] = r.count;

    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    m_socket.write(buildFrame(CMD_ALARM, payload));
    m_socket.flush();
}

// ---- 心跳：发一帧 + 清点漏答 ----
void AlarmReporter::onHeartbeatTick() {
    if (m_state != Online) return;
    if (++m_missedAcks > 2) {
        // 两个周期没收到任何应答：判定链路死了，主动断开触发重连
        qDebug() << "AlarmReporter: heartbeat timeout, reconnecting";
        m_socket.abort();
        return;
    }
    m_socket.write(buildFrame(CMD_HEARTBEAT, QByteArray()));
    m_socket.flush();
}

// ---- 收数据：追加缓冲 → 循环切帧 ----
void AlarmReporter::onReadyRead() {
    m_rxBuf.append(m_socket.readAll());

    QByteArray frame;
    while (tryParseFrame(frame)) {
        const quint8 cmd = quint8(frame[0]);
        // frame = 命令字+数据域（切帧时已排除 CRC），去掉开头 1 字节命令字即数据域
        const QByteArray payload = frame.mid(1, frame.size() - 1);

        switch (cmd) {
        case CMD_ACK:
            m_missedAcks = 0;                       // 活的
            emit platformAck(QString::fromUtf8(payload));
            break;
        default:
            break;                                  // 其他命令字暂不处理
        }
        frame.clear();
    }
    // 缓冲防炸：异常流（全是垃圾字节）最多积 1MB 就清空重来
    if (m_rxBuf.size() > 1024 * 1024) m_rxBuf.clear();
}

// 切帧逻辑：找帧头 → 读帧长 → 数据够不够。
// 用循环不用递归：垃圾字节流下递归深度 = 垃圾字节数，会打穿栈
bool AlarmReporter::tryParseFrame(QByteArray& outFrame) {
    for (;;) {
        if (m_rxBuf.size() < HEADER_SIZE) return false;

        // ① 同步帧头（前两个字节不是 AA 55 就逐字节丢弃重找）
        if (quint8(m_rxBuf[0]) != FRAME_MAGIC0 || quint8(m_rxBuf[1]) != FRAME_MAGIC1) {
            m_rxBuf.remove(0, 1);                   // 帧头错位：滑过一个字节继续找
            continue;
        }

        // ② 帧长（小端）：命令字+数据域
        const int bodyLen = quint8(m_rxBuf[2]) | (quint8(m_rxBuf[3]) << 8);
        const int total   = HEADER_SIZE + bodyLen - 1;  // 帧头2+帧长2 + 命令字1+数据域
        if (bodyLen < 1 || total > 64 * 1024) {
            m_rxBuf.remove(0, 2);                   // 非法帧长：当垃圾丢掉帧头继续找
            continue;
        }

        // ③ 数据够不够一帧（断包：不够就留着等下一次 readyRead）
        if (m_rxBuf.size() < total) return false;

        outFrame = m_rxBuf.mid(4, bodyLen);         // 命令字+数据域（可靠性由 TCP 保证，无应用层 CRC）
        m_rxBuf.remove(0, total);
        return true;
    }
}

// ---- 组帧：帧头 + 帧长 + 命令字 + 数据域 ----
QByteArray AlarmReporter::buildFrame(quint8 cmd, const QByteArray& payload) {
    const int bodyLen = 1 + payload.size();
    QByteArray f;
    f.reserve(HEADER_SIZE + payload.size());
    f.append(char(FRAME_MAGIC0));
    f.append(char(FRAME_MAGIC1));
    f.append(char(bodyLen & 0xFF));                 // 小端低字节
    f.append(char((bodyLen >> 8) & 0xFF));          // 小端高字节
    f.append(char(cmd));
    f.append(payload);
    return f;
}
