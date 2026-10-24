//
// MainWindow 实现（Tiny 版）。
// 阅读顺序：do_KaiShi_clicked（打包配置+起线程）→ do_frameReady（显示）
//   → Detection::run()（对照着看一帧怎么流动）。
//
#include "MainWindow.h"
#include "Detection.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QToolButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QRegularExpression>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_Detection = new Detection(this);      //父对象机制：窗口销毁时自动 delete
    buildUi();
    bindSignal();
}

MainWindow::~MainWindow() = default;

// ==================== 界面搭建 ====================
void MainWindow::buildUi() {
    resize(900, 520);

    QWidget *central = new QWidget(this);
    QHBoxLayout *layout_1 = new QHBoxLayout(central);
    QVBoxLayout *layout_2 = new QVBoxLayout;    //左：画面 + 结果
    QVBoxLayout *layout_3 = new QVBoxLayout;    //右：设置 + 开始/停止
    layout_1->addLayout(layout_2, 3);
    layout_1->addLayout(layout_3, 1);

    //主区域：画面 + 结果标签（原来的日志区 QTextBrowser 整个删掉了）
    m_TuXiangShuChu = new QLabel();
    m_TuXiangShuChu->setAlignment(Qt::AlignCenter);
    m_TuXiangShuChu->setStyleSheet("background:#202020;color:#dddddd;");
    m_TuXiangShuChu->setText("等待检测...");
    m_TuXiangShuChu->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);//防止窗口只能放大不能缩小
    layout_2->addWidget(m_TuXiangShuChu, 9);

    m_StatusLabel = new QLabel("状态：就绪");
    m_StatusLabel->setWordWrap(true);
    m_StatusLabel->setStyleSheet("font-size:14px; padding:4px;");
    layout_2->addWidget(m_StatusLabel, 1);

    //右侧：设置页 + 开始/停止
    m_tabWidget = new QTabWidget();
    layout_3->addWidget(m_tabWidget);
    QWidget *tabShuRu = new QWidget(m_tabWidget);
    QWidget *tabShuChu = new QWidget(m_tabWidget);
    QFormLayout *ShuRuYelayout = new QFormLayout(tabShuRu);
    QFormLayout *ShuChuYelayout = new QFormLayout(tabShuChu);
    m_tabWidget->addTab(tabShuRu, "输入");
    m_tabWidget->addTab(tabShuChu, "输出");

    QHBoxLayout *layout_Star = new QHBoxLayout;
    m_KaiShi = new QPushButton("开始检测");
    m_TingZhi = new QPushButton("停止");
    m_TingZhi->setEnabled(false);
    layout_Star->addWidget(m_KaiShi);
    layout_Star->addWidget(m_TingZhi);
    layout_3->addLayout(layout_Star);

    //输入页：只有视频文件（摄像头/屏幕已移除）。路径框只读，全靠"..."按钮选
    m_WenJian = new QLineEdit;
    m_WenJian->setReadOnly(true);           //只读：防止手输错误路径
    m_WenJian->setPlaceholderText("点右侧按钮选择视频/图片");
    m_XuanZeWenJian = new QToolButton;
    m_XuanZeWenJian->setText("...");
    QHBoxLayout *layout_choData = new QHBoxLayout;
    layout_choData->addWidget(m_WenJian);
    layout_choData->addWidget(m_XuanZeWenJian);
    ShuRuYelayout->addRow("文件路径", layout_choData);

    m_QuanZhong = new QLineEdit;
    m_QuanZhong->setReadOnly(true);
    m_QuanZhong->setPlaceholderText("点右侧按钮选择 onnx 模型");
    m_XuanZeQuanZhong = new QToolButton;
    m_XuanZeQuanZhong->setText("...");
    QHBoxLayout *layout_choQuanZhong = new QHBoxLayout;
    layout_choQuanZhong->addWidget(m_QuanZhong);
    layout_choQuanZhong->addWidget(m_XuanZeQuanZhong);
    ShuRuYelayout->addRow("权重路径", layout_choQuanZhong);

    m_LeiBie = new QLineEdit;
    m_LeiBie->setReadOnly(true);            //类别只由类别文件填充，不允许手改
    m_LeiBie->setPlaceholderText("选权重后自动配对，或点按钮选 txt");
    m_XuanZeLeiBie = new QToolButton;
    m_XuanZeLeiBie->setText("...");
    QHBoxLayout *layout_Type = new QHBoxLayout;
    layout_Type->addWidget(m_LeiBie);
    layout_Type->addWidget(m_XuanZeLeiBie);
    ShuRuYelayout->addRow("类别路径", layout_Type);

    m_FanZhuanTuXiang = new QComboBox();
    m_FanZhuanTuXiang->addItem("不翻转");
    m_FanZhuanTuXiang->addItem("水平翻转");
    m_FanZhuanTuXiang->addItem("垂直翻转");
    m_FanZhuanTuXiang->addItem("水平垂直翻转");
    ShuRuYelayout->addRow("翻转图像", m_FanZhuanTuXiang);

    m_XuanZhuanTuXiang = new QComboBox();
    m_XuanZhuanTuXiang->addItem("0°");
    m_XuanZhuanTuXiang->addItem("+90°");
    m_XuanZhuanTuXiang->addItem("-90°");
    m_XuanZhuanTuXiang->addItem("180°");
    ShuRuYelayout->addRow("旋转图像", m_XuanZhuanTuXiang);

    //输出页
    m_XianShiFPS = new QCheckBox("显示帧率");
    m_XianShiFPS->setLayoutDirection(Qt::RightToLeft);
    m_XianShiFPS -> setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_XianShiFPS->setChecked(true);
    ShuChuYelayout->addRow(m_XianShiFPS);

    m_XianShiMaoKuang = new QCheckBox("显示锚框");
    m_XianShiMaoKuang->setLayoutDirection(Qt::RightToLeft);
    m_XianShiMaoKuang -> setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_XianShiMaoKuang->setChecked(true);
    QLabel *Label_chosecoloer = new QLabel("锚框颜色");
    Label_chosecoloer -> setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_MaoKuangYanSe = new QToolButton;
    m_MaoKuangYanSe->setText("...");
    QHBoxLayout *layout_MaoKuang = new QHBoxLayout;
    layout_MaoKuang->addWidget(m_XianShiMaoKuang);
    layout_MaoKuang->addWidget(Label_chosecoloer);
    layout_MaoKuang->addWidget(m_MaoKuangYanSe);
    ShuChuYelayout->addRow(layout_MaoKuang);

    m_ZhiXinDu = new QDoubleSpinBox;    m_ZhiXinDu->setRange(0.00, 1.00);
    m_ZhiXinDu->setSingleStep(0.05);
    m_ZhiXinDu->setDecimals(2);
    m_ZhiXinDu->setValue(0.35);
    ShuChuYelayout->addRow("置信度阈值", m_ZhiXinDu);

    m_IOU = new QDoubleSpinBox;
    m_IOU->setRange(0.10, 1.00);
    m_IOU->setSingleStep(0.05);
    m_IOU->setDecimals(2);
    m_IOU->setValue(0.35);
    ShuChuYelayout->addRow("IOU阈值", m_IOU);

    m_BaoCun = new QLineEdit;
    m_BaoCun->setReadOnly(true);
    m_BaoCun->setText(QCoreApplication::applicationDirPath() + "/out");
    m_XuanZeLuJiing = new QToolButton();
    m_XuanZeLuJiing->setText("...");
    QHBoxLayout *layout_BaoCun = new QHBoxLayout;
    layout_BaoCun->addWidget(m_BaoCun);
    layout_BaoCun->addWidget(m_XuanZeLuJiing);
    ShuChuYelayout->addRow("保存路径", layout_BaoCun);

    m_JieTu = new QPushButton("截图");
    ShuChuYelayout->addRow(m_JieTu);

    setCentralWidget(central);
}

