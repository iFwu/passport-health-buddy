<p align="right">
  <a href="plan.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Development Plan

Goal: at the October 23 final, hold UP on the real device and say "I just drank a cup of water"; the counter increments and StepAudio 3 reads out today's total. Sitting / hydration reminders and gym set logging are also demoed.

## Architecture decisions

| Decision | Choice | Reason |
| --- | --- | --- |
| Connectivity | The device connects to `api.stepfun.com` over a TLS WebSocket, no relay | Works on any network; `api.stepfun.com` honours TLS Max Fragment Length, so the C3 memory plan holds |
| Base | Merge provisioning, Wi-Fi, TLS tuning, and the voice pipeline from [Qoder-Passport](https://github.com/kunlun322/Qoder-Passport) (MIT) | Also derived from `FoloToy/ai-passport` on ESP-IDF 5.5.3; already runs 24 kHz realtime voice downlink on the same C3 |
| Model | `stepaudio-3-realtime-preview`, fallback `step-audio-2` | See measurements below; if the preview is retired only the model name changes |
| Interaction | Hold UP to talk, `commit` on release | No server VAD, no wake word: lower power, no false triggers |
| Tool-call confirmation | Write NVS immediately, return the result, 5-second on-screen undo | StepAudio 3 waits for the tool result before speaking; a key confirmation would stall the voice |
| Secrets | Entered on the provisioning page, stored only in NVS | No key in firmware or the repository |
| Rejected | XiaoZhi framework, laptop relay | XiaoZhi needs IDF 6, C++, Opus, and a chatbot UI shell; a relay adds a machine |

## Measurements (2026-10-19, device-style calls from a laptop)

Same utterance "I just drank a cup of water", with the `log_water` tool, submitted push-to-talk style:

| Model | Tool call | Reply | First audio |
| --- | --- | --- | --- |
| `stepaudio-3-realtime-preview` | 3/3, `{"ml": 250}` | Calls the tool first, then speaks once with the result | 1.15–1.75 s |
| `step-audio-2` | 3/3 | Speaks before and after the tool call | ~1.0 s |
| `stepaudio-2.5-realtime` | Not triggered; reads the tool name aloud | — | ~0.55 s |

Protocol notes:

- Tools must use the nested shape `{"type":"function","function":{"name":…}}`; the flat shape returns 400.
- Send `function_call_output` only after `response.done`, otherwise the server returns `ongoing response already exists`.
- Downlink audio is 24 kHz PCM16; a 16 kHz uplink is recognised correctly. A single `response.audio.delta` reaches about 41 KB of base64, so the device must parse it as a stream.

Reproduce with `tools/step-realtime-probe.mjs` (reads `STEPFUN_API_KEY` from the environment).

## Schedule

| Date | Goal | Acceptance |
| --- | --- | --- |
| 10.19 | Merge the Qoder-Passport base; rewrite the protocol layer for StepFun events; provisioning page takes a StepFun key | In the emulator, after provisioning, logs show StepFun `session.updated` |
| 10.20 | Push-to-talk upload, streamed playback, `log_water` increments and returns its result | On device, "I drank a cup of water" increments the count and the reply is audible |
| 10.21 | Mode menu; hydration / sitting reminders; gym `log_set` + rest timer; plank back in the menu | Walk through all three modes on device |
| 10.22 | Rehearse on a phone hotspot, record the demo video, sync docs and Feishu | Video |

If time runs short, cut in order: evening report → interval workouts → gym queries. P0 and reminders stay.

## Verification

- Host tests: timers, reminders, records, voice state machine, streaming base64 parser.
- Emulator: the WASM core from [FoloToy-Passport-Simulator](https://github.com/FoloToy/FoloToy-Passport-Simulator), run headless with `tools/emu-headless.mjs` for key presses, screenshots, microphone PCM injection, and networking. It runs at about 0.3× real time, so it checks protocol and UI, not latency or sound.
- The emulator-only `sdkconfig.emu` moves the console to UART0; GPIO21 is the backlight on real hardware, so **never flash an image built with it**.
- Device: each flash is approved separately; by default only the application at `0x10000` is written and NVS is kept.

## Risks

| Risk | Mitigation |
| --- | --- |
| Preview model retired before the final | Configurable model name; switch to `step-audio-2` |
| Not enough RAM on device (TLS + audio + LVGL) | Reuse the Qoder-Passport measured configuration; track minimum free heap during sessions |
| Unreliable venue Wi-Fi | Phone hotspot on 2.4 GHz; timers and manual logs work offline |
| Poor recognition in noise | Push-to-talk close to the device; screen shows the recognised text and the result can be undone |
