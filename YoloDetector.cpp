//
// YoloDetector 实现。
// 推理部分：onnxruntime C API（动态加载）；
// 前处理/后处理/NMS/画框：OpenCV（与 Python YOLOv5 类逐步对应）。
//
// 顺手修了 Python 版的一个 bug：NMSBoxes 要求传 xywh（左上角+宽高），
// Python 版传的是 xyxy，导致框越叠越大；C++ 版从源头就按 xywh 存。
//
#include "YoloDetector.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>        // cv::dnn::blobFromImage 造输入张量 + NMSBoxes
#include <algorithm>
#include <cmath>
#include <fstream>
#include <windows.h>
#include "onnxruntime_c_api.h"    // D:/Libraries/onnxruntime-win-x64-1.20.1/include

// ---------------------------------------------------------------------------
// onnxruntime 动态加载：拿到 C API 函数指针表（进程内只做一次）
// ---------------------------------------------------------------------------
static const OrtApi* ortApi() {
    static const OrtApi* api = nullptr;
    if (api) return api;

    // ① 从 exe 目录/PATH 找到 DLL
    HMODULE lib = LoadLibraryA("onnxruntime.dll");
    if (!lib) return nullptr;
    // ② 拿到 OrtGetApiBase 函数（这是 DLL 导出的唯一入口之一）
    using GetApiBaseFn = const OrtApiBase* (*)();
    auto getBase = reinterpret_cast<GetApiBaseFn>(GetProcAddress(lib, "OrtGetApiBase"));
    if (!getBase) return nullptr;
    // ③ 用编译时头文件的版本号换取对应版本的函数表（版本必须和 DLL 匹配）
    api = getBase()->GetApi(ORT_API_VERSION);
    return api;
}

// 每次调 onnxruntime 都返回 OrtStatus*，统一检查（非空 = 出错）
static bool ortOk(const OrtApi* api, OrtStatus* st, const char* what, std::string* err) {
    if (!st) return true;
    if (err) *err = std::string(what) + ": " + api->GetErrorMessage(st);
    api->ReleaseStatus(st);
    return false;
}

// 环境对象进程级共享（创建一次，随进程销毁，属于可接受的"懒单例"）
static OrtEnv* ortEnv(const OrtApi* api, std::string* err) {
    static OrtEnv* env = nullptr;
    if (!env) {
        if (!ortOk(api, api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "FireSmokeDetection", &env),
                   "CreateEnv", err))
            return nullptr;
    }
    return env;
}

