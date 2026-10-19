#!/usr/bin/env python3
"""Capture structured device voice traces or summarize existing local JSONL."""

import argparse
import json
import os
from pathlib import Path
import sys
import time


def trace_row(line):
    marker = b"voice_trace: "
    start = line.find(marker)
    if start < 0:
        return None
    try:
        row = json.loads(line[start + len(marker):].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    if not isinstance(row, dict) or not isinstance(row.get("stage"), str):
        return None
    if not isinstance(row.get("run"), str) or not isinstance(row.get("turn"), int):
        return None
    return row


def capture(args):
    import serial

    # Never overwrite another diagnostic session or collect unrelated serial data.
    args.out.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(args.out, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as output:
        port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
        port.dtr = False
        port.rts = False
        port.port = args.port
        try:
            port.open()
            print(f"Listening on {args.port} for {args.seconds}s; no reset command sent.", flush=True)
            print("Diagnostic content may be private. Keep the JSONL file local.", flush=True)
            end = time.monotonic() + args.seconds
            pending = bytearray()
            count = 0
            while time.monotonic() < end:
                pending.extend(port.read(4096))
                while b"\n" in pending:
                    line, _, pending = pending.partition(b"\n")
                    row = trace_row(line)
                    if row is None:
                        continue
                    output.write(json.dumps(row, ensure_ascii=False) + "\n")
                    output.flush()
                    count += 1
                    print(f"{row['run']}:{row['turn']} {row['stage']} {row.get('status', '')}", flush=True)
                if len(pending) > 16384:
                    pending.clear()
                    print("Discarded oversized non-terminated serial line.", file=sys.stderr)
            print(f"Captured {count} structured events to {args.out}")
        finally:
            port.close()


def group_turns(rows):
    # ASR is asynchronous and can precede commit acknowledgement. Join by the
    # server's input ID after collecting the trace, never by arrival order.
    inputs = {(r["run"], r["input"]): r["turn"] for r in rows
              if r.get("turn") and r.get("input")}
    turns = {}
    for row in rows:
        turn = row["turn"]
        if not turn and row.get("input"):
            turn = inputs.get((row["run"], row["input"]), 0)
        if not turn and row["stage"].startswith("session."):
            continue
        turns.setdefault((row["run"], turn), []).append(row)
    return turns


def summarize(args):
    with args.path.open(encoding="utf-8") as source:
        rows = [json.loads(line) for line in source if line.strip()]
    for (run, turn), events in group_turns(rows).items():
        print(f"\n{run}:{turn or 'unassociated'}  config={events[0].get('config', '?')}")
        for row in events:
            stage = row["stage"]
            identifiers = " ".join(f"{k}={row[k]}" for k in ("input", "response", "call") if row.get(k))
            print(f"  {row.get('ms', 0):>8}ms {stage} {row.get('status', '')} {identifiers}".rstrip())
            if row.get("reason"):
                print("    reason=" + json.dumps(row["reason"], ensure_ascii=False))
            if row.get("truncated"):
                print("    content/metadata truncated; do not treat as complete")
            if args.content:
                for field in ("text", "args", "result"):
                    if row.get(field):
                        print(f"    {field}=" + json.dumps(row[field], ensure_ascii=False))
            if row.get("content_redacted"):
                print("    content disabled in firmware")
            if stage in ("input.commit", "response.done", "playback.done", "playback.failed") and row.get("metrics"):
                print("    " + json.dumps(row["metrics"], ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    record = sub.add_parser("capture", help="Capture only voice_trace JSON rows; requires pyserial")
    record.add_argument("--port", required=True)
    record.add_argument("--seconds", type=int, default=60)
    record.add_argument("--out", type=Path, required=True)
    report = sub.add_parser("summary", help="Group local trace rows by run/turn")
    report.add_argument("path", type=Path)
    report.add_argument("--content", action="store_true", help="Display captured private transcripts/arguments/results")
    args = parser.parse_args()
    if args.command == "capture":
        if not 1 <= args.seconds <= 600:
            parser.error("--seconds must be between 1 and 600")
        capture(args)
    else:
        summarize(args)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
    except (OSError, ValueError, ImportError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
