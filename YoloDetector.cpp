//
// YoloDetector 实现（onnxruntime C++ API 版）。
// 推理：Ort::Session；前处理/后处理/画框自写，不依赖 OpenCV 的 dnn/imgproc。
//
// 前处理 makeBlob 与旧版 cv::dnn::blobFromImage(frame, 1/255, Size(W,H), swapRB=false)
// 语义对齐：拉伸缩放（双线性、半像素中心对齐、边缘钳位）+ 乘 1/255 + HWC→NCHW。
// 后处理 nmsBoxes 与旧版 cv::dnn::NMSBoxes 语义对齐：类无关、按分数降序贪心保留、
// IoU 超过阈值的后继框抑制。
//
#include "YoloDetector.h"
#include <QPainter>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

// ---------- 加载模型 ----------
bool YoloDetector::load(const QString& onnxPath, QString* err) {
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(0);              // 0 = 默认线程数（≈物理核心数）
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Windows 上 Session 的路径参数是 wchar_t*，toStdWString 直达中文路径
        m_session = std::make_unique<Ort::Session>(
                m_env, onnxPath.toStdWString().c_str(), opts);

        // 从模型自动读输入尺寸（YOLOv5 640；模型写死多少就用多少）
        auto inShapeInfo = m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        auto inShape = inShapeInfo.GetShape();
        if (inShape.size() == 4 && inShape[2] > 0 && inShape[3] > 0) {
            m_inputH = static_cast<int>(inShape[2]);
            m_inputW = static_cast<int>(inShape[3]);
        }

        // 从模型读输入/输出层的名字（YOLOv5 通常是 "images"/"output0"，不写死）
        Ort::AllocatorWithDefaultOptions alloc;
        m_inputName  = m_session->GetInputNameAllocated(0, alloc).get();
        m_outputName = m_session->GetOutputNameAllocated(0, alloc).get();
    } catch (const Ort::Exception& e) {
        if (err) *err = e.what();
        m_session.reset();
        return false;
    }
    m_loaded = true;
    return true;
}

// ---------- 前处理：双线性缩放 + 归一化 + NCHW ----------
// 半像素中心对齐：dst 像素中心 (d+0.5) 映射回 src 坐标 (d+0.5)*scale-0.5，
// 落在两个相邻 src 像素之间按小数权重混合；越界坐标钳到边界（与 cv::resize
// INTER_LINEAR 的插值表一致）。写入按 NCHW 平面布局，通道序 R,G,B（RGB 帧直通）。
void YoloDetector::makeBlob(const QImage& src) {
    const int SW = src.width(),  SH = src.height();
    const int DW = m_inputW,     DH = m_inputH;
    m_blob.assign(size_t(DW) * DH * 3, 0.0f);

    const float sx = static_cast<float>(SW) / DW;
    const float sy = static_cast<float>(SH) / DH;

    float* planeR = m_blob.data();
    float* planeG = planeR + size_t(DW) * DH;
    float* planeB = planeG + size_t(DW) * DH;

    for (int dy = 0; dy < DH; ++dy) {
        const float fy = (dy + 0.5f) * sy - 0.5f;
        const int   y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, SH - 1);
        const int   y1 = std::clamp(y0 + 1, 0, SH - 1);
        const float wy = fy - std::floor(fy);
        const float wy0 = 1.0f - wy, wy1 = wy;

        const uchar* row0 = src.constScanLine(y0);
        const uchar* row1 = src.constScanLine(y1);

        for (int dx = 0; dx < DW; ++dx) {
            const float fx = (dx + 0.5f) * sx - 0.5f;
            const int   x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, SW - 1);
            const int   x1 = std::clamp(x0 + 1, 0, SW - 1);
            const float wx = fx - std::floor(fx);
            const float wx0 = 1.0f - wx, wx1 = wx;

            const int p00 = (x0 * 3), p01 = (x1 * 3);
            const float r = row0[p00+0]*wx0 + row0[p01+0]*wx1;
            const float g = row0[p00+1]*wx0 + row0[p01+1]*wx1;
            const float b = row0[p00+2]*wx0 + row0[p01+2]*wx1;
            const float r1 = row1[p00+0]*wx0 + row1[p01+0]*wx1;
            const float g1 = row1[p00+1]*wx0 + row1[p01+1]*wx1;
            const float b1 = row1[p00+2]*wx0 + row1[p01+2]*wx1;

            const size_t o = size_t(dy) * DW + dx;
            planeR[o] = (r * wy0 + r1 * wy1) * (1.0f / 255.0f);
            planeG[o] = (g * wy0 + g1 * wy1) * (1.0f / 255.0f);
            planeB[o] = (b * wy0 + b1 * wy1) * (1.0f / 255.0f);
        }
    }
}

