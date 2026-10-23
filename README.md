# FireSmokeDetection（onnx 分支）

**实时烟雾火焰检测桌面应用** —— Qt6 界面 + OpenCV 解码与后处理 + onnxruntime 推理，三线程管线。
推理引擎为 onnxruntime 的 C++ API（`Ort::Session`），输入尺寸与层名从模型自动读取，比 main 分支的 cv::dnn 限制更少。

> 本仓库四个分支共用同一套界面与三线程管线，差别在推理引擎与功能范围：
>
> | 分支 | 推理引擎 | 定位 |
> |---|---|---|
> | [main](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/main) | OpenCV `cv::dnn`（内置） | 单机完整版，依赖最少 |
> | **onnx**（本分支） | onnxruntime C++ API | 单机完整版，推理更快，模型限制更少 |
> | [tiny](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/tiny) | onnxruntime C API（动态加载） | 教学精简版，功能最少 |
> | [client-server](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/client-server) | `cv::dnn` | Qt 上位机 + 告警平台服务端 |

## 与 main 分支的区别

界面、三线程架构、警报/录像/截图等功能与 main 分支完全一致，**只有推理引擎及其带来的约束不同**：

| | main（cv::dnn） | onnx（本分支） |
|---|---|---|
| 推理引擎 | OpenCV 内置 dnn 模块（CPU） | onnxruntime 1.20.1 C++ API（CPU） |
| 第三方推理库依赖 | 无 | 需 onnxruntime 预编译包（CMake 多一处 `ORT_DIR` 路径） |
| 模型输入尺寸 | 写死 640×640 | **从模型自动读取**，任意尺寸均可 |
| 输入/输出层名 | 校验终输出层必须叫 `output` | **从模型自动读取**（通常是 `images`/`output0`，不写死） |
| 模型必须 onnxsim 化简 | 是（`Floor` 算子会被 cv::dnn 拒绝） | 否（onnxruntime 可直接加载，化简了也无害） |
| 中文模型路径 | 支持（MSMF/文件系统层面） | 支持（Session 路径参数是 `wchar_t*`，`toStdWString` 直达） |
| CPU 推理速度 | N0.6 ≈ 48ms/帧（见 main README 实测） | onnxruntime 带图优化与内置线程池，同模型通常更快 |

不变的部分：后处理仍按 **YOLOv5 检测头格式** `[1, N, 5+类别数]` 解析（详见下文模型要求），
所以依然**不是通用 ONNX 检测器**——YOLOv8 / YOLOv11 等其他检测头的模型不能直接换。

## 功能

- 检测视频文件 / 图片 / 摄像头，实时显示带锚框与置信度的画面（FPS 叠加可开关）
- 模型（.onnx）与类别文件（.txt）可随时更换（限 YOLOv5 检测头格式，见下文）
- 置信度 / IOU 阈值、翻转 / 旋转、锚框颜色运行中即时生效（atomic 热参数，不用重开模型）
- 警报：连续 10 帧检出任意目标 → 警报音（5 秒冷却，运行中可开关）
- 录制检测视频（mp4）、截图、日志导出，保存目录可选
- 界面配置持久化（QSettings），下次启动自动恢复
- 关窗确认 + 线程安全退出（检测中关窗会先询问）

## 模型要求：YOLOv5 检测头格式

| 约定项 | 取值 |
|---|---|
| 输入尺寸 | 从模型自动读取（任意尺寸，拉伸缩放） |
| 输入/输出层名 | 从模型自动读取 |
| 输出形状 | `[1, N, 5+类别数]`（YOLOv5 检测头；N 因输入尺寸而异） |
| 每行含义 | `cx, cy, w, h`（相对模型输入）+ obj 置信度 + 各类别分 |

- ✅ **可以换的模型**：YOLOv5（v6.0 及输出格式相同的版本）训练的任意类别、任意输入尺寸的检测模型——类别数按 `5+类别数` 自适应，尺寸和层名自动读取；
- ❌ **不能换的**：非 YOLOv5 检测头的模型（YOLOv8 是 `[1, 4+nc, 8400]` + DFL 解码），**必须重写推理与后处理代码**。

导出命令（`--simplify` 非必需，仅推荐）：

```bash
git clone -b v6.0 --depth 1 https://github.com/ultralytics/yolov5.git
cd yolov5
python export.py --weights best.pt --include onnx --imgsz 640 --opset 12
```

`need/` 下附带一个 YOLOv5 6.0 训练的 fire/smoke 两类模型（m0.9，s 规模），构建时自动同步到 exe 旁边。

## 架构：三线程 + 请求信箱

