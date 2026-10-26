//
// MainWindow 实现。
// 阅读顺序建议：构造函数 → do_KaiShi_clicked（最核心的槽）→ do_frameReady（显示）
//   → Detection::run()（对照着看数据怎么流动）。
//
#include "MainWindow.h"
#include <QDebug>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QComboBox>
#include <QLineEdit>
#include <QToolButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QVariant>
#include <QFileDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QUrl>
#include <QSoundEffect>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QRegularExpression>

#include "Detection.h"
#include "AlarmReporter.h"

// 报警统计平台地址（server/ 子工程默认监听本机 9000；连远程平台改这里）
static const QString kServerHost = QStringLiteral("127.0.0.1");
static const quint16 kServerPort = 9000;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_Detection = new Detection(this);      // 父对象机制：主窗口销毁时自动 delete
    buildUi();
    bindSignal();

    // 警报音：QSoundEffect 只支持 wav（need/JingBao/alarm.wav 由 CMake 拷到 exe 旁边）
    m_JingBao = new QSoundEffect(this);
    m_JingBao->setSource(QUrl::fromLocalFile(
            QCoreApplication::applicationDirPath() + "/need/JingBao/alarm.wav"));
    m_JingBao->setVolume(0.9f);
    m_JingBaoLengQue.invalidate();          // 计时器先置为"无效"，第一次警报立刻可响

    // 报警外发：连 server/ 子工程的报警统计平台（默认本机 9000 端口）。
    // 连不上 5 秒自动重连，平台在不在都不影响本地检测；地址见文件顶部的 kServerHost/kServerPort
    m_Reporter = new AlarmReporter(this);
    connect(m_Reporter, &AlarmReporter::connectedChanged, this, &MainWindow::do_reporterOnlineChanged);
    m_Reporter->connectToServer(kServerHost, kServerPort);

    loadConfig();                           // 恢复上次的界面设置

    displayLog("就绪。选择输入源，或直接点\"开始检测\"（默认打开 0 号摄像头）");
}

MainWindow::~MainWindow() = default;

