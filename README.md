# 烟雾火焰检测 —— tiny 分支（教学精简版）

检测视频文件里的 fire/smoke 并实时显示。这是完整版的极简改造版，专门用于**三天学习**：
全项目约 1100 行，功能砍到最小，每个文件都带"为什么这么写"的注释。
推理引擎与 main 分支相同：OpenCV 内置的 `cv::dnn`，**无需安装 onnxruntime**。

> 本仓库四个分支中，main / onnx / client-server 共用同一套界面与三线程管线，
> 本分支是**两线程**教学精简版；差别在功能范围：
>
> | 分支 | 推理引擎 | 定位 |
> |---|---|---|
> | [main](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/main) | OpenCV `cv::dnn`（内置） | 单机完整版，依赖最少 |
> | [onnx](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/onnx) | onnxruntime C++ API | 单机完整版，推理更快，模型限制更少 |
> | **tiny**（本分支） | OpenCV `cv::dnn`（内置） | 两线程教学精简版，功能最少 |
> | [client-server](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/client-server) | `cv::dnn` | Qt 上位机 + 告警平台服务端 |

## 运行环境

- **仅 Windows + MSVC 工具链**：Qt / OpenCV 官方只发布 MSVC 预编译包，MinGW 与 MSVC 的
  C++ ABI 不兼容、链不上这些库；`CMakeLists.txt` 里 `if (NOT MSVC)` 会直接报错退出
- 版本组合：Visual Studio 2022 及以上（MSVC 工具集）+ Qt 6.8.3 msvc2022_64 +
  OpenCV 4.13 MSVC 预编译版。**不需要任何推理库**——cv::dnn 就在 OpenCV 里
- 运行时零配置：构建时把全部依赖 DLL（Qt / OpenCV / MSVC 运行库，含 Debug 调试版）、
  Qt 插件、need/ 资源自动拷到 exe 旁，构建产物目录整体拷到任何 64 位 Windows 双击即跑

## 功能

- 检测视频文件 / 图片中的 fire/smoke，实时显示带锚框与置信度的画面（FPS 叠加可开关）
- 置信度 / IOU 阈值、翻转 / 旋转、锚框颜色、显示锚框——开始检测时一次性打包给线程（运行中修改不生效）
- 检测中输入页与输出页全部锁定，只有"停止"和"截图"可点，停止后恢复
- 截图（检测中也可用）
- 选模型时自动配对同名类别 txt，兼容 `序号 类别名` 格式与中英文逗号/竖线/换行分隔
- 关窗确认 + 线程安全退出；出错用 QMessageBox 弹窗提示

**没有的**（相对完整版裁掉的）：摄像头输入、警报音、视频录制、日志区与日志落盘、配置持久化。

## 两线程架构（全部内容就这一张图）

```
主线程(GUI)                     检测线程（点开始才诞生，停止即消亡）
──────────                     ──────────────────────────────
点"开始检测"：
  校验文件 → 打包 DetectConfig → startDetect(cfg) ──► run():
                                                      ① 打开视频
  （界面锁定，等待信号）                              ② 加载 onnx 模型（cv::dnn）
                                                      ③ 循环：
点"停止"/播完 ◄── finished 信号 ◄─────────────────      read(frame) 阻塞取帧
                                                         detect(frame) 推理画框
画面 ◄────── frameReady(QImage) ◄──────────────          emit frameReady
```

## 与 main 分支的区别

main 是功能完整的单机版（三线程），本分支是它的两线程教学精简版。
推理引擎、模型要求、YOLOv5 后处理两边完全相同（cv::dnn、640×640、`output` 层校验、
模型需 onnxsim 化简）。以代码为准逐项核对，**差别全在功能与线程模型**：

