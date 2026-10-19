<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# Passport Health Buddy

挂在身上的常驻健康助手。办公时提醒起身和喝水，健身房里说一句话记一组、自动计组间休息，在家带你做平板支撑和间歇跟练。按住按键说话，阶跃星辰 Step 实时语音模型听懂并直接执行。

硬件是 FoloToy AI Passport：ESP32-C3、240 × 320 彩屏、三键、麦克风和扬声器、NFC、500 mAh 电池。

## 三种模式

| 模式 | 你做什么 | 设备做什么 |
| --- | --- | --- |
| 常驻 | 正常上班 | 每 45 分钟提醒起身，定时提醒喝水；说「喝了半瓶」自动记毫升数 |
| 健身房 | 按住 UP 说「卧推 60 公斤 8 个」 | 立即记一组（5 秒内可撤销），自动开始 90 秒组间休息倒计时 |
| 居家 | 选平板支撑或间歇计划 | 发车提示音 + 屏幕倒计时带节奏，记录写入 Flash，断电不丢 |

晚上用同一个实时语音会话播报当天日报，屏幕同步显示。

## Step 模型怎么用

```mermaid
sequenceDiagram
    participant U as 用户
    participant D as AI Passport
    participant S as StepFun Realtime
    U->>D: 按住 UP 说话
    D->>S: PCM 16 kHz（TLS WebSocket 直连）
    U->>D: 松开 UP
    S-->>D: tool call log_water(ml=250)
    D->>D: 写入 NVS，计数 +1
    D->>S: 工具结果（今天累计 750 ml）
    S-->>D: 语音回复 PCM 24 kHz
    D-->>U: 「已记录，今天累计喝了 750 毫升」
```

- 模型：`stepaudio-3-realtime-preview`（StepAudio 3 Realtime），备用 `step-audio-2`；接口 `wss://api.stepfun.com/v1/realtime`。
- 按住说话：松开后 `commit` + `response.create`，不开 server VAD。
- 工具调用：`log_water`、`log_set`、`start_rest_timer`、`start_interval_plan`、`snooze_reminder`、`query_today`。设备先写入再回传结果，模型播报的是设备上的真实数据。
- 设备直连 StepFun，不需要电脑或服务器中转。首次开机用手机连设备热点、打开 `192.168.4.1` 填 Wi-Fi 和 API Key，Key 只存在设备 NVS。

完整设计见[随身健康助手](docs/health-companion.zh_CN.md)。

## 代码结构

```text
main/                  应用：模式、计时、提醒、配网、语音会话、界面
components/bsp/        板级驱动：屏幕、按键、音频、电池
tests/                 host 测试：计时状态机、存储、布局
tools/validate.sh      仓库检查 + host 测试 + 固件构建
docs/                  产品文档、硬件资料、开发指南
```

## 构建

需要 ESP-IDF 5.5.3。

```bash
./tools/validate.sh --static    # 仓库检查 + host 测试
./tools/validate.sh --firmware  # 固件构建 + 合并镜像校验
```

烧录前先看[固件布局](docs/development/engineering/firmware-layout.zh_CN.md)：合并镜像从 `0x0` 写入会覆盖 NVS（包括已配置的 Wi-Fi、API Key 和训练记录）。

开发计划与进度见 [docs/plan.zh_CN.md](docs/plan.zh_CN.md)。

## 文档

- [随身健康助手](docs/health-companion.zh_CN.md)：产品形态、语音链路、硬件边界
- [开发计划](docs/plan.zh_CN.md)：架构决策、实测数据、排期与验收
- [平板支撑自由计时器](docs/plank-trainer.md)：按键、提示音、记录语义
- [硬件规格](docs/hardware-design/specifications.zh_CN.md)
- [开发者文档索引](docs/README.zh_CN.md)：基于上游 AI Passport 的开发规范与 BSP 说明

## 致谢

- [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)：BSP 与开发框架（MIT）。
- [kunlun322/Qoder-Passport](https://github.com/kunlun322/Qoder-Passport)：配网、Wi-Fi、TLS 内存调优与实时语音管线（MIT）。

2026 阶跃 Hackathon 参赛作品。
