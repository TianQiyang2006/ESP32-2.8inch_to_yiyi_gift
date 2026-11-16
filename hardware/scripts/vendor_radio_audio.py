"""Reproduce the minimal, pinned Helix decoder subset used by desktop FM.

No I2S driver or ESP8266Audio application classes are included. The only local
source adaptation sends decoder heap allocations to ESP32 PSRAM. A manifest
records both upstream and adapted hashes. Existing unrelated files are untouched.
"""
from __future__ import annotations
import hashlib
import io
import json
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "hardware/arduino/yiyi_desk/src/radio_vendor"
URL = "https://codeload.github.com/earlephilhower/ESP8266Audio/zip/refs/tags/1.9.9"
SHA256 = "4773c1bfd044510665c853c313a2c9d021403cc47645d18d19f5c25240a44fa2"
CACHE = ROOT / "build/radio-audio-1.9.9.zip"


def main() -> None:
    archive = CACHE.read_bytes() if CACHE.exists() else urllib.request.urlopen(URL, timeout=60).read()
    if hashlib.sha256(archive).hexdigest() != SHA256:
        raise SystemExit("Pinned ESP8266Audio archive SHA-256 mismatch")
    DEST.mkdir(parents=True, exist_ok=True)
    records = []
    z = zipfile.ZipFile(io.BytesIO(archive))
    prefix = "ESP8266Audio-1.9.9/"
    for name in z.namelist():
        rel = name.removeprefix(prefix)
        if name.endswith("/") or not (rel.startswith("src/libhelix-mp3/") or rel.startswith("src/libhelix-aac/")):
            continue
        target = DEST / rel.removeprefix("src/")
        original = z.read(name)
        data = original
        if target.name == "assembly.h":
            # Upstream assumes every Windows compiler accepts MSVC inline asm.
            # Native GNU tests use the same portable C arithmetic as ESP32.
            lines = data.splitlines(keepends=True)
            for i, line in enumerate(lines):
                if line.startswith(b"#if ") and b"_WIN32" in line:
                    condition = line[4:].rstrip(b"\r\n")
                    lines[i] = b"#if !defined(RADIO_NATIVE_TEST) && (" + condition + b")\n"
                    break
            data = b"".join(lines)
        # All Helix dynamic allocations, including optional SBR state, use the
        # same PSRAM-only allocator; allocation failure remains recoverable.
        if target.suffix == ".c" and (b"malloc(" in data or b"calloc(" in data):
            marker = b'#include "radio_allocator.h"\n'
            # Insert after all includes: macro replacement must not touch libc
            # malloc declarations inside <stdlib.h>.
            lines = data.splitlines(keepends=True)
            after = max((i for i, line in enumerate(lines) if line.lstrip().startswith(b"#include")), default=-1) + 1
            lines.insert(after, marker)
            data = b"".join(lines)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        records.append({"path": target.relative_to(DEST).as_posix(), "upstream_sha256": hashlib.sha256(original).hexdigest(), "sha256": hashlib.sha256(data).hexdigest()})
    (DEST / "ESP8266Audio-GPL-3.0.txt").write_bytes(z.read(prefix + "LICENSE"))
    # The AAC directory refers to these terms in its file headers; retain a copy
    # alongside both decoder trees, rather than describing the codecs as GPL.
    for decoder in ("libhelix-mp3", "libhelix-aac"):
        for filename in ("RPSL.txt", "RCSL.txt"):
            (DEST / decoder / filename).write_bytes(z.read(prefix + "src/libhelix-mp3/" + filename))
    allocator = '''#pragma once
#if defined(ESP32)
#include <esp_heap_caps.h>
#define malloc(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define calloc(n,s) heap_caps_calloc((n), (s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#endif
'''
    for decoder in ("libhelix-mp3", "libhelix-aac"):
        (DEST / decoder / "radio_allocator.h").write_text(allocator, encoding="utf-8")
    (DEST / "manifest.json").write_text(json.dumps({"upstream": "https://github.com/earlephilhower/ESP8266Audio/tree/1.9.9", "archive": URL, "archive_sha256": SHA256, "files": records}, indent=2) + "\n", encoding="utf-8")
    print(f"Vendored {len(records)} decoder files; archive SHA-256 verified")


if __name__ == "__main__":
    main()
