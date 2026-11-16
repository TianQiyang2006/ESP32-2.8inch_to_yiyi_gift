"""Check the running GrokBot's real fonts, subtitle bounds and horizontal scroll."""
import argparse
import json
import time
from pathlib import Path

from PIL import Image

from check_official_device import official_capture
from inspect_device import command, open_device, read_status, warmup


def font_status(port):
    command(port, "fontcheck")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", "replace").strip()
        try:
            result = json.loads(line)
        except ValueError:
            continue
        if isinstance(result, dict) and "fontSize" in result:
            return result
    raise RuntimeError("Missing fontcheck response")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--out", type=Path,
                        default=Path("build/xiaozhi-font-fix-20261111/after"))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    samples = []
    with open_device(args.port) as port:
        warmup(port)
        before = read_status(port)
        assert before.get("implementation") == "grokbot-2.5.3", before
        assert before["state"] == 3, "Run the font check while idle"
        try:
            command(port, "notify 正在聆听，请说话…")
            cases = (
                ("chinese", "依依，今天也要开心呀！"),
                ("mixed", "Wi-Fi 100% · 17:28，小智你好！"),
                ("descenders", "gjpqy 0123456789，。、！？"),
                ("scroll", "小智正在认真听你说话，依依今天也要开开心心！"),
            )
            for name, text in cases:
                message = "text " + text
                assert len(message.encode("utf-8")) <= 80
                command(port, message)
                time.sleep(.25)
                state = font_status(port)
                assert (state["fontSize"], state["fontBpp"]) == (16, 4), state
                assert state["missingGlyphs"] == 0, (text, state)
                assert state["subtitleVisible"] and state["subtitleWidth"] == 308, state
                assert (0 <= state["barY"] <= state["subtitleY"] <=
                        state["subtitleBottom"] <= state["barBottom"] < 240), state
                assert state["subtitleBottom"] - state["subtitleY"] + 1 >= state["lineHeight"], state
                official_capture(port, args.out / (name + ".png"))
                if name == "scroll":
                    time.sleep(2)
                    official_capture(port, args.out / "scroll-next.png")
                    area = (6, max(state["subtitleY"], 220), 314, state["subtitleBottom"] + 1)
                    first = Image.open(args.out / "scroll.png").crop(area)
                    second = Image.open(args.out / "scroll-next.png").crop(area)
                    assert first.tobytes() != second.tobytes(), "Long subtitle did not scroll"
                samples.append({"case": name, "text": text, "font": state})
        finally:
            command(port, "text")
        time.sleep(.25)
        cleared = font_status(port)
        assert not cleared["subtitleVisible"], cleared
        after = read_status(port)
        assert after["uptimeMs"] > before["uptimeMs"], "Device restarted"
        assert after["partition"] == "app1" and after["state"] == 3, after
    report = {"passed": True, "before": before, "samples": samples,
              "cleared": cleared, "after": after}
    (args.out / "font-verification.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print("PASS: 16px/4bpp, Chinese/mixed glyphs, full line bounds, scrolling, subtitle cleanup")


if __name__ == "__main__":
    main()
