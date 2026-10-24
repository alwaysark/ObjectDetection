# FireSmokeDetection（onnx 分支）

**实时烟雾火焰检测桌面应用** —— Qt6 界面 + onnxruntime 推理 + 告警 SQLite 落库，三线程管线。
推理引擎为 onnxruntime 的 C++ API（`Ort::Session`），输入尺寸与层名从模型自动读取。

本分支的特点是**帧管线全 Qt 化**：从解码交付到界面显示、录像、锚框绘制，帧的载体统一是
`QImage`；OpenCV 只在两个"编解码边界"出现（`cv::VideoCapture` 解码、`cv::VideoWriter`
编码，分别藏在 `VideoSource` / `Recorder` 的 pimpl 里），整个代码库对外可调用的 OpenCV
接口只有这两个，所有头文件零 OpenCV 类型。前处理（缩放/归一化/NCHW）、后处理 NMS、
画框、FPS 叠加、翻转/旋转均为自实现（QImage / QPainter）。

> 本仓库四个分支中，main / onnx / client-server 共用同一套界面与三线程管线，
> tiny 是**两线程**教学精简版；差别在推理引擎与功能范围：
>
> | 分支 | 推理引擎 | 定位 |
> |---|---|---|
> | [main](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/main) | OpenCV `cv::dnn`（内置） | 单机完整版，依赖最少 |
> | **onnx**（本分支） | onnxruntime C++ API | 单机完整版，推理更快，模型限制更少，OpenCV 面最小 |
> | [tiny](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/tiny) | OpenCV `cv::dnn`（内置） | 两线程教学精简版，功能最少 |
> | [client-server](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/client-server) | `cv::dnn` | Qt 上位机 + 告警平台服务端 |

## 运行环境

- **仅 Windows + MSVC 工具链**：Qt / OpenCV / onnxruntime 官方只发布 MSVC 预编译包，
  MinGW 与 MSVC 的 C++ ABI 不兼容、链不上这些库（当初从 MinGW 迁到 MSVC 正是为了直接用
  onnxruntime 官方预编译包的 C++ API）；`CMakeLists.txt` 里 `if (NOT MSVC)` 会直接报错退出
- 版本组合：Visual Studio 2022 及以上（MSVC 工具集）+ Qt 6.8.3 msvc2022_64 +
  OpenCV 4.13 MSVC 预编译版 + onnxruntime 1.20.1（官方 MSVC 预编译包）
- 运行时零配置：构建时把全部依赖 DLL（Qt / OpenCV / onnxruntime / MSVC 运行库，含 Debug
  调试版）、Qt 插件、need/ 资源自动拷到 exe 旁，构建产物目录整体拷到任何 64 位 Windows 双击即跑

## 与 main 分支的区别

界面、三线程架构、警报/录像/截图/告警落库等功能与 main 分支一致，**推理引擎与帧管线不同**：

| | main（cv::dnn） | onnx（本分支） |
|---|---|---|
| 推理引擎 | OpenCV 内置 dnn 模块（CPU） | onnxruntime 1.20.1 C++ API（CPU） |
| 前处理 | `cv::dnn::blobFromImage` | 自写双线性缩放 + NCHW 打包（与前者数值等价，见下文） |
| 后处理 NMS | `cv::dnn::NMSBoxes` | 自写贪心 IoU 抑制（同语义） |
| 画框/FPS | `cv::rectangle`/`putText` | QPainter（**锚框标签支持中文**） |
| 帧载体 | `cv::Mat`（发界面前转 QImage） | **全程 QImage**，零中转 |
| 显示 | QLabel + QPixmap（每帧 2 次转换） | VideoView 自绘控件（paintEvent 直接画，零中转） |
| OpenCV 组件 | core imgproc videoio imgcodecs dnn | **core videoio**（仅编解码） |
| 第三方推理库依赖 | 无 | 需 onnxruntime 预编译包（CMake 多一处 `ORT_DIR` 路径） |
| 模型输入尺寸 | 写死 640×640 | **从模型自动读取**，任意尺寸均可 |
| 输入/输出层名 | 校验终输出层必须叫 `output` | **从模型自动读取**（通常是 `images`/`output0`，不写死） |
| 模型必须 onnxsim 化简 | 是（`Floor` 算子会被 cv::dnn 拒绝） | 否（onnxruntime 可直接加载，化简了也无害） |
| 中文模型路径 | 支持（MSMF/文件系统层面） | 支持（Session 路径参数是 `wchar_t*`，`toStdWString` 直达） |
| CPU 推理速度 | N0.6 ≈ 48ms/帧（见 main README 实测） | onnxruntime 带图优化与内置线程池，同模型通常更快 |