// ---------- 加载模型 ----------
bool YoloDetector::load(const std::string& onnxPath, std::string* err) {
    unload();
    const OrtApi* api = ortApi();
    if (!api) { if (err) *err = "找不到 onnxruntime.dll（检查 exe 旁是否有该 DLL）"; return false; }

    // 把模型文件整个读进内存：CreateSessionFromArray 按"字节"建会话，
    // 避开 Windows 宽字符路径的坑（CreateSession 的路径参数是 wchar_t*）
    std::ifstream f(onnxPath, std::ios::binary);
    if (!f) { if (err) *err = "打不开模型文件: " + onnxPath; return false; }
    std::vector<char> modelData((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    OrtSessionOptions* opts = nullptr;
    if (!ortOk(api, api->CreateSessionOptions(&opts), "CreateSessionOptions", err)) return false;
    api->SetIntraOpNumThreads(opts, 0);                                  // 0 = 用默认线程数（≈物理核心数），和 Python 版默认行为一致
    api->SetSessionGraphOptimizationLevel(opts, ORT_ENABLE_ALL);                // 打开图优化

    if (!ortOk(api, api->CreateSessionFromArray(ortEnv(api, err), modelData.data(),
                 modelData.size(), opts, &m_session), "CreateSession", err)) {
        api->ReleaseSessionOptions(opts);
        return false;
    }
    api->ReleaseSessionOptions(opts);

    // 内存描述符：告诉 onnxruntime 我们的输入张量在 CPU 内存里（detect 时要用）
    if (!ortOk(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &m_memInfo),
               "CreateCpuMemoryInfo", err)) { unload(); return false; }

    // 从模型里读输入/输出层的名字（YOLOv5 通常是 "images" / "output0"，但不写死）
    OrtAllocator* alloc = nullptr;
    api->GetAllocatorWithDefaultOptions(&alloc);
    char* name = nullptr;
    if (ortOk(api, api->SessionGetInputName(m_session, 0, alloc, &name), "GetInputName", err)) {
        m_inputName = name; api->AllocatorFree(alloc, name);
    }
    if (ortOk(api, api->SessionGetOutputName(m_session, 0, alloc, &name), "GetOutputName", err)) {
        m_outputName = name; api->AllocatorFree(alloc, name);
    }

    m_loaded = true;
    return true;
}

// ---------- 释放会话 ----------
void YoloDetector::unload() {
    const OrtApi* api = ortApi();
    if (!api) { m_loaded = false; return; }
    if (m_session)  { api->ReleaseSession(m_session);  m_session = nullptr; }
    if (m_memInfo)  { api->ReleaseMemoryInfo(m_memInfo); m_memInfo = nullptr; }
    m_loaded = false;
}

YoloDetector::~YoloDetector() {
    unload();
}

// ---------- 检测一帧 ----------
std::vector<DetectionResult> YoloDetector::detect(cv::Mat& frame) {
    std::vector<DetectionResult> results;
    std::string errLocal;             // onnxruntime 调用的错误暂存（本函数不对外报告，失败就放弃本帧）
    if (!m_loaded || frame.empty()) return results;
    const OrtApi* api = ortApi();

    // ===== 前处理（OpenCV 代劳）=====
    // blobFromImage 一次做完四件事：缩放到 640x640、乘 1/255 归一化、HWC→NCHW、加 batch 维
    // swapRB 传 false：因为 frame 已经是 RGB（VideoSource::read 保证），模型要的就是 RGB。
    // 如果哪天直接传 BGR 原始帧，这里要改成 true —— 通道顺序错了检测结果会莫名变差
    cv::Mat blob = cv::dnn::blobFromImage(frame, 1.0 / 255.0,
                                          cv::Size(m_inputW, m_inputH),
                                          cv::Scalar(), false, false, CV_32F);

    // 输入张量：把 blob 的内存包装成 onnxruntime 的 OrtValue（零拷贝，不复制数据）
    const int64_t inShape[4] = {1, 3, m_inputH, m_inputW};
    OrtValue* inTensor = nullptr;
    if (!ortOk(api, api->CreateTensorWithDataAsOrtValue(m_memInfo, blob.ptr<float>(),
                 blob.total() * sizeof(float), inShape, 4,
                 ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inTensor),
                 "CreateTensor", &errLocal)) {
        return results;
    }

    // ===== 推理 =====
    const char* inNames[]  = { m_inputName.c_str() };
    const char* outNames[] = { m_outputName.c_str() };
    OrtValue* outTensor = nullptr;
    bool ok = ortOk(api, api->Run(m_session, nullptr, inNames, &inTensor, 1, outNames, 1, &outTensor),
                    "Run", &errLocal);
    api->ReleaseValue(inTensor);
    if (!ok) return results;

    // ===== 把输出拷出来：[1, 25200, 7] 的 float 数组 =====
    OrtTensorTypeAndShapeInfo* info = nullptr;
    float* outData = nullptr;
    size_t count = 0;
    int64_t dims[3] = {1, 0, 0};
    if (ortOk(api, api->GetTensorTypeAndShape(outTensor, &info), "GetShape", &errLocal)) {
        size_t dimCount = 0;
        api->GetDimensionsCount(info, &dimCount);
        std::vector<int64_t> dimsV(dimCount);
        api->GetDimensions(info, dimsV.data(), dimCount);
        api->GetTensorShapeElementCount(info, &count);
        api->ReleaseTensorTypeAndShapeInfo(info);
        api->GetTensorMutableData(outTensor, reinterpret_cast<void**>(&outData));
        if (dimCount == 3) {
            dims[1] = dimsV[1];     // 25200 个候选
            dims[2] = dimsV[2];     // 每候选 7 个数
        }
    }
    api->ReleaseValue(outTensor);
    if (!outData || dims[2] < 6) return results;      // 形状不对就直接放弃本帧

    const int rows  = static_cast<int>(dims[1]);
    const int attrs = static_cast<int>(dims[2]);
    const int classCount = attrs - 5;                 // 7-5 = 2 类（fire/smoke）

    // 框坐标是相对 640 输入的，要按原图/输入的比例缩放回原图
    const float sx = static_cast<float>(frame.cols) / m_inputW;
    const float sy = static_cast<float>(frame.rows) / m_inputH;

    std::vector<cv::Rect> boxes;                      // xywh（NMSBoxes 要求的格式）
    std::vector<float>    scores;
    std::vector<int>      classIds;

    for (int i = 0; i < rows; ++i) {
        const float* p = outData + static_cast<size_t>(i) * attrs;

        const float obj = p[4];                       // 这格里"有没有东西"的置信度
        if (obj <= m_conf) continue;                  // 第一道门槛：先筛掉大半候选

        // 在两个类别分里挑最高的
        int bestClass = 0;
        float bestClassScore = p[5];
        for (int c = 1; c < classCount; ++c) {
            if (p[5 + c] > bestClassScore) { bestClassScore = p[5 + c]; bestClass = c; }
        }

        const float score = obj * bestClassScore;     // 最终得分 = obj × 类别分（YOLOv5 惯例）
        if (score <= m_conf) continue;                // 第二道门槛

        // 中心坐标 + 宽高，按比例缩放回原图，再换算成左上角 xywh
        const float cx = p[0] * sx, cy = p[1] * sy;
        const float w  = p[2] * sx, h  = p[3] * sy;
        cv::Rect box(static_cast<int>(cx - w / 2), static_cast<int>(cy - h / 2),
                     static_cast<int>(w), static_cast<int>(h));

        boxes.push_back(box);
        scores.push_back(score);
        classIds.push_back(bestClass);
    }

    // ===== 非极大值抑制（NMS）：同一目标被相邻格子重复检出，只留最好的 =====
    std::vector<int> keep;
    if (!boxes.empty())
        cv::dnn::NMSBoxes(boxes, scores, m_conf, m_iou, keep);

    for (int idx : keep) {
        DetectionResult r;
        r.score = scores[idx];
        r.label = classIds[idx] < static_cast<int>(m_classNames.size())
                  ? m_classNames[classIds[idx]]
                  : "class_" + std::to_string(classIds[idx]);
        r.box = boxes[idx] & cv::Rect(0, 0, frame.cols, frame.rows);   // 裁掉出界部分
        results.push_back(r);
    }

    // 按置信度从高到低排，界面显示和画框都更直观
    std::sort(results.begin(), results.end(),
              [](const DetectionResult& a, const DetectionResult& b) { return a.score > b.score; });

    if (m_drawBox) drawDetections(frame, results);    // 直接把框画在帧上（Python 同款做法）
    return results;
}

// ---------- 画框 + 标签（公式与 Python __formatResult 一致） ----------
void YoloDetector::drawDetections(cv::Mat& frame, const std::vector<DetectionResult>& dets) const {
    // 线宽随图像尺寸自适应：小图细线、大图粗线，但不小于设定的 thickness
    const int lw = std::max(static_cast<int>(std::lround((frame.rows + frame.cols) / 2.0 * 0.003)),
                            m_thickness);
    const int tf = std::max(lw - 1, 1);                       // 字体笔画粗细
    // 文字颜色取框颜色的反色（255-x），保证在色块上看得清（Python 同款）
    const cv::Scalar txtColor(255 - m_boxColor[0], 255 - m_boxColor[1], 255 - m_boxColor[2]);

    for (const auto& r : dets) {
        cv::rectangle(frame, r.box, m_boxColor, lw, cv::LINE_AA);

        const std::string tag = r.label + " " + std::to_string(static_cast<int>(r.score * 100)) + "%";
        int baseLine = 0;
        const cv::Size ts = cv::getTextSize(tag, cv::FONT_HERSHEY_SIMPLEX, lw / 3.0, tf, &baseLine);

        // 标签底色块：框上边放得下就放框外，放不下就塞框内
        const bool outside = r.box.y - ts.height >= 3;
        const cv::Point p2(r.box.x + ts.width,
                           outside ? r.box.y - ts.height - 3 : r.box.y + ts.height + 3);
        cv::rectangle(frame, cv::Point(r.box.x, r.box.y), p2, m_boxColor, -1, cv::LINE_AA);
        cv::putText(frame, tag,
                    cv::Point(r.box.x, outside ? r.box.y - 2 : r.box.y + ts.height + 2),
                    cv::FONT_HERSHEY_SIMPLEX, lw / 3.0, txtColor, tf, cv::LINE_AA);
    }
}
