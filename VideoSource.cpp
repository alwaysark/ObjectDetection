//
// VideoSource 实现（Tiny 同步版）。
// 对照 Python 版 DataLoader：open ↔ __init__，read ↔ __next__，close ↔ __del__。
// 没有解码线程：read() 直接驱动 cv::VideoCapture，播放速度 = 调用方消费速度。
//
#include "VideoSource.h"
#include <opencv2/imgproc.hpp>      // cvtColor / flip / rotate
#include <opencv2/imgcodecs.hpp>   // imread

// ---------- 打开源 ----------
bool VideoSource::open(const std::string& path, int flip, int rotate) {
    close();                          // 防御：从干净状态开始

    m_path = path;
    m_flip = flip;
    m_rotate = rotate;

    // 视频文件：显式指定 CAP_MSMF 后端（对 UTF-8 中文路径友好）
    bool ok = m_cap.open(path, cv::CAP_MSMF);
    if (ok) {
        m_w   = static_cast<int>(m_cap.get(cv::CAP_PROP_FRAME_WIDTH));
        m_h   = static_cast<int>(m_cap.get(cv::CAP_PROP_FRAME_HEIGHT));
        m_fps = m_cap.get(cv::CAP_PROP_FPS);
    }

    if (!ok) {
        // 视频打不开？当图片再试一次。注意 imread 失败不报错、只返回空 Mat，
        // 所以必须用 empty() 判断 —— OpenCV 的惯例是"空值表示失败"
        cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
        if (!img.empty()) {
            m_isImage = true;
            m_image   = img.clone();
            m_w       = img.cols;
            m_h       = img.rows;
            m_fps     = 0;
            ok        = true;
        }
    }

    if (!ok) {
        m_cap.release();
        return false;
    }

    m_opened = true;
    m_imageGiven = false;
    return true;
}

// ---------- 取下一帧（在检测线程里调用，阻塞到拿到一帧或结束） ----------
bool VideoSource::read(cv::Mat& frame) {
    if (!m_opened) return false;

    cv::Mat out;
    if (m_isImage) {
        // 图片源：只交付一次，之后就是"源结束"
        if (m_imageGiven) return false;
        out = m_image.clone();
        m_imageGiven = true;
    } else {
        // 视频源 bool ok = m_cap.read(out);      //VideoCapture类调用read如果没抓取到帧返回false，传入传出参数为空
        if (!m_cap.read(out) || out.empty()) return false;
    }

    // 统一加工：翻转 → 旋转 → BGR 转 RGB（顺序与 Python __next__ 一致）
    if (m_flip != 0) {
        // 下拉框索引 → cv::flip 的 code：1水平 0垂直 -1双向
        // 索引 0 表示"不翻转"
        int code = 0;
        if (m_flip == 1) code = 1;
        else if (m_flip == 2) code = 0;
        else if (m_flip == 3) code = -1;
        cv::flip(out, out, code);   // cv::flip（输入图像，输出图像，翻转方式）数值大于 0 表示绕 y 轴进行翻转；数值等于 0，表示绕 x 轴进行翻转；数值小于 0，表示绕两个轴翻转
    }
    if (m_rotate != 0) {
        if (m_rotate == 1)      cv::rotate(out, out, cv::ROTATE_90_CLOCKWISE);
        else if (m_rotate == 2) cv::rotate(out, out, cv::ROTATE_90_COUNTERCLOCKWISE);
        else if (m_rotate == 3) cv::rotate(out, out, cv::ROTATE_180);
    }
    cv::cvtColor(out, out, cv::COLOR_BGR2RGB);

    frame = out;
    return true;
}

// ---------- 收工 ----------
void VideoSource::close() {
    if (m_cap.isOpened()) m_cap.release();
    m_image.release();
    m_opened = false;
    m_isImage = false;
    m_imageGiven = false;
}

VideoSource::~VideoSource() {
    close();      // RAII：忘调 close 也会自动释放
}
