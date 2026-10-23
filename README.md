# FireSmokeDetection

智能监控上位机（Qt6 + OpenCV + YOLOv5）+ 告警平台服务端，两个组件：

```
FireSmokeDetection/
├── client/    监控客户端（原 ObjectDetectionOnnxRT tiny 分支 + 上位机扩展）
└── server/    告警平台模拟器（Qt 网络服务端，接收客户端告警上报）
```

## client：监控客户端

- 基于 Qt6 + OpenCV dnn 的实时目标检测（YOLOv5 6.0 专用推理，详见 client/README.md）
- **告警记录入库（SQLite）**：连续 10 帧检出目标触发告警时，自动截图 + 写入
  `alarms.db`（时间/类别/置信度/目标数/快照路径）；输出页"告警记录"按钮可查询历史
- **告警 TCP 上报（自定义协议）**：告警同时以 JSON 推给平台（默认 127.0.0.1:9000）。
  协议：`帧头 AA 55 | 帧长(2B) | 命令字(1B) | 数据域(JSON) | CRC16-Modbus(2B)`，
  心跳保活（30s）、心跳超时判死（连续 2 次）、断线 5s 自动重连
- **实时曲线（QCustomPlot）**："统计"页实时绘制目标数（左轴）与最高置信度（右轴），
  60 秒滚动窗口，历史点超 5 分钟自动丢弃防卡顿

## server：告警平台模拟器

Qt 写的 TCP 服务端（`AlarmPlatformServer.exe`），与 client 的 AlarmReporter 对接联调：

- 收告警帧(0x01)：打印 JSON → 存平台侧 `platform_alarms.db` → 回 ACK(0x81 "OK")
- 收心跳帧(0x02)：回 ACK
- 拆帧逻辑与客户端对称（帧头同步 / 断包等待 / CRC 校验）

原 ReactorHttpsServer_CPP（Linux epoll 版 HTTP 服务器）保留在 `server/react/`
作为学习参考，不参与编译。

## 构建（两套都要 MSVC 环境）

```bat
call "<VS路径>\VC\Auxiliary\Build\vcvars64.bat"

:: client
cmake -S client -B build/client -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/client

:: server
cmake -S server -B build/server -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/server
```

## 联调步骤

1. 启动 `build/server/AlarmPlatformServer.exe 9000`，看到 `listening on tcp/9000`
2. 启动 client，日志出现"告警平台已连接 (127.0.0.1:9000)"
3. 开始检测，触发告警后：平台控制台打印 ALARM JSON，`platform_alarms.db` 落一条记录，
   client 侧 `alarms.db` 同步落库
4. 关掉平台进程，client 日志 1 分钟内出现"连接断开，5 秒后自动重连"；重启平台后自动恢复
