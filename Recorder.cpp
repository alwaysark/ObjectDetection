#include "Recorder.h"
#include <opencv2/videoio.hpp>
#include <opencv2/core.hpp>
#include <cstring>

struct Recorder::Impl {
    cv::VideoWriter writer;
    cv::Mat         bgr;               // 复用的 BGR 缓冲（避免每帧分配）
};

Recorder::Recorder() : d(std::make_unique<Impl>()) {}

Recorder::~Recorder() { close(); }

bool Recorder::open(const QString& path, int fps, int width, int height) {
    close();
    d->writer.open(path.toStdString(), cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                   fps, cv::Size(width, height));
    return d->writer.isOpened();
}

bool Recorder::isOpened() const {
    return d && d->writer.isOpened();
}

void Recorder::write(const QImage& frame) {
    if (!isOpened() || frame.isNull()) return;

    // RGB888 → BGR888：交换 R/B 通道后内存布局正好是 Mat 要的 BGR
    const QImage bgrImg = frame.format() == QImage::Format_BGR888
                          ? frame : frame.rgbSwapped();
    const int h = bgrImg.height(), w = bgrImg.width();
    if (d->bgr.rows != h || d->bgr.cols != w)
        d->bgr.create(h, w, CV_8UC3);

    // QImage 每行 4 字节对齐、Mat 连续存储：逐行拷贝
    for (int y = 0; y < h; ++y)
        std::memcpy(d->bgr.ptr(y), bgrImg.constScanLine(y), size_t(w) * 3);

    d->writer.write(d->bgr);
}

void Recorder::close() {
    if (d && d->writer.isOpened()) d->writer.release();
}
