#include "VideoSource.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <opencv2/videoio.hpp>
#include <QImageReader>
#include <QTransform>

// ---- 翻转/旋转索引 → QImage 操作（在交付的 RGB 图上做，和旧版 Mat 上做等价）----
static QImage applyOrient(const QImage& img, int flipIdx, int rotIdx) {
    QImage out = img;
    if (flipIdx == 1)      out = out.mirrored(true, false);    // 水平翻转
    else if (flipIdx == 2) out = out.mirrored(false, true);    // 垂直翻转
    else if (flipIdx == 3) out = out.mirrored(true, true);     // 水平垂直翻转
    if (rotIdx == 1)       out = out.transformed(QTransform().rotate(90));    // +90° 顺时针
    else if (rotIdx == 2)  out = out.transformed(QTransform().rotate(-90));   // -90°
    else if (rotIdx == 3)  out = out.transformed(QTransform().rotate(180));   // 180°
    return out;
}

struct VideoSource::Impl {
    // ---- 流式源的"最新帧"缓冲（解码线程写 / read 读，全程持锁）----
    cv::VideoCapture        cap;                // 视频通道（摄像头/视频文件）
    std::thread             decoder;            // 内部解码线程
    std::mutex              mtx;                // 保护 latestFrame / frameNum
    std::condition_variable cond;
    cv::Mat                 latestFrame;        // 最新一帧（BGR 原始帧，OpenCV 只活在这）
    uint64_t                frameNum = 0;       // 帧号：解码线程每存一帧 +1
    uint64_t                lastDelivered = 0;  // read() 上一次交付出去的帧号
    std::atomic_bool        decoding{false};    // 解码线程开关
    std::atomic_bool        over{false};        // 源已读完（视频到尾/设备没了）

    std::atomic_int         flip{0};            // 翻转下拉框索引（热更新）
    std::atomic_int         rotate{0};          // 旋转下拉框索引（热更新）

    // ---- 图片源专用 ----
    QImage                  image;              // QImageReader 的结果（RGB888）
    bool                    imageOver = false;  // 图片只交付一次

    // ---- 打开时确定、之后只读的属性 ----
    SourceType              type = SourceType::File;
    std::string             path_;
    bool                    opened = false;
    bool                    isImage_ = false;
    double                  fps = 0;

    void decodeLoop();
};

VideoSource::VideoSource() : d(std::make_unique<Impl>()) {}
VideoSource::~VideoSource() { close(); }

