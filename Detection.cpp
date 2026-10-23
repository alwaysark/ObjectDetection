//
// Detection 实现（Tiny 版）。对照 Python 版 DetectThread.main() 读：
//   for img, path in self.dataset:      ↔ while (m_running) { m_Source.read(frame) }
//       res = self.model.detect(img)    ↔     results = m_Mode->detect(frame)
//       self.img_sig.emit(img)          ↔     emit frameReady(...)
// 区别：Tiny 版连 VideoSource 的解码线程也去掉了，read 就是检测线程自己阻塞取帧。
//
#include "Detection.h"
#include "VideoSource.h"
#include <QDebug>
#include <opencv2/imgproc.hpp>     // cvtColor / putText
#include <chrono>

Detection::Detection(QObject* parent) : QThread(parent) {}

// ---------- 开始：拷配置 + 起线程 ----------
void Detection::startDetect(const DetectConfig& cfg) {
    if (isRunning()) return;      // 防重入
    m_cfg = cfg;                  // 线程还没 start，直接拷贝是安全的
    m_running = true;
    start();                      // start() 内部开新系统线程并调用 run()
}

// ---------- 线程体：检测的完整生命周期 ----------
void Detection::run() {
    using namespace std::chrono;

    // ===== ① 打开视频源 =====
    VideoSource source;           // 栈对象：无论从哪 return 都自动 close，不会泄漏
    if (!source.open(m_cfg.videoPath.toStdString(), m_cfg.flip, m_cfg.rotate)) {
        emit errorOccurred(QString("无法打开视频文件：%1").arg(m_cfg.videoPath));
        return;
    }

    // ===== ② 加载模型 =====
    YoloDetector detector;
    std::string err;
    if (!detector.load(m_cfg.modelPath.toStdString(), &err)) {
        emit errorOccurred(QString("模型加载失败：%1").arg(QString::fromStdString(err)));
        return;
    }
    // QStringList(QString) → std::vector<std::string>：逐个 toStdString 转换
    std::vector<std::string> classNames;
    classNames.reserve(m_cfg.classNames.size());
    for (const QString& n : m_cfg.classNames)
        classNames.push_back(n.toStdString());
    detector.setClasses(classNames);
    detector.setConf(m_cfg.conf);
    detector.setIou(m_cfg.iou);
    detector.setDrawBox(m_cfg.drawBox);
    detector.setBoxColor(m_cfg.boxColor);

    // ===== ③ 主循环：取一帧 → 检测 → 发信号 =====
    auto fpsStart = steady_clock::now();    //获取当前时间点
    int fpsCount = 0;       //到目前循环几次就是处理了多少帧
    double shownFps = 0;    //FPS=帧数/时间

    while (m_running.load()) {
        cv::Mat frame;
        bool read = source.read(frame);     //读一帧，若是图片或者视频结束就返回false
        if (!read) break;          // 视频结束或图片就正常结束，跳出循环

        // 推理（勾选绘制锚框时，框和标签直接画在 frame 上）
        std::vector<DetectionResult> results = detector.detect(frame);

        // 循环几次就是处理了多少帧
        ++fpsCount;
        //从上一次循环开始，操作花费了elapsed个单位时间
        const double elapsed = duration<double>(steady_clock::now() - fpsStart).count();
        //每2秒画一次
        if (elapsed >= 2.0) {
            shownFps = fpsCount / elapsed;
            fpsCount = 0;
            fpsStart = steady_clock::now();
        }
        //在这一帧上画出FPS
        if (m_cfg.showFps && !source.isImage()) {
            cv::putText(frame, "FPS:" + std::to_string(static_cast<int>(shownFps)),
                        cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0,
                        cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
        }

        // 发送帧（QImage 构造后 copy() 深拷贝——不 copy 的话 frame 缓冲复用后就是脏数据）
        QImage img(frame.data, frame.cols, frame.rows,
                   static_cast<int>(frame.step), QImage::Format_RGB888);
        emit frameReady(img.copy());

    }

    // ===== ④ 收尾（自然播完或用户点停止都会走到这；VideoSource 析构自动关闭视频）=====
}
