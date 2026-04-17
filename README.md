# FitPass

`FitPass` 是一个基于 `ESP32` 的自动喂食器项目。你负责硬件结构和 3D 打印，我负责软件部分：固件、手机网页控制、定时策略，以及后续的远程联网能力。

当前目录名是 `FeedPass`，但项目名在文档和界面里统一按 `FitPass` 书写；如果后面你想把目录名也一起改掉，我们再顺手处理。

## 当前能力

- `ESP32 + PlatformIO + Arduino` 固件
- 本地网页控制台
- `28BYJ-48 + ULN2003` 步进电机控制
- 手动投喂
- 正转 / 反转 / 停止调试
- 调试速度控制
- 每日定时投喂
- 本地持久化保存投喂时间和默认份量
- `STA + AP` 双模式网络
  - 连上家里路由器时，用局域网 IP 访问
  - 连不上时，仍保留 `FitPass-Setup` 热点兜底
- 远程控制扩展骨架
  - 固件侧已预留 `MQTT` 云桥接入口
  - [docs/index.html](/Users/kang/Desktop/AI Studio/FeedPass/docs/index.html) 可作为 `GitHub Pages` 远程控制页

## 项目结构

- [platformio.ini](/Users/kang/Desktop/AI Studio/FeedPass/platformio.ini)
- [include/app_config.h](/Users/kang/Desktop/AI Studio/FeedPass/include/app_config.h)
- [src/main.cpp](/Users/kang/Desktop/AI Studio/FeedPass/src/main.cpp)
- [data/index.html](/Users/kang/Desktop/AI Studio/FeedPass/data/index.html)
- [docs/index.html](/Users/kang/Desktop/AI Studio/FeedPass/docs/index.html)

## 当前架构

本地控制是这样跑的：

- `ESP32` 自己提供网页和接口
- 手机浏览器打开 ESP32 页面
- 页面发请求给 ESP32
- ESP32 真正驱动步进电机

远程控制扩展是这样跑的：

- 手机在外网打开 `GitHub Pages` 页面
- 页面通过 `MQTT` 云桥发指令
- ESP32 在家里联网后订阅这些指令
- 所以局域网控制和外网控制可以并存

## 主要配置

先打开 [include/app_config.h](/Users/kang/Desktop/AI Studio/FeedPass/include/app_config.h)：

- 本地网络
  - `WIFI_SSID`
  - `WIFI_PASSWORD`
- 电机与步进参数
  - `STEPPER_IN1_PIN`
  - `STEPPER_IN2_PIN`
  - `STEPPER_IN3_PIN`
  - `STEPPER_IN4_PIN`
  - `STEPPER_STEPS_PER_PORTION`
  - `STEPPER_STEP_INTERVAL_US`
- 远程控制
  - `REMOTE_CONTROL_ENABLED`
  - `REMOTE_DEVICE_ID`
  - `MQTT_HOST`
  - `MQTT_PORT`
  - `MQTT_USERNAME`
  - `MQTT_PASSWORD`
  - `MQTT_TOPIC_ROOT`

如果你暂时只跑局域网版，就把 `REMOTE_CONTROL_ENABLED` 保持为 `0`。

## 推荐流程

1. 编译并烧录固件
2. 上传网页文件系统
3. 手机访问 ESP32 页面
4. 先验证手动投喂
5. 再验证定时是否按北京时间执行
6. 和机构设计一起标定“一份”到底转多少
7. 如果要外网访问，再配置 `MQTT` 云桥和 `GitHub Pages`

## 常用命令

```bash
pio run
pio run -t upload
pio run -t uploadfs
pio device monitor
```

## 当前边界

这是一版很适合硬件联调的 MVP，不是最终量产版。当前还没做：

- Wi‑Fi 配网页面
- 完整的远程身份认证与权限隔离
- 投喂日志
- 断网补偿策略
- RTC 掉电保持
- 卡粮检测
- 远程控制的一键部署脚本

但它已经足够让我们继续推进硬件联调，而且软件架构没有走歪。
