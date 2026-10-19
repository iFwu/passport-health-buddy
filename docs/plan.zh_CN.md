<p align="right">
  <strong>简体中文</strong> · <a href="plan.md">English</a>
</p>

# 开发计划

目标：10 月 23 日决赛现场，在真机上按住 UP 说「我刚喝了一杯水」，屏幕计数加一，StepAudio 3 播报今天的累计量；同时能演示久坐 / 喝水提醒和健身房记组。

## 架构决策

| 决策 | 选择 | 依据 |
| --- | --- | --- |
| 联网方式 | 设备通过 TLS WebSocket 直连 `api.stepfun.com`，不经中转 | 有网就能用；`api.stepfun.com` 支持 TLS Max Fragment Length，C3 的内存方案可行 |
| 底座 | 合并 [Qoder-Passport](https://github.com/kunlun322/Qoder-Passport)（MIT）的配网、Wi-Fi、TLS 调优和语音管线 | 同为 `FoloToy/ai-passport` 派生、ESP-IDF 5.5.3；已在同款 C3 上跑通 24 kHz 下行实时语音 |
| 模型 | `stepaudio-3-realtime-preview`，备用 `step-audio-2` | 见下方实测；preview 版下线时只改模型名 |
| 交互 | 按住 UP 说话，松开 `commit` | 不开 server VAD，没有唤醒词，省电不误触 |
| 工具调用确认 | 立即写入 NVS 并回传结果，屏幕给 5 秒撤销窗口 | StepAudio 3 等工具结果才开口，按键确认会卡住语音 |
| 密钥 | 配网页填写，只存 NVS | 固件和仓库不含 Key |
| 不采用 | 小智（xiaozhi）框架、电脑中转 | 小智要 IDF 6、C++、Opus，界面框架是聊天机器人；中转多一台机器 |

## 实测数据（2026-10-19，从开发机模拟设备调用）

同一句「我刚喝了一杯水」，带 `log_water` 工具，按住说话方式提交：

| 模型 | 工具调用 | 回复 | 首个音频 |
| --- | --- | --- | --- |
| `stepaudio-3-realtime-preview` | 3/3 成功，`{"ml": 250}` | 先调工具，拿到结果后只说一句 | 1.15–1.75 s |
| `step-audio-2` | 3/3 成功 | 调工具前后各说一次 | 约 1.0 s |
| `stepaudio-2.5-realtime` | 不触发，把工具名当文字念出 | — | 约 0.55 s |

协议要点：

- 工具必须用嵌套格式 `{"type":"function","function":{"name":…}}`；扁平格式返回 400。
- `function_call_output` 要等 `response.done` 之后再发，否则报 `ongoing response already exists`。
- 下行音频是 24 kHz PCM16；上行 16 kHz 能正确识别。单条 `response.audio.delta` 最大约 41 KB（base64），设备必须流式解析。

复现脚本：`tools/step-realtime-probe.mjs`（读取环境变量 `STEPFUN_API_KEY`）。

## 排期

| 日期 | 目标 | 验收 |
| --- | --- | --- |
| 10.19 | 合并 Qoder-Passport 底座；协议层改为 StepFun 事件；配网页改为 StepFun Key | 模拟器里配网后日志出现 StepFun `session.updated` |
| 10.20 | 按住说话上传、下行流式播放、`log_water` 计数加一并回传结果 | 真机说「喝了一杯水」，计数加一并听到播报 |
| 10.21 | 模式菜单；喝水 / 久坐提醒；健身房 `log_set` + 组间休息；平板支撑接回菜单 | 真机完整走一遍三个模式 |
| 10.22 | 手机热点下彩排、录演示视频、同步文档和飞书 | 视频 |

时间不够时依次砍掉：晚间日报 → 间歇跟练 → 健身房查询。P0 和提醒必须保住。

## 验证手段

- host tests：计时、提醒、记录、语音状态机、base64 流式解析。
- 模拟器：[FoloToy-Passport-Simulator](https://github.com/FoloToy/FoloToy-Passport-Simulator) 的 WASM 核心，用 `tools/emu-headless.mjs` 无界面运行，可按键、截图、注入麦克风 PCM、联网。速度约为实时的 0.3 倍，只验证协议和界面，不验证延迟和听感。
- 模拟器专用配置 `sdkconfig.emu` 把控制台切到 UART0；真机 GPIO21 是背光，**这个配置的产物不能烧到真机**。
- 真机：每次烧录前单独确认；默认只写主应用到 `0x10000`，保留 NVS。

## 风险

| 风险 | 应对 |
| --- | --- |
| preview 模型在决赛前下线 | 模型名可配置，切到 `step-audio-2` |
| 真机内存不够（TLS + 音频 + LVGL） | 沿用 Qoder-Passport 实测过的配置；会话期间监控最小剩余堆 |
| 现场 Wi-Fi 不稳定 | 用手机热点（2.4 GHz）；离线时计时和手动记录照常可用 |
| 嘈杂环境识别差 | 按住说话贴近设备；屏幕显示识别文本，错了可以撤销 |
