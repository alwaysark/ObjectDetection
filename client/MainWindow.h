//
// MainWindow —— 界面层：认识所有类，负责"把用户操作翻译成对 Detection 的请求"，
// 以及"把 Detection 发来的信号画到界面上"。
//
// 线程约定：本类所有槽函数都在主线程执行（Detection 的信号是跨线程排队连接），
// 所以槽里可以放心碰任何控件；对 Detection 只能调它的公开方法（信箱/atomic）。
//

#ifndef FIRESMOKEDETECTIONZCODE_MAINWINDOW_H
#define FIRESMOKEDETECTIONZCODE_MAINWINDOW_H

#include <QMainWindow>
#include <QElapsedTimer>
#include <QColor>
#include <vector>

class QLabel;           //标签
class QTextBrowser;     //文本浏览器
class QPushButton;      //按钮
class QToolButton;      //工具按钮
class QComboBox;        //下拉框
class QLineEdit;        //单行编辑框
class QDoubleSpinBox;   //双精度微调框
class QSpinBox;         //整数微调框
class QCheckBox;        //复选框
class QTabWidget;       //选项卡部件
class QCloseEvent;      //关闭事件
class QSoundEffect;     //警报音播放（Qt Multimedia）
class QCustomPlot;      //实时曲线（qcustomplot.h 在 .cpp 里包含）
class QTableView;       //告警记录表（对话框里用）

#include "YoloDetector.h"   // do_resultReady 的参数里用到 DetectionResult
#include "AlarmStore.h"     // 告警记录入库 + AlarmRecord 结构

class Detection;
class AlarmReporter;        // 告警外发客户端（TCP 自定义协议）
struct DetectConfig;        // 打包助手 packDetectConfig() 的返回类型（定义见 Detection.h）

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;   // 关窗拦截：确认退出 + 存配置 + 收线程

private:
    void buildUi();
    void bindSignal();

    // ---- 内部辅助 ----
    void displayLog(const QString &text, const QString &color = "black");   // 带时间戳写日志
    void setRunningUi(bool running);                    // 检测中禁用/恢复按钮
    QString loadClassFile(const QString &path) const;   // 读类别文件 → "fire,smoke"（剥序号）
    DetectConfig packDetectConfig() const;              // 从界面打包 {模型路径, 类别名}——开始检测与检测中换模型共用
    void loadConfig();      // 启动时恢复上次设置（QSettings）
    void saveConfig();      // 关窗时保存设置

private:
    Detection *m_Detection = nullptr;               //检测线程

private slots:
    void do_QingChu_clicked();      //清除：清空日志
    void do_BaoCunRiZhi_clicked();  //保存日志到文件

    void do_KaiShi_clicked();
    void do_TingZhi_clicked();

    void do_ShuRuFangShi_indexChanged(int index);
    void do_XuanZeWenJian_clicked();
    void do_XuanZeQuanZhong_clicked();
    void do_XuanZeLeiBie_clicked();
    void do_FanZhuanTuXiang_indexChanged(int index);
    void do_XuanZhuanTuXiang_indexChanged(int index);

    void do_XianShiFPS_StateChanged(Qt::CheckState state);
    void do_KaiQiJingBao_StateChanged(Qt::CheckState state);
    void do_DaYinRiZhi_StateChanged(Qt::CheckState state);
    void do_XianShiMaoKuang_StateChanged(Qt::CheckState state);
    void do_MaoKuangYanSe_clicked();
    void do_LuZhiShiPin_StateChanged(Qt::CheckState state);
    void do_LuZhiZhenLv_valueChanged(int);
    void do_ZhiXinDu_valueChanged(double);
    void do_IOU_valueChanged(double);
    void do_XuanZeLuJiing_clicked();
    void do_JieTu_clicked();

    // ---- 检测线程信号的目标槽（跨线程排队连接，主线程执行）----
    void do_frameReady(const QImage &img);                          //显示检测画面
    void do_resultReady(const std::vector<DetectionResult> &r);     //结果写日志 + 曲线
    void do_targetDetected();                                       //警报（带 5 秒冷却）
    void do_alarmTriggered(const std::vector<DetectionResult> &r);  //告警入库 + 快照 + 平台上报
    void do_errorOccurred(const QString &msg);                      //红字日志 + 按钮复位
    void do_sourceFinished();                                       //源播完 + 按钮复位
    void do_sourceOpened(const QString &desc);                      //日志
    void do_infoMessage(const QString &msg);                        //一般提示日志

    // ---- 统计页/告警记录 ----
    void do_openAlarmHistory();                 //打开告警记录对话框
    void do_platformOnlineChanged(bool online); //平台连接状态提示

