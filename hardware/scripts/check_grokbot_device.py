"""Exercise the deployed GrokBot / desktop bridge over the real USB port."""
import argparse
import json
import time
from pathlib import Path

import serial

from check_official_device import official_capture
from inspect_device import capture, command, open_device, read_status, warmup

ROOT = Path(__file__).resolve().parents[2]
IMPLEMENTATION = "grokbot-2.5.3"


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def wait_status(name, out, grokbot, seconds=40):
    deadline = time.monotonic() + seconds
    logs = []
    try:
        while time.monotonic() < deadline:
            try:
                with open_device(name) as port:
                    next_query = 0
                    while time.monotonic() < deadline:
                        if time.monotonic() >= next_query:
                            command(port, "status")
                            next_query = time.monotonic() + 1
                        line = port.readline().decode("utf-8", "replace").strip()
                        if not line:
                            continue
                        logs.append(line)
                        if any(s in line for s in ("Guru Meditation", "ESP_ERROR_CHECK failed", "abort() was called")):
                            raise RuntimeError(line)
                        try:
                            state = json.loads(line)
                        except ValueError:
                            continue
                        if not isinstance(state, dict) or "uptimeMs" not in state:
                            continue
                        matches = (state.get("implementation") == IMPLEMENTATION if grokbot else
                                   state.get("voiceConnection", {}).get("implementation") == "official-2.5.3")
                        if matches:
                            print(json.dumps(state, ensure_ascii=False), flush=True)
                            return state
            except (serial.SerialException, OSError):
                time.sleep(0.3)
        raise RuntimeError("Expected application did not answer status")
    finally:
        out.write_text("\n".join(logs), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["enter", "status", "capture", "emotions", "monitor", "exit"])
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--seconds", type=float, default=35)
    parser.add_argument("--out", type=Path, default=ROOT / "build/grokbot-device-20261105")
    args = parser.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    if args.action == "enter":
        with open_device(args.port) as port:
            warmup(port)
            state = read_status(port)
            assert state.get("voiceConnection", {}).get("implementation") == "official-2.5.3", state
            assert state["touchCalibrated"] and state["audioReady"], state
            save(out / "desktop-entry.json", state)
            command(port, "page 6")
            time.sleep(0.5)
            capture(port, out / "desktop-apps.png")
            command(port, "app voice")
        state = wait_status(args.port, out / "entry.log", True)
        assert state["partition"] == "app1" and state["touchCalibrated"], state
        save(out / "grokbot-entry.json", state)
    elif args.action == "exit":
        with open_device(args.port) as port:
            warmup(port)
            assert read_status(port).get("implementation") == IMPLEMENTATION
            command(port, "tap 285 38")
        state = wait_status(args.port, out / "exit.log", False)
        assert state["page"] == 6 and state["app"] == 0, state
        assert state["audioReady"] and state["touchCalibrated"], state
        before = json.loads((out / "desktop-entry.json").read_text(encoding="utf-8"))
        for field in ("brightness", "volume", "touchMode", "touchCalibrated", "buildFlavor"):
            assert state[field] == before[field], (field, before[field], state[field])
        assert state["timers"]["alarmCount"] == before["timers"]["alarmCount"]
        assert state["tetris"]["best"] == before["tetris"]["best"]
        save(out / "desktop-return.json", state)
        with open_device(args.port) as port:
            capture(port, out / "desktop-return.png")
        print("LVGL exit returned to app0; saved settings preserved", flush=True)
    elif args.action in ("capture", "emotions"):
        with open_device(args.port) as port:
            warmup(port)
            assert read_status(port).get("implementation") == IMPLEMENTATION
            if args.action == "capture":
                official_capture(port, out / "grokbot.png")
            else:
                for emotion in ("neutral", "happy", "thinking", "sad"):
                    command(port, "emotion " + emotion)
                    time.sleep(2)
                    official_capture(port, out / (emotion + ".png"))
                    print("Captured " + emotion, flush=True)
    elif args.action == "status":
        state = wait_status(args.port, out / "status.log", True)
        save(out / "grokbot-status.json", state)
    else:
        logs, samples = [], []
        with open_device(args.port) as port:
            warmup(port)
            deadline = time.monotonic() + args.seconds
            next_status = 0
            while time.monotonic() < deadline:
                if time.monotonic() >= next_status:
                    command(port, "status")
                    next_status = time.monotonic() + 3
                line = port.readline().decode("utf-8", "replace").strip()
                if not line:
                    continue
                logs.append(line)
                if any(s in line for s in ("Guru Meditation", "ESP_ERROR_CHECK failed", "abort() was called")):
                    raise RuntimeError(line)
                try:
                    state = json.loads(line)
                except ValueError:
                    if "Animation fps=" in line:
                        print(line, flush=True)
                    continue
                assert state.get("implementation") == IMPLEMENTATION, state
                assert state["partition"] == "app1", state
                if samples:
                    assert state["uptimeMs"] > samples[-1]["uptimeMs"], "Application restarted"
                samples.append(state)
                print(json.dumps(state), flush=True)
        (out / "monitor.log").write_text("\n".join(logs), encoding="utf-8")
        save(out / "samples.json", samples)
        assert len(samples) >= 2, "Insufficient runtime status samples"


if __name__ == "__main__":
    main()