// ==================== 界面搭建（手写布局，对应 Python 的 setupUi） ====================
void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("FireSmokeDetection — 实时目标检测"));
    resize(900,500);

    QWidget *central   = new QWidget(this);
    QHBoxLayout *layout_1 = new QHBoxLayout(central);
    QVBoxLayout *layout_2 = new QVBoxLayout;
    QVBoxLayout *layout_3 = new QVBoxLayout;
    layout_1 -> addLayout(layout_2,3);
    layout_1 -> addLayout(layout_3,1);

    //显示区域
    m_TuXiangShuChu = new QLabel();
    m_TuXiangShuChu->setAlignment(Qt::AlignCenter);
    m_TuXiangShuChu->setStyleSheet("background:#202020;color:#dddddd;");
    m_TuXiangShuChu->setText("等待输入源...");
    m_TuXiangShuChu->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);//防止窗口只能放大不能缩小
    layout_2 -> addWidget(m_TuXiangShuChu,9);
    QHBoxLayout *layout_4 = new QHBoxLayout;
    layout_2 -> addLayout(layout_4,1);
    m_QingChu = new QPushButton("清除");
    m_BaoCunRiZhi = new QPushButton("保存日志");
    QVBoxLayout *layout_5 = new QVBoxLayout;
    layout_5 -> addWidget(m_QingChu);
    layout_5 -> addWidget(m_BaoCunRiZhi);
    m_RiZhi = new QTextBrowser();
    // 日志限行：超过 300 条自动丢最旧的，防止长时间运行后 QTextBrowser 被撑卡
    m_RiZhi->document()->setMaximumBlockCount(300);
    layout_4 -> addWidget(m_RiZhi,9);
    layout_4 -> addLayout(layout_5,1);

    //操作区域
    m_tabWidget = new QTabWidget();
    layout_3 -> addWidget(m_tabWidget);
    QWidget *tabShuRu = new QWidget(m_tabWidget);
    QWidget *tabShuChu = new QWidget(m_tabWidget);
    QFormLayout *ShuRuYelayout = new QFormLayout(tabShuRu);
    QFormLayout *ShuChuYelayout = new QFormLayout(tabShuChu);
    m_tabWidget -> addTab(tabShuRu,"输入");
    m_tabWidget -> addTab(tabShuChu,"输出");
    //开始、停止
    QHBoxLayout *layout_Star = new QHBoxLayout;
    m_KaiShi = new QPushButton("开始");
    m_TingZhi = new QPushButton("停止");
    m_TingZhi->setEnabled(false);           //初始只有"开始"可点
    layout_Star -> addWidget(m_KaiShi);
    layout_Star -> addWidget(m_TingZhi);
    layout_3 -> addLayout(layout_Star);

    //输入页
    m_ShuRuFangShi = new QComboBox();
    // 枚举存进 item data（不依赖选项顺序），槽里用 itemData().value<SourceType>() 取回
    m_ShuRuFangShi->addItem("摄像头",   QVariant::fromValue(SourceType::Camera));
    m_ShuRuFangShi->addItem("图片/视频", QVariant::fromValue(SourceType::File));
    m_ShuRuFangShi->addItem("屏幕",     QVariant::fromValue(SourceType::Screen));
    ShuRuYelayout ->addRow("输入方式",m_ShuRuFangShi);

    m_WenJian = new QLineEdit;
    m_XuanZeWenJian = new QToolButton;
    m_XuanZeWenJian ->setText("...");
    QHBoxLayout *layout_choData = new QHBoxLayout;
    layout_choData -> addWidget(m_WenJian);
    layout_choData -> addWidget(m_XuanZeWenJian);
    ShuRuYelayout ->addRow("文件路径",layout_choData);

    m_QuanZhong = new QLineEdit;
    m_XuanZeQuanZhong = new QToolButton;
    m_XuanZeQuanZhong ->setText("...");
    QHBoxLayout *layout_choQuanZhong = new QHBoxLayout;
    layout_choQuanZhong -> addWidget(m_QuanZhong);
    layout_choQuanZhong -> addWidget(m_XuanZeQuanZhong);
    ShuRuYelayout ->addRow("权重路径",layout_choQuanZhong);

    m_LeiBie = new QLineEdit;
    m_XuanZeLeiBie = new QToolButton;
    m_XuanZeLeiBie ->setText("...");
    QHBoxLayout *layout_Type = new QHBoxLayout;
    layout_Type -> addWidget(m_LeiBie);
    layout_Type -> addWidget(m_XuanZeLeiBie);
    ShuRuYelayout ->addRow("类别路径",layout_Type);

    m_FanZhuanTuXiang = new QComboBox();
    m_FanZhuanTuXiang -> addItem("不翻转");
    m_FanZhuanTuXiang -> addItem("水平翻转");
    m_FanZhuanTuXiang -> addItem("垂直翻转");
    m_FanZhuanTuXiang -> addItem("水平垂直翻转");
    ShuRuYelayout ->addRow("翻转图像",m_FanZhuanTuXiang);

    m_XuanZhuanTuXiang = new QComboBox();
    m_XuanZhuanTuXiang -> addItem("0°");
    m_XuanZhuanTuXiang -> addItem("+90°");
    m_XuanZhuanTuXiang -> addItem("-90°");
    m_XuanZhuanTuXiang -> addItem("180°");
    ShuRuYelayout ->addRow("旋转图像",m_XuanZhuanTuXiang);

    //输出页
    m_XianShiFPS = new QCheckBox("显示帧率");
    m_XianShiFPS -> setLayoutDirection(Qt::RightToLeft);
    m_XianShiFPS -> setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_XianShiFPS->setChecked(true);
    ShuChuYelayout -> addRow(m_XianShiFPS);

    m_KaiQiJingBao = new QCheckBox("开启警报");
    m_KaiQiJingBao -> setLayoutDirection(Qt::RightToLeft);
    m_KaiQiJingBao -> setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_KaiQiJingBao->setChecked(true);
    ShuChuYelayout -> addRow(m_KaiQiJingBao);

    m_DaYinRiZhi = new QCheckBox("打印日志");
    m_DaYinRiZhi -> setLayoutDirection(Qt::RightToLeft);
    m_DaYinRiZhi -> setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_DaYinRiZhi->setChecked(true);
    ShuChuYelayout -> addRow(m_DaYinRiZhi);

    m_XianShiMaoKuang = new QCheckBox("显示锚框");
    m_XianShiMaoKuang -> setLayoutDirection(Qt::RightToLeft);
    m_XianShiMaoKuang -> setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_XianShiMaoKuang->setChecked(true);
    QLabel *Label_chosecoloer = new QLabel("锚框颜色");
    Label_chosecoloer -> setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_MaoKuangYanSe = new QToolButton;
    m_MaoKuangYanSe->setText("...");
    QHBoxLayout *layout_MaoKuang = new QHBoxLayout;
    layout_MaoKuang -> addWidget(m_XianShiMaoKuang);
    layout_MaoKuang -> addWidget(Label_chosecoloer);
    layout_MaoKuang -> addWidget(m_MaoKuangYanSe);
    ShuChuYelayout -> addRow(layout_MaoKuang);

    m_LuZhiShiPin = new QCheckBox("录制视频");
    m_LuZhiShiPin -> setLayoutDirection(Qt::RightToLeft);
    m_LuZhiShiPin -> setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QLabel *Label_saveFPS = new QLabel("录制帧率");
    m_LuZhiZhenLv = new QSpinBox;
    m_LuZhiZhenLv->setRange(1, 60);
    m_LuZhiZhenLv->setValue(15);
    m_LuZhiZhenLv->setSuffix(tr(" fps"));
    QHBoxLayout *layout_Vido = new QHBoxLayout;
    layout_Vido -> addWidget(m_LuZhiShiPin);
    layout_Vido -> addWidget(Label_saveFPS);
    layout_Vido -> addWidget(m_LuZhiZhenLv);
    ShuChuYelayout -> addRow(layout_Vido);

    m_ZhiXinDu = new QDoubleSpinBox;
    m_ZhiXinDu->setRange(0.00, 1.00);
    m_ZhiXinDu->setSingleStep(0.05);
    m_ZhiXinDu->setDecimals(2);
    m_ZhiXinDu->setValue(0.35);
    ShuChuYelayout -> addRow("置信度阈值",m_ZhiXinDu);

    m_IOU = new QDoubleSpinBox;
    m_IOU->setRange(0.10, 1.00);
    m_IOU->setSingleStep(0.05);
    m_IOU->setDecimals(2);
    m_IOU->setValue(0.35);
    ShuChuYelayout -> addRow("IOU阈值",m_IOU);

    m_BaoCun = new QLineEdit;
    m_XuanZeLuJiing = new QToolButton();
    m_XuanZeLuJiing -> setText("...");
    QHBoxLayout *layout_BaoCun = new QHBoxLayout;
    layout_BaoCun -> addWidget(m_BaoCun);
    layout_BaoCun -> addWidget(m_XuanZeLuJiing);
    ShuChuYelayout -> addRow("保存路径",layout_BaoCun);

    m_JieTu = new QPushButton("截图");
    ShuChuYelayout -> addRow(m_JieTu);

    setCentralWidget(central);
}