private:
    //主区域
    QLabel *m_TuXiangShuChu = nullptr;
    QTabWidget *m_BottomTabs = nullptr;      //底部选项卡：日志/统计（页钮在左）
    QTextBrowser *m_RiZhi = nullptr;
    QPushButton *m_QingChu = nullptr;
    QPushButton *m_BaoCunRiZhi = nullptr;
    QPushButton *m_KaiShi = nullptr;
    QPushButton *m_TingZhi = nullptr;
    QTabWidget *m_tabWidget = nullptr;

    //输入页
    QComboBox *m_ShuRuFangShi = nullptr;
    QLineEdit *m_WenJian = nullptr;
    QToolButton *m_XuanZeWenJian = nullptr;
    QLineEdit *m_QuanZhong = nullptr;
    QToolButton *m_XuanZeQuanZhong = nullptr;
    QLineEdit *m_LeiBie = nullptr;
    QToolButton *m_XuanZeLeiBie = nullptr;
    QComboBox *m_FanZhuanTuXiang = nullptr;
    QComboBox *m_XuanZhuanTuXiang = nullptr;

    //输出页
    QCheckBox *m_XianShiFPS = nullptr;
    QCheckBox *m_KaiQiJingBao = nullptr;
    QCheckBox *m_DaYinRiZhi = nullptr;
    QCheckBox *m_XianShiMaoKuang = nullptr;
    QToolButton *m_MaoKuangYanSe = nullptr;
    QCheckBox *m_LuZhiShiPin = nullptr;
    QSpinBox *m_LuZhiZhenLv = nullptr;
    QDoubleSpinBox *m_ZhiXinDu = nullptr;
    QDoubleSpinBox *m_IOU = nullptr;
    QLineEdit *m_BaoCun = nullptr;
    QToolButton *m_XuanZeLuJiing = nullptr;
    QPushButton *m_JieTu = nullptr;
    QPushButton *m_AlarmHistory = nullptr;   //打开告警记录对话框

    // ---- 界面侧状态 ----
    bool m_alarmEnabled = true;     //警报总开关（开启警报复选框）
    bool m_printResult = true;      //打印结果开关
    bool m_sourceOpen = false;      //检测线程里是否已打开源（开始按钮据此决定要不要请求开源）
    bool m_detectingUi = false;     //界面视角的"检测中"（closeEvent 用）
    QColor m_boxColor{255, 0, 0};   //锚框颜色（QColorDialog 选的）

    // ---- 警报（QSoundEffect 只支持 wav；5 秒冷却防止连响）----
    QSoundEffect *m_JingBao = nullptr;
    QElapsedTimer m_JingBaoLengQue;

    // ---- 上位机扩展：告警入库 / 平台上报 / 实时曲线 ----
    AlarmStore   *m_AlarmStore = nullptr;      // SQLite 告警记录
    AlarmReporter *m_Reporter = nullptr;       // TCP 告警外发
    QCustomPlot  *m_Plot = nullptr;            // 统计页实时曲线
    double        m_plotSeconds = 0;           // 曲线横轴（检测运行秒数）
    QElapsedTimer m_plotClock;                 // 曲线计时
};

#endif //FIRESMOKEDETECTIONZCODE_MAINWINDOW_H