// ==================== 信号连接 ====================
void MainWindow::bindSignal() {
    connect(m_KaiShi, &QPushButton::clicked, this, &MainWindow::do_KaiShi_clicked);
    connect(m_TingZhi, &QPushButton::clicked, this, &MainWindow::do_TingZhi_clicked);
    connect(m_XuanZeWenJian, &QToolButton::clicked, this, &MainWindow::do_XuanZeWenJian_clicked);
    connect(m_XuanZeQuanZhong, &QToolButton::clicked, this, &MainWindow::do_XuanZeQuanZhong_clicked);
    connect(m_XuanZeLeiBie, &QToolButton::clicked, this, &MainWindow::do_XuanZeLeiBie_clicked);
    connect(m_XuanZeLuJiing, &QToolButton::clicked, this, &MainWindow::do_XuanZeLuJiing_clicked);
    connect(m_MaoKuangYanSe, &QToolButton::clicked, this, &MainWindow::do_MaoKuangYanSe_clicked);
    connect(m_JieTu, &QPushButton::clicked, this, &MainWindow::do_JieTu_clicked);

    // ---- 检测线程信号 → 界面槽（发射方在线程、接收方在主线程，自动排队）----
    connect(m_Detection, &Detection::frameReady, this, &MainWindow::do_frameReady);
    connect(m_Detection, &Detection::errorOccurred, this, &MainWindow::do_errorOccurred);

    // 线程结束（播完/点停止/出错）→ 界面统一恢复。放这保证任何退出路径都会复位
    connect(m_Detection, &QThread::finished, this, [this] { setRunningUi(false); });
}

