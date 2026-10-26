# FireSmokeDetection（cs 分支）

**单机实时烟雾火焰检测 + 报警统计平台**：客户端 = main 分支全部功能 + 报警外发模块；
服务端是配套的 Qt 报警统计平台（数据库落库 + 实时曲线 + 分类统计）。
推理引擎与 main 相同：OpenCV 内置 `cv::dnn`，**不依赖 onnxruntime**。

> 本仓库分支导航（main / onnx / client-server 共用三线程管线，tiny 是两线程教学版）：
>
> | 分支 | 推理引擎 | 定位 |
> |---|---|---|
> | [main](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/main) | OpenCV `cv::dnn`（内置） | 单机完整版，依赖最少 |
> | [onnx](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/onnx) | onnxruntime C++ API | 单机完整版，推理更快，输入尺寸/层名从模型自动读取 |
> | [tiny](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/tiny) | OpenCV `cv::dnn`（内置） | 两线程教学精简版，功能最少 |
> | [client-server](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/client-server) | `cv::dnn` | Qt 上位机 + 告警平台服务端（含本地告警入库） |
> | **cs**（本分支） | `cv::dnn` | 单机检测 + 报警外发 + 报警统计平台（服务端带数据库与曲线） |

## 运行环境

- **仅 Windows + MSVC 工具链**：Qt / OpenCV 官方只发布 MSVC 预编译包，MinGW 与 MSVC 的
  C++ ABI 不兼容、链不上这些库；`CMakeLists.txt` 里 `if (NOT MSVC)` 会直接报错退出
- 版本组合：Visual Studio 2022 及以上（MSVC 工具集）+ Qt 6.8.3 msvc2022_64 +
  OpenCV 4.13 MSVC 预编译版。**不需要任何推理库**——cv::dnn 就在 OpenCV 里
- 客户端比 main 多用 Qt **Network**（报警外发）；服务端纯 Qt（Widgets / Network / Sql /
  PrintSupport），**无 OpenCV、无推理库依赖**
- 运行时零配置：构建时把全部依赖 DLL（Qt / OpenCV / MSVC 运行库，含 Debug 调试版）、
  Qt 插件、need/ 资源自动拷到各 exe 旁，构建产物目录整体拷到任何 64 位 Windows 双击即跑

## 功能

### 客户端（FireSmokeDetection.exe，= main 全量功能 + 报警外发）

- 检测视频文件 / 图片 / 摄像头，实时显示带锚框与置信度的画面（FPS 叠加可开关）
- **模型（.onnx）与类别文件（.txt）检测中即可更换**：选完新模型自动热切换
- 置信度 / IOU 阈值、翻转 / 旋转、锚框颜色运行中即时生效（atomic 热参数）
- 警报：连续 10 帧检出任意目标 → 警报音（5 秒冷却，运行中可开关）
- **报警外发**：触发警报后把明细（时间 / 类别 / 最高置信度 / 目标数）以 JSON
  打包发往统计平台（默认 `127.0.0.1:9000`）；上报 2 秒冷却（持续目标不轰炸平台）、
  30s 心跳保活、断线 5s 自动重连、
  未在线时静默丢弃——平台在不在都不影响本地检测
- 录制检测视频（mp4）：开关检测中即时生效；帧率与保存路径只影响下一个录像文件
- 界面配置持久化（QSettings）、关窗确认 + 线程安全退出

### 服务端（AlarmServer.exe，报警统计平台）

- **数据库模块**：收到的每条报警写入 SQLite（exe 旁 `server_alarms.db`），
  表字段：时间 / 类别 / 最高置信度 / 目标数 / 上报端
- **曲线模块**：QCustomPlot 双曲线实时滚动（60 分钟窗口）——
  每分钟报警数（左轴柱状）+ 每次报警的最高置信度%（右轴散点）；启动时从库里
  播种最近一小时历史，重启不白板
- **统计**：历史总报警数、今日报警数、按类别计数（fire ×n / smoke ×m）、在线客户端数
- 连接/报警日志（限 300 行）

## 报警链路与协议

客户端检测线程连续 10 帧检出目标 → `alarmTriggered(明细)` 信号 → 界面打包 AlarmRecord →
AlarmReporter 组帧 TCP 发出 → 服务端 Agent 拆帧校验 → 回 ACK + 落库 + 更新曲线统计。

自定义二进制协议，帧结构（小端）：

```
┌────────┬────────┬────────┬─────────┐
│ 帧头    │ 帧长    │ 命令字  │ 数据域   │
│ 2B     │ 2B     │ 1B     │ NB      │
│ AA 55  │ N+1    │ 1B     │ JSON    │
└────────┴────────┴────────┴─────────┘
帧长 = 命令字 + 数据域 的总字节数。
不做应用层 CRC：传输可靠性由 TCP 自身的校验和与重传保证，不重复造
```

| 命令字 | 方向 | 含义 |
|---|---|---|
| 0x01 | 客户端→平台 | 报警上报（数据域 = JSON：time/label/score/count） |
| 0x02 | 双向 | 心跳（数据域空） |
| 0x81 | 平台→客户端 | 应答（数据域 = "OK"） |

可靠性：拆帧循环处理粘包/断包（帧头错位逐字节滑过、非法帧长丢弃、64KB 上限防炸）；
心跳 30s、连续 2 个周期无应答判死；断线 5s 自动重连（主动断开不重连）。

## 模型要求：仅限 YOLOv5 6.0 导出的 onnx

与 main 分支完全相同：输入尺寸写死 640×640（拉伸缩放），终输出层名校验为 `output`，
输出按 `[1, 25200, 5+类别数]` 解析，类别数自适应。

