//
// YoloDetector —— 推理类：加载 onnx 模型 → 前处理 → 推理 → 后处理(NMS) → 画框
//
// 推理引擎：OpenCV 自带的 dnn 模块（cv::dnn::readNetFromONNX，CPU 后端）。
// 好处是零外部依赖：项目里有了 OpenCV 就顺带有了推理能力，不需要再安装、
// 部署 onnxruntime（与 main 分支同款方案）。
//
// 代价是两个限制（都源于 yolov5-6.0 导出格式的约定）：
//   - 模型必须经 onnxsim 化简导出（export.py 加 --simplify）：
//     未化简模型里的 Floor 算子 OpenCV dnn 不认识，readNetFromONNX 直接抛异常；
//   - 终输出层必须叫 "output"：load 时校验一次，名字不对直接拒绝加载。
//
// 推理输出是 YOLOv5 导出的 onnx：[1, 25200, 7]
//   25200 = 三个尺度特征图的 anchor 格子总数
//   7     = 4 个框坐标(cx,cy,w,h) + 1 个目标置信度(obj) + 2 个类别分(fire, smoke)
//

#ifndef FIRESMOKEDETECTIONZCODE_YOLODETECTOR_H
#define FIRESMOKEDETECTIONZCODE_YOLODETECTOR_H

#include <string>
#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>     // cv::dnn::Net：推理网络对象（自带 empty 判断）

// 检测结果数据载体：一个被检出的目标
// （Python 版把结果组织成 dict{label: {num, score[], pos[]}}，C++ 用扁平列表更直接，
//   想按类别统计时在界面上分组即可）
struct DetectionResult {
    std::string label;    // 类别名，如 "fire" / "smoke"
    float       score;    // 置信度 0~1
    cv::Rect    box;      // 框（x, y, w, h），相对原图
};

class YoloDetector {
public:
    YoloDetector() = default;    // cv::dnn::Net 自己管理资源，无需手写析构

    // 加载 onnx 模型。失败返回 false 并把原因写入 *err（不用异常，调用方好处理）
    bool load(const std::string& onnxPath, std::string* err = nullptr);
    bool isLoaded() const { return !m_net.empty(); }

    // 检测参数。conf/iou 会在检测线程里被频繁热更新，
    // setter 都做成一行内联——真正的跨线程保护由 Detection 层负责
    void setClasses(const std::vector<std::string>& names) { m_classNames = names; }
    void setConf(float v)     { m_conf = v; }
    void setIou(float v)      { m_iou = v; }
    void setDrawBox(bool b)   { m_drawBox = b; }
    void setBoxColor(const cv::Scalar& rgb) { m_boxColor = rgb; }   // 注意是 RGB 顺序

    // 对一帧做检测。frame 进出都是 RGB 格式；drawBox 打开时会把框和标签画在 frame 上
    std::vector<DetectionResult> detect(cv::Mat& frame);

private:
    void drawDetections(cv::Mat& frame, const std::vector<DetectionResult>& dets) const;

    cv::dnn::Net m_net;                        // 推理网络（空 Net = 未加载）
    float  m_conf = 0.35f;                     // 置信度阈值
    float  m_iou = 0.35f;                      // NMS 的 IoU 阈值
    bool   m_drawBox = true;                   // 是否把框画到帧上
    int    m_thickness = 2;                    // 框线宽
    cv::Scalar m_boxColor{255, 0, 0};          // 框颜色（RGB 顺序，因为帧已经是 RGB）
    std::vector<std::string> m_classNames{"fire", "smoke"};   // 类别名兜底值
};

#endif //FIRESMOKEDETECTIONZCODE_YOLODETECTOR_H
