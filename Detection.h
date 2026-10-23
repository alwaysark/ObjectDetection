//
// Detection —— 检测线程（QThread 子类），Tiny 简化版
//
// 生命周期极简：startDetect(配置) 开线程 → run() 里"开视频→加载模型→循环
// 取帧/检测/发信号"→ stopDetect() 置停 → run 收尾退出 → finished 信号让界面复位。
// 线程只在检测期间存在（和 Python 版一样），不做常驻待命。
//
// 全程序两个线程：本线程 + 主线程。所有 UI 界面对象只在主线程碰；
// 本类只通过信号把数据送回主线程（跨线程信号自动排队）。
//

#ifndef FIRESMOKEDETECTIONTINYCPP_DETECTION_H
#define FIRESMOKEDETECTIONTINYCPP_DETECTION_H

#include "YoloDetector.h"         // 带来 DetectionResult 结构
#include <QThread>
#include <QImage>
#include <QString>
#include <QStringList>
#include <atomic>
#include <opencv2/core.hpp>

// 检测配置：点"开始检测"时从界面打包，一次性带给线程（Tiny 版没有热更新，
// 运行中改设置不会生效，下次开始检测才应用）
struct DetectConfig {
    QString videoPath;                        // 视频文件路径
    QString modelPath;                        // 权重路径
    QStringList classNames;                   // 类别名（fire/smoke）
    float conf = 0.35f;                       // 置信度阈值
    float iou = 0.35f;                        // IOU 阈值
    bool drawBox = true;                      // 是否画锚框
    cv::Scalar boxColor{255, 0, 0};           // 锚框颜色（RGB）
    int flip = 0;                             // 翻转下拉框索引
    int rotate = 0;                           // 旋转下拉框索引
    bool showFps = true;                      // 是否在画面上标 FPS
};

class Detection : public QThread {
    Q_OBJECT
public:
    explicit Detection(QObject* parent = nullptr);

    // 开始：把配置拷进来然后 start()。线程已在跑则忽略（按钮禁用本来也点不到）
    void startDetect(const DetectConfig& cfg);
    // 停止：置停标志，run() 检测到后收尾退出，线程结束
    void stopDetect() { m_running = false; }

signals:
    void frameReady(const QImage& img);       // 一帧（已画框）→ 界面显示
    void errorOccurred(const QString& msg);   // 出错（打开视频失败/模型加载失败）

protected:
    void run() override;                      // 线程体：检测的整个生命周期

private:
    DetectConfig m_cfg;                       // 开始时拷入的配置
    std::atomic_bool m_running{false};        // 循环开关
};

#endif //FIRESMOKEDETECTIONTINYCPP_DETECTION_H
