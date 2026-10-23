# FireSmokeDetection（client-server 分支）

**智能监控上位机 + 告警平台服务端**：client 是在 main 分支全部功能之上扩展的上位机
（Qt6 + OpenCV + `cv::dnn` 推理），server 是与之联调的 Qt TCP 告警平台模拟器。

> 本仓库四个分支中，main / onnx / client-server 共用同一套界面与三线程管线，
> tiny 是**两线程**教学精简版；差别在推理引擎与功能范围：
>
> | 分支 | 推理引擎 | 定位 |
> |---|---|---|
> | [main](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/main) | OpenCV `cv::dnn`（内置） | 单机完整版，依赖最少 |
> | [onnx](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/onnx) | onnxruntime C++ API | 单机完整版，推理更快，模型限制更少 |
> | [tiny](https://github.com/alwaysark/ObjectDetectionOnnxRT/tree/tiny) | OpenCV `cv::dnn`（内置） | 两线程教学精简版，功能最少 |
> | **client-server**（本分支） | `cv::dnn` | Qt 上位机 + 告警平台服务端 |

```
FireSmokeDetection/
├── client/    监控客户端（= main 全量功能 + 告警入库/上报/实时曲线，详见 client/README.md）
├── server/    告警平台模拟器（AlarmPlatformServer.exe，Qt TCP 服务端）
└── CMakeLists.txt   顶层工程：一次配置同时编译 client 与 server
```

## 与 main 分支的区别

client 的检测部分（推理引擎 cv::dnn、640×640 写死、`output` 层校验、YOLOv5 6.0 专用后处理、
三线程管线、警报音/录像/截图/日志/QSettings）与 main 分支**完全一致**，模型要求也相同
（详见 main 的 README 或 client/README.md）。区别全部在上位机扩展与工程形态：

| 项 | main | client-server（本分支） |
|---|---|---|
| 工程形态 | 单个可执行程序 | client + server 两个子工程（顶层 CMake 统一编译） |
| 告警处理 | 警报音 + 日志 | 警报音 + 日志 + **快照截图 + SQLite 入库（alarms.db）+ TCP 上报平台** |
| 实时统计 | 无 | 统计页 QCustomPlot 曲线：目标数（左轴）+ 最高置信度%（右轴），60 秒滚动窗口 |
| 告警历史查询 | 无 | "告警记录"按钮打开对话框，按日期查询（QTableView） |
| Qt 组件 | Core / Gui / Widgets / Multimedia | 额外 + **Sql**（SQLite 入库）/ **Network**（TCP 上报）/ **PrintSupport**（QCustomPlot 依赖） |
| 新增源码 | — | AlarmStore、AlarmReporter、qcustomplot；server/ 整个子工程 |

## 告警链路（client 内部）

检测线程连续 10 帧检出目标后多发一个 `alarmTriggered(明细)` 信号，界面槽依次完成：

1. **快照**：当前画面存 png，路径记入告警记录；
2. **入库**（AlarmStore）：写 exe 旁的 `alarms.db`（单条事务，失败静默——记录功能不影响检测主流程），
   表结构 `alarms(id, time, label, score, count, snapshot)`；
3. **上报**（AlarmReporter）：组装 JSON 推给平台，未在线时静默丢弃（外发失败不影响本地入库）。

## 协议（client ↔ server）

自定义二进制协议，帧结构（小端）：

```
┌────────┬────────┬────────┬─────────┬──────────┐
│ 帧头    │ 帧长    │ 命令字  │ 数据域   │ CRC16    │
│ 2B     │ 2B     │ 1B     │ NB      │ 2B       │
│ AA 55  │ N+3    │ 1B     │ JSON    │ modbus   │
└────────┴────────┴────────┴─────────┴──────────┘
帧长 = 命令字 + 数据域 + CRC 的总字节数；CRC16-Modbus 覆盖 命令字+数据域
```

| 命令字 | 方向 | 含义 |
|---|---|---|
| 0x01 | 客户端→平台 | 告警上报（数据域 = 告警记录 JSON：time/label/score/count/snapshot） |
| 0x02 | 双向 | 心跳（数据域空） |
| 0x81 | 平台→客户端 | 应答（数据域 = "OK"） |

可靠性机制（AlarmReporter 内实现，两端拆帧逻辑对称）：

- **粘包/断包**：接收只往缓冲追加字节，按"帧头→帧长→够不够"循环切帧；帧头错位逐字节滑过，CRC 不对丢帧
- **心跳保活**：每 30s 发一帧心跳，连续 2 个周期没收到应答判定断线
- **断线重连**：Offline→Connecting→Online 状态机，断线后 5s 自动重连（主动断开不重连）

## server：告警平台模拟器

Qt 写的 TCP 服务端（控制台程序，依赖仅 Qt Core/Network/Sql）：

- 收告警帧(0x01)：控制台打印 JSON → 存平台侧 `platform_alarms.db`（表结构与 client 一致）→ 回 ACK(0x81 "OK")
- 收心跳帧(0x02)：回 ACK
- 监听端口由命令行参数指定，默认 9000

`server/react/` 保留了一份原 Reactor 模式的 Linux epoll HTTP 服务器源码作为学习参考，
**不参与编译**（Linux 专用代码，Windows 编不过；与告警协议也无关）。

## 运行环境

- **仅 Windows + MSVC 工具链**：Qt / OpenCV 官方只发布 MSVC 预编译包，MinGW 与 MSVC 的
  C++ ABI 不兼容、链不上这些库；`CMakeLists.txt` 里 `if (NOT MSVC)` 会直接报错退出
- 版本组合：Visual Studio 2022 及以上（MSVC 工具集）+ Qt 6.8.3 msvc2022_64 +
  OpenCV 4.13 MSVC 预编译版。**不需要任何推理库**——client 的推理用 OpenCV 内置 cv::dnn，
  server 只依赖 Qt（Core / Network / Sql）
- 运行时零配置：构建时把全部依赖 DLL（Qt / OpenCV / MSVC 运行库，含 Debug 调试版）、
  Qt 插件（含 SQLite 驱动）、need/ 资源自动拷到各自 exe 旁，两个构建产物目录
  整体拷到任何 64 位 Windows 双击即跑

## 构建

环境要求见上文"运行环境"。先在两个 CMakeLists 中把 `Qt6_DIR`、`OpenCV_DIR` 改成本机路径。

```bat
call "<VS路径>\VC\Auxiliary\Build\vcvars64.bat"

:: 方式一：顶层工程一次编译两个组件
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

:: 方式二：分开编译
cmake -S client -B build/client -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/client
cmake -S server -B build/server -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/server
```

构建完成后依赖 DLL、`need/` 资源、SQLite 驱动插件（qsqlite）会自动拷到 exe 旁边。

## 联调步骤

1. 启动 `server` 端 `AlarmPlatformServer.exe 9000`，控制台出现 `listening on tcp/9000`
2. 启动 client，日志出现"告警平台已连接 (127.0.0.1:9000)"
3. 开始检测，触发告警后：平台控制台打印 ALARM JSON，`platform_alarms.db` 落一条记录，
   client 侧 `alarms.db` 同步落库，快照存 png
4. 关掉平台进程，client 日志出现"连接断开，5 秒后自动重连"；重启平台后自动恢复上报

## 已知边界

- **类无关 NMS**：`cv::dnn::NMSBoxes` 把所有类别的候选框放同一个池子里互相抑制，
  不同类别目标重叠时（烟与火常重叠）低分框会被高分框压掉（与 main 相同）
- 输入尺寸固定 640×640 拉伸缩放；仅支持 YOLOv5 6.0 导出、经 onnxsim 化简的 onnx（与 main 相同）
- 平台离线时告警上报静默丢弃，不补发（本地入库不受影响）
- 屏幕捕获未实现（接口已预留）；视频源为实时丢帧模式（与 main 相同）