不变的部分：后处理仍按 **YOLOv5 检测头格式** `[1, N, 5+类别数]` 解析（详见下文模型要求），
所以依然**不是通用 ONNX 检测器**——YOLOv8 / YOLOv11 等其他检测头的模型不能直接换。

## 功能

- 检测视频文件 / 图片 / 摄像头，实时显示带锚框与置信度的画面（FPS 叠加可开关，锚框标签支持中文）
- 模型（.onnx）与类别文件（.txt）可随时更换（限 YOLOv5 检测头格式，见下文）
- 置信度 / IOU 阈值、翻转 / 旋转、锚框颜色运行中即时生效（atomic 热参数，不用重开模型）
- 警报：连续 10 帧检出任意目标 → 警报音（5 秒冷却，运行中可开关）
- **告警落库**：触发告警时自动截图存 png + 写入 SQLite（exe 旁 `alarms.db`）；
  输出页"告警记录"按钮打开查询对话框（最近 500 条，双击快照列用系统看图程序打开）
- 录制检测视频（mp4，含锚框画面）、截图、日志导出，保存目录可选
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
python export.py --weights best.pt --include onnx --imgsz 640 --simplify --opset 12
```

`--simplify` 对本分支非必需——onnxruntime 支持完整 ONNX 算子集，未化简的模型也能加载。
但建议加上：yolov5 v6.0 导出的 onnx 带有 `Floor`（取整）算子，而 OpenCV 的 dnn 模块
只实现了 ONNX 算子集的一个子集、**不认识 `Floor`**，未化简的模型在解析阶段就被拒——
main / tiny 分支用的是 cv::dnn，只有化简过的模型才能在四个分支间通用。

`need/` 下附带一个 YOLOv5 6.0 训练的 fire/smoke 两类模型（m0.9，s 规模），构建时自动同步到 exe 旁边。

## 架构：三线程 + 请求信箱 + QImage 帧管线

```
主线程(GUI)                     检测线程                      解码线程(VideoSource 内部)
──────────                     ─────────                     ─────────────────────
点"开始检测"：打包 DetectConfig
  ── requestDetect ──► 信箱
点"输入方式/文件" ── requestSource ──► 信箱
滑块/复选框 ── setConf/setFlip ──► atomic 热参数
                                 run() 三态循环：
                                 ① 消费信箱（换源/换模型）
                                 ② read(QImage) ◄──────── 解码线程按视频 fps 抓帧（cv::Mat BGR）
                                 ③ 推理 + QPainter 画框     只保留最新一帧，交付时转 QImage RGB
                                 ④ frameReady(QImage) ── 排队信号 ──► VideoView::paintEvent 直接画
                                 ⑤ 告警触发 ── alarmTriggered ──► 截图 + SQLite 落库
```

帧的完整旅程（全程一张 QImage，隐式共享传递，无逐像素拷贝）：

```
cv::VideoCapture 解码 (Mat BGR，VideoSource 内部)
  └─ rgbSwapped 一次 → QImage RGB888（自有内存）          ← OpenCV 的出界点
       └─ mirrored/transformed（按需翻转/旋转，QImage 自带）
            └─ YoloDetector::detect(QImage&)：自写双线性缩放→NCHW→Ort 推理→自写 NMS
            │    └─ QPainter 把锚框/标签画在这张 QImage 上（显示与录像共用）
            ├─ Recorder::write(QImage)：rgbSwapped→BGR Mat→cv::VideoWriter     ← OpenCV 的入界点
            └─ emit frameReady(QImage) → VideoView::paintEvent 等比缩放绘制（零 QPixmap 中转）
