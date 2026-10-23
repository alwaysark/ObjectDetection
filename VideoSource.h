//
// VideoSource —— 数据源类（Tiny 简化版：同步取帧，无内部线程）
//
// 全程序只有两个线程：主线程（界面）+ 检测线程（Detection::run）。
// read() 由检测线程调用，阻塞式地取下一帧 —— "等一帧、测一帧、发一帧"，
// 与 Python 版 DetectThread 的摄像头模式完全一致。
//
// 只支持 视频文件 / 图片 两种源（摄像头和屏幕已按需求移除）。
//
// read() 返回 false 的契约：
//   - 视频文件：读失败 = 读到文件尾 = 真正的"源结束"；
//   - 图片：第一次返回图片，之后返回 false。
//
// 翻转/旋转：open() 时一次性传入，运行中不可改（Tiny 版没有热更新）。
//

#ifndef FIRESMOKEDETECTIONTINYCPP_VIDEOSOURCE_H
#define FIRESMOKEDETECTIONTINYCPP_VIDEOSOURCE_H

#include <string>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

class VideoSource {
public:
    VideoSource() = default;
    ~VideoSource();                     // 析构自动释放（RAII）

    VideoSource(const VideoSource&) = delete;
    VideoSource& operator=(const VideoSource&) = delete;

    // 打开源：视频用 CAP_MSMF 后端（对中文路径友好）；打不开再试图片
    // flip/rotate 传界面下拉框的索引（0=不处理），之后每次 read 都会应用
    bool open(const std::string& path, int flip, int rotate);

    // 取下一帧（返回时已是 RGB、已按设置翻转/旋转）。返回 false = 源结束
    bool read(cv::Mat& frame);

    void close();                       // 释放摄像头/清空状态

    // 查询接口
    bool isOpen() const   { return m_opened; }
    bool isImage() const  { return m_isImage; }
    int width() const     { return m_w; }
    int height() const    { return m_h; }
    double fps() const    { return m_fps; }

private:
    cv::VideoCapture m_cap;             // OpenCV 的视频通道

    cv::Mat  m_image;                   // 图片源专用
    bool     m_imageGiven = false;      // 图片是否已交付（图片只出一次）

    int  m_flip = 0;                    // 翻转索引 0~3（open 时传入，之后只读）
    int  m_rotate = 0;                  // 旋转索引 0~3

    std::string m_path;
    bool   m_opened = false;
    bool   m_isImage = false;
    int    m_w = 0, m_h = 0;
    double m_fps = 0;
};

#endif //FIRESMOKEDETECTIONTINYCPP_VIDEOSOURCE_H
