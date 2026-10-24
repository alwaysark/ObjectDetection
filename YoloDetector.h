//
// YoloDetector —— 推理类：加载 onnx 模型 → 前处理 → 推理 → 后处理(NMS) → 画框
//
// 推理引擎：onnxruntime 的 C++ API（onnxruntime_cxx_api.h，隐式链接官方预编译包）。
// 前处理（缩放+归一化+NCHW）、后处理 NMS、画框全部自写：
// 帧与结果只用 Qt 类型（QImage / QRect / QColor），整个类不出现 OpenCV。
//
// 推理输出是 YOLOv5 导出的 onnx：[1, 25200, 5+类别数]
//   25200 = 三个尺度特征图的 anchor 格子总数（输入 640 时）
//   每行  = cx, cy, w, h（相对模型输入）+ obj 置信度 + 各类别分
//
#ifndef FIRESMOKEDETECTIONZCODE_YOLODETECTOR_H
#define FIRESMOKEDETECTIONZCODE_YOLODETECTOR_H

#include <QString>
#include <QImage>
#include <QRect>
#include <QColor>
#include <string>
#include <vector>
#include <memory>
#include <onnxruntime_cxx_api.h>

// 检测结果数据载体：一个被检出的目标
struct DetectionResult {
    std::string label;    // 类别名，如 "fire" / "smoke"
    float       score = 0;// 置信度 0~1
    QRect       box;      // 框（x, y, w, h），相对原图
};

class YoloDetector {
public:
    YoloDetector() = default;

    // 加载 onnx 模型。失败返回 false 并把原因写入 *err（不用异常，调用方好处理）
    bool load(const QString& onnxPath, QString* err = nullptr);
    bool isLoaded() const { return m_session != nullptr; }

    // 检测参数。conf/iou 会在检测线程里被频繁热更新，
    // setter 都做成一行内联——真正的跨线程保护由 Detection 层负责
    void setClasses(const std::vector<std::string>& names) { m_classNames = names; }
    void setConf(float v)     { m_conf = v; }
    void setIou(float v)      { m_iou = v; }
    void setDrawBox(bool b)   { m_drawBox = b; }
    void setBoxColor(const QColor& color) { m_boxColor = color; }

    // 对一帧做检测。frame 进出都是 RGB888；drawBox 打开时会把框和标签画在 frame 上
    // （QPainter 绘制，类别名可以是中文）
    std::vector<DetectionResult> detect(QImage& frame);

private:
    // 双线性缩放到模型输入尺寸 + 归一化 + 打包 NCHW，结果写进 m_blob（复用缓冲）
    void makeBlob(const QImage& src);
    // 手写 NMS（与旧版 cv::dnn::NMSBoxes 同语义：类无关、按分数贪心、IoU 阈值抑制）
    static void nmsBoxes(const std::vector<QRect>& boxes, const std::vector<float>& scores,
                         float iouThr, std::vector<int>& keep);
    void drawDetections(QImage& frame, const std::vector<DetectionResult>& dets) const;

    // ---- onnxruntime 对象（RAII 自动管理，无需手动 Release）----
    Ort::Env m_env{ORT_LOGGING_LEVEL_WARNING, "FireSmokeDetection"};
    std::unique_ptr<Ort::Session> m_session;               // 每次 load 重建
    std::string m_inputName  = "images";                   // 从模型里读出来的输入/输出名
    std::string m_outputName = "output0";
    bool m_loaded = false;

    int    m_inputW = 640, m_inputH = 640;     // 模型输入尺寸（自动从模型读取）
    float  m_conf = 0.35f;                     // 置信度阈值
    float  m_iou = 0.35f;                      // NMS 的 IoU 阈值
    bool   m_drawBox = true;                   // 是否把框画到帧上
    int    m_thickness = 2;                    // 框线宽
    QColor m_boxColor{255, 0, 0};              // 框颜色
    std::vector<std::string> m_classNames{"fire", "smoke"};   // 类别名兜底值
    std::vector<float> m_blob;                 // 前处理复用缓冲（NCHW float）
};

#endif //FIRESMOKEDETECTIONZCODE_YOLODETECTOR_H
