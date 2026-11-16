"""Set shared desktop Wi-Fi from stdin JSON without logging the password.

Input: {"ssid": "...", "password": "..."}. Uses the existing desktop USB
diagnostic command; it does not add a credential interface to the firmware.
"""
import argparse
import json
import sys
import time
from pathlib import Path

import serial

from inspect_device import command, open_device, read_status, warmup


def wait_desktop(name, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with open_device(name) as port:
                warmup(port)
                state = read_status(port)
                if "voiceConnection" in state:
                    return state
        except (serial.SerialException, OSError, RuntimeError):
            time.sleep(0.3)
    raise RuntimeError("Desktop did not become available")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--out", type=Path, default=Path("build/grokbot-device-20261105/qingyu-connection.json"))
    args = parser.parse_args()
    credentials = json.load(sys.stdin)
    ssid, password = credentials.get("ssid"), credentials.get("password")
    if not isinstance(ssid, str) or not 1 <= len(ssid.encode()) <= 32 or " " in ssid:
        raise ValueError("SSID is unsupported by the existing USB command")
    if not isinstance(password, str) or not 8 <= len(password.encode()) <= 63:
        raise ValueError("Invalid WPA password length")
    if any(c in ssid + password for c in "\r\n\0"):
        raise ValueError("Invalid credential control character")
    with open_device(args.port) as port:
        warmup(port)
        state = read_status(port)
        if state.get("implementation") == "grokbot-2.5.3":
            command(port, "back")
    wait_desktop(args.port)
    with open_device(args.port) as port:
        warmup(port)
        command(port, "wificred " + ssid + " " + password)
        del credentials, password
        saved = False
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            line = port.readline()
            if b"WIFI CRED saved" in line:
                saved = True
                break
        if not saved:
            raise RuntimeError("Desktop did not confirm saved network credentials")
        print("Desktop saved Wi-Fi: " + ssid, flush=True)
        while time.monotonic() < deadline:
            state = read_status(port)
            if state.get("wifi"):
                result = {"ssid": ssid, "wifi": True, "wifiIp": state["wifiIp"],
                          "uptimeMs": state["uptimeMs"]}
                args.out.parent.mkdir(parents=True, exist_ok=True)
                args.out.write_text(json.dumps(result, indent=2), encoding="utf-8")
                print(json.dumps(result), flush=True)
                return
            time.sleep(1)
    raise RuntimeError("Credentials saved; desktop has not connected yet")


if __name__ == "__main__":
    main()