// ==================== 信号连接（全部 connect 集中在此，对应 Python 的 UI()） ====================
void MainWindow::bindSignal() {
    connect(m_QingChu,&QPushButton::clicked,this,&MainWindow::do_QingChu_clicked);
    connect(m_BaoCunRiZhi,&QPushButton::clicked,this,&MainWindow::do_BaoCunRiZhi_clicked);

    connect(m_KaiShi, &QPushButton::clicked, this, &MainWindow::do_KaiShi_clicked);
    connect(m_TingZhi,&QPushButton::clicked, this, &MainWindow::do_TingZhi_clicked);

    //输入页
    connect(m_ShuRuFangShi,&QComboBox::currentIndexChanged,this,&MainWindow::do_ShuRuFangShi_indexChanged);
    connect(m_XuanZeWenJian,&QToolButton::clicked,this,&MainWindow::do_XuanZeWenJian_clicked);
    connect(m_XuanZeQuanZhong,&QToolButton::clicked,this,&MainWindow::do_XuanZeQuanZhong_clicked);
    connect(m_XuanZeLeiBie,&QToolButton::clicked,this,&MainWindow::do_XuanZeLeiBie_clicked);
    connect(m_FanZhuanTuXiang,&QComboBox::currentIndexChanged,this,&MainWindow::do_FanZhuanTuXiang_indexChanged);
    connect(m_XuanZhuanTuXiang,&QComboBox::currentIndexChanged,this,&MainWindow::do_XuanZhuanTuXiang_indexChanged);

    //输出页
    connect(m_XianShiFPS,&QCheckBox::checkStateChanged,this,&MainWindow::do_XianShiFPS_StateChanged);
    connect(m_KaiQiJingBao,&QCheckBox::checkStateChanged,this,&MainWindow::do_KaiQiJingBao_StateChanged);
    connect(m_DaYinRiZhi,&QCheckBox::checkStateChanged,this,&MainWindow::do_DaYinRiZhi_StateChanged);
    connect(m_XianShiMaoKuang,&QCheckBox::checkStateChanged,this,&MainWindow::do_XianShiMaoKuang_StateChanged);
    connect(m_MaoKuangYanSe,&QPushButton::clicked,this,&MainWindow::do_MaoKuangYanSe_clicked);
    connect(m_LuZhiShiPin,&QCheckBox::checkStateChanged,this,&MainWindow::do_LuZhiShiPin_StateChanged);
    connect(m_LuZhiZhenLv,&QSpinBox::valueChanged,this,&MainWindow::do_LuZhiZhenLv_valueChanged);
    connect(m_ZhiXinDu,&QDoubleSpinBox::valueChanged,this,&MainWindow::do_ZhiXinDu_valueChanged);
    connect(m_IOU,&QDoubleSpinBox::valueChanged,this,&MainWindow::do_IOU_valueChanged);
    connect(m_XuanZeLuJiing,&QToolButton::clicked,this,&MainWindow::do_XuanZeLuJiing_clicked);
    connect(m_JieTu,&QPushButton::clicked,this,&MainWindow::do_JieTu_clicked);

    // ---- 检测线程信号 → 界面槽 ----
    // 发射方在检测线程、接收方在主线程，Qt 自动用排队连接（参数被拷贝进事件队列）
    connect(m_Detection, &Detection::frameReady,     this, &MainWindow::do_frameReady);
    connect(m_Detection, &Detection::resultReady,    this, &MainWindow::do_resultReady);
    connect(m_Detection, &Detection::targetDetected, this, &MainWindow::do_targetDetected);
    connect(m_Detection, &Detection::alarmTriggered, this, &MainWindow::do_alarmTriggered);
    connect(m_Detection, &Detection::errorOccurred,  this, &MainWindow::do_errorOccurred);
    connect(m_Detection, &Detection::sourceFinished, this, &MainWindow::do_sourceFinished);
    connect(m_Detection, &Detection::sourceOpened,   this, &MainWindow::do_sourceOpened);
    connect(m_Detection, &Detection::infoMessage,    this, &MainWindow::do_infoMessage);
}

