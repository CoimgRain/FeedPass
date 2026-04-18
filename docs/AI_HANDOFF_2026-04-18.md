# FeedPass 项目交接说明（2026-04-18 更新版）

## 0. 本次更新一句话

**OTA 已经打通**：HTTP OTA 方案真机验证通过，不再卡在 `ArduinoOTA` 的 `Receive Failed`。以后升级一条 `curl` 搞定，不用再插 USB。

---

## 1. 项目概述

`FeedPass`（用户口头也会说 `FitPass`）是一个基于 `ESP32` 的自动喂食器项目。

目标：
- 用 `ESP32` 驱动喂食机构
- 手机直接打开网页控制（不做原生 App）
- 手动控制 + 定时投喂
- 外网远程控制
- OTA 升级（**本次已完成**）

分工：
- 用户：硬件结构、3D 打印、机构设计
- AI：软件、网页、联网、远程控制、升级链路

---

## 2. 关键地址与路径

### 2.1 本地项目目录
- 主目录：`/Users/kang/Desktop/AI Studio/FeedPass`
- 之前的临时目录 `/tmp/FeedPass-OTA` **已不存在**（`/tmp` 重启清空）。现在所有 OTA 代码都已回到主目录，不要再去找 `/tmp`。

### 2.2 知识库
- 主笔记：`/Users/kang/Library/Mobile Documents/iCloud~md~obsidian/Documents/KK个人知识库/个人项目/FeedPass.md`

### 2.3 线上 / 局域网地址
- 公网控制页：`https://coimgrain.github.io/FeedPass/`
- 局域网控制页：`http://192.168.1.181`
- AP 热点：`http://192.168.4.1`（SSID `FitPass-Setup`）

### 2.4 仓库
- `CoimgRain/FeedPass`，公网页来自仓库 `docs/` 目录

---

## 3. 当前功能状态

已跑通并真机验证：
- ESP32 固件烧录
- 本地网页控制
- 外网网页控制（GitHub Pages + MQTT 中转）
- MQTT 云桥远程控制
- 4 位 PIN 校验
- 电机正转 / 反转 / 停止
- 手动投喂
- 定时投喂
- 调试速度控制
- **HTTP OTA 无线升级** ← 本次新增

正常运行不依赖电脑常驻，电脑只在改代码 / 编译时需要。

---

## 4. 当前关键配置

### 4.1 网络与鉴权
- Wi-Fi：`KANG_SH`
- 局域网 IP：`192.168.1.181`
- 控制 PIN：`0728`

### 4.2 远程控制
- `REMOTE_CONTROL_ENABLED = 1`
- `REMOTE_DEVICE_ID = fitpass-52fe04f8aa9d7bf440c7fad3`
- `MQTT_HOST = broker.emqx.io`，`PORT = 8883`，`TLS = 1`（insecure=1）
- `MQTT_TOPIC_ROOT = fitpass-268d55c3348537845960bb9b`

这些都在 `include/app_config_local.h`。

### 4.3 OTA 配置（这次加上的）
- `include/app_config.h` 新增默认：
  - `OTA_ENABLED = 1`
  - `OTA_HOSTNAME = "fitpass"`
  - `OTA_PASSWORD = DEVICE_ACCESS_PIN`（= `0728`）

---

## 5. 电机与接线

历史约定（用户若未换板子继续有效）：
- `28BYJ-48` 步进电机 + `ULN2003` 驱动板
- `GPIO16→IN1`，`GPIO17→IN2`，`GPIO18→IN3`，`GPIO19→IN4`
- `ESP32 5V/VIN → VCC`，`GND → GND`

真要改控制逻辑前建议先再问用户一下当前接线。

---

## 6. 本次 OTA 工作详情（最重要）

### 6.1 背景
之前的 `ArduinoOTA`（espota）在用户网络下会在 35–65% 出现 `Receive Failed`，多次尝试优化未能根治。判断是 UDP 丢包问题，遂转向 **HTTP OTA**（走 TCP，稳定可控）。

### 6.2 实现
同一次改动里在 `src/main.cpp` 完成：

- 新增 `#include <Update.h>`
- 新增路由：`server.on("/api/ota", HTTP_POST, handleHttpOtaComplete, handleHttpOtaUpload);`
- `handleHttpOtaUpload()`：WebServer 的 upload 回调，分四段处理（START/WRITE/END/ABORTED），在 `UPLOAD_FILE_START` 阶段先校验 PIN，通过才 `Update.begin(UPDATE_SIZE_UNKNOWN)`
- `handleHttpOtaComplete()`：返回 JSON，成功则 `ESP.restart()`
- 独立状态位 `httpOtaInProgress`（**不与 `ArduinoOTA` 的逻辑共用**）
- `loop()` 中保留 `server.handleClient()` 照常跑，其他业务（MQTT / 电机 / 定时 / handleOta）在 `httpOtaInProgress` 期间跳过