**`--simplify`（onnx-simplifier）是必须的**——卡点在 `Floor` 算子：yolov5 v6.0 导出的
onnx 带有 `Floor`（取整）算子，而 OpenCV 的 dnn 模块只实现了 ONNX 算子集的一个子集，
**不认识 `Floor`**——未化简的模型在 `readNetFromONNX` 解析阶段就抛异常拒绝加载。
onnxsim 做常量折叠把 `Floor` 消掉，化简后即可正常解析。

```bash
git clone -b v6.0 --depth 1 https://github.com/ultralytics/yolov5.git
cd yolov5
python export.py --weights best.pt --include onnx --imgsz 640 --simplify --opset 12
```

`need/` 下附带三个 YOLOv5 6.0 训练的 fire/smoke 两类模型（N0.6 / m0.9 / L0.9，n/s/l 三种规模，均已化简），
构建时自动同步到 exe 旁边。

### 类别文件格式（硬约定）

**一行一个类别名，行序 = 类别索引**（第 1 行 = 类别 0，必须与训练时的类别顺序一致）：

```text
fire
smoke
```

不做宽容解析：不剥行首序号（`1 fire` 会被当成名字 "1 fire"），也不支持逗号/竖线分隔。
文件与模型不配套时（名字个数 ≠ 模型输出类别数），开始检测后立即红字报错
"类别文件与模型输出不匹配"并回到预览态。

## 架构

客户端与 main 相同：三线程 + 请求信箱（主线程 GUI / 检测线程三态循环 / VideoSource 内部解码线程，
"谁用谁创建"、标量参数走 atomic、复合数据走锁、换模型换源走信箱——详见 main 分支 README）。
本分支新增的报警外发全部在 **UI 线程**：检测线程只多发一个 `alarmTriggered` 信号，
QTcpSocket 异步非阻塞，网络慢不拖检测。

服务端单线程 Qt 事件驱动：每个客户端连接一个 Agent 对象（拆帧/回 ACK），
报警 JSON 经信号进窗口 → 入库 → 曲线与统计刷新。

## 技术栈

| 组件 | 用途 |
|---|---|
| Qt 6.8.3 (msvc2022_64) | 界面、信号槽、QThread、警报音；客户端 Network 报警外发；服务端 Widgets/Network/Sql |
| OpenCV 4.13 (opencv_world) | （仅客户端）解码、前处理 blob、cv::dnn 推理（CPU）、NMS、画框 |
| QCustomPlot 2.x | （仅服务端）报警曲线 |
| SQLite (Qt Sql) | （仅服务端）报警落库 |
| C++17 / CMake / Ninja | 构建 |

## 构建

环境要求见上文"运行环境"。本机依赖路径（`Qt6_DIR`、`OpenCV_DIR` 等）统一在**顶层
`CMakeLists.txt`** 里改一处即可（Client / Server 共用）。

顶层 CMake 一次编译两个组件：

```bat
call "<VS路径>\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物：`FireSmokeDetection.exe`（客户端，build 根目录）与 `server/AlarmServer.exe`（统计平台）。
依赖 DLL、Qt 插件（含 SQLite 驱动）、need/ 资源自动拷到各自 exe 旁。

## 联调步骤

1. 先启动 `AlarmServer.exe`，窗口日志出现"监听 tcp/9000"
2. 启动客户端，日志出现"已连接报警统计平台 (127.0.0.1:9000)"；服务端日志出现"客户端接入"
3. 客户端开始检测，触发警报后：服务端日志打印报警明细，曲线新增柱与散点，
   统计标签（总数/今日/分类）刷新，`server_alarms.db` 落一条记录
4. 关掉服务端，客户端日志出现"报警统计平台连接断开，5 秒后自动重连"；
   重启服务端后自动恢复，期间产生的报警被丢弃（不补发）

## 项目结构

```
├── CMakeLists.txt         # 顶层工程：依赖路径、编译选项，一次编译 Client + Server
├── Client/                # 监控客户端（FireSmokeDetection.exe）
│   ├── CMakeLists.txt
│   ├── main.cpp           # 客户端入口
│   ├── MainWindow.h/cpp   # 界面层：控件、槽、配置持久化、报警外发接线
│   ├── Detection.h/cpp    # 检测线程：三态循环、请求信箱、警报计数、录像、alarmTriggered 信号
│   ├── VideoSource.h/cpp  # 数据源：解码线程、最新帧缓冲、翻转/旋转
│   ├── YoloDetector.h/cpp # 推理：blobFromImage → forward("output") → NMS → 画框
│   ├── AlarmReporter.h/cpp# 报警外发（TCP 自定义协议/心跳/重连，UI 线程）
│   └── need/              # 模型、类别文件、警报音
└── Server/                # 报警统计平台（AlarmServer.exe，纯 Qt）
    ├── CMakeLists.txt
    ├── main.cpp           # 入口
    ├── ServerWindow.h/cpp # 主窗口：TCP 接入（Agent 拆帧）、曲线、统计、日志
    ├── AlarmDb.h/cpp      # 数据库模块：报警落库 + 统计查询
    └── qcustomplot.h/cpp  # 曲线库（第三方，Qt PrintSupport）
```

## 已知边界

- 报警外发未在线时静默丢弃，不补发（不丢本地检测事实）
- 平台地址写死在 `MainWindow.cpp` 顶部（`kServerHost`/`kServerPort`），要连远程平台改这里
- 服务端曲线窗口 60 分钟，窗口外的历史只在数据库里（曲线不回看更早）
- 类无关 NMS、640×640 拉伸缩放、屏幕捕获未实现等检测侧边界与 main 相同
