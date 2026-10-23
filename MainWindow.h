//
// MainWindow —— Tiny 精简版
//
// 布局：左边大图（检测画面）+ 画面下方一个结果标签；右边 TabWidget（输入/输出）。
// 职责链非常短：用户操作 → 打包 DetectConfig → 交给检测线程；线程信号 → 更新界面。
//
// 状态机只有两态：
//   空闲：所有控件可操作，只有"开始"能点
//   检测中：输入页 + 输出页参数全部禁用（配置已打包进线程，改了也不生效），
//           只有"停止"和截图可点；线程 finished 信号自动恢复界面
//

#ifndef FIRESMOKEDETECTIONTINY_MAINWINDOW_H
#define FIRESMOKEDETECTIONTINY_MAINWINDOW_H

#include <QMainWindow>
#include <QColor>
#include <vector>

class QLabel;
class QPushButton;
class QToolButton;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QCloseEvent;

class Detection;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;   // 关窗：确认 + 等线程退出

private:
    void buildUi();
    void bindSignal();
    void setRunningUi(bool running);                // 检测中锁定控件 / 结束后恢复
    QString loadClassFile(const QString &path) const;   // 读类别文件 → "fire,smoke"（剥序号）

private slots:
    void do_KaiShi_clicked();
    void do_TingZhi_clicked();

    void do_XuanZeWenJian_clicked();      //选视频
    void do_XuanZeQuanZhong_clicked();    //选权重（自动配同名类别文件）
    void do_XuanZeLeiBie_clicked();       //选类别文件
    void do_XuanZeLuJiing_clicked();      //选保存目录
    void do_MaoKuangYanSe_clicked();      //选锚框颜色（开始检测时生效）
    void do_JieTu_clicked();              //截图（检测中也可用）

    // ---- 检测线程信号的目标槽 ----
    void do_frameReady(const QImage &img);      //显示画面
    void do_errorOccurred(const QString &msg);  //弹窗报错并复位界面

private:
    //主区域
    QLabel *m_TuXiangShuChu = nullptr;    //检测画面
    QLabel *m_StatusLabel = nullptr;      //检测结果摘要（替代原来的日志区）
    QTabWidget *m_tabWidget = nullptr;    //设置页容器（输入/输出）

    //输入页（检测中全部禁用）
    QLineEdit *m_WenJian = nullptr;
    QToolButton *m_XuanZeWenJian = nullptr;
    QLineEdit *m_QuanZhong = nullptr;
    QToolButton *m_XuanZeQuanZhong = nullptr;
    QLineEdit *m_LeiBie = nullptr;        //只读：由类别文件按钮填充
    QToolButton *m_XuanZeLeiBie = nullptr;
    QComboBox *m_FanZhuanTuXiang = nullptr;
    QComboBox *m_XuanZhuanTuXiang = nullptr;

    //输出页
    QCheckBox *m_XianShiFPS = nullptr;
    QCheckBox *m_XianShiMaoKuang = nullptr;
    QToolButton *m_MaoKuangYanSe = nullptr;
    QDoubleSpinBox *m_ZhiXinDu = nullptr;
    QDoubleSpinBox *m_IOU = nullptr;
    QLineEdit *m_BaoCun = nullptr;        //只读：由目录按钮填充
    QToolButton *m_XuanZeLuJiing = nullptr;
    QPushButton *m_JieTu = nullptr;

    QPushButton *m_KaiShi = nullptr;
    QPushButton *m_TingZhi = nullptr;

    //状态
    Detection *m_Detection = nullptr;
    bool m_detectingUi = false;           //界面视角的"检测中"
    QColor m_boxColor{255, 0, 0};         //锚框颜色
};

#endif //FIRESMOKEDETECTIONTINY_MAINWINDOW_H