// ==================== 内部辅助：打包检测配置 ====================
// 从界面控件打包 {模型路径, 类别名}。开始检测与检测中换模型两处共用。
// 其余可调参数都是热参数（atomic/锁，各自改动时已实时同步），不进信箱
DetectConfig MainWindow::packDetectConfig() const {
    DetectConfig cfg;
    cfg.modelPath = m_QuanZhong->text().trimmed();
    // 类别编辑框的文本 → 列表（容忍中英文逗号、竖线、换行）
    QStringList names;
    for (const QString &tok : m_LeiBie->text().split(QRegularExpression("[,，|\\n]")))
        if (!tok.trimmed().isEmpty()) names << tok.trimmed();
    for (const QString &n : names) cfg.classNames.push_back(n.toStdString());
    return cfg;
}

// ==================== 槽函数：操作区 ====================
// 校验 → 打包配置投进信箱 → 确保源已打开 → 启动推理
void MainWindow::do_KaiShi_clicked() {
    // ① 校验权重文件（廉价的检查同步做，昂贵/易错的打开动作交给线程）
    const QString model = m_QuanZhong->text().trimmed();
    if (model.isEmpty() || !QFile::exists(model)) {
        displayLog(QString("\"%1\" 模型文件不存在").arg(model), "red");
        return;
    }

    // ② 源没打开过就先请求打开（选文件时已经打开过就跳过，避免视频从头再放）
    if (!m_sourceOpen) {
        const auto type = m_ShuRuFangShi->itemData(m_ShuRuFangShi->currentIndex()).value<SourceType>();
        if (type == SourceType::Camera) {
            m_Detection->requestSource(SourceType::Camera, "0");
        } else if (type == SourceType::File) {
            if (!QFile::exists(m_WenJian->text())) {
                displayLog(QString("\"%1\" 文件不存在").arg(m_WenJian->text()), "red");
                return;
            }
            m_Detection->requestSource(SourceType::File, m_WenJian->text());
        } else {
            displayLog("屏幕捕获暂未实现", "red");
            return;
        }
    }

    // ③ 打包 {模型路径, 类别名} 投进信箱（其余参数都是热参数，随时已同步）
    m_Detection->requestDetect(packDetectConfig());

    // ④ 起飞：线程没跑就启动，再把推理开关拨到"开"
    m_Detection->StartThread();
    m_Detection->BeginDetect();
    setRunningUi(true);
    displayLog("开始检测");
}