// ---------- 解码线程：按视频自身帧率节奏抓帧，只保留最新一帧 ----------
void VideoSource::Impl::decodeLoop() {
    const int delayMs = fps > 1 ? static_cast<int>(1000.0 / fps) : 33;

    int failCount = 0;    // 连续读帧失败计数
    while (decoding.load()) {
        cv::Mat f;
        const bool ok = cap.read(f) && !f.empty();

        if (!ok) {
            if (type == SourceType::Camera) {
                // 摄像头读帧失败绝大多数是暂时性的（预热中、USB 抖动），
                // 睡 100ms 重试；连续约 5 秒失败才认定设备真的没了
                if (++failCount >= 50) {
                    over = true;
                    cond.notify_all();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            // 视频文件不做重试：读失败就是读到尾了
            over = true;
            cond.notify_all();
            break;
        }
        failCount = 0;
        {
            // clone 是关键：不 clone 的话下一轮 cap.read 会改写同一块内存
            std::lock_guard<std::mutex> lk(mtx);
            latestFrame = f.clone();
            ++frameNum;
        }
        cond.notify_all();       // 叫醒正在等帧的 read()

        // 睡够一帧间隔：消费不过来时旧帧被覆盖，消费得过来时按视频节奏走
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
}

// ---------- open：识别类型 → 建通道 → 流式源起解码线程 ----------
bool VideoSource::open(const std::string& path) {
    close();                      // 防御：换源前先把旧源收干净

    if (path == "screen") {
        // 屏幕捕获预留位（将来用 QScreen::grabWindow，必须在 GUI 线程抓屏）
        return false;
    }

    d->path_ = path;

    // 纯数字 = 摄像头索引
    const bool isCameraIndex = (!path.empty() && (path.find_first_not_of("0123456789") == std::string::npos));

    if (isCameraIndex) {
        d->type = SourceType::Camera;
        // CAP_DSHOW：Windows 的 DirectShow 后端，摄像头启动快
        if (!d->cap.open(std::stoi(path), cv::CAP_DSHOW)) {
            d->cap.release();
            return false;
        }
        d->fps = d->cap.get(cv::CAP_PROP_FPS);
        if (d->fps <= 1) d->fps = 30;   // 有些摄像头不报帧率，兜底 30
    } else {
        d->type = SourceType::File;
        // 显式指定 CAP_MSMF 后端：对 UTF-8（中文）路径友好
        if (d->cap.open(path, cv::CAP_MSMF)) {
            d->fps = d->cap.get(cv::CAP_PROP_FPS);
        } else {
            // 视频打不开 → 当图片试一次（jpg/png/bmp...；QImageReader 走 QString，
            // 中文路径天然支持）
            QImageReader reader(QString::fromStdString(path));
            QImage img = reader.read();
            if (!img.isNull()) {
                d->image = img.convertToFormat(QImage::Format_RGB888);
                d->isImage_ = true;
                d->fps = 0;
            } else {
                d->cap.release();
                return false;
            }
        }
    }

    d->opened = true;
    d->imageOver = false;

    // 流式源（摄像头/视频）起解码线程；图片源不需要线程
    if (!d->isImage_) {
        d->frameNum = 0;
        d->lastDelivered = 0;
        d->over = false;
        d->decoding = true;
        d->decoder = std::thread(&VideoSource::Impl::decodeLoop, d.get());
    }
    return true;
}

// ---------- read：交付"下一帧"（RGB888、按需翻转/旋转） ----------
bool VideoSource::read(QImage& frame) {
    if (!d->opened) return false;

    if (d->isImage_) {
        // 图片只交付一次
        if (d->imageOver) return false;
        d->imageOver = true;
        frame = applyOrient(d->image, d->flip.load(), d->rotate.load());
        return true;
    }

    // 持续等待，直到：有新帧 或 源结束 或 线程被要求关闭。
    // 不能"超时一次就当结束"——摄像头预热期第一帧经常超过 200ms
    cv::Mat raw;
    {
        std::unique_lock<std::mutex> lk(d->mtx);
        while (d->frameNum == d->lastDelivered && d->decoding.load() && !d->over.load())
            d->cond.wait_for(lk, std::chrono::milliseconds(200));
        if (d->frameNum == d->lastDelivered)
            return false;    // 只可能是 eof 或已关闭
        raw = d->latestFrame.clone();   // 锁内拷出，出锁再加工，缩小临界区
        d->lastDelivered = d->frameNum;
    }

    // Mat(BGR) → QImage(RGB)：先零拷贝包一层 BGR888 视图，rgbSwapped 产出
    // 拥有独立内存的 RGB888 图（此后与 Mat 缓冲再无瓜葛）
    QImage bgrView(raw.data, raw.cols, raw.rows, static_cast<int>(raw.step),
                   QImage::Format_BGR888);
    frame = applyOrient(bgrView.rgbSwapped(), d->flip.load(), d->rotate.load());
    return true;
}

// ---------- close：三步收尾（可在任意线程调用，可重复调用） ----------
void VideoSource::close() {
    d->decoding = false;                // 让解码线程退出循环
    d->over = true;
    d->cond.notify_all();               // 叫醒可能睡在 read() 里的线程
    if (d->decoder.joinable())          // 回收线程
        d->decoder.join();
    if (d->cap.isOpened())              // 释放摄像头/视频通道
        d->cap.release();

    {
        std::lock_guard<std::mutex> lk(d->mtx);
        d->latestFrame.release();
    }
    d->image = QImage();

    d->opened = false;
    d->isImage_ = false;
    d->imageOver = false;
    d->over = false;
    d->fps = 0;
}

void VideoSource::setFlip(int i)   { d->flip.store(i); }
void VideoSource::setRotate(int i) { d->rotate.store(i); }
bool VideoSource::isImage() const  { return d->isImage_; }
const std::string& VideoSource::path() const { return d->path_; }
