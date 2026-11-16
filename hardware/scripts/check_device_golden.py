"""yiyi-desk 设备 golden 工具（V2 Core Phase 1 · 只写代码，不烧录）。

工具职责
========
遍历 `hardware/tests/golden/device-views.json` 里的视图表：对每个视图发送入口串口
命令序列 → 冻结 RTC → 等待画面稳定（transitionActive=false + 可选 mask 的 hash 稳定
探针；animations 只 WARN）→ 抓 `screenshot` 返回的原始 RGB565 帧
（320x240x2 = 153,600 B）→ 算 SHA-256 → 连同抓图前后的 `status` 环境信封写成基线
JSON，供之后 `--check` 做逐字节对照。已批准视觉变化（K2 键盘）用 `--update-only`
只替换指定视图的基线并留 diff 证据。
本工具**不修改固件、不烧录、不修改 verify.ps1**；默认动作（`--list`）只在本地
打印视图表，完全不碰串口。

用法
====
    # 0) 默认 dry-run：只打印视图表（不打开串口）
    python hardware/scripts/check_device_golden.py

    # 1) 采集设备基线（Lead 在基线固件烧录后执行；建议双跑一致性）
    python hardware/scripts/check_device_golden.py --baseline --repeat 2

    # 2) 对照基线（任何 FAIL 或 UNSTABLE 都 exit 1）
    python hardware/scripts/check_device_golden.py --check

    # 3) 只更新 K2 已批准变化的键盘视图基线（其余视图条目原样保留，旧/新 hash 写入
    #    baseline.updates，changed 视图在 build/device-golden/last/ 下留 diff 证据）
    python hardware/scripts/check_device_golden.py --update-only wifi-keyboard,wifi-keyboard-upper,wifi-symbols

    # 4) 离线自测：不打开任何串口，用已有 PNG 夹具验证"发现视图/哈希/对比/报告"
    python hardware/scripts/check_device_golden.py --from-dir build/review/device-allb --check

    # 5) 离线自测进阶：先生成离线基线，再用同一套 --check 逻辑对照
    python hardware/scripts/check_device_golden.py --from-dir build/review/device-allb --baseline --baseline-json build/audit-20261028/offline-baseline.json
    python hardware/scripts/check_device_golden.py --from-dir build/review/device-allb --check --baseline-json build/audit-20261028/offline-baseline.json

    # 也可用 PowerShell 包装脚本：
    #   .\\hardware\\scripts\\verify_device.ps1 -Baseline -Repeat 2
    #   .\\hardware\\scripts\\verify_device.ps1 -Check
    #   .\\hardware\\scripts\\verify_device.ps1 -FromDir build/review/device-allb -Check

退出码：0 = PASS / 自测通过；1 = FAIL 或 UNSTABLE；2 = 参数/环境错误。

冻结协议（确定性）
==================
1. 每个视图都先 `timeset <freezeEpoch>`（manifest 默认 1793066160 = 2026-10-27
   09:56 +08，周二；`timeset` 在固件里要求 epoch > 1700000000，否则打印
   "TIME INVALID" 且不改 RTC，所以不能用 `timeset 1` 把 timeValid 置 false）。
2. 抓图时把 `timeset <epoch>` 和 `screenshot` 作为**一次串口写入**发送：固件在同一个
   loop 迭代里先 render/refresh 再取帧，所以 HH:MM:SS 这类秒级标签被固定在 epoch 上，
   双跑/跨固件比较都稳定。
3. 抓图前先等 `status.transitionActive == false`；`animations` 只作为 WARN，不再硬性要求为 0
   （键盘 textarea 的 LVGL caret blink 会让 animations 常驻 1）。随后按视图的
   `settle` 声明做"hash 稳定探针"：用带 mask 的帧 hash 连续采样，要求
   `stableFrames` 帧一致（默认 2，间隔 220ms，超时 8s 只 WARN）；时钟 flip / Wi-Fi
   列表这类声明 `stableFrames:1` 的视图跳过探针。探针不通过不会让普通视图变成
   UNSTABLE——最终由 `--repeat 2` 双跑一致性裁决。
   并记录 `status`（环境信封："pre"）；抓图后再读一次 `status`（"post"）。
4. `mask`：视图可声明 `[[x,y,w,h], ...]`；hash / 参考帧 / diff PNG 都在 mask 之后。
   Wi-Fi 键盘视图 mask 掉名称/密码输入框，屏蔽光标闪烁的不可控相位（键帽区 y>=134
   不受影响）。mask 是"不参与比对"的显式声明，不是截图裁切。
5. 环境信封按 manifest 声明断言的字段：wifi / timeValid / weatherValid /
   batteryInstalled / externalPower / brightness / volume / bgr / inverted /
   transitionActive / uptimeMs / resetReason / version / sketchMd5 / page / app。
   规则：`true|false|数字` 精确值，`"same"` 必须与基线记录一致，`"monotonic"` 抓图后
   不减，`"stable"` 前后一致，`"record"` 只记录，`"expect"` 取视图的 expect 声明。
   任何不一致 → 该视图 UNSTABLE（不算 PASS，也不算像素 FAIL）；`optional` 视图
   （statusbar-online/offline）的采集/信封失败降级为 SKIP+WARN。
6. `--repeat N`（N>=2）对每个 hash 视图连抓 N 帧，要求逐字节一致，否则 UNSTABLE。
7. 推荐环境：设备**不连 Wi-Fi**（无 NTP / 无天气/阅读刷新，requestWeather/
   requestReading 都要求 WiFi.status()==WL_CONNECTED && timeValid()），此时
   weatherValid=false、数据层静止，最可复现。若设备在线，天气/阅读刷新可能改变画面
   内容，env 信封只能发现"状态"变化、不能发现"内容"变化，见已知限制。

视图表维护方法
==============
`hardware/tests/golden/device-views.json` 里每个视图：
- `id`：唯一名，也是基线 JSON 的 key / 参考帧 PNG 文件名；
- `fixture`：离线夹具 PNG 文件名（省略时用 `id`）；
- `entry`：[{ "cmd": "...", "wait": 秒 }] 入口命令序列（来源见各视图注释/参考脚本）；
- `settleSeconds`：入口结束后额外等待（省略用 protocol.settleSeconds）；
- `freezeEpoch`：该视图的冻结 epoch（省略用 protocol.freezeEpoch）；
- `captureFreeze`：默认 true；false 表示入口里已 timeset 过、抓图时只发 screenshot
  （用于"render() 会丢页面内部状态"的视图：阅读/每日详情会被打回主页，课程表详情
  浮层会被关闭，Wi-Fi 键盘模式按钮文字会被重建复位）；
- `settle`：控件动画/稳定探针选项。`maxAnimations`（默认 1，只 WARN 不阻塞）、
  `stableFrames`（默认 2=启用 hash 稳定探针；1=跳过）、`intervalMs`（220）、
  `timeoutSeconds`（8）。周期性变化视图（clock-flip / wifi-list）用
  `{"stableFrames": 1}` 跳过探针；
- `mask`：`[[x,y,w,h], ...]`，在 hash / 参考帧 / diff 之前把矩形清 0。Wi-Fi 键盘
  mask 名称/密码输入框（52,34,258,26）与（52,63,258,26），屏蔽 caret blink；
- `expect`：{"page": n, "app": n} 抓图前 status 期望值（不一致 → UNSTABLE）；
- `expectNoChange`：默认 true；false 表示已批准的视觉变化（K2 键盘视图），hash 变化
  记为 EXPECTED-CHANGE 而不是 FAIL；
- `hash`：默认 true；false 表示 informational（只出图/记录 hash，hash 变化记为
  INFO-DIFF，不参与门禁）；
- `optional` + `available`：可选环境变体（如 statusbar-online 要求 wifi=true），
  环境不匹配或采集/信封失败时 SKIP+WARN；
- `distinctFrom`：要求与列出的视图 hash 不同，用于发现"入口 tap 没打开目标子页面"
  （等价于目标视图静默回退成基础页），违反 → UNSTABLE。

新增/修改视图时：先手动在设备上确认入口命令能到达目标画面，再更新 manifest，
用 `--baseline`（可加 `--only` 只采部分视图）重建该项基线，最后 `--check` 复核。
已批准视觉变化的基线更新用 `--update-only <ids>`：它要求现有基线存在，校验旧基线
环境信封仍一致，只替换指定视图，并把旧/新 hash + diff 证据写进 `baseline.updates`。

已知限制
========
1. **设备在线时的天气/阅读内容**：env 信封能发现 wifi/weatherValid 状态变化，但
   发现不了"内容刷新"（温度/文案在两次运行间变化）。建议基线/对照都在无 Wi-Fi 的
   离线夹具环境执行；若必须在线，两次运行应尽量相邻并在报告中记录风险。
2. **clock-flip**：翻页秒动画每秒触发（300 ms），原始全帧 hash 天然不稳定，视图中
   标记 `hash=false`，只作为 informational 出图；同样 `wifi-list` 的扫描结果随周边
   2.4GHz 网络变化，也只出图不设 hash 门禁。
3. **K2 键盘两视图需人工更新基线**：`wifi-keyboard` / `wifi-symbols`（以及
   `wifi-keyboard-upper`）在 K2 键盘改版后视觉会变化（符号页键位、空格宽度、聚焦
   输入框 1px 边框）。manifest 已把这三项标为 `expectNoChange=false`，`--check`
   不会因它们报 FAIL；K2 落地并烧录后由 Lead 人工确认画面，再执行
   `--update-only wifi-keyboard,wifi-keyboard-upper,wifi-symbols` 更新基线，
   工具会把旧/新 hash、diff PNG/bbox 记进 `baseline.updates` 作为证据。
4. **键盘 caret blink / animations 计数**：LVGL textarea 聚焦光标每 400ms 翻转一次，
   `status.animations` 会常驻 1，且光标相位不可控。协议因此（a）不把
   animations==0 当硬条件（只 WARN），（b）键盘视图用 `mask` 屏蔽名称/密码输入框后
   再 hash，（c）用 hash 稳定探针 + `--repeat 2` 保证整帧稳定。
5. **详情态视图必须 captureFreeze=false**：阅读/每日页的详情是模块内部 view，
   额外 render() 会把它打回主页（`reading_page.cpp:663`、`daily_page.cpp:761`）；
   saved_design 的课程表详情浮层也会被重建关闭。所以这些视图的入口里先 timeset
   再 tap，抓图时只发 screenshot。改动这些页面渲染逻辑时要同步复核该约定。
6. **schedule-detail** 是 best-effort：用 tap 77 69（第 9 周周二第 1 大节课程块）打开
   详情；若设备课表在该格没有课，`distinctFrom` 会把它判 UNSTABLE 提示人工检查。
7. **设备状态影响面**：tool 只发 page/app/tap/timeset/back/screenshot/status，不会
   保存 Wi-Fi、不会连接网络、不会改亮度/音量；但时钟预设视图会点选 clock preset，
   会把 `clockMode.preset` 写进设备 NVS（预期副作用，picker 视图入口每次都先点回
   landscape，因此不依赖它）。
8. **串口/时间**：一帧 153,600 B 的传输需要数秒（取决于 CDC 吞吐）；settle 探针
   （默认每视图多抓 1 帧）会让 32 视图基线约多 1–2 分钟。`timeset`/`screenshot`
   之间若被 >1 s 的调度卡顿打断，秒级标签会偏 1 s（协议已尽量把两者放在同一次写入；
   `--repeat 2` 可以提前发现）。
9. 本工具不判断"画面是否正确"，只判断"与基线逐字节一致"；内容/文案变化属于需人工
   评审的预期变化，必须先看 `build/device-golden/last/*.png` 再更新基线。
10. 离线自测模式不打开串口、不读设备 status：只验证发现视图/哈希/对比/报告逻辑；
    PNG 夹具是 RGB888（由设备 RGB565 展开），工具按精确逆变换回 RGB565 再应用 mask
    后 hash，与设备原始帧 hash 可逐字节比较（已按 32/64/32 级别验证往返无损）。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
import zlib
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / "hardware" / "tests" / "golden" / "device-views.json"
DEFAULT_DEVICE_BASELINE = ROOT / "hardware" / "tests" / "golden" / "device-hashes.json"
DEFAULT_DEVICE_REFS = ROOT / "build" / "device-golden" / "baseline"
DEFAULT_LAST_DIR = ROOT / "build" / "device-golden" / "last"
DEFAULT_OFFLINE_BASELINE = ROOT / "build" / "device-golden" / "offline-hashes.json"
DEFAULT_OFFLINE_REFS = ROOT / "build" / "device-golden" / "offline-baseline"

FRAME_WIDTH = 320
FRAME_HEIGHT = 240
FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT * 2
FRAME_PIXELS = FRAME_WIDTH * FRAME_HEIGHT
MIN_VALID_EPOCH = 1700000000  # firmware `timeset` rejects epochs <= this value

# Fields copied from `status` into the environment envelope stored next to the hash.
STATUS_ENVELOPE_FIELDS = [
    "page",
    "app",
    "wifi",
    "timeValid",
    "weatherValid",
    "batteryInstalled",
    "externalPower",
    "brightness",
    "volume",
    "bgr",
    "inverted",
    "transitionActive",
    "animations",
    "uptimeMs",
    "resetReason",
    "version",
    "sketchMd5",
    "weatherAt",
    "weatherError",
    "readingError",
    "apHealthy",
]

# Fields whose value may legitimately differ across boots (recorded, not compared).
BOOT_ONLY_FIELDS = {"uptimeMs"}

ENV_WAIT_FIELDS = [
    "wifi",
    "weatherValid",
    "batteryInstalled",
    "externalPower",
    "brightness",
    "volume",
    "bgr",
    "inverted",
]

RESULT_PASS = "PASS"
RESULT_FAIL = "FAIL"
RESULT_UNSTABLE = "UNSTABLE"
RESULT_EXPECTED_CHANGE = "EXPECTED-CHANGE"
RESULT_INFO_DIFF = "INFO-DIFF"
RESULT_SKIP = "SKIP"
RESULT_HASHED = "HASHED"

FAILING_RESULTS = {RESULT_FAIL, RESULT_UNSTABLE}
_MISSING = object()


# ---------------------------------------------------------------------------
# small utilities
# ---------------------------------------------------------------------------
class ToolError(RuntimeError):
    """Configuration / environment error (exit code 2)."""


def log(message: str = "") -> None:
    print(message, flush=True)


def now_iso() -> str:
    return datetime.now().astimezone().isoformat(timespec="seconds")


def display_path(path: Path) -> str:
    """Prefer a workspace-relative path for logs / JSON, fall back to absolute."""
    try:
        return str(Path(path).resolve().relative_to(ROOT))
    except ValueError:
        return str(Path(path))


def resolve_path(value: Optional[Path], default: Path) -> Path:
    if value is None:
        return default
    path = Path(value)
    return path if path.is_absolute() else (ROOT / path)


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def write_text_atomic(path: Path, text: str) -> None:
    ensure_parent(path)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text, encoding="utf-8")
    temporary.replace(path)


def write_json(path: Path, document: Any) -> None:
    write_text_atomic(path, json.dumps(document, ensure_ascii=False, indent=2) + "\n")


def sha256_hex(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def values_equal(left: Any, right: Any) -> bool:
    """Equality that keeps JSON `true` distinct from `1`."""
    if isinstance(left, bool) != isinstance(right, bool):
        return False
    return left == right


def describe(value: Any) -> str:
    if value is None:
        return "None"
    return json.dumps(value, ensure_ascii=False)


# ---------------------------------------------------------------------------
# frame <-> png conversion, hashing, diff
# ---------------------------------------------------------------------------
def rgb565_to_rgb_bytes(raw: bytes) -> bytes:
    if len(raw) != FRAME_BYTES:
        raise ToolError(f"frame must be {FRAME_BYTES} bytes, got {len(raw)}")
    rgb = bytearray(FRAME_PIXELS * 3)
    index = 0
    for offset in range(0, FRAME_BYTES, 2):
        color = raw[offset] | (raw[offset + 1] << 8)
        rgb[index] = (color >> 11) * 255 // 31
        rgb[index + 1] = ((color >> 5) & 63) * 255 // 63
        rgb[index + 2] = (color & 31) * 255 // 31
        index += 3
    return bytes(rgb)


def rgb_bytes_to_rgb565(rgb: bytes) -> bytes:
    """Exact inverse of inspect_device's RGB565->RGB expansion (rounding recovers 5/6/5)."""
    if len(rgb) != FRAME_PIXELS * 3:
        raise ToolError(f"RGB buffer must be {FRAME_PIXELS * 3} bytes, got {len(rgb)}")
    raw = bytearray(FRAME_BYTES)
    for index in range(FRAME_PIXELS):
        red = rgb[3 * index]
        green = rgb[3 * index + 1]
        blue = rgb[3 * index + 2]
        color = (
            (((red * 31 + 127) // 255) << 11)
            | (((green * 63 + 127) // 255) << 5)
            | (((blue * 31 + 127) // 255))
        )
        raw[2 * index] = color & 0xFF
        raw[2 * index + 1] = (color >> 8) & 0xFF
    return bytes(raw)


def save_frame_png(raw: bytes, path: Path) -> Path:
    from PIL import Image  # local import so --list works without Pillow

    ensure_parent(path)
    image = Image.frombytes("RGB", (FRAME_WIDTH, FRAME_HEIGHT), rgb565_to_rgb_bytes(raw))
    image.save(path)
    return path


def load_fixture_png(path: Path) -> bytes:
    from PIL import Image

    try:
        image = Image.open(path)
    except OSError as error:
        raise ToolError(f"cannot open fixture PNG {display_path(path)}: {error}")
    with image:
        image = image.convert("RGB")
        if image.size != (FRAME_WIDTH, FRAME_HEIGHT):
            raise ToolError(
                f"fixture PNG {display_path(path)} is {image.size[0]}x{image.size[1]}, "
                f"expected {FRAME_WIDTH}x{FRAME_HEIGHT}"
            )
        return rgb_bytes_to_rgb565(image.tobytes())


def apply_frame_mask(raw: bytes, mask: List[List[int]]) -> bytes:
    """Zero the masked rectangles (RGB565 black) before hashing / saving / diffing.

    Used for the Wi-Fi keyboard input rows: the LVGL textarea caret blinks with an
    uncontrollable phase, so its pixels can never be part of a byte-stable frame.
    The mask is applied to both the baseline and every check frame, so the diff PNG
    also shows black in those rectangles.
    """
    if not mask:
        return raw
    if len(raw) != FRAME_BYTES:
        raise ToolError(f"frame must be {FRAME_BYTES} bytes, got {len(raw)}")
    masked = bytearray(raw)
    for x, y, width, height in mask:
        for row in range(y, y + height):
            start = 2 * (row * FRAME_WIDTH + x)
            masked[start:start + 2 * width] = b"\x00" * (2 * width)
    return bytes(masked)


def diff_frames(reference: bytes, current: bytes) -> Dict[str, Any]:
    if len(reference) != len(current):
        return {"diffPx": None, "bbox": None, "lengths": [len(reference), len(current)]}
    diff = 0
    first: Optional[List[int]] = None
    last: Optional[List[int]] = None
    min_x = min_y = 10 ** 9
    max_x = max_y = -1
    for offset in range(0, len(reference), 2):
        if reference[offset:offset + 2] != current[offset:offset + 2]:
            diff += 1
            pixel = offset // 2
            x = pixel % FRAME_WIDTH
            y = pixel // FRAME_WIDTH
            if first is None:
                first = [x, y]
            last = [x, y]
            min_x, max_x = min(min_x, x), max(max_x, x)
            min_y, max_y = min(min_y, y), max(max_y, y)
    return {
        "diffPx": diff,
        "ratio": round(diff / FRAME_PIXELS, 6),
        "bbox": None if first is None else [min_x, min_y, max_x, max_y],
        "first": first,
        "last": last,
    }


def save_diff_png(reference_png: Path, current_raw: bytes, out_path: Path, bbox: Optional[List[int]]) -> Path:
    from PIL import Image, ImageDraw

    reference = Image.open(reference_png).convert("RGB")
    if reference.size != (FRAME_WIDTH, FRAME_HEIGHT):
        raise ToolError(f"reference frame {display_path(reference_png)} is not {FRAME_WIDTH}x{FRAME_HEIGHT}")
    current = Image.frombytes("RGB", (FRAME_WIDTH, FRAME_HEIGHT), rgb565_to_rgb_bytes(current_raw))
    header = 16
    gap = 12
    canvas = Image.new("RGB", (FRAME_WIDTH * 2 + gap, FRAME_HEIGHT + header), (24, 24, 24))
    canvas.paste(reference, (0, header))
    canvas.paste(current, (FRAME_WIDTH + gap, header))
    draw = ImageDraw.Draw(canvas)
    draw.text((4, 3), "baseline reference", fill=(235, 235, 235))
    draw.text((FRAME_WIDTH + gap + 4, 3), "current device frame", fill=(235, 235, 235))
    if bbox:
        x0, y0, x1, y1 = bbox
        for offset in (0, FRAME_WIDTH + gap):
            draw.rectangle(
                [offset + x0 - 2, header + y0 - 2, offset + x1 + 2, header + y1 + 2],
                outline=(255, 60, 60),
                width=2,
            )
    ensure_parent(out_path)
    canvas.save(out_path)
    return out_path


# ---------------------------------------------------------------------------
# view manifest
# ---------------------------------------------------------------------------
@dataclass
class EntryStep:
    cmd: str
    wait: float


@dataclass
class View:
    id: str
    description: str = ""
    entry: List[EntryStep] = field(default_factory=list)
    settle_seconds: Optional[float] = None
    freeze_epoch: int = 0
    capture_freeze: bool = True
    settle: Dict[str, Any] = field(default_factory=dict)
    expect: Dict[str, Any] = field(default_factory=dict)
    expect_no_change: bool = True
    hash_gated: bool = True
    optional: bool = False
    available: Dict[str, Any] = field(default_factory=dict)
    distinct_from: List[str] = field(default_factory=list)
    mask: List[List[int]] = field(default_factory=list)
    fixture: str = ""
    envelope: Dict[str, Any] = field(default_factory=dict)
    note: str = ""

    @property
    def fixture_name(self) -> str:
        return self.fixture or self.id


@dataclass
class Manifest:
    path: Path
    doc: Dict[str, Any]
    protocol: Dict[str, Any]
    envelope: Dict[str, Any]
    views: List[View]

    @property
    def view_map(self) -> Dict[str, View]:
        return {view.id: view for view in self.views}


def _require(condition: Any, message: str) -> None:
    if not condition:
        raise ToolError(message)


def _normalize_entry(view_id: str, raw_entry: Any, default_wait: float) -> List[EntryStep]:
    _require(isinstance(raw_entry, list), f"view '{view_id}': 'entry' must be a list")
    steps: List[EntryStep] = []
    for item in raw_entry:
        if isinstance(item, str):
            command = item.strip()
            _require(command, f"view '{view_id}': entry command must not be empty")
            steps.append(EntryStep(command, default_wait))
        elif isinstance(item, dict):
            command = item.get("cmd")
            _require(
                isinstance(command, str) and command.strip(),
                f"view '{view_id}': entry item needs a non-empty 'cmd'",
            )
            wait = item.get("wait", default_wait)
            _require(_is_number(wait) and float(wait) >= 0,
                     f"view '{view_id}': entry wait must be a non-negative number")
            steps.append(EntryStep(command.strip(), float(wait)))
        else:
            raise ToolError(f"view '{view_id}': entry item must be a string or {{'cmd','wait'}}")
    _require(steps, f"view '{view_id}': 'entry' must contain at least one command")
    return steps


def _normalize_settle_defaults(raw: Any) -> Dict[str, Any]:
    """Defaults for the settle/animations/hash-stability probe."""
    defaults: Dict[str, Any] = {
        "maxAnimations": 1,
        "stableFrames": 2,
        "intervalMs": 220,
        "timeoutSeconds": 8.0,
    }
    if raw is None:
        return defaults
    _require(isinstance(raw, dict), "protocol.settleDefaults must be an object")
    for key, minimum in (("maxAnimations", 0), ("stableFrames", 1), ("intervalMs", 60), ("timeoutSeconds", 1)):
        if key in raw:
            value = raw[key]
            _require(_is_number(value) and value >= minimum,
                     f"protocol.settleDefaults.{key} must be a number >= {minimum}")
            defaults[key] = value
    return defaults


def _normalize_settle(view_id: str, raw: Any, defaults: Dict[str, Any]) -> Dict[str, Any]:
    _require(isinstance(raw, dict), f"view '{view_id}': 'settle' must be an object")
    result: Dict[str, Any] = {}
    for key, minimum in (("maxAnimations", 0), ("stableFrames", 1), ("intervalMs", 60), ("timeoutSeconds", 1)):
        if key in raw:
            value = raw[key]
            _require(_is_number(value) and value >= minimum,
                     f"view '{view_id}': settle.{key} must be a number >= {minimum}")
            result[key] = value
    known = {"maxAnimations", "stableFrames", "intervalMs", "timeoutSeconds"}
    unknown = sorted(set(raw) - known)
    _require(not unknown, f"view '{view_id}': unknown settle option(s): {', '.join(unknown)}")
    return result


def _normalize_mask(view_id: str, raw: Any) -> List[List[int]]:
    _require(isinstance(raw, list), f"view '{view_id}': 'mask' must be a list of [x,y,w,h] rects")
    rects: List[List[int]] = []
    for rect in raw:
        _require(isinstance(rect, list) and len(rect) == 4 and all(_is_number(value) for value in rect),
                 f"view '{view_id}': mask rect must be [x, y, w, h] numbers")
        x, y, w, h = (int(value) for value in rect)
        _require(
            0 <= x < FRAME_WIDTH and 0 <= y < FRAME_HEIGHT and w > 0 and h > 0
            and x + w <= FRAME_WIDTH and y + h <= FRAME_HEIGHT,
            f"view '{view_id}': mask rect [{x},{y},{w},{h}] is outside the {FRAME_WIDTH}x{FRAME_HEIGHT} frame",
        )
        rects.append([x, y, w, h])
    return rects


def load_manifest(path: Path) -> Manifest:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise ToolError(f"view manifest not found: {path}")
    except json.JSONDecodeError as error:
        raise ToolError(f"view manifest is not valid JSON: {path}: {error}")
    _require(isinstance(document, dict), "manifest root must be a JSON object")
    _require(document.get("schema") == 1, "manifest schema must be 1")

    frame = document.get("frame") or {}
    _require(
        int(frame.get("width", 0)) == FRAME_WIDTH
        and int(frame.get("height", 0)) == FRAME_HEIGHT
        and int(frame.get("bytes", 0)) == FRAME_BYTES,
        f"manifest frame must be {FRAME_WIDTH}x{FRAME_HEIGHT} RGB565 ({FRAME_BYTES} B)",
    )

    protocol = document.get("protocol") or {}
    _require(isinstance(protocol, dict) and protocol, "manifest 'protocol' must be a non-empty object")
    default_wait = float(protocol.get("entryDefaultWait", 0.9))
    _require(default_wait >= 0, "protocol.entryDefaultWait must be non-negative")
    default_epoch = protocol.get("freezeEpoch")
    _require(_is_number(default_epoch) and int(default_epoch) > MIN_VALID_EPOCH,
             f"protocol.freezeEpoch must be an integer > {MIN_VALID_EPOCH}")
    settle_defaults = _normalize_settle_defaults(protocol.get("settleDefaults"))

    envelope = document.get("envelope") or {}
    _require(isinstance(envelope, dict) and envelope, "manifest 'envelope' must be a non-empty object")

    raw_views = document.get("views")
    _require(isinstance(raw_views, list) and raw_views, "manifest 'views' must be a non-empty list")

    views: List[View] = []
    seen: set = set()
    for raw in raw_views:
        _require(isinstance(raw, dict), "each manifest view must be an object")
        view_id = raw.get("id")
        _require(isinstance(view_id, str) and view_id.strip(), "each view needs a non-empty string 'id'")
        view_id = view_id.strip()
        _require(view_id not in seen, f"duplicate view id: {view_id}")
        seen.add(view_id)

        epoch = raw.get("freezeEpoch", int(default_epoch))
        _require(
            _is_number(epoch) and int(epoch) > MIN_VALID_EPOCH,
            f"view '{view_id}': freezeEpoch must be an integer > {MIN_VALID_EPOCH}",
        )

        expect_raw = raw.get("expect") or {}
        _require(isinstance(expect_raw, dict), f"view '{view_id}': 'expect' must be an object")
        expect: Dict[str, Any] = {}
        for key in ("page", "app"):
            if key in expect_raw:
                _require(_is_number(expect_raw[key]), f"view '{view_id}': expect.{key} must be a number")
                expect[key] = int(expect_raw[key])
        for key, value in expect_raw.items():
            if key not in expect:
                expect[key] = value

        available = raw.get("available") or {}
        _require(isinstance(available, dict), f"view '{view_id}': 'available' must be an object")
        for key in available:
            _require(key in STATUS_ENVELOPE_FIELDS,
                     f"view '{view_id}': available.{key} is not a status field")

        distinct = raw.get("distinctFrom") or []
        _require(isinstance(distinct, list) and all(isinstance(item, str) for item in distinct),
                 f"view '{view_id}': 'distinctFrom' must be a list of view ids")

        entry = _normalize_entry(view_id, raw.get("entry"), default_wait)
        capture_freeze = bool(raw.get("captureFreeze", True))
        if not capture_freeze:
            _require(
                any(step.cmd.startswith("timeset ") for step in entry),
                f"view '{view_id}': captureFreeze=false requires a 'timeset <epoch>' step in 'entry'",
            )
        settle = dict(settle_defaults)
        settle.update(_normalize_settle(view_id, raw.get("settle") or {}, settle_defaults))
        mask = _normalize_mask(view_id, raw.get("mask") or [])

        views.append(
            View(
                id=view_id,
                description=str(raw.get("description", "")),
                entry=entry,
                settle_seconds=(
                    None if raw.get("settleSeconds") is None else float(raw["settleSeconds"])
                ),
                freeze_epoch=int(epoch),
                capture_freeze=capture_freeze,
                settle=settle,
                expect=expect,
                expect_no_change=bool(raw.get("expectNoChange", True)),
                hash_gated=bool(raw.get("hash", True)),
                optional=bool(raw.get("optional", False)),
                available=dict(available),
                distinct_from=list(distinct),
                mask=mask,
                fixture=str(raw.get("fixture", "") or ""),
                envelope=dict(raw.get("envelope") or {}),
                note=str(raw.get("note", "")),
            )
        )

    for view in views:
        for reference in view.distinct_from:
            _require(reference in seen, f"view '{view.id}': distinctFrom references unknown view '{reference}'")
            _require(reference != view.id, f"view '{view.id}': distinctFrom must not reference itself")

    return Manifest(path=path, doc=document, protocol=protocol, envelope=envelope, views=views)


def select_views(manifest: Manifest, only: str) -> List[View]:
    if not only.strip():
        return list(manifest.views)
    wanted = [item.strip() for item in only.split(",") if item.strip()]
    if not wanted:
        raise ToolError("--only was given but no view ids were parsed")
    known = manifest.view_map
    unknown = [item for item in wanted if item not in known]
    if unknown:
        raise ToolError(f"--only references unknown view id(s): {', '.join(unknown)}")
    wanted_set = set(wanted)
    selected = [view for view in manifest.views if view.id in wanted_set]
    missing = wanted_set - {view.id for view in selected}
    if missing:
        raise ToolError(f"--only view id(s) missing from manifest: {', '.join(sorted(missing))}")
    return selected


def merge_envelope_expectations(manifest: Manifest, view: View) -> Dict[str, Any]:
    merged = dict(manifest.envelope)
    merged.update(view.envelope)
    return merged


# ---------------------------------------------------------------------------
# environment envelope
# ---------------------------------------------------------------------------
def envelope_snapshot(status: Dict[str, Any]) -> Dict[str, Any]:
    snapshot: Dict[str, Any] = {}
    for name in STATUS_ENVELOPE_FIELDS:
        if name in status:
            snapshot[name] = status[name]
    return snapshot


def evaluate_envelope(
    view: View,
    expectations: Dict[str, Any],
    baseline_env: Optional[Dict[str, Any]],
    pre: Dict[str, Any],
    post: Dict[str, Any],
) -> List[str]:
    problems: List[str] = []

    def compare(label: str, snapshot: Dict[str, Any], field_name: str, expected: Any) -> None:
        got = snapshot.get(field_name)
        if not values_equal(got, expected):
            problems.append(f"{label}.{field_name}={describe(got)} != {describe(expected)}")

    for field_name, rule in expectations.items():
        if rule == "record":
            continue
        if rule == "expect":
            if field_name not in view.expect:
                continue
            for label, snapshot in (("pre", pre), ("post", post)):
                compare(label, snapshot, field_name, view.expect[field_name])
            continue
        if rule == "same":
            if baseline_env is None:
                continue
            if field_name not in baseline_env:
                problems.append(f"baseline envelope lacks '{field_name}'")
                continue
            expected = baseline_env[field_name]
            for label, snapshot in (("pre", pre), ("post", post)):
                compare(label, snapshot, field_name, expected)
            continue
        if rule == "monotonic":
            before, after = pre.get(field_name), post.get(field_name)
            if not (_is_number(before) and _is_number(after) and after >= before):
                problems.append(f"{field_name} not monotonic: pre={describe(before)} post={describe(after)}")
            continue
        if rule == "stable":
            if not values_equal(pre.get(field_name), post.get(field_name)):
                problems.append(
                    f"{field_name} changed during capture: pre={describe(pre.get(field_name))} "
                    f"post={describe(post.get(field_name))}"
                )
            continue
        # exact scalar (true / false / number / string)
        for label, snapshot in (("pre", pre), ("post", post)):
            compare(label, snapshot, field_name, rule)

    # page/app expectations are always enforced, even if not listed in envelope
    for field_name in ("page", "app"):
        if field_name not in view.expect:
            continue
        for label, snapshot in (("pre", pre), ("post", post)):
            compare(label, snapshot, field_name, view.expect[field_name])

    return _dedupe(problems)


def _dedupe(items: List[str]) -> List[str]:
    seen = set()
    ordered: List[str] = []
    for item in items:
        if item not in seen:
            seen.add(item)
            ordered.append(item)
    return ordered


def environment_satisfies(view: View, status: Dict[str, Any]) -> Tuple[bool, str]:
    for field_name, required in view.available.items():
        got = status.get(field_name)
        if not values_equal(got, required):
            return False, f"{field_name}={describe(got)} != required {describe(required)}"
    return True, ""


@dataclass
class RunState:
    sketch_md5: Any = _MISSING
    reset_reason: Any = _MISSING
    last_uptime: Optional[int] = None

    def absorb(self, view_id: str, pre: Dict[str, Any], post: Dict[str, Any]) -> List[str]:
        problems: List[str] = []
        sketch = pre.get("sketchMd5")
        if sketch not in (None, ""):
            if self.sketch_md5 is _MISSING:
                self.sketch_md5 = sketch
            elif self.sketch_md5 != sketch:
                problems.append(
                    f"{view_id}: status.sketchMd5 changed mid-run "
                    f"({str(self.sketch_md5)[:12]} -> {str(sketch)[:12]})"
                )
        reset_reason = pre.get("resetReason")
        if self.reset_reason is _MISSING:
            self.reset_reason = reset_reason
        elif self.reset_reason != reset_reason:
            problems.append(
                f"{view_id}: status.resetReason changed mid-run "
                f"({describe(self.reset_reason)} -> {describe(reset_reason)})"
            )
        before = pre.get("uptimeMs")
        after = post.get("uptimeMs")
        if _is_number(self.last_uptime) and _is_number(before) and before < self.last_uptime:
            problems.append(
                f"{view_id}: device uptime went backwards ({before} < {self.last_uptime}); device rebooted"
            )
        if _is_number(after):
            self.last_uptime = int(after)
        elif _is_number(before):
            self.last_uptime = int(before)
        return problems


# ---------------------------------------------------------------------------
# device serial session (mirrors hardware/scripts/inspect_device.py)
# ---------------------------------------------------------------------------
def load_inspect_device():
    scripts_dir = ROOT / "hardware" / "scripts"
    if str(scripts_dir) not in sys.path:
        sys.path.insert(0, str(scripts_dir))
    try:
        import inspect_device  # type: ignore  # noqa: E402
    except Exception as error:  # pragma: no cover - depends on host environment
        raise ToolError(
            "device mode requires hardware/scripts/inspect_device.py and pyserial: " + str(error)
        )
    return inspect_device


@dataclass
class CaptureOutcome:
    raw: bytes
    frames: List[bytes]
    pre: Dict[str, Any]
    post: Dict[str, Any]
    status_pre: Dict[str, Any]
    status_post: Dict[str, Any]
    sha: str
    warnings: List[str] = field(default_factory=list)


class DeviceSession:
    def __init__(self, port_name: str):
        self.port_name = port_name
        self.dev = load_inspect_device()
        self.port = None
        self.boot_status: Dict[str, Any] = {}

    def __enter__(self) -> "DeviceSession":
        try:
            self.port = self.dev.open_device(self.port_name)
        except Exception as error:
            raise ToolError(f"cannot open serial port {self.port_name!r}: {error}")
        try:
            time.sleep(0.3)
            self.port.reset_input_buffer()
            self.dev.warmup(self.port)
            self.boot_status = self.read_status()
        except Exception:
            self.close()
            raise
        return self

    def __exit__(self, *_exc: Any) -> None:
        self.close()

    def close(self) -> None:
        if self.port is not None:
            try:
                self.port.close()
            except Exception:
                pass
            self.port = None

    def send(self, command: str, wait: float = 0.0) -> None:
        self.dev.command(self.port, command)
        if wait > 0:
            time.sleep(wait)

    def read_status(self) -> Dict[str, Any]:
        last_error: Optional[Exception] = None
        for attempt in range(4):
            try:
                return self.dev.read_status(self.port)
            except Exception as error:  # noqa: BLE001 - a dropped serial byte is expected
                last_error = error
                if "restarted" in str(error):
                    raise
                if attempt >= 3:
                    break
                time.sleep(0.25)
                try:
                    self.port.reset_input_buffer()
                except Exception:
                    pass
        if last_error is not None:
            raise last_error
        raise RuntimeError("no device status response")

    def capture_frame(self, freeze_epoch: Optional[int], attempts: int) -> bytes:
        last_error: Optional[Exception] = None
        for _attempt in range(max(1, attempts)):
            try:
                return self._capture_once(freeze_epoch)
            except RuntimeError as error:
                last_error = error
                try:
                    self.port.reset_input_buffer()
                except Exception:
                    pass
                time.sleep(0.4)
        if isinstance(last_error, RuntimeError):
            raise last_error
        raise RuntimeError("frame capture failed")

    def _capture_once(self, freeze_epoch: Optional[int]) -> bytes:
        # Sending timeset and screenshot in one write makes the firmware render and
        # copy the framebuffer in the same loop iteration, freezing second-level
        # clock labels on the epoch.
        if freeze_epoch is not None:
            payload = f"timeset {int(freeze_epoch)}\nscreenshot\n".encode()
        else:
            payload = b"screenshot\n"
        self.port.write(payload)
        self.port.flush()
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            line = self.port.readline()
            if not line:
                continue
            if line.startswith(b"FRAME "):
                fields = line.split()
                if len(fields) != 5:
                    raise RuntimeError(f"firmware must support CRC-checked frames: {line!r}")
                try:
                    width = int(fields[1])
                    height = int(fields[2])
                    size = int(fields[3])
                    checksum = int(fields[4], 16)
                except ValueError:
                    raise RuntimeError(f"malformed FRAME header: {line!r}")
                if (width, height, size) != (FRAME_WIDTH, FRAME_HEIGHT, FRAME_BYTES):
                    raise RuntimeError(f"unexpected frame dimensions: {line!r}")
                data = self.dev.read_exact(self.port, size)
                terminator = b""
                for _ in range(3):
                    terminator = self.port.readline().strip()
                    if terminator:
                        break
                if terminator != b"END":
                    raise RuntimeError(f"invalid frame terminator: {terminator!r}")
                if zlib.crc32(data) != checksum:
                    raise RuntimeError("frame CRC mismatch; refusing to hash corrupt pixels")
                return bytes(data)
            if b"FRAME FAILED" in line:
                raise RuntimeError("device reported FRAME FAILED (display snapshot buffer unavailable)")
            if b"Guru Meditation" in line or b"YIYI DESK boot" in line:
                raise RuntimeError(f"device restarted during capture: {line!r}")
        raise RuntimeError("device did not return a framebuffer within 15 s")


def wait_view_stable(
    session: DeviceSession,
    manifest: Manifest,
    view: View,
) -> Tuple[Dict[str, Any], List[str]]:
    """Wait for a settled frame without hard-blocking on `animations`.

    The device may report a permanent `animations=1` (e.g. the LVGL textarea caret
    blink). Only `transitionActive` is a hard condition. For views that ask for a
    hash-stability probe (`settle.stableFrames >= 2`, the default) we capture probe
    frames (mask applied) and require consecutive identical digests. Probe timeout
    is a WARN, never a fatal error: `--repeat 2` remains the final stability gate.
    """
    settle = view.settle or {}
    timeout = float(settle.get("timeoutSeconds", manifest.protocol.get("settleTimeoutSeconds", 8.0)))
    max_animations = int(settle.get("maxAnimations", 1))
    stable_frames = int(settle.get("stableFrames", 2))
    interval = max(0.05, float(settle.get("intervalMs", 220)) / 1000.0)
    attempts = int(manifest.protocol.get("captureAttempts", 3))
    capture_epoch: Optional[int] = view.freeze_epoch if view.capture_freeze else None
    warnings: List[str] = []

    deadline = time.monotonic() + timeout
    last: Dict[str, Any] = {}
    last_digest: Optional[str] = None
    equal_run = 0
    animations_seen = 0
    while time.monotonic() < deadline:
        last = session.read_status()
        if last.get("transitionActive"):
            time.sleep(0.1)
            continue
        animations = last.get("animations")
        if _is_number(animations):
            animations_seen = max(animations_seen, int(animations))
        if stable_frames <= 1:
            if animations_seen > max_animations:
                warnings.append(
                    f"settle: animations={animations_seen} > maxAnimations={max_animations}; "
                    "captured without a hash-stability probe (view declares stableFrames<=1)"
                )
            return last, warnings
        raw = apply_frame_mask(session.capture_frame(capture_epoch, attempts), view.mask)
        digest = sha256_hex(raw)
        if digest == last_digest:
            equal_run += 1
        else:
            equal_run = 1
            last_digest = digest
        if equal_run >= stable_frames:
            if animations_seen > max_animations:
                warnings.append(
                    f"settle: animations={animations_seen} > maxAnimations={max_animations} "
                    f"(accepted: {stable_frames} consecutive identical frames)"
                )
            return last, warnings
        time.sleep(interval)

    warnings.append(
        f"settle: no {stable_frames} consecutive identical frames within {timeout:g}s "
        f"(transitionActive={last.get('transitionActive')!r}, animations={last.get('animations')!r}); "
        "captured anyway — re-run with --repeat 2 to judge stability"
    )
    return last, warnings


def capture_view_device(
    session: DeviceSession,
    manifest: Manifest,
    view: View,
    repeat: int,
) -> CaptureOutcome:
    for step in view.entry:
        session.send(step.cmd, step.wait)

    settle = (
        float(view.settle_seconds)
        if view.settle_seconds is not None
        else float(manifest.protocol.get("settleSeconds", 0.6))
    )
    if settle > 0:
        time.sleep(settle)

    freeze_settle = float(manifest.protocol.get("freezeSettleSeconds", 0.6))
    if view.capture_freeze:
        session.send(f"timeset {view.freeze_epoch}", freeze_settle)

    status_pre, settle_warnings = wait_view_stable(session, manifest, view)
    # Fresh "before capture" envelope read (the spec requires one read before the
    # frame and one after it; the settle loop's last read may be a probe old).
    status_pre = session.read_status()
    if status_pre.get("transitionActive"):
        status_pre, extra = wait_view_stable(session, manifest, view)
        settle_warnings.extend(extra)
        status_pre = session.read_status()
    pre = envelope_snapshot(status_pre)

    capture_epoch: Optional[int] = view.freeze_epoch if view.capture_freeze else None
    attempts = int(manifest.protocol.get("captureAttempts", 3))
    frame_count = repeat if (view.hash_gated and repeat > 1) else 1
    frames: List[bytes] = []
    for index in range(frame_count):
        raw = session.capture_frame(capture_epoch, attempts)
        frames.append(apply_frame_mask(raw, view.mask))
        if index + 1 < frame_count:
            time.sleep(0.2)

    status_post = session.read_status()
    post = envelope_snapshot(status_post)
    return CaptureOutcome(
        raw=frames[0],
        frames=frames,
        pre=pre,
        post=post,
        status_pre=status_pre,
        status_post=status_post,
        sha=sha256_hex(frames[0]),
    )


# ---------------------------------------------------------------------------
# results / reports / baseline documents
# ---------------------------------------------------------------------------
@dataclass
class ViewResult:
    view_id: str
    result: str
    reasons: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)
    sha: str = ""
    baseline_sha: str = ""
    bytes: int = FRAME_BYTES
    envelope: Optional[Dict[str, Any]] = None
    frame: str = ""
    diff: Optional[Dict[str, Any]] = None
    diff_png: str = ""


def _repeat_problem(outcome: CaptureOutcome) -> Optional[str]:
    if len(outcome.frames) <= 1:
        return None
    digests = [sha256_hex(frame) for frame in outcome.frames]
    if len(set(digests)) > 1:
        short = ", ".join(digest[:8] for digest in digests)
        return f"repeat={len(digests)} frames are not byte-identical ({short}); freeze protocol unstable"
    return None


def save_failure_artifacts(
    view: View,
    outcome: CaptureOutcome,
    refs_dir: Path,
    last_dir: Path,
) -> ViewResult:
    artifacts = ViewResult(view_id=view.id, result=RESULT_FAIL, sha=outcome.sha)
    frame_path = last_dir / f"{view.id}.png"
    save_frame_png(outcome.raw, frame_path)
    artifacts.frame = display_path(frame_path)
    reference_png = refs_dir / f"{view.id}.png"
    if reference_png.exists():
        try:
            reference_raw = load_fixture_png(reference_png)
            diff = diff_frames(reference_raw, outcome.raw)
            artifacts.diff = diff
            if diff.get("bbox") is not None:
                diff_path = last_dir / f"{view.id}-diff.png"
                save_diff_png(reference_png, outcome.raw, diff_path, diff.get("bbox"))
                artifacts.diff_png = display_path(diff_path)
            elif diff.get("diffPx") == 0:
                artifacts.warnings.append(
                    "reference pixels are identical but the recorded hash differs; "
                    "the baseline entry may be stale/corrupt"
                )
        except ToolError as error:
            artifacts.warnings.append(f"reference diff skipped: {error}")
    else:
        artifacts.warnings.append(
            f"no local reference frame at {display_path(reference_png)}; diff PNG not generated"
        )
    return artifacts


def validate_distinct(views: List[View], sha_by_id: Dict[str, str]) -> Dict[str, List[str]]:
    issues: Dict[str, List[str]] = {}
    for view in views:
        digest = sha_by_id.get(view.id)
        if not digest:
            continue
        for reference in view.distinct_from:
            reference_digest = sha_by_id.get(reference)
            if not reference_digest:
                continue
            if reference_digest == digest:
                issues.setdefault(view.id, []).append(
                    f"frame hash identical to '{reference}' (entry probably did not open the expected sub-view)"
                )
    return issues


def result_counts(results: List[ViewResult]) -> Dict[str, int]:
    counts: Dict[str, int] = {}
    for result in results:
        counts[result.result] = counts.get(result.result, 0) + 1
    return counts


def print_results(results: List[ViewResult]) -> None:
    order = {
        RESULT_FAIL: 0,
        RESULT_UNSTABLE: 1,
        RESULT_SKIP: 2,
        RESULT_EXPECTED_CHANGE: 3,
        RESULT_INFO_DIFF: 4,
        RESULT_PASS: 5,
        RESULT_HASHED: 6,
    }
    for result in sorted(results, key=lambda item: (order.get(item.result, 9), item.view_id)):
        if result.result in (RESULT_PASS, RESULT_HASHED):
            detail = f"sha256={result.sha[:12]}" if result.sha else ""
        else:
            detail = "; ".join(result.reasons)
        log(f"{result.result:<16} {result.view_id:<26} {detail}")
        for warning in result.warnings:
            log(f"{'':16} {'':26} WARN: {warning}")
    counts = result_counts(results)
    summary = ", ".join(f"{name}={counts[name]}" for name in sorted(counts))
    log(f"SUMMARY: {summary}")


def view_result_to_dict(result: ViewResult) -> Dict[str, Any]:
    document: Dict[str, Any] = {
        "result": result.result,
        "sha256": result.sha or None,
        "baselineSha256": result.baseline_sha or None,
        # Always present, even for PASS/SKIP: the device report is used as evidence.
        "reasons": list(result.reasons),
        "warnings": list(result.warnings),
    }
    if result.frame:
        document["frame"] = result.frame
    if result.diff is not None:
        document["diff"] = result.diff
    if result.diff_png:
        document["diffPng"] = result.diff_png
    return document


def build_report(
    mode: str,
    source: str,
    manifest: Manifest,
    results: List[ViewResult],
    extra: Dict[str, Any],
) -> Dict[str, Any]:
    counts = result_counts(results)
    failed = [result.view_id for result in results if result.result == RESULT_FAIL]
    unstable = [result.view_id for result in results if result.result == RESULT_UNSTABLE]
    report: Dict[str, Any] = {
        "schema": 1,
        "tool": "hardware/scripts/check_device_golden.py",
        "mode": mode,
        "source": source,
        "generatedAt": now_iso(),
        "manifest": display_path(manifest.path),
        "viewCount": len(results),
        "counts": counts,
        "failed": failed,
        "unstable": unstable,
        # Reasons are duplicated at the top level so a result is never reason-less.
        "unstableReasons": {
            result.view_id: list(result.reasons)
            for result in results
            if result.result in FAILING_RESULTS
        },
        "passed": not failed and not unstable,
        "views": {result.view_id: view_result_to_dict(result) for result in results},
    }
    report.update(extra)
    return report


def write_report(last_dir: Path, report: Dict[str, Any], name: str = "report.json") -> Path:
    path = last_dir / name
    write_json(path, report)
    return path


def load_baseline_document(path: Path) -> Dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise ToolError(f"baseline not found: {display_path(path)}")
    except json.JSONDecodeError as error:
        raise ToolError(f"baseline is not valid JSON: {display_path(path)}: {error}")
    _require(isinstance(document, dict), f"baseline root must be an object: {display_path(path)}")
    _require(document.get("schema") == 1, f"baseline schema must be 1: {display_path(path)}")
    views = document.get("views")
    _require(isinstance(views, dict) and views, f"baseline has no 'views' object: {display_path(path)}")
    return document


def normalize_baseline_entry(view: View, raw_entry: Any) -> Dict[str, Any]:
    _require(isinstance(raw_entry, dict), f"baseline entry for '{view.id}' must be an object")
    digest = raw_entry.get("sha256")
    _require(
        isinstance(digest, str) and len(digest) == 64,
        f"baseline entry for '{view.id}' has no valid sha256",
    )
    size = raw_entry.get("bytes", FRAME_BYTES)
    _require(
        _is_number(size) and int(size) == FRAME_BYTES,
        f"baseline entry for '{view.id}' has bytes={describe(size)}; expected {FRAME_BYTES}",
    )
    envelope = raw_entry.get("envelope")
    _require(isinstance(envelope, dict), f"baseline entry for '{view.id}' has no envelope object")
    normalized = dict(raw_entry)
    normalized["sha256"] = digest.lower()
    normalized["bytes"] = int(size)
    normalized["envelope"] = envelope
    return normalized


def build_baseline_entry(
    view: View,
    outcome: CaptureOutcome,
    reference_png: Path,
) -> Dict[str, Any]:
    entry: Dict[str, Any] = {
        "sha256": outcome.sha,
        "bytes": FRAME_BYTES,
        "expectNoChange": view.expect_no_change,
        "hash": view.hash_gated,
        "freezeEpoch": view.freeze_epoch,
        "expect": dict(view.expect),
        "envelope": {"pre": dict(outcome.pre), "post": dict(outcome.post)},
        "referencePng": display_path(reference_png),
        "capturedAt": now_iso(),
        "fixture": view.fixture_name,
    }
    if view.note:
        entry["note"] = view.note
    return entry


def build_baseline_document(
    manifest: Manifest,
    entries: Dict[str, Dict[str, Any]],
    metadata: Dict[str, Any],
) -> Dict[str, Any]:
    return {"schema": 1, "baseline": metadata, "views": entries}


def baseline_environment(baseline_views: Dict[str, Any]) -> Dict[str, Any]:
    for raw_entry in baseline_views.values():
        if not isinstance(raw_entry, dict):
            continue
        envelope = raw_entry.get("envelope") or {}
        pre = envelope.get("pre") or {}
        if pre:
            return dict(pre)
    return {}


def _device_metadata(boot_status: Dict[str, Any], port: str, repeat: int, manifest: Manifest) -> Dict[str, Any]:
    return {
        "createdAt": now_iso(),
        "tool": "hardware/scripts/check_device_golden.py",
        "manifest": display_path(manifest.path),
        "source": "device",
        "port": port,
        "repeat": repeat,
        "frame": {
            "width": FRAME_WIDTH,
            "height": FRAME_HEIGHT,
            "bytes": FRAME_BYTES,
            "pixelFormat": "RGB565 little-endian",
        },
        "device": {
            "version": boot_status.get("version"),
            "buildFlavor": boot_status.get("buildFlavor"),
            "sketchMd5": boot_status.get("sketchMd5"),
            "resetReason": boot_status.get("resetReason"),
            "flash": boot_status.get("flash"),
            "psram": boot_status.get("psram"),
        },
        "environment": envelope_snapshot(boot_status),
    }


def _print_device_header(boot_status: Dict[str, Any], port: str) -> None:
    log(
        "device: port={port} version={version} sketchMd5={sketch} resetReason={reset}".format(
            port=port,
            version=boot_status.get("version"),
            sketch=("missing" if boot_status.get("sketchMd5") in (None, "") else str(boot_status.get("sketchMd5"))[:12]),
            reset=boot_status.get("resetReason"),
        )
    )
    log(
        "environment: wifi={wifi} timeValid={time} weatherValid={weather} batteryInstalled={battery} "
        "externalPower={power} brightness={brightness} volume={volume} bgr={bgr} inverted={inverted}".format(
            wifi=boot_status.get("wifi"),
            time=boot_status.get("timeValid"),
            weather=boot_status.get("weatherValid"),
            battery=boot_status.get("batteryInstalled"),
            power=boot_status.get("externalPower"),
            brightness=boot_status.get("brightness"),
            volume=boot_status.get("volume"),
            bgr=boot_status.get("bgr"),
            inverted=boot_status.get("inverted"),
        )
    )
    if boot_status.get("sketchMd5") in (None, ""):
        log("WARN: status 里没有 sketchMd5（固件早于 V2 Phase 0？无法核对跑的是哪个 bin）")


def _wait_for_environment(
    session: DeviceSession,
    expected: Dict[str, Any],
    timeout_seconds: float,
) -> None:
    if timeout_seconds <= 0 or not expected:
        return
    wanted = {name: expected[name] for name in ENV_WAIT_FIELDS if name in expected}
    if not wanted:
        return
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        status = session.read_status()
        if all(values_equal(status.get(name), expected[name]) for name in wanted):
            log(f"environment matched the baseline after {timeout_seconds - (deadline - time.monotonic()):.1f}s")
            return
        time.sleep(1.0)
    log(f"WARN: environment did not match the baseline environment within {timeout_seconds:g}s; "
        "views will be judged UNSTABLE if the envelope differs")


# ---------------------------------------------------------------------------
# device --baseline
# ---------------------------------------------------------------------------
def run_baseline_device(args: argparse.Namespace, manifest: Manifest) -> int:
    views = select_views(manifest, args.only)
    baseline_path = resolve_path(args.baseline_json, DEFAULT_DEVICE_BASELINE)
    refs_dir = resolve_path(args.refs_dir, DEFAULT_DEVICE_REFS)
    last_dir = resolve_path(args.last_dir, DEFAULT_LAST_DIR)
    repeat = int(args.repeat)

    log(f"manifest: {display_path(manifest.path)} ({len(manifest.views)} views, {len(views)} selected)")
    log(f"baseline json: {display_path(baseline_path)}")
    log(f"reference frames: {display_path(refs_dir)}")
    log(f"repeat: {repeat}" + (" (double-run consistency enabled)" if repeat > 1 else ""))

    existing_views: Dict[str, Any] = {}
    if args.only and baseline_path.exists():
        existing = load_baseline_document(baseline_path)
        existing_views = dict(existing.get("views") or {})
        log(f"merging into existing baseline ({len(existing_views)} views)")

    started_at = now_iso()
    results: List[ViewResult] = []
    captures: Dict[str, CaptureOutcome] = {}
    boot_status: Dict[str, Any] = {}

    with DeviceSession(args.port) as session:
        boot_status = session.boot_status
        _print_device_header(boot_status, args.port)
        state = RunState()
        total = len(views)
        for index, view in enumerate(views, 1):
            log(f"[{index}/{total}] {view.id}: {view.description or view.id}")
            available, reason = environment_satisfies(view, boot_status)
            if not available:
                results.append(
                    ViewResult(view.id, RESULT_SKIP, [f"environment not available: {reason}"])
                )
                continue
            try:
                outcome = capture_view_device(session, manifest, view, repeat)
            except Exception as error:  # serial/CRC failures never become PASS
                message = str(error)
                if view.optional:
                    results.append(
                        ViewResult(view.id, RESULT_SKIP, [f"optional view capture failed: {message}"])
                    )
                else:
                    results.append(ViewResult(view.id, RESULT_UNSTABLE, [f"capture failed: {message}"]))
                if "restarted" in message:
                    log("ERROR: device restarted mid-run; aborting baseline")
                    break
                continue
            captures[view.id] = outcome

            problems = evaluate_envelope(
                view,
                merge_envelope_expectations(manifest, view),
                None,
                outcome.pre,
                outcome.post,
            )
            problems += state.absorb(view.id, outcome.pre, outcome.post)
            repeat_problem = _repeat_problem(outcome) if view.hash_gated else None
            if repeat_problem:
                problems.append(repeat_problem)

            if problems:
                if view.optional:
                    results.append(
                        ViewResult(
                            view.id,
                            RESULT_SKIP,
                            [f"optional view unstable: {problem}" for problem in problems],
                        )
                    )
                    continue
                artifacts = save_failure_artifacts(view, outcome, refs_dir, last_dir)
                artifacts.result = RESULT_UNSTABLE
                artifacts.reasons = problems
                results.append(artifacts)
                continue

            result = ViewResult(view.id, RESULT_PASS, sha=outcome.sha, envelope={"pre": outcome.pre, "post": outcome.post})
            result.warnings.extend(outcome.warnings)
            if not view.hash_gated:
                result.warnings.append("informational view: hash recorded but not gated by --check")
            results.append(result)

        try:
            session.send("page 0", 0.3)
        except Exception:
            pass

    # distinctFrom: an entry sequence that silently fell back to the base page is a
    # tool/manifest problem, not an approved visual change. Demote to UNSTABLE.
    sha_by_id = {result.view_id: result.sha for result in results if result.sha}
    for view_id, entry in existing_views.items():
        if view_id not in sha_by_id and isinstance(entry, dict) and entry.get("sha256"):
            sha_by_id[view_id] = str(entry["sha256"])
    issues = validate_distinct(views, sha_by_id)
    for result in results:
        if result.view_id in issues and result.result == RESULT_PASS:
            result.result = RESULT_UNSTABLE
            result.reasons = list(issues[result.view_id])

    # Persist reference frames + entries only for views that ended up PASS.
    entries: Dict[str, Dict[str, Any]] = {}
    view_map = manifest.view_map
    for result in results:
        if result.result != RESULT_PASS:
            continue
        view = view_map[result.view_id]
        outcome = captures[result.view_id]
        reference_png = refs_dir / f"{view.id}.png"
        save_frame_png(outcome.raw, reference_png)
        entries[view.id] = build_baseline_entry(view, outcome, reference_png)

    print_results(results)
    counts = result_counts(results)
    failed = [result for result in results if result.result in FAILING_RESULTS]

    report_extra = {
        "startedAt": started_at,
        "baselineJson": display_path(baseline_path),
        "referenceFrames": display_path(refs_dir),
        "repeat": repeat,
        "device": _device_metadata(boot_status, args.port, repeat, manifest),
    }
    report = build_report("baseline", "device", manifest, results, report_extra)
    report_path = write_report(last_dir, report, "baseline-report.json")
    log(f"report: {display_path(report_path)}")

    if failed:
        log(f"BASELINE NOT WRITTEN: {len(failed)} view(s) FAIL/UNSTABLE; fix the environment/protocol and re-run")
        log(f"FAIL {[(result.view_id, result.result) for result in failed]}")
        log("RESULT: FAIL (baseline NOT written)")
        return 1

    merged_entries = dict(existing_views) if (args.only and existing_views) else {}
    merged_entries.update(entries)
    metadata = _device_metadata(boot_status, args.port, repeat, manifest)
    metadata["viewCount"] = len(merged_entries)
    metadata["hashGated"] = sum(
        1 for entry in merged_entries.values() if entry.get("hash", True) is not False
    )
    if args.only:
        metadata["partialUpdate"] = sorted(entries)
    document = build_baseline_document(manifest, merged_entries, metadata)
    write_json(baseline_path, document)
    log(f"BASELINE WRITTEN: {display_path(baseline_path)} ({len(merged_entries)} views, {metadata['hashGated']} hash-gated)")
    log(f"reference frames: {display_path(refs_dir)}")
    log("RESULT: PASS (baseline captured)")
    return 0


# ---------------------------------------------------------------------------
# device --check
# ---------------------------------------------------------------------------
def run_check_device(args: argparse.Namespace, manifest: Manifest) -> int:
    views = select_views(manifest, args.only)
    baseline_path = resolve_path(args.baseline_json, DEFAULT_DEVICE_BASELINE)
    refs_dir = resolve_path(args.refs_dir, DEFAULT_DEVICE_REFS)
    last_dir = resolve_path(args.last_dir, DEFAULT_LAST_DIR)
    repeat = int(args.repeat)

    if not baseline_path.exists():
        raise ToolError(
            f"no device baseline at {display_path(baseline_path)}; "
            "capture one first with --baseline (Lead: after flashing the baseline firmware)"
        )
    baseline = load_baseline_document(baseline_path)
    baseline_views = dict(baseline.get("views") or {})
    baseline_meta = baseline.get("baseline") or {}
    baseline_env = baseline_environment(baseline_views)
    stale = sorted(set(baseline_views) - set(manifest.view_map))

    log(f"manifest: {display_path(manifest.path)} ({len(views)} views selected)")
    log(f"baseline: {display_path(baseline_path)} ({len(baseline_views)} views, "
        f"created {baseline_meta.get('createdAt')})")
    log(f"reference frames: {display_path(refs_dir)}")
    log(f"repeat: {repeat}" + (" (double-run consistency enabled)" if repeat > 1 else ""))
    if stale:
        log(f"WARN: baseline contains {len(stale)} view(s) not in the manifest: {', '.join(stale)}")

    results: List[ViewResult] = []
    boot_status: Dict[str, Any] = {}
    device_sketch_md5: Any = _MISSING

    with DeviceSession(args.port) as session:
        boot_status = session.boot_status
        device_sketch_md5 = boot_status.get("sketchMd5")
        _print_device_header(boot_status, args.port)
        if args.env_wait > 0:
            _wait_for_environment(session, baseline_env, float(args.env_wait))
        state = RunState()
        total = len(views)
        for index, view in enumerate(views, 1):
            log(f"[{index}/{total}] {view.id}: {view.description or view.id}")
            if view.id not in baseline_views:
                results.append(ViewResult(view.id, RESULT_SKIP, ["not in baseline (unbaselined view)"]))
                continue
            available, reason = environment_satisfies(view, boot_status)
            if not available:
                results.append(ViewResult(view.id, RESULT_SKIP, [f"environment not available: {reason}"]))
                continue
            try:
                baseline_entry = normalize_baseline_entry(view, baseline_views[view.id])
            except ToolError as error:
                results.append(ViewResult(view.id, RESULT_UNSTABLE, [f"bad baseline entry: {error}"]))
                continue

            expected_env = baseline_entry["envelope"].get("pre") or {}
            try:
                outcome = capture_view_device(session, manifest, view, repeat)
            except Exception as error:  # serial/CRC failures never become PASS
                message = str(error)
                if view.optional:
                    results.append(
                        ViewResult(view.id, RESULT_SKIP, [f"optional view capture failed: {message}"])
                    )
                else:
                    results.append(ViewResult(view.id, RESULT_UNSTABLE, [f"capture failed: {message}"]))
                if "restarted" in message:
                    log("ERROR: device restarted mid-run; aborting check")
                    break
                continue

            problems = evaluate_envelope(
                view,
                merge_envelope_expectations(manifest, view),
                expected_env,
                outcome.pre,
                outcome.post,
            )
            problems += state.absorb(view.id, outcome.pre, outcome.post)
            repeat_problem = _repeat_problem(outcome) if view.hash_gated else None
            if repeat_problem:
                problems.append(repeat_problem)

            same_as_baseline = (
                outcome.sha == baseline_entry["sha256"] and FRAME_BYTES == baseline_entry["bytes"]
            )
            result = ViewResult(
                view.id,
                RESULT_PASS,
                sha=outcome.sha,
                baseline_sha=baseline_entry["sha256"],
                envelope={"pre": outcome.pre, "post": outcome.post},
            )
            result.warnings.extend(outcome.warnings)
            if problems:
                if view.optional:
                    result.result = RESULT_SKIP
                    result.reasons = [f"optional view unstable: {problem}" for problem in problems]
                    results.append(result)
                    continue
                artifacts = save_failure_artifacts(view, outcome, refs_dir, last_dir)
                result.result = RESULT_UNSTABLE
                result.reasons = problems
                result.frame = artifacts.frame
                result.diff = artifacts.diff
                result.diff_png = artifacts.diff_png
                result.warnings.extend(artifacts.warnings)
                results.append(result)
                continue

            if same_as_baseline:
                if not view.expect_no_change:
                    result.warnings.append(
                        "approved-change view is still byte-identical to its baseline "
                        "(K2 change not present / not rendered yet; update the baseline after K2)"
                    )
            else:
                artifacts = save_failure_artifacts(view, outcome, refs_dir, last_dir)
                result.sha = outcome.sha
                result.frame = artifacts.frame
                result.diff = artifacts.diff
                result.diff_png = artifacts.diff_png
                result.warnings.extend(artifacts.warnings)
                if not view.hash_gated:
                    result.result = RESULT_INFO_DIFF
                    result.reasons = ["informational view changed (not gated)"]
                elif not view.expect_no_change:
                    result.result = RESULT_EXPECTED_CHANGE
                    result.reasons = ["hash changed; approved visual change (expectNoChange=false)"]
                else:
                    result.result = RESULT_FAIL
                    result.reasons = ["frame hash differs from the baseline"]
                    if result.diff and _is_number(result.diff.get("diffPx")):
                        result.reasons.append(
                            f"diff {result.diff['diffPx']} px bbox={result.diff.get('bbox')}"
                        )
            results.append(result)

        try:
            session.send("page 0", 0.3)
        except Exception:
            pass

    # distinctFrom against current frames (fall back to baseline hashes for refs not run).
    sha_by_id = {result.view_id: result.sha for result in results if result.sha}
    for view_id, entry in baseline_views.items():
        if view_id not in sha_by_id and isinstance(entry, dict) and entry.get("sha256"):
            sha_by_id[view_id] = str(entry["sha256"])
    issues = validate_distinct(views, sha_by_id)
    for result in results:
        if result.view_id in issues and result.result == RESULT_PASS:
            result.result = RESULT_UNSTABLE
            result.reasons = list(issues[result.view_id])

    print_results(results)
    counts = result_counts(results)
    failed_count = sum(1 for result in results if result.result == RESULT_FAIL)
    unstable_count = sum(1 for result in results if result.result == RESULT_UNSTABLE)

    baseline_sketch = baseline_meta.get("device", {}).get("sketchMd5")
    firmware_changed = None
    if baseline_sketch not in (None, "") and device_sketch_md5 not in (None, ""):
        firmware_changed = str(baseline_sketch) != str(device_sketch_md5)

    report = build_report(
        "check",
        "device",
        manifest,
        results,
        {
            "baselineJson": display_path(baseline_path),
            "baselineCreatedAt": baseline_meta.get("createdAt"),
            "baselineSketchMd5": baseline_sketch,
            "deviceSketchMd5": device_sketch_md5,
            "firmwareChangedVsBaseline": firmware_changed,
            "referenceFrames": display_path(refs_dir),
            "repeat": repeat,
            "environment": envelope_snapshot(boot_status),
        },
    )
    report_path = write_report(last_dir, report)
    log(f"report: {display_path(report_path)}")
    if failed_count or unstable_count:
        log(
            f"RESULT: FAIL ({failed_count} FAIL / {unstable_count} UNSTABLE; "
            f"{counts.get(RESULT_EXPECTED_CHANGE, 0)} expected-change / "
            f"{counts.get(RESULT_INFO_DIFF, 0)} info-diff / {counts.get(RESULT_SKIP, 0)} skip)"
        )
        return 1
    log(
        f"RESULT: PASS ({counts.get(RESULT_PASS, 0)} hash-gated views identical to the baseline; "
        f"{counts.get(RESULT_EXPECTED_CHANGE, 0)} expected-change / "
        f"{counts.get(RESULT_INFO_DIFF, 0)} info-diff / {counts.get(RESULT_SKIP, 0)} skip)"
    )
    return 0


# ---------------------------------------------------------------------------
# device --update-only (approved visual changes, e.g. Wi-Fi keyboard K2)
# ---------------------------------------------------------------------------
def run_update_only_device(args: argparse.Namespace, manifest: Manifest) -> int:
    """Update only the given view baselines, keeping every other entry untouched.

    This is the controlled path for a Lead-approved visual change (K2 keyboard):
    1. the existing baseline must exist and the selected views must already be in it;
    2. the run environment envelope must still match the old baseline entry
       (otherwise UNSTABLE and nothing is written);
    3. changed views get new hash + reference PNG + a side-by-side diff PNG under
       build/device-golden/last/ as evidence, and the old/new hashes are appended to
       baseline.updates; unchanged entries are left byte-for-byte untouched.
    """
    views = select_views(manifest, args.only)
    baseline_path = resolve_path(args.baseline_json, DEFAULT_DEVICE_BASELINE)
    refs_dir = resolve_path(args.refs_dir, DEFAULT_DEVICE_REFS)
    last_dir = resolve_path(args.last_dir, DEFAULT_LAST_DIR)
    repeat = int(args.repeat)

    if not baseline_path.exists():
        raise ToolError(
            f"no device baseline at {display_path(baseline_path)}; "
            "--update-only needs an existing baseline (capture it with --baseline first)"
        )
    baseline = load_baseline_document(baseline_path)
    baseline_views = dict(baseline.get("views") or {})
    baseline_meta = dict(baseline.get("baseline") or {})

    log(f"manifest: {display_path(manifest.path)} ({len(views)} views selected for update)")
    log(f"baseline: {display_path(baseline_path)} ({len(baseline_views)} views, "
        f"created {baseline_meta.get('createdAt')})")
    log(f"reference frames: {display_path(refs_dir)}")
    log(f"repeat: {repeat}" + (" (double-run consistency enabled)" if repeat > 1 else ""))
    log("update-only: only the selected views may change; every other baseline entry is preserved")

    results: List[ViewResult] = []
    captures: Dict[str, CaptureOutcome] = {}
    update_records: List[Dict[str, Any]] = []
    boot_status: Dict[str, Any] = {}

    with DeviceSession(args.port) as session:
        boot_status = session.boot_status
        _print_device_header(boot_status, args.port)
        device_meta = _device_metadata(boot_status, args.port, repeat, manifest)
        state = RunState()
        total = len(views)
        for index, view in enumerate(views, 1):
            log(f"[{index}/{total}] update {view.id}: {view.description or view.id}")
            if view.id not in baseline_views:
                results.append(
                    ViewResult(view.id, RESULT_UNSTABLE,
                               ["not in baseline; add a brand-new view with --baseline --only instead"])
                )
                continue
            available, reason = environment_satisfies(view, boot_status)
            if not available:
                results.append(ViewResult(view.id, RESULT_SKIP, [f"environment not available: {reason}"]))
                continue
            try:
                baseline_entry = normalize_baseline_entry(view, baseline_views[view.id])
            except ToolError as error:
                results.append(ViewResult(view.id, RESULT_UNSTABLE, [f"bad baseline entry: {error}"]))
                continue
            expected_env = baseline_entry["envelope"].get("pre") or {}

            try:
                outcome = capture_view_device(session, manifest, view, repeat)
            except Exception as error:
                message = str(error)
                if view.optional:
                    results.append(ViewResult(view.id, RESULT_SKIP, [f"optional view capture failed: {message}"]))
                else:
                    results.append(ViewResult(view.id, RESULT_UNSTABLE, [f"capture failed: {message}"]))
                if "restarted" in message:
                    log("ERROR: device restarted mid-run; aborting update-only")
                    break
                continue
            captures[view.id] = outcome

            problems = evaluate_envelope(
                view,
                merge_envelope_expectations(manifest, view),
                expected_env,
                outcome.pre,
                outcome.post,
            )
            problems += state.absorb(view.id, outcome.pre, outcome.post)
            repeat_problem = _repeat_problem(outcome) if view.hash_gated else None
            if repeat_problem:
                problems.append(repeat_problem)
            if problems:
                if view.optional:
                    results.append(ViewResult(view.id, RESULT_SKIP,
                                              [f"optional view unstable: {problem}" for problem in problems]))
                    continue
                artifacts = save_failure_artifacts(view, outcome, refs_dir, last_dir)
                artifacts.result = RESULT_UNSTABLE
                artifacts.reasons = problems
                results.append(artifacts)
                continue

            changed = outcome.sha != baseline_entry["sha256"]
            result = ViewResult(
                view.id,
                RESULT_PASS,
                sha=outcome.sha,
                baseline_sha=baseline_entry["sha256"],
                envelope={"pre": outcome.pre, "post": outcome.post},
            )
            result.warnings.extend(outcome.warnings)
            if changed:
                artifacts = save_failure_artifacts(view, outcome, refs_dir, last_dir)
                result.frame = artifacts.frame
                result.diff = artifacts.diff
                result.diff_png = artifacts.diff_png
                result.warnings.extend(artifacts.warnings)
                result.reasons = [f"baseline updated ({baseline_entry['sha256'][:12]} -> {outcome.sha[:12]})"]
                update_records.append(
                    {
                        "view": view.id,
                        "previousSha256": baseline_entry["sha256"],
                        "sha256": outcome.sha,
                        "bytes": FRAME_BYTES,
                        "updatedAt": now_iso(),
                        "diffPx": None if artifacts.diff is None else artifacts.diff.get("diffPx"),
                        "bbox": None if artifacts.diff is None else artifacts.diff.get("bbox"),
                        "frame": artifacts.frame or None,
                        "diffPng": artifacts.diff_png or None,
                        "deviceSketchMd5": device_meta.get("device", {}).get("sketchMd5"),
                    }
                )
            else:
                result.reasons = ["frame identical to the existing baseline (entry left untouched)"]
            results.append(result)

        try:
            session.send("page 0", 0.3)
        except Exception:
            pass

    # Guard the same "entry did not reach the target sub-view" mistake using the
    # updated hash map plus untouched baseline entries.
    sha_by_id = {result.view_id: result.sha for result in results if result.sha}
    for view_id, entry in baseline_views.items():
        if view_id not in sha_by_id and isinstance(entry, dict) and entry.get("sha256"):
            sha_by_id[view_id] = str(entry["sha256"])
    issues = validate_distinct(views, sha_by_id)
    for result in results:
        if result.view_id in issues and result.result == RESULT_PASS:
            result.result = RESULT_UNSTABLE
            result.reasons = list(issues[result.view_id])

    # Persist new reference frames only for accepted+changed views; untouched
    # entries stay exactly as they were.
    merged_entries = dict(baseline_views)
    for result in results:
        if result.result != RESULT_PASS:
            continue
        view = manifest.view_map[result.view_id]
        outcome = captures[result.view_id]
        previous = baseline_views.get(view.id) or {}
        if outcome.sha == previous.get("sha256"):
            continue
        reference_png = refs_dir / f"{view.id}.png"
        save_frame_png(outcome.raw, reference_png)
        entry = build_baseline_entry(view, outcome, reference_png)
        entry["previousSha256"] = previous.get("sha256")
        entry["updatedAt"] = now_iso()
        merged_entries[view.id] = entry

    print_results(results)
    counts = result_counts(results)
    failed = [result for result in results if result.result in FAILING_RESULTS]

    report = build_report(
        "update-only",
        "device",
        manifest,
        results,
        {
            "baselineJson": display_path(baseline_path),
            "baselineCreatedAt": baseline_meta.get("createdAt"),
            "referenceFrames": display_path(refs_dir),
            "repeat": repeat,
            "updates": update_records,
            "device": _device_metadata(boot_status, args.port, repeat, manifest),
        },
    )
    report_path = write_report(last_dir, report, "update-report.json")
    log(f"report: {display_path(report_path)}")

    if failed:
        log(f"BASELINE NOT UPDATED: {len(failed)} selected view(s) are FAIL/UNSTABLE")
        log("RESULT: FAIL (baseline NOT updated)")
        return 1

    metadata = dict(baseline_meta)
    metadata["updatedAt"] = now_iso()
    metadata["viewCount"] = len(merged_entries)
    metadata["hashGated"] = sum(
        1 for entry in merged_entries.values() if entry.get("hash", True) is not False
    )
    metadata["device"] = _device_metadata(boot_status, args.port, repeat, manifest).get("device")
    history = list(metadata.get("updates") or [])
    history.extend(update_records)
    metadata["updates"] = history

    document = build_baseline_document(manifest, merged_entries, metadata)
    write_json(baseline_path, document)
    log(f"BASELINE UPDATED: {display_path(baseline_path)} "
        f"({len(update_records)} view(s) changed, {len(merged_entries)} total views)")
    for record in update_records:
        detail = f"diff {record.get('diffPx')} px bbox={record.get('bbox')}"
        log(f"  updated {record['view']}: {record['previousSha256'][:12]} -> {record['sha256'][:12]} ({detail})")
    log("RESULT: PASS (selected baselines updated; other views preserved)")
    return 0


# ---------------------------------------------------------------------------
# offline mode: --from-dir <png dir>
# ---------------------------------------------------------------------------
def load_fixture_statuses(fixture_dir: Path) -> Optional[Dict[str, Any]]:
    """Best-effort: device_capture_allb.py writes status.json ({label: status})."""
    path = fixture_dir / "status.json"
    if not path.is_file():
        return None
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if not isinstance(document, dict):
        return None
    if "uptimeMs" in document:
        return {"_session": envelope_snapshot(document)}
    mapping: Dict[str, Any] = {}
    for key, value in document.items():
        if isinstance(value, dict) and "uptimeMs" in value:
            mapping[str(key)] = envelope_snapshot(value)
    return mapping or None


def _fixture_envelope(status_map: Optional[Dict[str, Any]], view: View) -> Optional[Dict[str, Any]]:
    if not status_map:
        return None
    for key in (view.fixture_name, view.id):
        if key in status_map:
            return {"pre": dict(status_map[key]), "post": None, "source": "fixture status.json"}
    return None


def run_offline(args: argparse.Namespace, manifest: Manifest) -> int:
    fixture_dir = resolve_path(args.from_dir, ROOT)
    if not fixture_dir.is_dir():
        raise ToolError(f"--from-dir is not a directory: {display_path(fixture_dir)}")
    views = select_views(manifest, args.only)
    writing_baseline = bool(args.baseline)
    baseline_path = resolve_path(
        args.baseline_json,
        DEFAULT_OFFLINE_BASELINE if writing_baseline else DEFAULT_OFFLINE_BASELINE,
    )
    refs_dir = resolve_path(args.refs_dir, DEFAULT_OFFLINE_REFS)
    last_dir = resolve_path(args.last_dir, DEFAULT_LAST_DIR)

    if writing_baseline:
        try:
            baseline_path.resolve().relative_to((ROOT / "hardware").resolve())
        except ValueError:
            pass
        else:
            raise ToolError(
                "refusing to write a repo file from PNG fixtures; "
                f"choose --baseline-json outside hardware/ (got {display_path(baseline_path)})"
            )
        if baseline_path.resolve() == DEFAULT_DEVICE_BASELINE.resolve():
            raise ToolError("refusing to overwrite the device baseline with PNG fixture hashes")

    baseline_views: Optional[Dict[str, Any]] = None
    if not writing_baseline and baseline_path.exists():
        baseline_document = load_baseline_document(baseline_path)
        baseline_views = dict(baseline_document.get("views") or {})
        log(f"offline baseline: {display_path(baseline_path)} ({len(baseline_views)} views)")
    elif not writing_baseline:
        log("NO OFFLINE BASELINE: self-test mode (discovery + hash + report only, no comparison)")

    status_map = load_fixture_statuses(fixture_dir)
    log(f"offline fixtures: {display_path(fixture_dir)}")
    log("offline mode: no serial port is opened; environment-envelope assertions are skipped (PNG fixtures only)")

    results: List[ViewResult] = []
    sha_by_id: Dict[str, str] = {}
    offline_entries: Dict[str, Dict[str, Any]] = {}
    for view in views:
        png_path = fixture_dir / f"{view.fixture_name}.png"
        if not png_path.is_file():
            results.append(ViewResult(view.id, RESULT_SKIP, [f"fixture PNG missing: {display_path(png_path)}"]))
            continue
        try:
            raw = load_fixture_png(png_path)
            raw = apply_frame_mask(raw, view.mask)
        except ToolError as error:
            results.append(ViewResult(view.id, RESULT_UNSTABLE, [f"cannot read fixture: {error}"]))
            continue
        digest = sha256_hex(raw)
        sha_by_id[view.id] = digest
        envelope = _fixture_envelope(status_map, view)

        if writing_baseline:
            reference_png = refs_dir / f"{view.id}.png"
            save_frame_png(raw, reference_png)
            offline_entries[view.id] = {
                "sha256": digest,
                "bytes": FRAME_BYTES,
                "expectNoChange": view.expect_no_change,
                "hash": view.hash_gated,
                "freezeEpoch": view.freeze_epoch,
                "expect": dict(view.expect),
                # PNG fixtures carry no live `status`; keep the shape so the same
                # baseline reader works, but record that no envelope exists.
                "envelope": envelope if envelope is not None else {
                    "pre": None,
                    "post": None,
                    "source": "png-fixtures (no status.json)",
                },
                "fixturePng": display_path(png_path),
                "referencePng": display_path(reference_png),
                "capturedAt": now_iso(),
            }
            results.append(ViewResult(view.id, RESULT_PASS, sha=digest))
            continue

        result = ViewResult(view.id, RESULT_PASS, sha=digest)
        if baseline_views is None:
            result.result = RESULT_HASHED
            result.reasons = ["hashed from PNG fixture (no baseline to compare)"]
        elif view.id not in baseline_views:
            result.result = RESULT_SKIP
            result.reasons = ["not in offline baseline"]
        else:
            baseline_entry = normalize_baseline_entry(view, baseline_views[view.id])
            result.baseline_sha = baseline_entry["sha256"]
            if digest == baseline_entry["sha256"]:
                if not view.expect_no_change:
                    result.warnings.append("approved-change view still identical to its baseline")
            else:
                # Same failure-localization contract as device mode: latest frame PNG
                # + bbox side-by-side diff against the local reference frame.
                frame_path = last_dir / f"{view.id}.png"
                save_frame_png(raw, frame_path)
                result.frame = display_path(frame_path)
                reference_png = refs_dir / f"{view.id}.png"
                if reference_png.exists():
                    try:
                        reference_raw = load_fixture_png(reference_png)
                        result.diff = diff_frames(reference_raw, raw)
                        if result.diff.get("bbox") is not None:
                            diff_path = last_dir / f"{view.id}-diff.png"
                            save_diff_png(reference_png, raw, diff_path, result.diff.get("bbox"))
                            result.diff_png = display_path(diff_path)
                        elif result.diff.get("diffPx") == 0:
                            result.warnings.append(
                                "reference pixels are identical but the baseline hash differs; "
                                "the offline baseline hash may be stale/corrupt"
                            )
                    except ToolError as error:
                        result.warnings.append(f"reference diff skipped: {error}")
                else:
                    result.warnings.append(
                        f"no local reference frame at {display_path(reference_png)}; diff PNG not generated"
                    )
                if not view.hash_gated:
                    result.result = RESULT_INFO_DIFF
                    result.reasons = ["informational view changed (not gated)"]
                elif not view.expect_no_change:
                    result.result = RESULT_EXPECTED_CHANGE
                    result.reasons = ["hash differs; approved visual change (expectNoChange=false)"]
                else:
                    result.result = RESULT_FAIL
                    result.reasons = ["fixture hash differs from the offline baseline"]
                    if result.diff and _is_number(result.diff.get("diffPx")):
                        result.reasons.append(
                            f"diff {result.diff['diffPx']} px bbox={result.diff.get('bbox')}"
                        )
        results.append(result)

    issues = validate_distinct(views, sha_by_id)
    for result in results:
        if result.view_id in issues and result.result in (RESULT_PASS, RESULT_HASHED):
            result.result = RESULT_UNSTABLE
            result.reasons = list(issues[result.view_id])

    print_results(results)
    counts = result_counts(results)

    if writing_baseline:
        failed = [result for result in results if result.result in FAILING_RESULTS]
        report = build_report(
            "offline-baseline",
            "png-fixtures",
            manifest,
            results,
            {
                "fromDir": display_path(fixture_dir),
                "baselineJson": display_path(baseline_path),
                "referenceFrames": display_path(refs_dir),
            },
        )
        report_path = write_report(last_dir, report, "offline-baseline-report.json")
        log(f"report: {display_path(report_path)}")
        if failed:
            log(f"OFFLINE BASELINE NOT WRITTEN: {len(failed)} view(s) FAIL/UNSTABLE")
            log("RESULT: FAIL (offline baseline NOT written)")
            return 1
        document = build_baseline_document(
            manifest,
            offline_entries,
            {
                "createdAt": now_iso(),
                "tool": "hardware/scripts/check_device_golden.py",
                "manifest": display_path(manifest.path),
                "source": "png-fixtures",
                "fromDir": display_path(fixture_dir),
                "frame": {
                    "width": FRAME_WIDTH,
                    "height": FRAME_HEIGHT,
                    "bytes": FRAME_BYTES,
                    "pixelFormat": "RGB565 little-endian",
                    "note": "hashed after exact PNG->RGB565 reconstruction of the fixtures",
                },
                "viewCount": len(offline_entries),
                "hashGated": sum(1 for entry in offline_entries.values() if entry.get("hash", True) is not False),
            },
        )
        write_json(baseline_path, document)
        log(f"OFFLINE BASELINE WRITTEN: {display_path(baseline_path)} ({len(offline_entries)} views)")
        log("RESULT: PASS (offline baseline captured)")
        return 0

    failed_count = sum(1 for result in results if result.result == RESULT_FAIL)
    unstable_count = sum(1 for result in results if result.result == RESULT_UNSTABLE)
    if counts.get(RESULT_FAIL) or counts.get(RESULT_UNSTABLE):
        report = build_report(
            "offline-check",
            "png-fixtures",
            manifest,
            results,
            {
                "fromDir": display_path(fixture_dir),
                "baselineJson": display_path(baseline_path) if baseline_views is not None else None,
            },
        )
        if args.last_dir is not None:
            report_path = write_report(last_dir, report, "offline-report.json")
            log(f"report: {display_path(report_path)}")
        log(f"RESULT: FAIL ({failed_count} FAIL / {unstable_count} UNSTABLE; "
            f"{counts.get(RESULT_SKIP, 0)} SKIP)")
        return 1

    if baseline_views is None:
        log(
            f"RESULT: PASS (offline self-test: {counts.get(RESULT_HASHED, 0)} fixtures hashed, "
            f"{counts.get(RESULT_SKIP, 0)} SKIP; discovery/hash/diff/report logic exercised, "
            "no device comparison performed)"
        )
        if args.last_dir is not None:
            report = build_report(
                "offline-selftest",
                "png-fixtures",
                manifest,
                results,
                {"fromDir": display_path(fixture_dir), "baselineJson": None},
            )
            report_path = write_report(last_dir, report, "offline-report.json")
            log(f"report: {display_path(report_path)}")
        return 0

    report = build_report(
        "offline-check",
        "png-fixtures",
        manifest,
        results,
        {"fromDir": display_path(fixture_dir), "baselineJson": display_path(baseline_path)},
    )
    if args.last_dir is not None:
        report_path = write_report(last_dir, report, "offline-report.json")
        log(f"report: {display_path(report_path)}")
    log(
        f"RESULT: PASS ({counts.get(RESULT_PASS, 0)} fixtures identical to the offline baseline; "
        f"{counts.get(RESULT_EXPECTED_CHANGE, 0)} expected-change / "
        f"{counts.get(RESULT_INFO_DIFF, 0)} info-diff / {counts.get(RESULT_SKIP, 0)} SKIP)"
    )
    return 0


# ---------------------------------------------------------------------------
# dry-run / view list
# ---------------------------------------------------------------------------
def print_views(manifest: Manifest, fixture_dir: Optional[Path] = None) -> int:
    log(f"manifest: {display_path(manifest.path)}")
    protocol = manifest.protocol
    log(
        "protocol: freezeEpoch={epoch} settle={settle}s settleTimeout={timeout}s "
        "freezeSettle={freeze}s attempts={attempts}".format(
            epoch=protocol.get("freezeEpoch"),
            settle=protocol.get("settleSeconds"),
            timeout=protocol.get("settleTimeoutSeconds"),
            freeze=protocol.get("freezeSettleSeconds"),
            attempts=protocol.get("captureAttempts"),
        )
    )
    settle_defaults = _normalize_settle_defaults(protocol.get("settleDefaults"))
    log(
        "settle defaults: maxAnimations={max_animations} stableFrames={stable_frames} "
        "intervalMs={interval_ms} timeoutSeconds={timeout}".format(
            max_animations=settle_defaults["maxAnimations"],
            stable_frames=settle_defaults["stableFrames"],
            interval_ms=settle_defaults["intervalMs"],
            timeout=settle_defaults["timeoutSeconds"],
        )
    )
    log("envelope defaults: " + json.dumps(manifest.envelope, ensure_ascii=False, sort_keys=True))
    if fixture_dir is not None:
        log(f"offline fixtures: {display_path(fixture_dir)}")
    log("")
    log(f"{'view id':<28} {'flags':<28} {'fixture':<18} entry")
    log("-" * 130)
    missing_fixtures: List[str] = []
    for view in manifest.views:
        flags: List[str] = []
        if not view.hash_gated:
            flags.append("info")
        if not view.expect_no_change:
            flags.append("expect-change")
        if view.optional:
            flags.append("optional")
        if not view.capture_freeze:
            flags.append("no-refreeze")
        if view.mask:
            flags.append(f"mask{len(view.mask)}")
        if int(view.settle.get("stableFrames", 2)) <= 1:
            flags.append("no-probe")
        fixture_name = view.fixture_name
        fixture_state = ""
        if fixture_dir is not None:
            fixture_state = "OK" if (fixture_dir / f"{fixture_name}.png").is_file() else "MISSING"
            if fixture_state == "MISSING":
                missing_fixtures.append(view.id)
        entry = " -> ".join(step.cmd for step in view.entry)
        log(f"{view.id:<28} {','.join(flags):<28} {fixture_name:<18} {entry}")
    total = len(manifest.views)
    hash_gated = sum(1 for view in manifest.views if view.hash_gated)
    informational = total - hash_gated
    optional = sum(1 for view in manifest.views if view.optional)
    expect_change = sum(1 for view in manifest.views if not view.expect_no_change)
    log("")
    log(
        f"views: {total} (hash-gated {hash_gated}, informational {informational}, "
        f"optional {optional}, expect-change {expect_change})"
    )
    if fixture_dir is not None:
        log(
            f"fixture coverage: {total - len(missing_fixtures)} present, "
            f"{len(missing_fixtures)} missing (missing views are SKIPped, never fatal)"
        )
        if missing_fixtures:
            log("missing fixtures: " + ", ".join(missing_fixtures))
    log("dry-run: no serial port was opened. Use --baseline / --check (device) or --from-dir (offline).")
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="check_device_golden.py",
        description="Device golden tool: capture/compare raw RGB565 framebuffer SHA-256 per view.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "default action: print the view manifest (no serial port is opened)\n"
            "device: --baseline [--repeat 2] / --check / --update-only <ids>\n"
            "offline: --from-dir <png dir> --check (or --baseline to build an offline baseline)"
        ),
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--baseline", action="store_true",
                      help="capture a baseline (device; with --from-dir: build an offline PNG-fixture baseline)")
    mode.add_argument("--check", action="store_true",
                      help="compare against the baseline (exit 1 on FAIL/UNSTABLE)")
    parser.add_argument("--update-only", default="", metavar="IDS",
                        help="device: replace the baseline entries of these comma-separated approved-change "
                             "views (e.g. K2 keyboard) against the existing baseline; all other entries are "
                             "preserved and old/new hashes are recorded as evidence")
    parser.add_argument("--from-dir", type=Path, default=None,
                        help="offline self-test: use PNG fixtures in this directory instead of the device")
    parser.add_argument("--port", default="COM8",
                        help="serial port used by device modes only (default COM8)")
    parser.add_argument("--repeat", type=int, default=1,
                        help="capture each hash-gated view N times and require byte-identical frames (default 1)")
    parser.add_argument("--baseline-json", type=Path, default=None,
                        help="baseline JSON path (default: hardware/tests/golden/device-hashes.json on device, "
                             "build/device-golden/offline-hashes.json offline)")
    parser.add_argument("--refs-dir", type=Path, default=None,
                        help="local reference-frame PNG directory (default: build/device-golden/baseline on device, "
                             "build/device-golden/offline-baseline offline)")
    parser.add_argument("--last-dir", type=Path, default=None,
                        help="failure-artifact/report directory (default build/device-golden/last)")
    parser.add_argument("--only", default="",
                        help="comma-separated view ids to run (default: all views; with --baseline this is a "
                             "partial baseline that merges into the existing JSON)")
    parser.add_argument("--env-wait", type=float, default=0.0,
                        help="device --check: wait up to N seconds for the device environment to match the baseline")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST,
                        help="view manifest path")
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        _require(1 <= int(args.repeat) <= 5, "--repeat must be between 1 and 5")
        manifest_path = resolve_path(args.manifest, DEFAULT_MANIFEST)
        manifest = load_manifest(manifest_path)
        from_dir = resolve_path(args.from_dir, ROOT) if args.from_dir is not None else None

        if args.baseline and args.check:
            raise ToolError("--baseline and --check are mutually exclusive")
        if args.update_only and (args.baseline or args.check):
            raise ToolError("--update-only is its own mode; do not combine it with --baseline/--check")
        if args.update_only and from_dir is not None:
            raise ToolError("--update-only is device-only and cannot be combined with --from-dir")
        if args.update_only and args.only:
            raise ToolError("use either --update-only <ids> or --only <ids>, not both")
        if args.update_only:
            args.only = args.update_only

        if from_dir is not None:
            if args.baseline or args.check:
                return run_offline(args, manifest)
            return print_views(manifest, from_dir)
        if args.update_only:
            return run_update_only_device(args, manifest)
        if args.baseline:
            return run_baseline_device(args, manifest)
        if args.check:
            return run_check_device(args, manifest)
        return print_views(manifest, None)
    except ToolError as error:
        log(f"ERROR: {error}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