// "停止检测"。只关推理，预览继续（再点开始不用重开源）
void MainWindow::do_TingZhi_clicked() {
    m_Detection->StopDetect();
    setRunningUi(false);
    displayLog("停止检测");
}

void MainWindow::setRunningUi(bool running) {
    m_detectingUi = running;
    m_KaiShi->setEnabled(!running);
    m_TingZhi->setEnabled(running);
}

// ==================== 槽函数：输入页 ====================
// 切换输入方式：先停推理 → 按新类型请求开源 → 确保线程在跑（对应 Python indexChanged）
void MainWindow::do_ShuRuFangShi_indexChanged(int index) {
    const auto type = m_ShuRuFangShi->itemData(index).value<SourceType>();
    m_Detection->StopDetect();
    setRunningUi(false);
    m_sourceOpen = false;

    if (type == SourceType::Camera) {
        m_Detection->requestSource(SourceType::Camera, "0");
    } else if (type == SourceType::File) {
        if (!QFile::exists(m_WenJian->text())) {
            displayLog(QString("\"%1\" 文件不存在，请先选择文件").arg(m_WenJian->text()), "red");
            return;
        }
        m_Detection->requestSource(SourceType::File, m_WenJian->text());
    } else {
        displayLog("屏幕捕获暂未实现", "red");
        return;
    }
    m_Detection->StartThread();
}

// 选择媒体文件：选完立刻请求打开并预览（对应 Python changeMediaFile → setSource）
void MainWindow::do_XuanZeWenJian_clicked() {
    const QString path = QFileDialog::getOpenFileName(this, "选择文件",
            m_WenJian->text(),
            "*.asf *.avi *.gif *.m4v *.mkv *.mov *.mp4 *.mpeg *.mpg *.ts *.wmv "
            "*.bmp *.dng *.jpeg *.jpg *.mpo *.png *.tif *.tiff *.webp");
    if (path.isEmpty()) return;
    m_WenJian->setText(path);
    m_ShuRuFangShi->blockSignals(true);              //手动同步下拉框，避免再触发 indexChanged
    m_ShuRuFangShi->setCurrentIndex(1);
    m_ShuRuFangShi->blockSignals(false);
    m_Detection->StopDetect();
    setRunningUi(false);
    m_Detection->requestSource(SourceType::File, path);
    m_sourceOpen = false;
    m_Detection->StartThread();
}

// 选择权重：选完顺手找同名类别文件自动填（M0.9.onnx → M0.9.txt）。
// 用 completeBaseName 保留 "M0.9" 的点号 —— Python 版 split('.') 会把点号当分隔符丢掉
void MainWindow::do_XuanZeQuanZhong_clicked() {
    const QString path = QFileDialog::getOpenFileName(this, "选择模型",
            m_QuanZhong->text(), "ONNX 模型 (*.onnx)");
    if (path.isEmpty()) return;
    m_QuanZhong->setText(path);

    const QFileInfo info(path);
    const QString base = info.completeBaseName();
    const QStringList candidates {
            info.dir().filePath(base + ".txt"),            // 和模型同目录
            info.dir().filePath("../" + base + ".txt"),    // 或上一级
    };
    for (const QString &c : candidates) {
        if (QFile::exists(c)) {
            m_LeiBie->setText(loadClassFile(c));
            displayLog("已自动加载类别文件：" + c);
            break;
        }
    }

    // 检测进行中：选完立即投信箱热切换模型。检测线程在两帧之间销毁旧模型、
    // 加载新模型（期间画面停顿零点几秒、解码线程照常丢帧保实时，属正常）；
    // 加载失败会走 errorOccurred → 回预览态，不影响预览
    if (m_detectingUi) {
        m_Detection->requestDetect(packDetectConfig());
        displayLog("检测中切换模型：" + path);
    }
}

// 选择类别文件：读文件（一行一个类别名）→ 填进编辑框
void MainWindow::do_XuanZeLeiBie_clicked() {
    const QString path = QFileDialog::getOpenFileName(this
                                                        , "选择类别文件"
                                                        ,QString()
                                                        , "类别文件 (*.txt)"
                                                        );
    if (path.isEmpty()) return;
    const QString text = loadClassFile(path);
    if (!text.isEmpty()) m_LeiBie->setText(text);
}

