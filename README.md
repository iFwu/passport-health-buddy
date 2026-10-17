<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Passport Health Buddy

An always-on health companion you wear. At work it reminds you to stand up and drink water; at the gym one sentence logs a set and starts the rest timer; at home it leads planks and interval workouts. Hold a button and speak: a StepFun Step realtime voice model understands and acts.

Hardware is the FoloToy AI Passport: ESP32-C3, 240 × 320 color screen, three keys, microphone and speaker, NFC, 500 mAh battery.

## Three modes

| Mode | You | The device |
| --- | --- | --- |
| Everyday | Work as usual | Stand-up reminder every 45 minutes, timed hydration reminders; say "half a bottle" to log millilitres |
| Gym | Hold UP and say "bench press 60 kg, 8 reps" | Confirms on screen, logs the set, starts a 90-second rest countdown |
| Home | Pick plank or an interval plan | Start cues and on-screen countdown keep the pace; records persist in Flash across power loss |

In the evening `step-5-preview` turns the day's records into a short report, shown on screen and read aloud.

## How Step models are used

```mermaid
sequenceDiagram
    participant U as User
    participant D as AI Passport
    participant R as Relay
    participant S as StepFun Realtime
    U->>D: Hold UP and speak
    D->>R: 16 kHz PCM stream
    R->>S: stepaudio-2.5-realtime
    S-->>R: Voice reply + tool call
    R-->>D: Reply audio + tool command
    D->>D: Confirm on screen, write NVS
    D-->>U: Play reply on speaker
```

- Realtime voice: `wss://api.stepfun.com/v1/realtime`, model `stepaudio-2.5-realtime`, server VAD.
- Tool calls: `log_water`, `log_set`, `start_rest_timer`, `start_interval_plan`, `snooze_reminder`, `query_today`. The model listens and speaks; state and records stay on the device.
- Daily report: `step-5-preview`.
- The API key lives only on the relay, never in firmware or the repository.

Full design: [Wearable health companion](docs/health-companion.md).

## Layout

```text
main/                  Application: modes, timers, reminders, voice, UI
components/bsp/        Board drivers: display, keys, audio, battery
tests/                 Host tests: timer state machine, storage, layout
tools/validate.sh      Repo checks + host tests + firmware build
docs/                  Product docs, hardware reference, developer guides
```

## Build

Requires ESP-IDF 5.5.3.

```bash
./tools/validate.sh --static    # repo checks + host tests
./tools/validate.sh --firmware  # firmware build + merged-image check
```

Read the [firmware layout](docs/development/engineering/firmware-layout.md) before flashing: a merged image written from `0x0` overwrites NVS.

## Documentation

- [Wearable health companion](docs/health-companion.md): product shape, voice path, hardware limits
- [Plank free timer](docs/plank-trainer.md) (Chinese): keys, cues, record semantics
- [Hardware specification](docs/hardware-design/specifications.md)
- [Developer documentation index](docs/README.md): development rules and BSP notes from upstream AI Passport

## Credits

Built on the BSP and development framework of [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport). Entry for the 2026 StepFun Hackathon.