// ==================== 开始 / 停止 ====================
void MainWindow::do_KaiShi_clicked() {
    // ① 便宜校验在主线程同步做：文件在不在、路径选没选
    const QString model = m_QuanZhong->text().trimmed();
    if (model.isEmpty() || !QFile::exists(model)) {
        QMessageBox::warning(this, "提示", "请先选择 onnx 模型文件");
        return;
    }
    if (m_WenJian->text().isEmpty() || !QFile::exists(m_WenJian->text())) {
        QMessageBox::warning(this, "提示", "请先选择视频/图片文件");
        return;
    }
    if (m_LeiBie->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "提示", "类别为空，请选择类别文件或先选权重自动配对");
        return;
    }

    // ② 打包配置（Tiny 版没有热更新：所有设置在点开始这一刻一次性生效）
    DetectConfig cfg;
    cfg.videoPath = m_WenJian->text();
    cfg.modelPath = model;
    for (const QString &tok : m_LeiBie->text().split(','))
        if (!tok.trimmed().isEmpty()) cfg.classNames << tok.trimmed();
    cfg.conf = static_cast<float>(m_ZhiXinDu->value());
    cfg.iou = static_cast<float>(m_IOU->value());
    cfg.drawBox = m_XianShiMaoKuang->isChecked();
    cfg.boxColor = cv::Scalar(m_boxColor.red(), m_boxColor.green(), m_boxColor.blue());
    cfg.flip = m_FanZhuanTuXiang->currentIndex();
    cfg.rotate = m_XuanZhuanTuXiang->currentIndex();
    cfg.showFps = m_XianShiFPS->isChecked();

    // ③ 起飞
    m_Detection->startDetect(cfg);
    setRunningUi(true);
    m_StatusLabel->setText("检测中...");
}

void MainWindow::do_TingZhi_clicked() {
    m_Detection->stopDetect();       //置停标志即可；线程退出后 finished 信号复位界面
}

// 检测中锁定输入页 + 输出页的"开始时才生效"的配置项；
// 保持可用的只有：停止按钮、截图
void MainWindow::setRunningUi(bool running) {
    m_detectingUi = running;
    m_KaiShi->setEnabled(!running);
    m_TingZhi->setEnabled(running);

    //输入页
    m_WenJian->setEnabled(!running);
    m_XuanZeWenJian->setEnabled(!running);
    m_QuanZhong->setEnabled(!running);
    m_XuanZeQuanZhong->setEnabled(!running);
    m_LeiBie->setEnabled(!running);
    m_XuanZeLeiBie->setEnabled(!running);
    m_FanZhuanTuXiang->setEnabled(!running);
    m_XuanZhuanTuXiang->setEnabled(!running);

    //输出页（截图保留可用）
    m_XianShiFPS->setEnabled(!running);
    m_XianShiMaoKuang->setEnabled(!running);
    m_MaoKuangYanSe->setEnabled(!running);
    m_ZhiXinDu->setEnabled(!running);
    m_IOU->setEnabled(!running);
    m_BaoCun->setEnabled(!running);
    m_XuanZeLuJiing->setEnabled(!running);
}

