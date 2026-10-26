//
// ServerWindow 实现 + 每连接一 Agent（拆帧与客户端 AlarmReporter 对称）。
//
#include "ServerWindow.h"
#include "AlarmDb.h"
#include "qcustomplot.h"

#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLabel>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QCoreApplication>
#include <QScrollBar>
#include <cmath>

// ---------------------------------------------------------------------------
// Agent —— 一个客户端连接：收字节流 → 拆帧 → 回 ACK / 上交报警 JSON。
// 协议与客户端 AlarmReporter 完全一致（帧结构见客户端 AlarmReporter.h）：
//   帧头 AA 55 | 帧长 2B（命令字+数据域+CRC） | 命令字 1B | 数据域 NB | CRC16 2B
// ---------------------------------------------------------------------------
class Agent : public QObject {
    Q_OBJECT
public:
    explicit Agent(QTcpSocket* sock, QObject* parent = nullptr)
            : QObject(parent), m_sock(sock) {
        connect(m_sock, &QTcpSocket::readyRead, this, &Agent::onRead);
        connect(m_sock, &QTcpSocket::disconnected, this, &Agent::bye);
    }

    QString peer() const { return m_sock->peerAddress().toString() + ':' +
                                  QString::number(m_sock->peerPort()); }

signals:
    void alarmReceived(const QJsonObject& obj);   // 报警落地（窗口连接，带 peer 上下文）

private slots:
    void onRead() {
        m_buf.append(m_sock->readAll());
        for (;;) {
            if (m_buf.size() < HEADER_SIZE) return;            // 帧头+帧长都不够
            if (quint8(m_buf[0]) != MAGIC0 || quint8(m_buf[1]) != MAGIC1) {
                m_buf.remove(0, 1);                            // 帧头错位：逐字节滑过
                continue;
            }
            const int bodyLen = quint8(m_buf[2]) | (quint8(m_buf[3]) << 8);
            const int total   = HEADER_SIZE + bodyLen - 1;
            if (bodyLen < 1 || total > 64 * 1024) {
                m_buf.remove(0, 2);                            // 非法帧长：丢帧头重找
                continue;
            }
            if (m_buf.size() < total) return;                  // 断包：等下一次 readyRead

            // body = 命令字+数据域。无应用层 CRC：传输可靠性由 TCP 自身的校验和与重传保证
            const QByteArray body    = m_buf.mid(4, bodyLen);
            m_buf.remove(0, total);                            // 消费掉这一帧

            const quint8     cmd     = quint8(body[0]);
            const QByteArray payload = body.mid(1, body.size() - 1);
            switch (cmd) {
            case CMD_ALARM: {
                const QJsonObject obj = QJsonDocument::fromJson(payload).object();
                m_sock->write(buildFrame(CMD_ACK, "OK"));      // 应答客户端
                m_sock->flush();
                emit alarmReceived(obj);
                break;
            }
            case CMD_HEARTBEAT:
                m_sock->write(buildFrame(CMD_ACK, "OK"));      // 心跳也回 ACK（保活判定靠它）
                m_sock->flush();
                break;
            default:
                break;
            }
        }
    }

    void bye() {                                               // 对端断开：自我销毁
        emit clientGone();
        m_sock->deleteLater();                                 // socket 是 server 的子对象，主动收掉防累积
        deleteLater();
    }

signals:
    void clientGone();                                         // 断开通知（窗口在线数-1）

private:
    // ---- 协议工具（与客户端同款实现）----
    static QByteArray buildFrame(quint8 cmd, const QByteArray& payload) {
        const int bodyLen = 1 + payload.size();
        QByteArray f;
        f.append(char(MAGIC0)).append(char(MAGIC1));
        f.append(char(bodyLen & 0xFF)).append(char((bodyLen >> 8) & 0xFF));
        f.append(char(cmd)).append(payload);
        return f;
    }

    QTcpSocket* m_sock;
    QByteArray  m_buf;                                         // 接收缓冲（拆帧）

    static const quint8 MAGIC0 = 0xAA, MAGIC1 = 0x55;
    static const quint8 CMD_ALARM = 0x01, CMD_HEARTBEAT = 0x02, CMD_ACK = 0x81;
    static const int    HEADER_SIZE = 5;
};

