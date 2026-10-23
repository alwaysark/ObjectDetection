# FireSmokeDetection

**实时烟雾火焰检测桌面应用** —— Qt6 界面 + OpenCV 解码与后处理 + `cv::dnn` 推理，三线程管线。
推理引擎使用 OpenCV 自带的 dnn 模块，**不依赖 onnxruntime**——只需要 Qt 和 OpenCV 就能编译运行。

> 本仓库四个分支中，main / onnx / client-server 共用同一套界面与三线程管线，
> tiny 是**两线程**教学精简版；差别在推理引擎与功能范围：
>
> | 分支 | 推理引擎 | 定位 |
> |---|---|---|
> | **main**（本分支） | OpenCV `cv::dnn`（内置） | 单机完整版，依赖最少 |
> | [onnx](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/onnx) | onnxruntime C++ API | 单机完整版，推理更快，输入尺寸/层名从模型自动读取 |
> | [tiny](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/tiny) | OpenCV `cv::dnn`（内置） | 两线程教学精简版，功能最少 |
> | [client-server](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/client-server) | `cv::dnn` | Qt 上位机 + 告警平台服务端 |

## 运行环境

- **仅 Windows + MSVC 工具链**：Qt / OpenCV 官方只发布 MSVC 预编译包，MinGW 与 MSVC 的
  C++ ABI 不兼容、链不上这些库；`CMakeLists.txt` 里 `if (NOT MSVC)` 会直接报错退出
- 版本组合：Visual Studio 2022 及以上（MSVC 工具集）+ Qt 6.8.3 msvc2022_64 +
  OpenCV 4.13 MSVC 预编译版。**不需要任何推理库**——cv::dnn 就在 OpenCV 里
- 运行时零配置：构建时把全部依赖 DLL（Qt / OpenCV / MSVC 运行库，含 Debug 调试版）、
  Qt 插件、need/ 资源自动拷到 exe 旁，构建产物目录整体拷到任何 64 位 Windows 双击即跑

## 功能

- 检测视频文件 / 图片 / 摄像头，实时显示带锚框与置信度的画面（FPS 叠加可开关）
- 模型（.onnx）与类别文件（.txt）可随时更换（限 YOLOv5 6.0 格式，见下文）
- 置信度 / IOU 阈值、翻转 / 旋转、锚框颜色运行中即时生效（atomic 热参数，不用重开模型）
- 警报：连续 10 帧检出任意目标 → 警报音（5 秒冷却，运行中可开关）
- 录制检测视频（mp4）、截图、日志导出，保存目录可选
- 界面配置持久化（QSettings），下次启动自动恢复
- 关窗确认 + 线程安全退出（检测中关窗会先询问）

## 模型要求：仅限 YOLOv5 6.0 导出的 onnx

**推理代码按 YOLOv5 6.0 的导出格式写死，不是通用 ONNX 检测器。**

| 约定项 | 固定值 |
|---|---|
| 输入尺寸 | **640×640**（代码写死，拉伸缩放） |
| 终输出层名 | **`output`**（load 时校验，找不到直接拒绝加载） |
| 输出形状 | `[1, 25200, 5+类别数]`，25200 = (80×80+40×40+20×20)×3 anchor |
| 每行含义 | `cx, cy, w, h`（相对 640 输入）+ obj 置信度 + 各类别分 |

由此：

- ✅ **可以换的模型**：用 **YOLOv5 v6.0** 训练**其他类别的目标检测**再导出——类别数可变（后处理按 `5+类别数` 自适应），输入尺寸、输出名都不变；
- ❌ **不能换的**：其他输入尺寸、其他版本（YOLOv5 新版 / YOLOv8 / YOLOv11 等）的模型。
  换版本 = 输出布局、输出名、anchor 结构全变（如 YOLOv8 是 `[1, 4+nc, 8400]`、DFL 解码），**必须重写推理与后处理代码**。

导出命令：

```bash
git clone -b v6.0 --depth 1 https://github.com/ultralytics/yolov5.git
cd yolov5
python export.py --weights best.pt --include onnx --imgsz 640 --simplify --opset 12
```

`--simplify`（onnxsim）是关键：它折叠了新版导出中的 `Floor` 算子——**未经化简的模型会被 cv::dnn 拒绝加载**（onnx 分支无此限制）。

`need/` 下附带三个 YOLOv5 6.0 训练的 fire/smoke 两类模型（N0.6 / m0.9 / L0.9，n/s/l 三种规模），
构建时自动同步到 exe 旁边。

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

## 性能参考

实测（Release 版 OpenCV，640 输入，CPU 推理）：
N0.6 ≈ 48ms/帧（约 20 FPS）；m0.9 ≈ 257ms/帧（约 4 FPS）。
**Debug 构建下推理慢 3 倍以上（N0.6 ≈ 157ms），觉得慢先查构建配置。**

## 技术栈

| 组件 | 用途 |
|---|---|
| Qt 6.8.3 (msvc2022_64) | 界面、信号槽、QThread、QSoundEffect 警报音 |
| OpenCV 4.13 (opencv_world) | 视频解码、前处理 blob、**cv::dnn 推理**（CPU）、NMS、画框 |
| C++17 / CMake / Ninja | 构建 |

## 构建

环境要求见上文"运行环境"。

1. 修改 `CMakeLists.txt` 中两处本地路径为你的安装位置：
   - `Qt6_DIR`（Qt msvc 版）
   - `OpenCV_DIR`（OpenCV MSVC 版 build 目录）
2. CLion 打开项目（选用 Visual Studio 工具链），或命令行：

```bat
call "<VS路径>\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B cmake-build-debug-msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug-msvc
```

构建完成后 `need/`（模型等资源）与全部依赖 DLL 会自动拷贝到 exe 旁边。

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
├── YoloDetector.h/cpp    # 推理：blobFromImage → forward("output") → NMS → 画框
└── need/                 # 模型、类别文件、警报音
```

## 已知边界

- **类无关 NMS**：`cv::dnn::NMSBoxes` 把所有类别的候选框放在同一个池子里互相抑制。
  两个不同类别的目标位置重叠时（烟雾火焰场景很常见：烟和火本就长在一起），
  置信度低的框会被高的压掉——界面上少画一个框、日志里少报一类。
  正确做法是类内 NMS：按类别分组、各组各做一遍（YOLOv5 官方的坐标平移法效果等价）。
- 输入尺寸固定 640×640 拉伸缩放（非 letterbox），长宽比悬殊的画面两端会有形变
- 屏幕捕获未实现（接口已预留）
- 视频源为实时模式：推理慢于视频帧率时丢帧采样（画面跳跃但时间线对齐）；
  如需逐帧全覆盖分析，去掉 VideoSource 的解码线程、改为推模式即可
- 更换模型类别后，警报对所有检出目标生效（连续 10 帧检出任意目标即响）
