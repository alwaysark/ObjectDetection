//
// AlarmPlatformServer —— 告警平台模拟器（server/ 子项目，Qt 重写版）。
//
// 与 client 的 AlarmReporter 对接，验证第 2 期的自定义协议：
//   收告警帧(0x01) → 打印 JSON → 存入自己的 SQLite → 回 ACK 帧(0x81 "OK")
//   收心跳帧(0x02) → 回 ACK 帧
//
// 为什么不用原 ReactHttpsServer_CPP 直接改：那份代码是 Linux 专用
// （epoll/socketpair/unistd/memmem），在 Windows 上编译不过；且它是 HTTP 文件
// 服务器，与告警协议无关。这里用 Qt 重写告警平台，Windows/Linux 都能编。
// 原 Reactor 源码保留在 server/react/ 目录作为 Linux 学习参考（不参与编译）。
//
// 协议帧结构（与 AlarmReporter 完全一致，小端）：
//   帧头 AA 55 | 帧长 2B（命令字+数据域+CRC） | 命令字 1B | 数据域 NB | CRC16 2B
//
#include <QTcpServer>
#include <QTcpSocket>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QCoreApplication>
#include <QByteArray>
#include <cstdio>

// ---- 协议常量（与 client/AlarmReporter 保持一致）----
static const quint8  MAGIC0 = 0xAA, MAGIC1 = 0x55;
static const quint8  CMD_ALARM = 0x01, CMD_HEARTBEAT = 0x02, CMD_ACK = 0x81;
static const int     HEADER_SIZE = 5, CRC_SIZE = 2;

static quint16 crc16Modbus(const QByteArray& data) {
    quint16 crc = 0xFFFF;
    for (char byte : data) {
        crc ^= quint8(byte);
        for (int b = 0; b < 8; ++b)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

static QByteArray buildFrame(quint8 cmd, const QByteArray& payload) {
    const int bodyLen = 1 + payload.size() + CRC_SIZE;
    QByteArray f;
    f.append(char(MAGIC0)).append(char(MAGIC1));
    f.append(char(bodyLen & 0xFF)).append(char((bodyLen >> 8) & 0xFF));
    f.append(char(cmd)).append(payload);
    const quint16 crc = crc16Modbus(f.mid(4));
    f.append(char(crc & 0xFF)).append(char((crc >> 8) & 0xFF));
    return f;
}

// ---- 每个客户端连接一个 Agent：拆帧逻辑与 client 的 tryParseFrame 对称 ----
class Agent : public QObject {
    Q_OBJECT
public:
    explicit Agent(QTcpSocket* sock, QObject* parent = nullptr)
        : QObject(parent), m_sock(sock) {
        connect(m_sock, &QTcpSocket::readyRead, this, &Agent::onRead);
        connect(m_sock, &QTcpSocket::disconnected, this, &Agent::bye);
    }
signals:
    void alarmReceived(const QJsonObject& obj);   // 告警入库（main 里连接）

private slots:
    void onRead() {
        m_buf.append(m_sock->readAll());
        for (;;) {
            if (m_buf.size() < HEADER_SIZE) return;          // 帧头+帧长都不够
            if (quint8(m_buf[0]) != MAGIC0 || quint8(m_buf[1]) != MAGIC1) {
                m_buf.remove(0, 1);                          // 帧头错位：逐字节滑过
                continue;
            }
            const int bodyLen = quint8(m_buf[2]) | (quint8(m_buf[3]) << 8);
            const int total   = HEADER_SIZE + bodyLen - 1;   // 帧头2+帧长2+命令字+数据域+CRC
            if (bodyLen < 1 + CRC_SIZE || total > 64 * 1024) {
                m_buf.remove(0, 2);                          // 非法帧长：丢帧头重找
                continue;
            }
            if (m_buf.size() < total) return;                // 断包：等下一次 readyRead

            const QByteArray body    = m_buf.mid(4, bodyLen - CRC_SIZE);
            const int        crcPos  = 4 + bodyLen - CRC_SIZE;
            const quint16    crcRecv = quint8(m_buf[crcPos]) | (quint8(m_buf[crcPos + 1]) << 8);
            m_buf.remove(0, total);                          // 消费掉这一帧
            if (crc16Modbus(body) != crcRecv) {
                std::printf("[platform] CRC mismatch, drop frame\n");
                continue;
            }

            const quint8     cmd     = quint8(body[0]);
            const QByteArray payload = body.mid(1, body.size() - 1 - CRC_SIZE);
            switch (cmd) {
            case CMD_ALARM: {
                const QJsonObject obj = QJsonDocument::fromJson(payload).object();
                std::printf("[platform] ALARM  time=%s label=%s score=%.1f%% count=%d\n",
                            qPrintable(obj["time"].toString()),
                            qPrintable(obj["label"].toString()),
                            obj["score"].toDouble(), obj["count"].toInt());
                m_sock->write(buildFrame(CMD_ACK, "OK"));    // 应答客户端
                m_sock->flush();
                emit alarmReceived(obj);
                break;
            }
            case CMD_HEARTBEAT:
                m_sock->write(buildFrame(CMD_ACK, "OK"));
                m_sock->flush();
                break;
            default:
                break;
            }
        }
    }

    void bye() {                                             // 对端断开：自我销毁
        std::printf("[platform] client disconnected\n");
        deleteLater();
    }

private:
    QTcpSocket* m_sock;
    QByteArray  m_buf;                                       // 接收缓冲（拆帧）
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const quint16 tcpPort = argc > 1 ? quint16(QString(argv[1]).toUShort()) : 9000;

    // 平台自己的告警留档库（表结构与 client 的 alarms.db 一致，便于对照）
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                QStringLiteral("platform"));
    db.setDatabaseName(QStringLiteral("platform_alarms.db"));
    if (!db.open()) { std::printf("platform db open failed\n"); return 1; }
    QSqlQuery(db).exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS alarms ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "time TEXT NOT NULL, label TEXT NOT NULL, score REAL NOT NULL,"
        "count INTEGER NOT NULL, snapshot TEXT)"));

    QTcpServer server;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        while (QTcpSocket* sock = server.nextPendingConnection()) {
            auto* agent = new Agent(sock, &app);
            QObject::connect(agent, &Agent::alarmReceived,
                             [&db](const QJsonObject& obj) {    // 平台侧留档入库
                QSqlQuery q(db);
                q.prepare(QStringLiteral(
                    "INSERT INTO alarms (time,label,score,count,snapshot) VALUES (?,?,?,?,?)"));
                q.addBindValue(obj["time"].toString());
                q.addBindValue(obj["label"].toString());
                q.addBindValue(obj["score"].toDouble());
                q.addBindValue(obj["count"].toInt());
                q.addBindValue(obj["snapshot"].toString());
                q.exec();
            });
            std::printf("[platform] client connected: %s:%u\n",
                        qPrintable(sock->peerAddress().toString()), sock->peerPort());
        }
    });

    if (!server.listen(QHostAddress::Any, tcpPort)) {
        std::printf("listen tcp/%u failed\n", tcpPort);
        return 1;
    }
    std::printf("[platform] alarm platform listening on tcp/%u, db=platform_alarms.db\n", tcpPort);
    return app.exec();
}

#include "main.moc"