// 读类别文件。格式是硬约定（README 有说明）：一行一个类别名，行序 = 类别索引
// （第 1 行 = 类别 0，必须与训练时的类别顺序一致）。
// 不做宽容解析：不剥行首序号（"1 fire" 会被当成名字 "1 fire"），也不支持逗号/竖线分隔。
// 名字个数与模型输出类别数是否匹配在检测线程首帧校验（Detection::run），不在这里管
QString MainWindow::loadClassFile(const QString &path) const {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QStringList out;
    for (QString line : QString::fromUtf8(f.readAll()).split('\n')) {
        line = line.trimmed();              // 去首尾空白，顺带处理 Windows 的行尾 \r
        if (!line.isEmpty()) out << line;   // 空行跳过
    }
    return out.join(",");
}

// 翻转/旋转下拉框：直接写 Detection 的 atomic 热参数，下一帧生效
void MainWindow::do_FanZhuanTuXiang_indexChanged(int index) {
    m_Detection->setFlip(index);
}

void MainWindow::do_XuanZhuanTuXiang_indexChanged(int index) {
    m_Detection->setRotate(index);
}

// ==================== 槽函数：输出页 ====================
void MainWindow::do_XianShiFPS_StateChanged(Qt::CheckState state) {
    m_Detection->setDisplayFps(state == Qt::Checked);
}

void MainWindow::do_KaiQiJingBao_StateChanged(Qt::CheckState state) {
    m_alarmEnabled = (state == Qt::Checked);
}

void MainWindow::do_DaYinRiZhi_StateChanged(Qt::CheckState state) {
    m_printResult = (state == Qt::Checked);
}

void MainWindow::do_XianShiMaoKuang_StateChanged(Qt::CheckState state) {
    m_Detection->setDrawBox(state == Qt::Checked);
}

// 选锚框颜色：存起来 + 给按钮上色，开始检测时随配置进线程
void MainWindow::do_MaoKuangYanSe_clicked() {
    const QColor c = QColorDialog::getColor(m_boxColor, this, "选择锚框颜色");
    if (!c.isValid()) return;
    m_boxColor = c;
    m_MaoKuangYanSe->setStyleSheet(
            QString("background-color: rgb(%1,%2,%3);").arg(c.red()).arg(c.green()).arg(c.blue()));
    m_Detection->setBoxColor(cv::Scalar(c.red(), c.green(), c.blue()));
}

// 录像开关：热参数——检测中勾上立即从下一帧起新文件，取消勾选立刻封存当前文件。
// 同时锁定/解锁"录制帧率"和"保存路径"：录制进行中不许改（改了也只影响下一个文件）
void MainWindow::do_LuZhiShiPin_StateChanged(Qt::CheckState state) {
    const bool on = (state == Qt::Checked);
    m_Detection->setRecordOn(on);
    m_LuZhiZhenLv->setEnabled(!on);
    m_BaoCun->setEnabled(!on);
    m_XuanZeLuJiing->setEnabled(!on);
}

// 录制帧率：热参数。只在开新录像文件时读一次，改动只影响下一个文件（当前文件头已写死）
void MainWindow::do_LuZhiZhenLv_valueChanged(int v) {
    m_Detection->setRecordFps(v);
}

// 置信度/IOU：滑动即时生效（热更新走 atomic，不用重开模型）
void MainWindow::do_ZhiXinDu_valueChanged(double v) {
    m_Detection->setConf(static_cast<float>(v));
}

void MainWindow::do_IOU_valueChanged(double v) {
    m_Detection->setIou(static_cast<float>(v));
}

// 选择保存目录（截图/录像/日志都存这）
void MainWindow::do_XuanZeLuJiing_clicked() {
    const QString dir = QFileDialog::getExistingDirectory(this, "选择保存位置", m_BaoCun->text());
    if (!dir.isEmpty()) {
        m_BaoCun->setText(dir);
        m_Detection->setSaveDir(dir);      // 热参数：只影响下一个录像文件（截图/日志在点击时现读，不走线程）
    }
}

// 截图：把当前显示画面存成 png
void MainWindow::do_JieTu_clicked() {
    const QPixmap pm = m_TuXiangShuChu->pixmap();   // Qt 6.8 起 pixmap() 按值返回
    if (pm.isNull()) return;
    QDir().mkpath(m_BaoCun->text());
    const QString path = m_BaoCun->text() + "/ScreenShot_" +
                         QDateTime::currentDateTime().toString("MM-dd HH-mm-ss") + ".png";
    if (pm.save(path)) displayLog("截图已保存：" + path);
    else displayLog("截图保存失败：" + path, "red");
}

// ==================== 槽函数：日志区 ====================
void MainWindow::do_QingChu_clicked() {
    m_RiZhi->clear();
}

