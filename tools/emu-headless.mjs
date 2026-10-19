#!/usr/bin/env node
// Headless runner for the FoloToy AI Passport browser emulator (ESP-EMU WASM).
// Runs a merged full-flash image, prints UART, presses keys, injects microphone
// PCM, and saves screenshots on a schedule measured in emulated CPU time.
//
//   SIM_DIR=../FoloToy-Passport-Simulator node tools/emu-headless.mjs \
//     --firmware build/FoloToy-AI-Passport-full.bin --seconds 12 \
//     --at 4:press:OK --at 8:shot:build/emu/after-ok.png \
//     [--network ws://127.0.0.1:4190/api/emulator-network] [--at 6:mic:speech.pcm]
//
// Actions: press:<UP|DOWN|OK> (120 ms tap), hold:<KEY>:<ms>, shot:<png>, mic:<pcm16 16 kHz mono file>.

import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { deflateSync } from "node:zlib";

const CPU_HZ = 160_000_000;
const BATCH = 50_000;

function parseArgs(argv) {
  const opts = { seconds: 10, at: [], network: null, firmware: null };
  for (let i = 0; i < argv.length; i += 1) {
    const key = argv[i];
    const value = argv[i + 1];
    if (key === "--firmware") opts.firmware = value;
    else if (key === "--seconds") opts.seconds = Number(value);
    else if (key === "--network") opts.network = value;
    else if (key === "--at") opts.at.push(value);
    else throw new Error(`Unknown argument ${key}`);
    i += 1;
  }
  if (!opts.firmware) throw new Error("--firmware is required");
  return opts;
}

function crc32(bytes) {
  let crc = ~0;
  for (const byte of bytes) {
    crc ^= byte;
    for (let k = 0; k < 8; k += 1) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return ~crc >>> 0;
}

function encodePng(width, height, rgba) {
  const raw = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; y += 1) {
    raw[y * (width * 4 + 1)] = 0;
    Buffer.from(rgba.buffer, rgba.byteOffset + y * width * 4, width * 4)
      .copy(raw, y * (width * 4 + 1) + 1);
  }
  const chunk = (type, data) => {
    const body = Buffer.concat([Buffer.from(type), data]);
    const out = Buffer.alloc(body.length + 8);
    out.writeUInt32BE(data.length, 0);
    body.copy(out, 4);
    out.writeUInt32BE(crc32(body), body.length + 4);
    return out;
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header.set([8, 6, 0, 0, 0], 8);
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk("IHDR", header),
    chunk("IDAT", deflateSync(raw)),
    chunk("IEND", Buffer.alloc(0)),
  ]);
}

async function main() {
  const opts = parseArgs(process.argv.slice(2));
  const simDir = resolve(process.env.SIM_DIR ?? "../FoloToy-Passport-Simulator");
  const wasmDir = `${simDir}/public/wasm`;
  const pkg = await import(pathToFileURL(`${wasmDir}/pkg/esp_emu.js`).href);
  const { AiPassportBoard } = await import(pathToFileURL(`${wasmDir}/ai-passport-board.js`).href);
  const net = await import(pathToFileURL(`${wasmDir}/network.js`).href);
  const { detectUnsupportedFeature } = await import(
    pathToFileURL(`${wasmDir}/unsupported-features.js`).href
  );

  const wasm = pkg.initSync({ module: readFileSync(`${wasmDir}/pkg/esp_emu_bg.wasm`) });
  const emulator = new pkg.WasmEmulator("esp32c3");
  emulator.load_default_rom();
  emulator.set_boot_from_rom(true);
  emulator.set_wifi_config(net.EMULATOR_WIFI_SSID, net.EMULATOR_WIFI_PASSWORD);
  emulator.load_firmware(new Uint8Array(readFileSync(opts.firmware)));

  const screen = new Uint8ClampedArray(240 * 320 * 4);
  let audioBytes = 0;
  const board = new AiPassportBoard(wasm, {
    emulator,
    onFrame: ({ width, pixels, dirtyRegion: r }) => {
      for (let row = 0; row < r.height; row += 1) {
        const offset = ((r.y + row) * width + r.x) * 4;
        screen.set(pixels.subarray(offset, offset + r.width * 4), offset);
      }
    },
    onAudio: (packet) => {
      if (packet.bytes) audioBytes += packet.bytes.byteLength;
    },
  });
  board.releaseButtons();

  let bridge = null;
  if (opts.network) {
    bridge = new net.EmulatorNetworkBridge(emulator, {
      url: opts.network,
      onEvent: (detail) => process.stderr.write(`[net] ${JSON.stringify(detail)}\n`),
    });
    bridge.connect();
  }

  const events = opts.at.flatMap((spec) => {
    const [at, kind, arg, extra] = spec.split(":");
    const cycle = Math.round(Number(at) * CPU_HZ);
    if (kind === "press") {
      return [
        { cycle, run: () => board.setButton(arg, true) },
        { cycle: cycle + 0.12 * CPU_HZ, run: () => board.setButton(arg, false) },
      ];
    }
    if (kind === "hold") {
      return [
        { cycle, run: () => board.setButton(arg, true) },
        { cycle: cycle + (Number(extra) / 1000) * CPU_HZ, run: () => board.setButton(arg, false) },
      ];
    }
    if (kind === "shot") {
      return [{
        cycle,
        run: () => {
          mkdirSync(dirname(arg), { recursive: true });
          writeFileSync(arg, encodePng(240, 320, screen));
          process.stderr.write(`[emu] screenshot ${arg} at ${at}s\n`);
        },
      }];
    }
    if (kind === "mic") {
      const pcm = readFileSync(arg);
      return [{ cycle, run: () => board.pushMicrophone(new Uint8Array(pcm)) }];
    }
    throw new Error(`Unknown action ${spec}`);
  }).sort((a, b) => a.cycle - b.cycle);

  const endCycle = opts.seconds * CPU_HZ;
  const started = performance.now();
  while (emulator.cycles() < endCycle) {
    while (events.length && events[0].cycle <= emulator.cycles()) events.shift().run();
    const uart = emulator.run_batch(BATCH);
    if (uart) process.stdout.write(uart);
    const feature = detectUnsupportedFeature(emulator.pc());
    if (feature) {
      process.stderr.write(`[emu] stopped: unsupported feature ${feature}\n`);
      break;
    }
    board.drain({ flushFrame: true });
    board.pumpAudio(performance.now(), true);
    bridge?.drain();
    if (emulator.needs_restart()) emulator.restart();
    if (bridge) await new Promise((r) => setImmediate(r));
  }
  while (events.length) events.shift().run();
  const wall = (performance.now() - started) / 1000;
  process.stderr.write(
    `[emu] ${(emulator.cycles() / CPU_HZ).toFixed(1)} s emulated in ${wall.toFixed(1)} s wall, audio ${audioBytes} bytes\n`,
  );
  bridge?.close();
}

main().catch((error) => {
  process.stderr.write(`${error?.stack ?? error}\n`);
  process.exit(1);
});