```
主线程(GUI)                     检测线程                      解码线程(VideoSource 内部)
──────────                     ─────────                     ─────────────────────
点"开始检测"：打包 DetectConfig
  ── requestDetect ──► 信箱
点"输入方式/文件" ── requestSource ──► 信箱
滑块/复选框 ── setConf/setFlip ──► atomic 热参数
                                 run() 三态循环：
                                 ① 消费信箱（换源/换模型）
                                 ② read(frame) ◄───────── 解码线程按视频 fps 抓帧
                                 ③ 推理 + 画框                            只保留最新一帧
                                 ④ frameReady/resultReady ── 排队信号 ──► 主线程槽更新界面
```

设计要点：

- **谁用谁创建**：VideoSource / YoloDetector 对象只在检测线程内创建和销毁，
  UI 线程永不持有它们的指针——跨线程只递"请求"（mutex 信箱）和"参数"（atomic），无指针竞态
- **最新帧缓冲 + 帧号**：解码线程按视频自身帧率持续抓帧、只保留最新一帧；
  推理慢时自动丢帧保实时，推理快时 `read()` 被解码节奏限速防快进
- **识别与决策分层**：模型只回答"这帧有什么"；"什么触发警报"（连续 10 帧）是可配置的业务规则

## 技术栈

| 组件 | 用途 |
|---|---|
| Qt 6.8.3 (msvc2022_64) | 界面、信号槽、QThread、QSoundEffect 警报音 |
| OpenCV 4.13 (opencv_world) | 视频解码、前处理 blob、NMS、画框 |
| onnxruntime 1.20.1 (C++ API) | ONNX 模型推理（隐式链接 onnxruntime.lib，RAII 管理资源） |
| C++17 / CMake / Ninja | 构建 |

## 构建

环境要求：**Windows + Visual Studio 2022（MSVC）+ Qt 6.8.3 msvc2022_64 + OpenCV 4.13 MSVC 预编译版 + onnxruntime 1.20.1**。

1. 修改 `CMakeLists.txt` 中三处本地路径为你的安装位置（比 main 分支多一处）：
   - `Qt6_DIR`（Qt msvc 版）
   - `OpenCV_DIR`（OpenCV MSVC 版 build 目录）
   - `ORT_DIR`（onnxruntime 解压目录，需含 include/ 与 lib/）
2. CLion 打开项目（选用 Visual Studio 工具链），或命令行：

```bat
call "<VS路径>\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B cmake-build-debug-msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug-msvc
```

构建完成后 `need/`（模型等资源）、全部依赖 DLL（含 onnxruntime.dll）会自动拷贝到 exe 旁边。

## 使用

1. 输入页选择摄像头 / 视频文件 / 图片；选择 onnx 模型时会自动配对同名类别 txt（如 `m0.9.onnx` → `m0.9.txt`，同目录或上一级），类别文件兼容 `序号 类别名` 格式与中英文逗号/竖线/换行分隔
2. 输出页调整阈值、锚框颜色、录制选项
3. 点"开始检测"；点"停止"回到预览态，视频播完自动结束
4. 截图 / 录像 / 日志默认保存在 exe 旁的 `out/`，可在"保存路径"里改

## 项目结构

```
├── main.cpp              # 入口
├── MainWindow.h/cpp      # 界面层：控件、槽、配置持久化
├── Detection.h/cpp       # 检测线程：三态循环、请求信箱、警报计数、录像
├── VideoSource.h/cpp     # 数据源：解码线程、最新帧缓冲、翻转/旋转
├── YoloDetector.h/cpp    # 推理：blobFromImage → Ort::Session::Run → NMS → 画框
└── need/                 # 模型、类别文件、警报音
```

## 已知边界

- **类无关 NMS**：`cv::dnn::NMSBoxes` 把所有类别的候选框放在同一个池子里互相抑制。
  两个不同类别的目标位置重叠时（烟雾火焰场景很常见：烟和火本就长在一起），
  置信度低的框会被高的压掉——界面上少画一个框、日志里少报一类。
  正确做法是类内 NMS：按类别分组、各组各做一遍（YOLOv5 官方的坐标平移法效果等价）。
- 输入为拉伸缩放（非 letterbox），长宽比悬殊的画面两端会有形变
- 屏幕捕获未实现（接口已预留）
- 视频源为实时模式：推理慢于视频帧率时丢帧采样（画面跳跃但时间线对齐）；
  如需逐帧全覆盖分析，去掉 VideoSource 的解码线程、改为推模式即可
- 更换模型类别后，警报对所有检出目标生效（连续 10 帧检出任意目标即响）
