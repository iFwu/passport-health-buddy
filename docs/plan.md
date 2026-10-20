<p align="right">
  <a href="plan.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Development Plan

Goal: at the October 23 final, long-press OK on the device and say "I just drank a cup of water" after the cue; hydration increases by 250 ml and StepAudio 3 reads the total. Demonstrate automatic activity/hydration reminders and gym set logging as well.

## Architecture decisions

| Decision | Choice | Reason |
| --- | --- | --- |
| Connectivity | The device connects to `api.stepfun.com` over a TLS WebSocket, no relay | Works on any network; `api.stepfun.com` honours TLS Max Fragment Length, so the C3 memory plan holds |
| Base | Merge provisioning, Wi-Fi, TLS tuning, and the voice pipeline from [Qoder-Passport](https://github.com/kunlun322/Qoder-Passport) (MIT) | Also derived from `FoloToy/ai-passport` on ESP-IDF 5.5.3; already runs 24 kHz realtime voice downlink on the same C3 |
| Model | `stepaudio-3-realtime-preview`, fallback `step-audio-2` | See measurements below; if the preview is retired only the model name changes |
| Interaction | Short UP/DOWN adjusts volume, long UP/DOWN pages; short OK acts, long OK talks, release sends `commit` | Consistent across modes, no double-click; holds never also tap |
| Tool-call confirmation | Commit persistent records to NVS before returning storage results; explicitly report failures while keeping RAM data | No second key confirmation or physical undo key; corrections and full-capacity additions both save |
| Secrets | Provisioning-page NVS, or optional untracked `sdkconfig.local` embedded in firmware | Never log keys or publish credential-bearing images |
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

The current offline Home iteration requires all four activities; interval presets are not optional scope. Keep unrelated evening-report and gym-query expansion separate from this iteration.

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
| Poor recognition in noise | Speak close to the device after its cue, release to send; show execution results and voice status, not arbitrary transcripts; use spoken corrections when needed |

The [product operating guide](health-companion.md) defines current controls and cues. The old standalone plank-timer manual describes historical firmware, not Health Buddy's button mapping.

## Offline Home activity iteration

- Keep plank as the default Home destination and free stopwatch. Add squat, jumping-jack and stretch pages using the existing local interval engine; no network, speech or new sensor is required to start a preset.
- Preserve the global controls. Long navigation traverses Office → Gym → plank → squat → jumping jacks → stretch → Help, wrapping in reverse as well. All four activities highlight Home; Help returns to the previous activity without interrupting it.
- Show distinct line-and-circle action illustrations, centered timing, activity position and navigation hints; interval screens include phase, round and progress. These are timing aids, not posture detection, repetition counting or evidence that the exercise was performed. No history carousel is added.
- Acceptance must cover offline starts/stops for all four activities, uninterrupted timers while paging and in Help, safe handling of an already active workout, default Home routing, and unchanged volume/speech gestures. Exact presets and conflict behavior are specified in the [operating guide](health-companion.md#offline-home-activities).
- Desktop LVGL previews, host tests, firmware build, application write verification and physical-device acceptance are separate evidence. The new Home pages and preset workflows have not yet been accepted on hardware; the earlier device result below does not cover this iteration.

## Device acceptance boundary

Current idle policy and controls are defined in the [operating guide](health-companion.md#screen-idle-and-panel-sleep). Basic on-device screen sleep and key wake are confirmed; this does not certify every gesture, sustained operation or power consumption.

### Current display-power firmware

| Identity | Value |
| --- | --- |
| Verified bundle / full-image SHA-256 | `build/firmware/4c74aaf68c31a9655a4c8b812ad5771ee23574cb4bd106dfe5fc8ca58f979d1b/` |
| Application SHA-256 | `8e325669f61c4c8bd4b3a7bdf60fdc22246c32954b6464a81cdaad6eb1680739` |
| ELF SHA-256 | `f1299d127168d1c4fedf0005dda70712f6c04ee483de513de8636585ffb123db` |

Confirmed evidence:

- Complete gate passed, including host tests, firmware layout verification and actual desktop LVGL rendering. A separate real-LVGL smoke run exercised three sleep/wake cycles, refresh suppression, continuing timers and wake-only gesture consumption with simulated panel IO.
- Before flashing, the device partition table passed MD5 validation. Its factory application starts at `0x10000` with a 3 MiB limit; the 1,705,936-byte image fits. Only the application was written and hash-verified. The existing partition table, NVS, other data and Recovery were not written; do not substitute a merged `0x0` flash for this preservation workflow.
- Boot matched the ELF prefix; buttons/audio/storage initialized, Ready reported 147,244 bytes free heap, Wi-Fi obtained an address and the StepFun session became ready.
- Bounded board observation recorded `Screen dim` at uptime 121,242 ms and `Screen asleep` at 301,267 ms, with no observed panic or screen-transition failure marker. The serial monitor closed afterward.
- Basic physical screen sleep and key wake are accepted. No measured current or battery-runtime result is available.

### Screen-sleep regression checklist

These checks extend basic acceptance; they are not recorded as passed on hardware.

| Scenario | Steps and expected result | Priority |
| --- | --- | --- |
| Complete wake gesture matrix | From Office after 5 minutes idle, test short/long presses of each key and overlapping keys. Until every key is released and a fresh gesture begins, water, volume, page and recording state must remain unchanged. | P0 |
| Protected operations and release | Run plank or a long interval/rest timer for over 5 minutes, including while viewing Help; the screen stays bright. Stop/complete it and check a full idle period restarts. Repeat during recording and a reply. | P1 |
| Repeated wake and sustained voice | Keep the realtime session connected and repeat sleep/wake; check no stale frame, color change or black screen, then exercise sustained speech. Measure board current separately; no runtime improvement is claimed. | P1 |
| Reminder wake while muted or audible | Leave Office asleep until an activity/hydration reminder. It shows the notice without a key press, sounds only when unmuted, and does not change records. | P1 |
| Provisioning and saved state | Leave the provisioning page visible beyond 5 minutes; credentials remain readable. After sleep/wake and a power cycle, confirm the last successfully saved volume and records remain intact. | P1 |

### Earlier firmware observations


The four-activity application ELF `093145328dd4195198d4f740caaaf9aa1d4e0d4f7fa66bc1e989adbc35adc797` belongs to verified bundle `171e17699cb55ec1a1e7a79b1ef063e780f0ebf84ac89fa5a15ef45b8f83fcfc`. The complete gate passed, including actual LVGL activity/phase rendering and conflict/navigation checks. Only the application at `0x10000` was flashed and hash-verified; partition table, NVS and Recovery were not written. Boot matched the ELF, reported buttons/audio/storage initialized and 147,144 bytes free heap at Ready, then established the StepFun realtime session. The bounded startup observation showed no panic or allocation failure. Physical viewing, button execution, phase sounds and sustained speech under activity rendering remain unverified; the earlier spoken-workout issue is not claimed fixed.

Application ELF `acc633936ab9d52090669457874068dcc88078b64b40cc7a243a993bc1dcb4f9` was written only at `0x10000` and verified. Physical controls, hydration speech, cues, and hydration/mute persistence across power-off were confirmed on device. The spoken bench-press set of 60 kg for 8 reps did not add a record; interval plans remain unverified on hardware. That serial trace showed input submission, final ASR, an audio response and `response.done`, but no logged tool-call event; it does not establish whether recognition or prompt behavior failed. Further diagnosis uses the new per-utterance trace. Old-image acceptance does not certify all functions in a new image.
