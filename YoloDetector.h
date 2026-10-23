//
// YoloDetector —— 推理类：加载 onnx 模型 → 前处理 → 推理 → 后处理(NMS) → 画框
//
// 对应 Python 版 utils/detect.py 的 YOLOv5 类（t='onnxruntime' 路线）。
//
// 为什么用 onnxruntime 的 **C API** 而不是 C++ API（onnxruntime_cxx_api.h）？
//   - onnxruntime 官方预编译包是 MSVC 编译的，C++ 类的内存布局和 MinGW 不兼容；
//   - C API 是纯 C 结构 + 函数指针表，ABI 与编译器无关，MinGW 可以直接用；
//   - 本实现用 LoadLibrary 动态加载 onnxruntime.dll，连导入库都不需要，
//     只要 DLL 在 exe 旁边就能跑（CMake 已负责拷贝）。
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

// onnxruntime 的 C 结构体，这里只用指针，前置声明即可（不用包含它的头）
struct OrtEnv;
struct OrtSession;
struct OrtMemoryInfo;

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
    YoloDetector() = default;
    ~YoloDetector();      // 释放 session 等资源

    // 加载 onnx 模型。失败返回 false 并把原因写入 *err（不用异常，调用方好处理）
    bool load(const std::string& onnxPath, std::string* err = nullptr);
    bool isLoaded() const { return m_loaded; }

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
    void unload();        // 释放 session（load 新模型前 / 析构时调用）
    void drawDetections(cv::Mat& frame, const std::vector<DetectionResult>& dets) const;

    // onnxruntime 会话（仅推理线程使用；指针用完必须调对应的 Release）
    OrtSession*     m_session  = nullptr;
    OrtMemoryInfo*  m_memInfo  = nullptr;
    std::string     m_inputName = "images";    // 从模型里读出来的输入/输出名
    std::string     m_outputName = "output0";
    bool m_loaded = false;

    int    m_inputW = 640, m_inputH = 640;     // 模型输入尺寸（和导出 onnx 时一致）
    float  m_conf = 0.35f;                     // 置信度阈值
    float  m_iou = 0.35f;                      // NMS 的 IoU 阈值
    bool   m_drawBox = true;                   // 是否把框画到帧上
    int    m_thickness = 2;                    // 框线宽
    cv::Scalar m_boxColor{255, 0, 0};          // 框颜色（RGB 顺序，因为帧已经是 RGB）
    std::vector<std::string> m_classNames{"fire", "smoke"};   // 类别名兜底值
};

#endif //FIRESMOKEDETECTIONZCODE_YOLODETECTOR_H