void MainWindow::do_BaoCunRiZhi_clicked() {
    QDir().mkpath(m_BaoCun->text());
    const QString path = m_BaoCun->text() + "/log_" +
                         QDateTime::currentDateTime().toString("MM-dd HH-mm-ss") + ".log";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(m_RiZhi->toPlainText().toUtf8());
        displayLog("日志已保存：" + path);
    } else {
        displayLog("日志保存失败：" + path, "red");
    }
}

// ==================== 内部辅助 ====================
void MainWindow::displayLog(const QString &text, const QString &color) {
    const QString head = QTime::currentTime().toString("HH:mm:ss.zzz") + " >> ";    // 带时间戳写一行日志
    m_RiZhi->append(QString("<font color='%1'>%2%3</font>")
                            .arg(color, head, text.toHtmlEscaped()));
}

// ==================== 检测线程信号的接收槽 ====================
// 显示画面：QImage → QPixmap，按标签大小等比缩放
void MainWindow::do_frameReady(const QImage &img) {
    m_TuXiangShuChu->setPixmap(QPixmap::fromImage(img).scaled(
            m_TuXiangShuChu->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

// 检测结果 → 日志。按类别分组显示
// fire ×2 (85.3%, 71.0%)；smoke ×1 (66.2%)
void MainWindow::do_resultReady(const std::vector<DetectionResult> &results) {
    if (!m_printResult) return;
    if (results.empty()) {
        displayLog("未检测到目标");
        return;
    }
    QMap<QString, QVector<double>> grouped;      // label → 各目标的置信度百分数
    for (const auto &r : results)
        grouped[QString::fromStdString(r.label)].append(r.score * 100.0);
    QStringList parts;
    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        QStringList ss;
        for (double s : it.value()) ss << QString::number(s, 'f', 1) + "%";
        parts << QString("%1 ×%2 (%3)")
                        .arg(it.key())
                        .arg(it.value().size())
                        .arg(ss.join(", "));        //输出格式是：fire ×1 (51.9%)
    }
    displayLog(parts.join("；"));
}

// 警报：检测线程连续 10 帧检出任意目标后发来。本地再加 5 秒冷却防连响
void MainWindow::do_targetDetected() {
    if (!m_alarmEnabled) return;
    if (m_JingBaoLengQue.isValid() && m_JingBaoLengQue.elapsed() < 5000) return;
    m_JingBao->play();
    m_JingBaoLengQue.restart();
    displayLog("警告：检测到目标！", "red");
}

// 报警外发：检测线程 alarmTriggered 传来明细 → 打包 AlarmRecord 发统计平台。
// 未在线时 AlarmReporter 内部静默丢弃，本地检测不受影响
void MainWindow::do_alarmTriggered(const std::vector<DetectionResult> &r) {
    if (r.empty()) return;
    AlarmRecord rec;
    rec.time  = QDateTime::currentDateTime();
    rec.count = static_cast<int>(r.size());
    QStringList labels;                       // 去重后的类别名
    for (const auto& d : r) {
        const QString name = QString::fromStdString(d.label);
        if (!labels.contains(name)) labels << name;
        if (d.score > rec.score) rec.score = d.score;   // 顺手挑最高置信度
    }
    rec.label = labels.join(", ");
    m_Reporter->reportAlarm(rec);
}

// 平台连接状态：只在"在线→掉线"时记掉线日志（重试期间每次失败都记会刷屏）
void MainWindow::do_reporterOnlineChanged(bool online) {
    if (online)
        displayLog(QString("已连接报警统计平台 (%1:%2)").arg(kServerHost).arg(kServerPort));
    else if (m_reporterWasOnline)
        displayLog("报警统计平台连接断开，5 秒后自动重连");
    m_reporterWasOnline = online;
}

void MainWindow::do_errorOccurred(const QString &msg) {
    displayLog(msg, "red");
    setRunningUi(false);
    m_sourceOpen = false;
}

void MainWindow::do_sourceFinished() {
    displayLog("输入源播放结束");
    setRunningUi(false);
    m_sourceOpen = false;
}

void MainWindow::do_sourceOpened(const QString &desc) {
    displayLog("输入源已打开：" + desc);
    m_sourceOpen = true;
}

void MainWindow::do_infoMessage(const QString &msg) {
    displayLog(msg);
}

// ==================== 配置持久化（QSettings，对应 Python 的 cfg 类） ====================
void MainWindow::loadConfig() {
    QSettings s("FireSmokeDetection", "FireSmokeDetection");
    const QString appDir = QCoreApplication::applicationDirPath();

    m_QuanZhong->setText(s.value("modelPath", appDir + "/need/m0.9.onnx").toString());
    m_BaoCun->setText(s.value("outDir", appDir + "/out").toString());
    m_WenJian->setText(s.value("sourceFile").toString());
    m_LeiBie->setText(s.value("classes", "fire,smoke").toString());
    m_ZhiXinDu->setValue(s.value("conf", 0.35).toDouble());
    m_IOU->setValue(s.value("iou", 0.35).toDouble());
    m_LuZhiZhenLv->setValue(s.value("recordFps", 15).toInt());

    m_XianShiFPS->setChecked(s.value("showFps", true).toBool());
    m_KaiQiJingBao->setChecked(s.value("alarm", true).toBool());
    m_DaYinRiZhi->setChecked(s.value("printResult", true).toBool());
    m_XianShiMaoKuang->setChecked(s.value("drawBox", true).toBool());
    m_LuZhiShiPin->setChecked(s.value("record", false).toBool());

    m_boxColor = s.value("boxColor", QColor(255, 0, 0)).value<QColor>();
    m_MaoKuangYanSe->setStyleSheet(QString("background-color: rgb(%1,%2,%3);")
                                           .arg(m_boxColor.red()).arg(m_boxColor.green())
                                           .arg(m_boxColor.blue()));
    m_Detection->setBoxColor(cv::Scalar(m_boxColor.red(), m_boxColor.green(), m_boxColor.blue()));

    // 下拉框恢复时挡住信号，避免启动瞬间就去开摄像头/文件
    m_FanZhuanTuXiang->blockSignals(true);
    m_FanZhuanTuXiang->setCurrentIndex(s.value("flip", 0).toInt());
    m_FanZhuanTuXiang->blockSignals(false);
    m_XuanZhuanTuXiang->blockSignals(true);
    m_XuanZhuanTuXiang->setCurrentIndex(s.value("rotate", 0).toInt());
    m_XuanZhuanTuXiang->blockSignals(false);
    m_ShuRuFangShi->blockSignals(true);
    m_ShuRuFangShi->setCurrentIndex(s.value("sourceType", 0).toInt());
    m_ShuRuFangShi->blockSignals(false);

    m_Detection->setFlip(m_FanZhuanTuXiang->currentIndex());
    m_Detection->setRotate(m_XuanZhuanTuXiang->currentIndex());

    // 录像热参数同步恢复值（复选框恢复时会触发自己的槽同步开关；帧率/路径setText不触发信号，手动同步）
    m_Detection->setRecordFps(m_LuZhiZhenLv->value());
    m_Detection->setSaveDir(m_BaoCun->text());
}

void MainWindow::saveConfig() {
    QSettings s("FireSmokeDetection", "FireSmokeDetection");
    s.setValue("modelPath", m_QuanZhong->text());
    s.setValue("outDir", m_BaoCun->text());
    s.setValue("sourceFile", m_WenJian->text());
    s.setValue("classes", m_LeiBie->text());
    s.setValue("conf", m_ZhiXinDu->value());
    s.setValue("iou", m_IOU->value());
    s.setValue("recordFps", m_LuZhiZhenLv->value());
    s.setValue("showFps", m_XianShiFPS->isChecked());
    s.setValue("alarm", m_KaiQiJingBao->isChecked());
    s.setValue("printResult", m_DaYinRiZhi->isChecked());
    s.setValue("drawBox", m_XianShiMaoKuang->isChecked());
    s.setValue("record", m_LuZhiShiPin->isChecked());
    s.setValue("flip", m_FanZhuanTuXiang->currentIndex());
    s.setValue("rotate", m_XuanZhuanTuXiang->currentIndex());
    s.setValue("sourceType", m_ShuRuFangShi->currentIndex());
    s.setValue("boxColor", m_boxColor);
}

// ==================== 关窗拦截 ====================
void MainWindow::closeEvent(QCloseEvent *event) {
    // 如果正在检测就弹窗确认
    if (m_detectingUi) {
        const auto ret = QMessageBox::question(this, windowTitle(),
                "检测正在进行，确定停止并退出吗？",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ret != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    saveConfig();
    m_Reporter->disconnectFromServer();   // 主动断开统计平台（抑制自动重连）
    m_Detection->StopThread();   // 通知线程退出并等它收尾
    m_Detection->wait();         // 等它收完尾（最多约 200ms）
    event->accept();
}