// ==================== 窗口 ====================
ServerWindow::ServerWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("FireSmokeDetection — 报警统计平台"));
    resize(900, 600);

    // ---- 数据库模块：exe 旁 server_alarms.db，失败只提示不拦截（还能看实时曲线）----
    m_db = new AlarmDb(this);
    QString dbErr;
    if (!m_db->open(QCoreApplication::applicationDirPath() + "/server_alarms.db", &dbErr))
        qDebug() << "AlarmDb open failed:" << dbErr;

    // ---- 界面：统计标签行 / 曲线 / 日志 ----
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    auto* statsRow = new QHBoxLayout();
    m_totalLabel  = new QLabel(this);
    m_todayLabel  = new QLabel(this);
    m_byLabelLabel = new QLabel(this);
    m_onlineLabel = new QLabel(this);
    for (QLabel* l : {m_totalLabel, m_todayLabel, m_byLabelLabel, m_onlineLabel}) {
        l->setFrameShape(QFrame::StyledPanel);
        statsRow->addWidget(l);
    }
    root->addLayout(statsRow);

    m_plot = new QCustomPlot(central);
    m_plot->addGraph();                                   // graph0：每分钟报警数（左轴）
    m_plot->graph(0)->setPen(QPen(QColor(220, 60, 60), 2));
    m_plot->graph(0)->setBrush(QBrush(QColor(220, 60, 60, 30)));
    m_plot->graph(0)->setName("报警数/分钟");
    m_plot->addGraph(m_plot->xAxis, m_plot->yAxis2);      // graph1：最高置信度%（右轴）
    m_plot->graph(1)->setPen(QPen(QColor(60, 110, 220), 2));
    m_plot->graph(1)->setLineStyle(QCPGraph::lsNone);     // 散点：每次报警一个点
    m_plot->graph(1)->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 6));
    m_plot->graph(1)->setName("最高置信度%");
    m_plot->xAxis->setLabel("时间");
    // QCustomPlot 2.x：时间轴靠 ticker 配置，x 轴直接吃 epoch 秒
    auto timeTicker = QSharedPointer<QCPAxisTickerDateTime>::create();
    timeTicker->setDateTimeFormat(QStringLiteral("hh:mm"));
    timeTicker->setDateTimeSpec(Qt::LocalTime);
    m_plot->xAxis->setTicker(timeTicker);
    m_plot->yAxis->setLabel("报警数/分钟");
    m_plot->yAxis2->setLabel("置信度%");
    m_plot->yAxis2->setVisible(true);
    m_plot->yAxis2->setRange(0, 100);
    m_plot->legend->setVisible(true);
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    root->addWidget(m_plot, 3);

    m_log = new QTextBrowser(central);
    m_log->document()->setMaximumBlockCount(300);         // 限行防撑卡
    root->addWidget(m_log, 1);
    setCentralWidget(central);

    // ---- 曲线播种：把库里最近一小时的历史画进窗口（重启不白板）----
    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
    m_minuteBuckets = m_db->minuteCountsSince(nowSec - kCurveWindowMin * 60);
    for (const auto& pt : m_db->scoresSince(nowSec - kCurveWindowMin * 60))
        m_scorePts.append({pt.first, pt.second * 100.0});
    refreshStats();
    rebuildPlot();

    // ---- TCP 监听 ----
    connect(&m_server, &QTcpServer::newConnection, this, &ServerWindow::onNewConnection);
    if (!m_server.listen(QHostAddress::Any, kListenPort)) {
        log(QStringLiteral("监听 tcp/%1 失败：%2").arg(kListenPort)
                    .arg(m_server.errorString()), "red");
    } else {
        log(QStringLiteral("监听 tcp/%1，数据库 server_alarms.db").arg(kListenPort));
    }
}

ServerWindow::~ServerWindow() = default;

// ---- 新客户端接入：每连接挂一个 Agent，转发报警与断开事件 ----
void ServerWindow::onNewConnection() {
    while (QTcpSocket* sock = m_server.nextPendingConnection()) {
        auto* agent = new Agent(sock, this);
        const QString peer = agent->peer();
        connect(agent, &Agent::alarmReceived, this,
                [this, peer](const QJsonObject& obj) { onAlarmReceived(obj, peer); });
        connect(agent, &Agent::clientGone, this, &ServerWindow::onClientGone);
        // Agent 被 deleteLater 销毁时（连接断开），m_sock 也随之释放（父子关系）
        ++m_onlineClients;
        m_onlineLabel->setText(QStringLiteral("在线客户端：%1").arg(m_onlineClients));
        log(QStringLiteral("客户端接入：%1").arg(peer));
    }
}

void ServerWindow::onClientGone() {
    if (m_onlineClients > 0) --m_onlineClients;
    m_onlineLabel->setText(QStringLiteral("在线客户端：%1").arg(m_onlineClients));
    log("客户端断开");
}