```

自写前处理与旧版 `cv::dnn::blobFromImage` 的等价性做过逐值验证：合成帧上两种管线的检出
结果集合一致（同类别同位置，分数差 ≤0.008），残差来自 OpenCV INTER_LINEAR 内部的
11 位定点数量化，属噪声级。

设计要点：

- **谁用谁创建**：VideoSource / YoloDetector 对象只在检测线程内创建和销毁，
  UI 线程永不持有它们的指针——跨线程只递"请求"（mutex 信箱）和"参数"（atomic），无指针竞态
- **最新帧缓冲 + 帧号**：解码线程按视频自身帧率持续抓帧、只保留最新一帧；
  推理慢时自动丢帧保实时，推理快时 `read()` 被解码节奏限速防快进
- **识别与决策分层**：模型只回答"这帧有什么"；"什么触发警报"（连续 10 帧）是可配置的业务规则
- **告警多消费者**：同一告警事件接出两条路径——`targetDetected`（放警报音）与
  `alarmTriggered`（带检出明细 → 截图 + 入库），互不阻塞，落库失败不影响检测主流程

## 技术栈

| 组件 | 用途 |
|---|---|
| Qt 6.8.3 (msvc2022_64) | 界面、信号槽、QThread、QImage 帧管线、QPainter 绘制、QSoundEffect 警报音、QtSql 告警入库 |
| OpenCV 4.13 (opencv_world) | **仅** `cv::VideoCapture` 解码 + `cv::VideoWriter` 编码（藏在 VideoSource/Recorder 的 pimpl 里） |
| onnxruntime 1.20.1 (C++ API) | ONNX 模型推理（隐式链接 onnxruntime.lib，RAII 管理资源） |
| SQLite（QtSql/QSQLITE） | 告警记录落库与查询（alarms.db） |
| C++17 / CMake / Ninja | 构建 |

## 构建

环境要求见上文"运行环境"。

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

构建完成后 `need/`（模型等资源）、全部依赖 DLL（含 onnxruntime.dll）、Qt 插件
（platforms/multimedia/sqldrivers）会自动拷贝到 exe 旁边。

## 使用

1. 输入页选择摄像头 / 视频文件 / 图片；选择 onnx 模型时会自动配对同名类别 txt（如 `m0.9.onnx` → `m0.9.txt`，同目录或上一级），类别文件兼容 `序号 类别名` 格式与中英文逗号/竖线/换行分隔
2. 输出页调整阈值、锚框颜色、录制选项
3. 点"开始检测"；点"停止"回到预览态，视频播完自动结束
4. 截图 / 录像 / 日志默认保存在 exe 旁的 `out/`，可在"保存路径"里改；
   告警快照（`alarm_时间.png`）与录像同样存在该目录
5. "告警记录"按钮查历史：表格显示时间/类别/置信度/目标数/快照，双击快照列打开对应截图

## 项目结构

```
├── main.cpp              # 入口
├── MainWindow.h/cpp      # 界面层：控件、槽、配置持久化、告警查询对话框
├── Detection.h/cpp       # 检测线程：三态循环、请求信箱、警报计数、FPS 叠加（QPainter）
├── VideoSource.h/cpp     # 数据源：解码线程、最新帧缓冲、交付 QImage（OpenCV 藏 pimpl）
├── YoloDetector.h/cpp    # 推理：自写前处理 → Ort::Session::Run → 自写 NMS → QPainter 画框
├── VideoView.h/cpp       # 自绘视频控件：paintEvent 等比缩放画 QImage（替代 QLabel+QPixmap）
├── Recorder.h/cpp        # 录像：QImage → BGR → cv::VideoWriter（OpenCV 藏 pimpl）
├── AlarmStore.h/cpp      # 告警记录 SQLite 入库/查询（QSqlQueryModel）
└── need/                 # 模型、类别文件、警报音
```

## 已知边界

- **类无关 NMS**：自写 NMS 沿用 `NMSBoxes` 的语义，所有类别的候选框放在同一个池子里互相抑制。
  两个不同类别的目标位置重叠时（烟雾火焰场景很常见：烟和火本就长在一起），
  置信度低的框会被高的压掉——界面上少画一个框、日志里少报一类。
  正确做法是类内 NMS：按类别分组、各组各做一遍（YOLOv5 官方的坐标平移法效果等价）。
- 前处理为拉伸缩放（非 letterbox，与 blobFromImage 行为一致），长宽比悬殊的画面两端会有形变
- 屏幕捕获未实现（接口已预留）
- 视频源为实时模式：推理慢于视频帧率时丢帧采样（画面跳跃但时间线对齐）；
  如需逐帧全覆盖分析，去掉 VideoSource 的解码线程、改为推模式即可
- 更换模型类别后，警报对所有检出目标生效（连续 10 帧检出任意目标即响）