// ---------- 手写 NMS ----------
void YoloDetector::nmsBoxes(const std::vector<QRect>& boxes, const std::vector<float>& scores,
                            float iouThr, std::vector<int>& keep) {
    std::vector<int> order(scores.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(),
              [&scores](int a, int b) { return scores[a] > scores[b]; });

    std::vector<char> suppressed(scores.size(), 0);
    for (size_t ii = 0; ii < order.size(); ++ii) {
        const int i = order[ii];
        if (suppressed[i]) continue;
        keep.push_back(i);
        const int ai = boxes[i].width() * boxes[i].height();
        for (size_t jj = ii + 1; jj < order.size(); ++jj) {
            const int j = order[jj];
            if (suppressed[j]) continue;
            const QRect inter = boxes[i].intersected(boxes[j]);
            const int ia = inter.width() * inter.height();
            if (ia <= 0) continue;
            const int aj = boxes[j].width() * boxes[j].height();
            if (static_cast<float>(ia) / (ai + aj - ia) > iouThr)
                suppressed[j] = 1;
        }
    }
}

// ---------- 检测一帧 ----------
std::vector<DetectionResult> YoloDetector::detect(QImage& frame) {
    std::vector<DetectionResult> results;
    if (!m_session || frame.isNull()) return results;

    // 帧格式保险：管线里应是 RGB888；万一不是，转一份只用于推理
    const QImage src = frame.format() == QImage::Format_RGB888
                       ? frame : frame.convertToFormat(QImage::Format_RGB888);

    std::vector<float> output;          // 模型输出的拷贝
    int64_t rows = 0, attrs = 0;

    try {
        // ===== 前处理（自写）=====
        makeBlob(src);

        // ===== 推理 =====
        const int64_t inShape[4] = {1, 3, m_inputH, m_inputW};
        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inTensor = Ort::Value::CreateTensor<float>(memInfo, m_blob.data(),
                m_blob.size(), inShape, 4);

        const char* inNames[]  = { m_inputName.c_str() };
        const char* outNames[] = { m_outputName.c_str() };
        auto outputs = m_session->Run(Ort::RunOptions{nullptr},
                                      inNames, &inTensor, 1, outNames, 1);

        // ===== 取输出：[1, 25200, 5+类别数] 的 float 数组 =====
        float* outData = outputs.front().GetTensorMutableData<float>();
        auto shapeInfo = outputs.front().GetTensorTypeAndShapeInfo();
        const std::vector<int64_t> dims = shapeInfo.GetShape();
        const size_t count = shapeInfo.GetElementCount();
        if (dims.size() != 3 || dims[2] < 6) return results;   // 形状不对就放弃本帧
        output.assign(outData, outData + count);
        rows  = dims[1];                       // 25200 个候选
        attrs = static_cast<int>(dims[2]);     // 每候选 5+类别数 个数
    } catch (const Ort::Exception&) {
        return results;                        // 推理出错：放弃本帧，不逃逸到调用方
    }

    const int classCount = static_cast<int>(attrs) - 5;

    // 框坐标是相对模型输入的，要按原图/输入的比例缩放回原图
    // （前处理是拉伸缩放，所以 x/y 要分轴各乘各的）
    const float sx = static_cast<float>(frame.width())  / m_inputW;
    const float sy = static_cast<float>(frame.height()) / m_inputH;

    std::vector<QRect>   boxes;          // xywh（NMS 的格式）
    std::vector<float>   scores;
    std::vector<int>     classIds;

    for (int64_t i = 0; i < rows; ++i) {
        const float* p = output.data() + static_cast<size_t>(i) * attrs;

        const float obj = p[4];               // 这格里"有没有东西"的置信度
        if (obj <= m_conf) continue;          // 第一道门槛：先筛掉大半候选

        // 在各类别分里挑最高的
        int bestClass = 0;
        float bestClassScore = p[5];
        for (int c = 1; c < classCount; ++c) {
            if (p[5 + c] > bestClassScore) { bestClassScore = p[5 + c]; bestClass = c; }
        }

        const float score = obj * bestClassScore;   // 最终得分 = obj × 类别分（YOLOv5 惯例）
        if (score <= m_conf) continue;              // 第二道门槛

        // 中心坐标 + 宽高，按比例缩放回原图，再换算成左上角 xywh
        const float cx = p[0] * sx, cy = p[1] * sy;
        const float w  = p[2] * sx, h  = p[3] * sy;
        boxes.emplace_back(static_cast<int>(cx - w / 2), static_cast<int>(cy - h / 2),
                           static_cast<int>(w), static_cast<int>(h));
        scores.push_back(score);
        classIds.push_back(bestClass);
    }

    // ===== 非极大值抑制（NMS，自写）：同一目标被相邻格子重复检出，只留最好的 =====
    std::vector<int> keep;
    if (!boxes.empty())
        nmsBoxes(boxes, scores, m_iou, keep);

    for (int idx : keep) {
        DetectionResult r;
        r.score = scores[idx];
        r.label = classIds[idx] < static_cast<int>(m_classNames.size())
                  ? m_classNames[classIds[idx]]
                  : "class_" + std::to_string(classIds[idx]);
        r.box = boxes[idx].intersected(QRect(0, 0, frame.width(), frame.height()));  // 裁掉出界部分
        results.push_back(r);
    }

    // 按置信度从高到低排，界面显示和画框都更直观
    std::sort(results.begin(), results.end(),
              [](const DetectionResult& a, const DetectionResult& b) { return a.score > b.score; });

    if (m_drawBox) drawDetections(frame, results);   // 直接把框画在帧上（显示/录像共用）
    return results;
}