// ==================== 文件选择 ====================
void MainWindow::do_XuanZeWenJian_clicked() {
    const QString path = QFileDialog::getOpenFileName(this, "选择视频/图片",
            QString(),
            "*.mp4 *.avi *.mkv *.mov *.wmv *.bmp *.dng *.jpeg *.jpg *.png *.tif *.tiff *.webp");
    if (!path.isEmpty()) m_WenJian->setText(path);
}

// 选权重后自动配同名类别文件：m0.9.onnx → m0.9.txt（completeBaseName 保留 "m0.9" 的点号）
void MainWindow::do_XuanZeQuanZhong_clicked() {
    const QString path = QFileDialog::getOpenFileName(this, "选择模型",
            QString(), "ONNX 模型 (*.onnx)");
    if (path.isEmpty()) return;
    m_QuanZhong->setText(path);

    const QFileInfo info(path);
    const QString base = info.completeBaseName();
    const QStringList candidates {
            info.dir().filePath(base + ".txt"),          //和模型同目录
            info.dir().filePath("../" + base + ".txt"),  //或上一级
    };
    for (const QString &c : candidates) {
        if (QFile::exists(c)) {
            m_LeiBie->setText(loadClassFile(c));
            break;
        }
    }
}

void MainWindow::do_XuanZeLeiBie_clicked() {
    const QString path = QFileDialog::getOpenFileName(this, "选择类别文件",
            QString(), "类别文件 (*.txt)");
    if (path.isEmpty()) return;
    const QString text = loadClassFile(path);
    if (!text.isEmpty()) m_LeiBie->setText(text);
}

// 读类别文件：兼容换行/逗号/中文逗号/竖线，剥掉 "1 fire" 的行号前缀
// （Python 原版没剥序号，类别名会变成 "1 fire"，导致按名匹配类别失败）
QString MainWindow::loadClassFile(const QString &path) const {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QString text = QString::fromUtf8(f.readAll());
    text.replace(QStringLiteral("，"), ",").replace('|', ',').replace('\n', ',');
    static const QRegularExpression leadingNumber("^\\d+\\s+");
    QStringList out;
    for (QString tok : text.split(',')) {
        tok = tok.trimmed();
        tok.remove(leadingNumber);
        if (!tok.isEmpty()) out << tok;
    }
    return out.join(",");
}

void MainWindow::do_XuanZeLuJiing_clicked() {
    const QString dir = QFileDialog::getExistingDirectory(this, "选择保存位置", m_BaoCun->text());
    if (!dir.isEmpty()) m_BaoCun->setText(dir);
}

// ==================== 输出页 ====================
void MainWindow::do_MaoKuangYanSe_clicked() {
    const QColor c = QColorDialog::getColor(m_boxColor, this, "选择锚框颜色");
    if (!c.isValid()) return;
    m_boxColor = c;
    m_MaoKuangYanSe->setStyleSheet(
            QString("background-color: rgb(%1,%2,%3);").arg(c.red()).arg(c.green()).arg(c.blue()));
}

void MainWindow::do_JieTu_clicked() {
    const QPixmap pm = m_TuXiangShuChu->pixmap();   // Qt 6.8 起 pixmap() 按值返回
    if (pm.isNull()) return;
    QDir().mkpath(m_BaoCun->text());
    const QString path = m_BaoCun->text() + "/ScreenShot_" +
                         QDateTime::currentDateTime().toString("MM-dd HH-mm-ss") + ".png";
    if (pm.save(path))
        m_StatusLabel->setText("截图已保存：" + path);
}

// ==================== 检测线程信号的接收槽 ====================
void MainWindow::do_frameReady(const QImage &img) {
    m_TuXiangShuChu->setPixmap(QPixmap::fromImage(img).scaled(
            m_TuXiangShuChu->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::do_errorOccurred(const QString &msg) {
    QMessageBox::critical(this, "错误", msg);     //没有日志区了，用弹窗报错
}

// ==================== 关窗 ====================
void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_detectingUi) {
        const auto ret = QMessageBox::question(this, windowTitle(),
                "检测正在进行，确定停止并退出吗？",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ret != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    m_Detection->stopDetect();    //置停标志
    m_Detection->wait();          //等线程收尾（视频 read 很快，最多等一帧的时间）
    event->accept();
}