`ArduinoOTA` 相关代码**保留未删**，作为备用路径。将来若想省 Flash 可以整段 `#if OTA_ENABLED` 连同 `<ArduinoOTA.h>` 一起删。

### 6.3 文件结构现状
- `src/main.cpp`：当前生效版本，**已带 HTTP OTA + ArduinoOTA**
- `src/main.cpp.new`：与 main.cpp 内容等同（历史遗留）
- `src/main.cpp.pre-ota-bak`：OTA 加进去之前的备份（可删）
- `include/app_config.h`：已补 `OTA_*` 默认宏

### 6.4 Flash 占用
当前 `79.5%`，约 1.04 MB。arduino-esp32 默认分区表有两个 ~1.28 MB 的 app 槽，够用。后续如果功能堆多到 > 1.2 MB 再考虑换分区表（不要换成 `huge_app.csv`，那个不支持 OTA 双槽）。

### 6.5 真机验证记录（2026-04-18）
- USB 烧录成功（`/dev/cu.usbserial-10`）
- 设备启动后 `/api/status` 正常，`remoteConnected=true`
- **正向**：`curl -F firmware=@firmware.bin "http://192.168.1.181/api/ota?pin=0728"` → HTTP 200，1 MB 在 13.8 秒内传完，返回 `{"ok":true,"message":"firmware written, rebooting"}`，设备自动重启，10 秒后服务回来
- **负向**：同样命令改成 `pin=9999` → HTTP 500 `{"ok":false,"error":"invalid pin"}`，设备保持在线
- 结论：HTTP OTA 完全可用

### 6.6 以后升级的标准命令
```bash
cd "/Users/kang/Desktop/AI Studio/FeedPass" && \
  ~/.platformio/penv/bin/pio run -e esp32dev && \
  curl -F "firmware=@.pio/build/esp32dev/firmware.bin" \
       "http://192.168.1.181/api/ota?pin=0728"
```

---

## 7. 已经做过的重要决策（仍有效）

- **不做原生 App**：网页 + ESP32 Web 服务 + MQTT 桥接，这是长期方向
- **安全目标**：不做强鉴权，只做 4 位 PIN，当前 `0728`
- **远程控制必须保留**：不允许回退到纯局域网

---

## 8. 下一位 AI 的任务建议

OTA 这条主线已经完成，用户接下来大概率会让你做这些（按优先级）：

1. **网页端 OTA 入口**：在 `data/index.html` 里加一个文件选择框 + 上传按钮，用 `fetch('/api/ota?pin=...', {method:'POST', body: FormData})` 调用。上传进度条可用 `XMLHttpRequest` 的 `upload.onprogress`。
2. **固件版本号**：固件里定义 `FIRMWARE_VERSION`，在 `/api/status` 的 JSON 里带上，网页显示。
3. **升级成功后的自动重连提示**：前端 `ESP.restart()` 后轮询 `/api/status` 直到回来再给提示。
4. **外网 OTA**：目前 HTTP OTA 只在局域网。如果要外网升级，最直接是把固件 bin 扔到 GitHub Release，ESP32 主动拉（`HTTPUpdate` / `esp_https_ota`），由 MQTT 下发"去拉新版本"的指令触发。这块还没开工。

---

## 9. 注意事项

- **不要**破坏公网控制页
- **不要**回退远程控制
- **不要**随意改 4 位 PIN 逻辑
- **不要**以为电脑要常驻——正常运行不依赖电脑
- 不要再去找 `/tmp/FeedPass-OTA`，那个目录已经没了，代码都在主项目
- 改 `src/main.cpp` 前看一眼 `src/main.cpp.pre-ota-bak` 了解 OTA 前的样子；确认没用可以删
- Flash 已经 79.5%，再加功能前留意分区余量

---

## 10. 一句话总结

这是一个已经跑通"本地网页 + 公网网页 + MQTT 远程控制 + PIN 鉴权 + **HTTP OTA 升级**"的 ESP32 自动喂食器 MVP。核心链路全部打通，接下来做的是产品化体验（网页 OTA 入口、版本号、进度条）以及可选的外网 OTA。