| 项 | main | tiny（本分支） |
|---|---|---|
| 线程模型 | 三线程：检测线程常驻 + VideoSource 内部解码线程，最新帧覆盖式缓冲，推理慢自动丢帧 | 两线程：`read()` 由检测线程同步阻塞调用，**播放速度 = 推理速度** |
| 生命周期 | 三态循环（无源待命 / 预览 / 检测），换源换模型走 mutex 请求信箱 | 两态（空闲 / 检测中），线程随检测生灭，无信箱、无预览 |
| 输入源 | 摄像头 / 视频文件 / 图片 | 仅视频文件 / 图片 |
| 运行中热参数 | 置信度 / IOU / 翻转 / 旋转 / 锚框颜色 atomic 热更新，即时生效 | 配置开始时一次性打包，运行中改不生效 |
| 警报音 | 有（连续 10 帧触发、5 秒冷却、运行中可开关） | 无 |
| 视频录制 | 有（cv::VideoWriter 录 mp4） | 无 |
| 日志 | QTextBrowser 日志区（限流）+ 时间戳 + .log 落盘 | 仅一行结果摘要标签，错误弹 QMessageBox |
| 配置持久化 | QSettings，重启自动恢复 | 无，重启即失 |
| 模型资产 | need/ 下 N0.6 / m0.9 / L0.9 三个模型 + 警报音 | 仅 m0.9 一个模型 |

## 模型要求：仅限 YOLOv5 6.0 导出的 onnx（与 main 相同）

输入尺寸写死 640×640（拉伸缩放），终输出层名校验为 `output`，
输出按 `[1, 25200, 5+类别数]` 解析（25200 = (80×80+40×40+20×20)×3 anchor，类别数自适应）。
**模型必须经 onnxsim 化简导出**（`--simplify`）：未化简模型里的 `Floor` 算子
会被 cv::dnn 拒绝加载。`need/` 附带的 m0.9 已是化简版，构建时自动同步到 exe 旁边。

自己导出模型：

```bash
git clone -b v6.0 --depth 1 https://github.com/ultralytics/yolov5.git
cd yolov5
python export.py --weights best.pt --include onnx --imgsz 640 --simplify --opset 12
```

## 文件导读（建议学习顺序）

| 顺序 | 文件 | 行数 | 学什么 |
|---|---|---|---|
| 1 | `main.cpp` | 12 | Qt 程序最小骨架 |
| 2 | `VideoSource.h/cpp` | ~160 | 类设计：open/read/close 三动词；cv::VideoCapture；imread 空值判失败；flip/rotate 索引→cv 码 |
| 3 | `YoloDetector.h/cpp` | ~225 | cv::dnn 推理（readNetFromONNX / forward）；YOLOv5 后处理：25200×7 解析、置信度过滤、NMS、坐标缩放、画框 |
| 4 | `Detection.h/cpp` | ~150 | QThread 子类；start/stop 生命周期；跨线程信号 emit |
| 5 | `MainWindow.h/cpp` | ~460 | 打包配置→交线程；接收槽；控件锁定 |

## 构建

环境见上文"运行环境"。只需修改 `CMakeLists.txt` 中两处本地路径：

- `Qt6_DIR`（Qt msvc 版）
- `OpenCV_DIR`（OpenCV MSVC 版 build 目录）

CLion 打开项目（选用 Visual Studio 工具链），或命令行：

```bat
call "<VS路径>\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B cmake-build-debug-msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug-msvc
```

## 使用

1. 选视频/图片，选 onnx 模型（自动配对同名类别 txt，如 `m0.9.onnx` → `m0.9.txt`）
2. 输出页调整阈值、锚框颜色（开始检测时生效）
3. 点"开始检测"；检测中参数锁定，点"停止"或视频播完结束，随时可截图

## 已知取舍

- 视频播放速度 = 推理速度（推理 5fps，视频就 5fps 慢放）——没有解码线程丢帧机制
- 运行中改置信度/翻转等不生效，下次开始检测才应用
- 屏幕捕获、摄像头输入已移除（教学版只保留文件源，聚焦推理主线）