// ---- 报警落地：入库 → 曲线增量 → 统计刷新 → 日志 ----
void ServerWindow::onAlarmReceived(const QJsonObject& obj, const QString& client) {
    // 时间优先用客户端上报值（时钟可能有偏差也不至于乱序太多），解析失败退回本机时间
    QDateTime t = QDateTime::fromString(obj["time"].toString(), Qt::ISODateWithMs);
    if (!t.isValid()) t = QDateTime::currentDateTime();
    const QString label  = obj["label"].toString();
    const double  score  = obj["score"].toDouble();
    const int     count  = obj["count"].toInt();

    m_db->addAlarm(t, label, score, count, client);       // 落库（失败静默）

    const qint64 sec = t.toSecsSinceEpoch();
    ++m_minuteBuckets[sec / 60];                          // 分钟桶 +1
    m_scorePts.append({sec, score * 100.0});              // 置信度散点
    pruneOldData();

    refreshStats();
    rebuildPlot();
    log(QStringLiteral("%1  %2  报警：%3  最高 %4%  共 %5 个目标")
                .arg(t.toString("hh:mm:ss"), client, label)
                .arg(score * 100, 0, 'f', 1).arg(count));
}

// ---- 查库刷新统计标签 ----
void ServerWindow::refreshStats() {
    m_totalLabel->setText(QStringLiteral("历史报警：%1").arg(m_db->totalCount()));
    m_todayLabel->setText(QStringLiteral("今日：%1").arg(m_db->todayCount()));
    const QMap<QString, int> byLabel = m_db->countByLabel();   // 存本地再遍历（临时量的迭代器不能跨容器比较）
    QStringList parts;
    for (auto it = byLabel.cbegin(); it != byLabel.cend(); ++it)
        parts << QStringLiteral("%1 ×%2").arg(it.key()).arg(it.value());
    m_byLabelLabel->setText(parts.isEmpty() ? QStringLiteral("分类：暂无")
                                            : QStringLiteral("分类：") + parts.join("  "));
    m_onlineLabel->setText(QStringLiteral("在线客户端：%1").arg(m_onlineClients));
}

// ---- 重画两条曲线（数据都在内存里，直接 setData）----
void ServerWindow::rebuildPlot() {
    const double now = double(QDateTime::currentSecsSinceEpoch());

    QVector<double> xs, ys;
    for (auto it = m_minuteBuckets.cbegin(); it != m_minuteBuckets.cend(); ++it) {
        xs << double(it.key() * 60) + 30.0;               // 分钟桶画在该分钟中点
        ys << it.value();
    }
    m_plot->graph(0)->setData(xs, ys);

    QVector<double> xs2, ys2;
    for (const auto& pt : m_scorePts) {
        xs2 << double(pt.first);
        ys2 << pt.second;
    }
    m_plot->graph(1)->setData(xs2, ys2);

    m_plot->xAxis->setRange(now - kCurveWindowMin * 60, now + 60);   // 滚动窗口
    m_plot->yAxis->rescale(true);                                  // 左轴随数据自适应
    if (m_plot->yAxis->range().upper < 2)
        m_plot->yAxis->setRange(0, 2);                             // 空数据时也留点高度
    m_plot->yAxis2->setRange(0, 100);
    m_plot->replot();
}

// ---- 丢掉曲线窗口外的历史点，防止长时间运行内存无限涨 ----
void ServerWindow::pruneOldData() {
    const qint64 horizon = QDateTime::currentSecsSinceEpoch() - kCurveWindowMin * 60;
    while (!m_minuteBuckets.isEmpty() && m_minuteBuckets.firstKey() * 60 < horizon)
        m_minuteBuckets.remove(m_minuteBuckets.firstKey());
    while (!m_scorePts.isEmpty() && m_scorePts.first().first < horizon)
        m_scorePts.removeFirst();
}

// ---- 带时间戳写一行日志（color 空 = 黑色）----
void ServerWindow::log(const QString& text, const QString& color) {
    const QString head = QTime::currentTime().toString("HH:mm:ss") + " >> ";
    m_log->append(QStringLiteral("<font color='%1'>%2%3</font>")
                          .arg(color.isEmpty() ? QStringLiteral("black") : color,
                               head, text.toHtmlEscaped()));
    QScrollBar* sb = m_log->verticalScrollBar();
    if (sb->value() >= sb->maximum())
        sb->setValue(sb->maximum());
}

#include "ServerWindow.moc"   // Agent 的 Q_OBJECT 在 .cpp 里，AUTOMOC 靠这行收尾