// ---------- 画框 + 标签（QPainter，支持中文类别名） ----------
void YoloDetector::drawDetections(QImage& frame, const std::vector<DetectionResult>& dets) const {
    if (dets.empty() || frame.isNull()) return;

    QPainter p(&frame);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 线宽随图像尺寸自适应：小图细线、大图粗线，但不低于设定的 thickness
    const int lw = std::max(static_cast<int>(std::lround((frame.height() + frame.width()) / 2.0 * 0.003)),
                            m_thickness);
    QFont f = p.font();
    f.setPixelSize(std::max(14, lw * 5));       // 与旧版 HERSHEY 字号(scale=lw/3)视觉大小接近
    p.setFont(f);
    const QFontMetrics fm(f);

    // 文字颜色取框颜色的反色，保证在色块上看得清
    const QColor txtColor(255 - m_boxColor.red(), 255 - m_boxColor.green(),
                          255 - m_boxColor.blue());

    for (const auto& r : dets) {
        // 锚框
        p.setPen(QPen(m_boxColor, lw));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.box);

        // 标签底色块：框上边放得下就放框外，放不下就塞框内（旧版同款规则）
        const QString tag = QString("%1 %2%")
                .arg(QString::fromStdString(r.label))
                .arg(static_cast<int>(r.score * 100));
        const int tw = fm.horizontalAdvance(tag), th = fm.height();
        const bool outside = r.box.y() - th >= 3;
        const QRect tagRect(r.box.x(),
                            outside ? r.box.y() - th - 3 : r.box.y(),
                            tw + 2 * lw, th);
        p.setPen(Qt::NoPen);
        p.setBrush(m_boxColor);
        p.drawRect(tagRect);

        p.setPen(txtColor);
        p.drawText(tagRect, Qt::AlignCenter, tag);
    }
}
