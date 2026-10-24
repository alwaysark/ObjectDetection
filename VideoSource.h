//
// VideoSource —— 数据源类：把"摄像头 / 视频文件 / 图片"统一成同一个取帧接口
//
// 对外只暴露三个动词（类的使用者只需要知道这三个）：
//     open(path)      打开源：自动识别类型，成败有返回值
//     read(frame)     取下一帧（QImage RGB888、已按界面设置翻转/旋转过），
//                     返回 false = 源结束
//     close()         收工：停解码线程、释放摄像头
//
// 内部结构：
//   - 视频文件/摄像头：解码线程持续 grab，只保留最新一帧（cv::Mat BGR）。
//     read() 永远拿最新帧 —— 检测慢就丢帧，播放速度不受推理速度拖累。
//     交付时才做 Mat→QImage（rgbSwapped 一次），消费多少转多少。
//   - 图片：open 时 QImageReader 读一次，read 第一次返回它、之后返回 false。
//
// OpenCV 全部藏在 pimpl（Impl）里：头文件只剩 Qt 类型。解码用 cv::VideoCapture，
// 翻转/旋转/通道交换用 QImage 自带的 mirrored/transformed/rgbSwapped。
//
// 线程安全说明（哪些成员被谁碰，均在 Impl 内）：
//   m_latestFrame/m_FrameNum   解码线程写、read() 读   → 必须在 mutex 内
//   m_decoding/m_videoCameraOver  两线程都读写          → atomic
//   m_flip/m_rotate            UI 线程写、read() 读     → atomic（热更新）
//
#ifndef FIRESMOKEDETECTIONZCODE_VIDEOSOURCE_H
#define FIRESMOKEDETECTIONZCODE_VIDEOSOURCE_H

#include <string>
#include <memory>
#include <QImage>

// 输入源类型（和界面下拉框的 item data 对应）
enum class SourceType {
    Camera, File, Screen
};

class VideoSource {
public:
    VideoSource();
    ~VideoSource();               // 析构自动收工（RAII：忘调 close 也不泄漏）

    // 线程 + 锁成员在 Impl 里，天生不可拷贝，显式删除防止误用
    VideoSource(const VideoSource&) = delete;
    VideoSource& operator=(const VideoSource&) = delete;

    bool open(const std::string& path);   // 打开源：纯数字=摄像头，"screen"=屏幕(未实现)，其他=文件
    bool read(QImage& frame);             // 取下一帧；返回 false = 没有更多帧了
    void close();                         // 三步收尾：停线程 → join → 释放摄像头

    // 热更新参数（对应界面"翻转/旋转"下拉框，索引 0~3，0=不处理）
    void setFlip(int i);
    void setRotate(int i);

    // 查询接口
    bool isImage() const;
    const std::string& path() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

#endif //FIRESMOKEDETECTIONZCODE_VIDEOSOURCE_H
