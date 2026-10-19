#!/usr/bin/env node
// Probe the StepFun Realtime API exactly as the firmware will: push-to-talk
// (no server VAD), pcm16 in base64 chunks, one function tool. Prints event
// timing and frame sizes so device buffers can be sized from measurements.
//
//   STEPFUN_API_KEY=... node tools/step-realtime-probe.mjs speech.pcm [model] [rate]
//
// speech.pcm: raw pcm16 mono at <rate> Hz (default 16000). Reply audio is
// written to build/probe/reply.pcm.

import { readFileSync, writeFileSync, mkdirSync } from "node:fs";

const [, , pcmPath, model = "stepaudio-2.5-realtime", rateArg = "16000"] = process.argv;
const key = process.env.STEPFUN_API_KEY;
if (!pcmPath || !key) {
  console.error("usage: STEPFUN_API_KEY=... node tools/step-realtime-probe.mjs speech.pcm [model] [rate]");
  process.exit(2);
}
const rate = Number(rateArg);
const pcm = readFileSync(pcmPath);
const chunkBytes = (rate / 50) * 2; // 20 ms frames, same as the device

const ws = new WebSocket(`wss://api.stepfun.com/v1/realtime?model=${model}`, {
  headers: { Authorization: `Bearer ${key}` },
});
const t0 = performance.now();
const ms = () => `${(performance.now() - t0).toFixed(0).padStart(6)} ms`;
const reply = [];
let maxFrame = 0;
let committedAt = 0;
let firstAudioAt = 0;
const counts = new Map();
const pendingCalls = [];

const send = (event) => ws.send(JSON.stringify(event));
// TOOL_FORMAT=nested follows the StepFun doc ({type, function:{...}});
// TOOL_FORMAT=flat follows the OpenAI Realtime shape ({type, name, ...}).
const toolSpec = (fn) =>
  process.env.TOOL_FORMAT === "flat" ? { type: "function", ...fn } : { type: "function", function: fn };

ws.addEventListener("open", () => {
  console.log(`${ms()} open`);
  send({
    type: "session.update",
    session: {
      modalities: ["text", "audio"],
      instructions: "你是随身健康助手，回答简短。用户报告喝水时必须调用 log_water 工具，不要把工具名念出来。",
      voice: "qingchunshaonv",
      input_audio_format: "pcm16",
      output_audio_format: "pcm16",
      tools: [toolSpec({
        name: "log_water",
        description: "记录用户喝水量",
        parameters: {
          type: "object",
          properties: { ml: { type: "integer", description: "毫升数，一杯按 250" } },
          required: ["ml"],
        },
      })],
    },
  });
  for (let offset = 0; offset < pcm.length; offset += chunkBytes) {
    send({ type: "input_audio_buffer.append", audio: pcm.subarray(offset, offset + chunkBytes).toString("base64") });
  }
  send({ type: "input_audio_buffer.commit" });
  send({ type: "response.create" });
  committedAt = performance.now();
  console.log(`${ms()} sent ${pcm.length} bytes in ${Math.ceil(pcm.length / chunkBytes)} chunks, committed`);
});

ws.addEventListener("message", (message) => {
  const raw = String(message.data);
  maxFrame = Math.max(maxFrame, raw.length);
  const event = JSON.parse(raw);
  counts.set(event.type, (counts.get(event.type) ?? 0) + 1);
  if (event.type === "response.audio.delta") {
    if (!firstAudioAt) {
      firstAudioAt = performance.now();
      console.log(`${ms()} first audio delta (${(firstAudioAt - committedAt).toFixed(0)} ms after commit), frame ${raw.length} chars`);
    }
    reply.push(Buffer.from(event.delta, "base64"));
    return;
  }
  if (event.type === "response.audio_transcript.delta" || event.type === "response.text.delta") return;
  const brief = JSON.stringify(event).slice(0, 400);
  console.log(`${ms()} ${event.type} ${brief}`);
  if (event.type === "response.function_call_arguments.done") {
    pendingCalls.push({ call_id: event.call_id, name: event.name, arguments: event.arguments });
  }
  if (event.type === "error") ws.close();
  if (event.type !== "response.done") return;
  // A new response may only start after the current one finishes.
  if (!pendingCalls.length) {
    setTimeout(() => ws.close(), 500);
    return;
  }
  for (const call of pendingCalls.splice(0)) {
    console.log(`${ms()} tool ${call.name}(${call.arguments}) -> function_call_output`);
    send({
      type: "conversation.item.create",
      item: { type: "function_call_output", call_id: call.call_id, output: JSON.stringify({ ok: true, today_ml: 750 }) },
    });
  }
  send({ type: "response.create" });
});

ws.addEventListener("close", (event) => {
  const audio = Buffer.concat(reply);
  mkdirSync("build/probe", { recursive: true });
  writeFileSync("build/probe/reply.pcm", audio);
  console.log(`${ms()} close ${event.code} ${event.reason}`);
  console.log(`events ${JSON.stringify(Object.fromEntries(counts))}`);
  console.log(`reply audio ${audio.length} bytes, max frame ${maxFrame} chars -> build/probe/reply.pcm`);
});

setTimeout(() => ws.close(), 30_000);
