//
// Recorder —— 检测视频录制（mp4）。
//
// 全项目唯一接触视频编码（cv::VideoWriter）的地方，藏在 pimpl 后面：
// 对外只认 QImage（RGB888），头文件里不出现任何 OpenCV 类型。
// VideoWriter 要的是 BGR Mat，通道交换与逐行拷贝都在 .cpp 里完成
// （QImage 行按 4 字节对齐，不能整块 memcpy 到 Mat）。
//
#ifndef FIRESMOKEDETECTIONZCODE_RECORDER_H
#define FIRESMOKEDETECTIONZCODE_RECORDER_H

#include <QString>
#include <QImage>
#include <memory>

class Recorder {
public:
    Recorder();
    ~Recorder();                       // 忘了 close 也不泄漏（RAII 收尾）

    // 打开输出文件。失败返回 false。换源/尺寸变化 = 上一段作结，重新 open
    bool open(const QString& path, int fps, int width, int height);
    bool isOpened() const;
    void write(const QImage& frame);   // RGB888 帧转 BGR 喂编码器
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> d;           // cv::VideoWriter 藏在这里
};

#endif //FIRESMOKEDETECTIONZCODE_RECORDER_H
