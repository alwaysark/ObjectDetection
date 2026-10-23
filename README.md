# 烟雾火焰检测 —— tiny 分支（教学精简版）

> 本分支即原独立项目 **FireSmokeDetectionTinyCPP** 导入本仓库的版本。
> 本仓库四个分支的定位：`main` = cv::dnn 单机完整版、`onnx` = onnxruntime
> 单机完整版、`tiny` = 本分支（教学精简版，功能最少）、`client-server` =
> 客户端 + 告警平台完整系统（曲线/数据库/后端齐全）。

检测视频文件里的 fire/smoke 并实时显示。这是完整版的极简改造版，专门用于
**三天学习**：功能砍到最小，每个文件都带"为什么这么写"的注释。

## 功能

- 只检测视频文件 / 图片（摄像头、屏幕捕获已移除）
- onnx 推理 fire/smoke，置信度/IOU 阈值、翻转/旋转、锚框颜色——开始检测时生效
- 截图
- 检测中输入页与输出页参数全部锁定，停止后恢复

## 两线程架构（全部内容就这一张图）

```
主线程(GUI)                     检测线程（点开始才诞生，停止即消亡）
──────────                     ──────────────────────────────
点"开始检测"：
  校验文件 → 打包 DetectConfig → startDetect(cfg) ──► run():
                                                      ① 打开视频
  （界面锁定，等待信号）                              ② 加载 onnx 模型
                                                      ③ 循环：
点"停止"/播完 ◄── finished 信号 ◄─────────────────      read(frame) 阻塞取帧
                                                         detect(frame) 推理画框
画面 ◄────── frameReady(QImage) ◄──────────────          emit frameReady
```

## 文件导读（建议学习顺序）

| 顺序 | 文件 | 行数 | 学什么 |
|---|---|---|---|
| 1 | `main.cpp` | 12 | Qt 程序最小骨架 |
| 2 | `VideoSource.h/cpp` | ~130 | 类设计：open/read/close 三动词；cv::VideoCapture；imread 空值判失败；flip/rotate 索引→cv 码 |
| 3 | `YoloDetector.h/cpp` | ~230 | onnxruntime C API 动态加载；YOLOv5 后处理：25200×7 解析、置信度过滤、NMS、坐标缩放、画框 |
| 4 | `Detection.h/cpp` | ~180 | QThread 子类；start/stop 生命周期；跨线程信号 emit |
| 5 | `MainWindow.h/cpp` | ~450 | 打包配置→交线程；接收槽；控件锁定 |

## 与完整版（zcode）的差异

| 完整版 | Tiny 版 |
|---|---|
| 三个线程（VideoSource 内部解码线程） | 两个线程（read 由检测线程自己调） |
| 请求信箱 + atomic 热更新 | 配置一次性打包，检测中锁定修改 |
| 三态循环（待命/预览/检测） | 两态（空闲/检测中），线程随检测生灭 |
| 日志区 QTextBrowser + QSettings | 结果标签 + 无持久化 |
| 摄像头/视频/图片/屏幕 | 仅视频/图片 |

## 已知取舍

- 视频播放速度 = 推理速度（推理 5fps，视频就 5fps 慢放）——没有解码线程丢帧机制
- 运行中改置信度/翻转等不生效，下次开始检测才应用
- 屏幕捕获未实现

## 与本仓库 main 分支的差异

`main` 分支（原 ObjectDetectionOnnxRT 的 tiny 分支）是功能完整的单机版，
本分支是它的前身精简版。差异如下（以代码为准，已逐项核对）：

### tiny 比 main 少的

| 功能 | main | tiny（本分支） |
|---|---|---|
| 摄像头输入 | CAP_DSHOW 摄像头 / CAP_MSMF 视频文件 / 图片 | 仅视频文件 / 图片 |
| 解码线程 | 三线程：VideoSource 内部解码线程，最新帧覆盖式缓冲 + 条件变量，推理慢自动丢帧 | 两线程：read() 由检测线程同步阻塞调用，播放速度 = 推理速度 |
| 源切换与预览 | 检测线程常驻，选完源即解码预览，点"开始检测"才叠加推理；换源/换模型走 mutex 请求信箱 | 线程随检测生灭，点开始才打开视频，无预览、无信箱 |
| 运行中热参数 | 置信度 / IOU / 翻转 / 旋转 / 锚框颜色 atomic 热更新，即时生效 | 配置开始时一次性打包，运行中改不生效 |
| 报警音 | QSoundEffect 播 need/JingBao/alarm.wav（连续 10 帧触发、5 秒冷却、运行中可开关） | 无 |
| 视频录制 | cv::VideoWriter 录 mp4（首帧懒打开） | 无 |
| 日志 | QTextBrowser 日志区（限流）+ 时间戳 + .log 落盘保存 | 仅一行结果标签，错误弹 QMessageBox |
| 配置持久化 | QSettings 十几项，重启自动恢复 | 无，重启即失 |
| 模型资产 | need/ 下 N0.6 / M0.9 / L0.9 三个模型 + 警报音 | 仅 m0.9 一个模型 |

### tiny 相对 main 改的

| 项 | main | tiny（本分支） |
|---|---|---|
| onnxruntime 接法 | 链接 onnxruntime.lib，用 C++ 封装类 Ort::Session（wchar_t 路径直达中文） | 不链接 lib：运行时 `LoadLibraryA("onnxruntime.dll")` 拿 C API 函数表（OrtGetApiBase → GetApi），会话从内存字节创建，绕开宽字符路径问题 |
| 输入尺寸 | 从模型运行时读取（模型写多少用多少） | 代码写死 640×640 |
| 输出层名 | 从模型读第一个输出名 | 从模型读，兜底默认 "images" / "output0" |
| CMake | 链接 ORT 库 + 拷 ORT DLL | 只借 ORT 头文件，拷 DLL 但不链接 |

其余两边一致：YOLOv5 后处理与 NMS（含类无关 NMS 的已知问题）、类别文件
兼容解析（换行/中英文逗号/竖线/剥序号前缀）、截图、置信度下限 0.00、
关窗确认 + 线程安全退出、仅 MSVC 工具链。
