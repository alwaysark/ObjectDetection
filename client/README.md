# FireSmokeDetection —— client（监控客户端）

**智能监控上位机**：main 分支的全部检测功能 + 告警入库 / 平台上报 / 实时曲线三项扩展。
推理引擎为 OpenCV 内置 `cv::dnn`（无需 onnxruntime），三线程管线。

> 本文件只讲 client 自身；分支对照、协议、server 端与联调步骤见仓库根目录 README.md。

## 检测功能（与 main 分支一致）

- 检测视频文件 / 图片 / 摄像头，实时显示带锚框与置信度的画面（FPS 叠加可开关）
- 模型（.onnx）与类别文件（.txt）可随时更换（限 YOLOv5 6.0 格式，见下文）
- 置信度 / IOU 阈值、翻转 / 旋转、锚框颜色运行中即时生效（atomic 热参数）
- 警报音：连续 10 帧检出任意目标 → wav 警报（5 秒冷却，运行中可开关）
- 录制检测视频（mp4）、截图、日志导出；界面配置持久化（QSettings）
- 关窗确认 + 线程安全退出

模型要求（与 main 相同）：输入尺寸写死 640×640，终输出层名校验为 `output`，
输出按 `[1, 25200, 5+类别数]` 解析；**模型必须经 onnxsim 化简**（`--simplify`），
否则 `Floor` 算子会被 cv::dnn 拒绝加载。`need/` 下附带 N0.6 / m0.9 / L0.9 三个
fire/smoke 模型（n/s/l 规模），构建时自动同步到 exe 旁边。

## 上位机扩展（相对 main 新增的三块）

### 1. 告警记录入库 —— AlarmStore（SQLite）

告警触发（连续 10 帧检出）时自动快照截图，并把记录写进 exe 旁的 `alarms.db`：

| 字段 | 含义 |
|---|---|
| time | 告警时间（ISO 字符串，可排序） |
| label | 类别名（多类逗号拼接，如 "fire, smoke"） |
| score | 最高置信度百分数（0~100） |
| count | 该次告警检出的目标总数 |
| snapshot | 快照 png 路径（可空） |

入库是单条事务，失败静默——记录功能不影响检测主流程。
输出页"告警记录"按钮打开对话框，按日期查询历史（QTableView）。

### 2. 告警 TCP 上报 —— AlarmReporter（自定义协议）

告警同时以 JSON 推给告警平台（默认 `127.0.0.1:9000`，即本仓库 server/ 子项目）。
协议帧：`帧头 AA 55 | 帧长 2B | 命令字 1B | 数据域(JSON) | CRC16-Modbus 2B`，
命令字 0x01 告警 / 0x02 心跳 / 0x81 应答。

可靠性机制：拆帧循环处理粘包断包；30s 心跳保活，连续 2 个周期无应答判死；
断线 5s 自动重连（主动断开不重连）。未在线时上报静默丢弃，本地入库不受影响。

### 3. 实时曲线 —— QCustomPlot（统计页）

底部"日志 / 统计"选项卡的统计页实时绘制：目标数（左轴）与最高置信度%（右轴），
横轴为检测运行秒数，60 秒滚动窗口，超窗历史点自动丢弃防卡顿。

## 架构：三线程 + 请求信箱（与 main 相同）

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
                                 ④ frameReady/resultReady/alarmTriggered ──► 主线程槽
```

告警链路在 main 的"警报音 + 日志"之后多一步：检测线程发 `alarmTriggered(明细)`，
界面槽完成 快照 → alarms.db 入库 → 平台上报 三件事。

## 构建

环境要求：**Windows + Visual Studio 2022（MSVC）+ Qt 6.8.3 msvc2022_64 + OpenCV 4.13 MSVC 预编译版**。
Qt 组件比 main 多三个：**Sql**（入库）、**Network**（上报）、**PrintSupport**（QCustomPlot 依赖）。

1. 修改 `client/CMakeLists.txt` 中两处本地路径：`Qt6_DIR`、`OpenCV_DIR`
2. 单独编译本子目录，或用仓库根目录的顶层 CMakeLists 一次编译 client + server：

```bat
call "<VS路径>\VC\Auxiliary\Build\vcvars64.bat"
cmake -S client -B build/client -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/client
```

构建完成后依赖 DLL、`need/` 资源、SQLite 驱动插件（qsqlite）会自动拷到 exe 旁边。

## 项目结构

```
├── main.cpp              # 入口
├── MainWindow.h/cpp      # 界面层：控件、槽、配置持久化、告警链路、统计曲线
├── Detection.h/cpp       # 检测线程：三态循环、请求信箱、警报计数、录像
├── VideoSource.h/cpp     # 数据源：解码线程、最新帧缓冲、翻转/旋转
├── YoloDetector.h/cpp    # 推理：blobFromImage → forward("output") → NMS → 画框
├── AlarmStore.h/cpp      # 告警记录 SQLite 入库/查询
├── AlarmReporter.h/cpp   # 告警 TCP 外发（自定义协议/心跳/重连）
├── qcustomplot.h/cpp     # 实时曲线（第三方，Qt PrintSupport）
└── need/                 # 模型、类别文件、警报音
```

## 已知边界

- 类无关 NMS：不同类别目标重叠时低分框被高分框压掉（与 main 相同，详见根 README）
- 平台离线时告警上报静默丢弃，不补发
- 屏幕捕获未实现（接口已预留）；视频源为实时丢帧模式
