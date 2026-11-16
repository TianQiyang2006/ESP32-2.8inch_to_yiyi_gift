"""设备负例：滑动后不得触发点击（T0–T17；审计 02 §7 / task-10）。

在真板上通过 USB CDC 运行，断言：
  * status.page / status.app（翻页、App 入口、滑动后正常点击）；
  * 截图裁剪差分（Wi-Fi 键盘密码框、Wi-Fi 列表、日历格、课程表周标题——
    这些副作用没有 status 字段可读）；
  * uptimeMs 单调（测试期间不得重启）。

前提：Phase 2 输入层抑制（desk.cpp onTouchGesture 里 lv_indev_wait_release）与
Wi-Fi 键盘 K2（CLICK_TRIG + 长按）已经合并。旧固件上跑会失败，可作为缺陷复现记录。

安全约束（与 V2 接口契约 §8 一致）：
  * 不点“保存/连接”（不写 Wi-Fi 凭据）；不点列表里的网络行（只点“重新搜索”行与
    “手动输入”行；它们不会持久化任何东西）；
  * T14 滑杆会持久化改亮度（NVS），默认 SKIP，需显式 --allow-slider 才执行并尝试还原；
  * 结束前回到 page 0；结束时断言 brightness/volume 未变（除非 T14 授权）。

用法：
  python hardware/scripts/check_mistouch_device.py --port COM8
  python hardware/scripts/check_mistouch_device.py --port COM8 --allow-slider
  python hardware/scripts/check_mistouch_device.py --port COM8 --full   # 追加 selftest
  python hardware/scripts/check_mistouch_device.py --port COM8 --long-press  # T17 长按近似

复用 inspect_device.py 的 open_device/warmup/command/read_status/capture；它自带的
check() 是 stress() 的内嵌函数，这里按同样语义在模块级实现（等 transition 结束、
断言 page/app、uptime 单调）。T15 只提醒另跑 stress/手势脚本，不自动开跑长回归。
T17（长按退格/长按模式按钮）无法用单次串口 simulate 复现（按下保持约 220ms，短于
LVGL 400ms 长按阈值）；默认 SKIP，加 --long-press 会用连续 tap 维持按下做近似自动化，
真机手感仍以 T16/T17 的人工清单为准。
"""
import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "hardware" / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from PIL import Image, ImageChops  # noqa: E402

import inspect_device as dev  # noqa: E402

DEFAULT_OUT = ROOT / "build" / "mistouch-review"

# App 枚举（src/app_state.h:25）：None=0, Weather=3, Calendar=4, Countdown=5,
# Settings=6, Phone=7, About=8, Diagnostics=9, Voice=10, Wifi=11, Clock=12。
APP = {"none": 0, "weather": 3, "calendar": 4, "countdown": 5, "settings": 6,
       "phone": 7, "about": 8, "diagnostics": 9, "voice": 10, "wifi": 11, "clock": 12}

# 设置页行 y（gift_ui.cpp:312-314）：Wi-Fi 行 y=126..153；连接手机 153..180；…
SETTINGS_WIFI_ROW = (160, 139)
# Wi-Fi 列表（wifi_setup.cpp:302）：“手动输入网络名称”在列表内 y=69，面板原点 (8,38)。
WIFI_MANUAL_ENTRY = (160, 122)
# 键盘矩阵（wifi_setup.cpp:437-445）：4×10，行列见 design/WiFi配网键盘 设计稿 §4.1。
KEY_ONE = (17, 148)
KEY_SWIPE = "swipe 20 148 200 148"
# 键盘页命令行（wifi_setup.cpp:426-434）：返回按钮 (244,92,72,26)。
WIFI_RETURN = (280, 105)
# 密码输入框（wifi_setup.cpp:423）：(52,63,258,26)。
PASSWORD_BOX = (52, 63, 310, 89)
# Wi-Fi 列表面板（wifi_setup.cpp:399）：(8,38,304,174)。
WIFI_LIST_BOX = (8, 38, 312, 212)
# 课程表绿头带（screen_schedule.cpp）：标题/日期/N 节；避开右上角状态图标。
SCHEDULE_HEADER_BOX = (0, 0, 218, 32)
# 日历网格 + 底部“设为期待”条（desk.cpp:618-707）。
CALENDAR_BOX = (10, 88, 310, 239)


class FrameDir:
    """Screenshot helper: 每次抓图存一个 PNG 到 --out。"""

    def __init__(self, out):
        self.out = Path(out)
        self.out.mkdir(parents=True, exist_ok=True)

    def shot(self, port, name):
        path = self.out / (name + ".png")
        dev.capture(port, path)
        with Image.open(path) as image:
            return image.convert("RGB")


# ---- status helpers（语义对齐 inspect_device.stress() 内嵌 check()） --------
def _assert_state(state, page=None, app=None):
    if page is not None and state["page"] != page:
        raise RuntimeError("expected page %s, got page=%s app=%s"
                           % (page, state["page"], state["app"]))
    if app is not None and state["app"] != app:
        raise RuntimeError("expected app %s, got page=%s app=%s"
                           % (app, state["page"], state["app"]))


def check(port, page=None, app=None, timeout=2.5):
    """等过渡结束并断言 page/app（read_status 自身会在设备重启时抛错）。"""
    state = dev.read_status(port)
    deadline = time.monotonic() + timeout
    while state.get("transitionActive") and time.monotonic() < deadline:
        time.sleep(0.04)
        state = dev.read_status(port)
    if state.get("transitionActive"):
        raise RuntimeError("page transition did not complete in %.1fs" % timeout)
    _assert_state(state, page, app)
    return state


def wait_state(port, page=None, app=None, timeout=5.0):
    """轮询到 page/app 命中且过渡结束；超时抛出最后状态。"""
    deadline = time.monotonic() + timeout
    state = None
    while time.monotonic() < deadline:
        state = dev.read_status(port)
        if state.get("transitionActive"):
            time.sleep(0.05)
            continue
        try:
            _assert_state(state, page, app)
            return state
        except RuntimeError:
            time.sleep(0.05)
    raise RuntimeError("timeout waiting page=%s app=%s; last=%r" % (page, app, state))


def reset_page(port, page=0):
    # A synthetic gesture spans ~350 ms (press samples + two release samples). Give
    # the previous case's last tap time to finish before switching pages, otherwise
    # the pending release can be delivered on the next page (test-tool race only).
    time.sleep(0.45)
    dev.command(port, "page %d" % page)
    return wait_state(port, page=page, app=APP["none"])


def tap_until(port, x, y, page=None, app=None, timeout=4.0, retries=1):
    """Send a tap and wait for the expected state; retry once on a dropped byte.

    The USB CDC link occasionally loses a byte of a serial command (documented in
    desk.cpp), which used to turn a single dropped `tap` into a red test. The retry
    does not weaken the assertion: it only re-sends the same tap, and a real
    regression keeps failing after the retry.
    """
    last = None
    for _attempt in range(retries + 1):
        dev.command(port, "tap %d %d" % (x, y))
        try:
            return wait_state(port, page=page, app=app, timeout=timeout)
        except RuntimeError as error:
            last = error
            time.sleep(0.4)
    raise last


def back_until(port, page=None, app=None, timeout=4.0, retries=1):
    last = None
    for _attempt in range(retries + 1):
        dev.command(port, "back")
        try:
            return wait_state(port, page=page, app=app, timeout=timeout)
        except RuntimeError as error:
            last = error
            time.sleep(0.4)
    raise last


def open_wifi_list(port):
    dev.command(port, "app settings")
    wait_state(port, app=APP["settings"])
    return tap_until(port, SETTINGS_WIFI_ROW[0], SETTINGS_WIFI_ROW[1], app=APP["wifi"])


def swipe(port, command, settle=0.5):
    """发一条 swipe 命令并等待 ≥0.5s（契约 §8：每步 swipe 后留出过渡时间）。"""
    dev.command(port, command)
    time.sleep(settle)


def command_until(port, text, needles, timeout=6.0):
    """发一条串口命令并读到包含任一 needle 的行为止（用于 selftest 文本输出）。"""
    dev.command(port, text)
    deadline = time.monotonic() + timeout
    lines = []
    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", "replace").strip()
        if not line:
            continue
        lines.append(line)
        if any(needle in line for needle in needles):
            return lines
    raise RuntimeError("command %r produced no %r; last lines: %r"
                       % (text, needles, lines[-5:]))


# ---- pixel helpers ---------------------------------------------------------
def changed_pixels(first, second, box=None):
    """返回两个 RGB 帧（可裁剪 box）中像素颜色不同的个数。"""
    if box is not None:
        first = first.crop(box)
        second = second.crop(box)
    raw = ImageChops.difference(first, second).tobytes()
    return sum(1 for i in range(0, len(raw), 3) if raw[i] or raw[i + 1] or raw[i + 2])


def baseline_noise(port, frames, tag, box, copies=5, gap=0.3):
    """连续抓 copies 帧，返回两两最大差异（光标闪烁/时钟抖动等本底噪声）与最后一帧。"""
    images = []
    for index in range(copies):
        images.append(frames.shot(port, "%s-noise%d" % (tag, index)))
        time.sleep(gap)
    noise = 0
    for i in range(len(images)):
        for j in range(i + 1, len(images)):
            noise = max(noise, changed_pixels(images[i], images[j], box))
    return noise, images[-1]


# ---- test cases ------------------------------------------------------------
def test_t0_stress_page_turn(port, frames, args):
    """T0：inspect_device.py stress 的第一步—page 0 右滑 60→260，只应翻到 page 1。"""
    reset_page(port)
    swipe(port, "swipe 60 120 260 120")
    state = wait_state(port, page=1, app=APP["none"])
    reset_page(port)
    return {"page": state["page"], "app": state["app"]}


def test_t1_home_clock_left(port, frames, args):
    """T1：page 0 左滑、抬手 (230,70) 落在时钟卡内：只翻页，不打开 Clock。"""
    reset_page(port)
    swipe(port, "swipe 290 70 230 70")
    state = wait_state(port, page=6, app=APP["none"])
    reset_page(port)
    return {"page": state["page"], "app": state["app"]}


def test_t2_home_clock_right(port, frames, args):
    """T2：page 0 右滑、抬手 (270,70) 落在时钟卡内：只翻页，不打开 Clock。"""
    reset_page(port)
    swipe(port, "swipe 150 70 270 70")
    state = wait_state(port, page=1, app=APP["none"])
    reset_page(port)
    return {"page": state["page"], "app": state["app"]}


def test_t3_home_weather_card(port, frames, args):
    """T3：page 0 右滑、抬手 (100,180) 落在天气卡内：不打开 Weather App。"""
    reset_page(port)
    swipe(port, "swipe 30 180 100 180")
    state = wait_state(port, page=1, app=APP["none"])
    reset_page(port)
    return {"page": state["page"], "app": state["app"]}


def test_t4_home_week_card(port, frames, args):
    """T4：page 0 右滑、抬手 (200,180) 落在周卡内：不打开 Countdown。"""
    reset_page(port)
    swipe(port, "swipe 130 180 200 180")
    state = wait_state(port, page=1, app=APP["none"])
    reset_page(port)
    return {"page": state["page"], "app": state["app"]}


def test_t5_home_calendar_card(port, frames, args):
    """T5：page 0 右滑、抬手 (290,180) 落在日历卡内：不打开 Calendar。"""
    reset_page(port)
    swipe(port, "swipe 220 180 290 180")
    state = wait_state(port, page=1, app=APP["none"])
    reset_page(port)
    return {"page": state["page"], "app": state["app"]}


def test_t6_schedule_week(port, frames, args):
    """T6：page 5 翻页、抬手 (300,16) 落在“下一周”按钮：翻页但不得改周。

    改周没有 status 字段，用课程表绿头带（第 N 周/日期/节数）截图对比；从 page 5
    重新进入后标题应完全一致。
    """
    dev.command(port, "page 5")
    wait_state(port, page=5, app=APP["none"])
    noise, baseline = baseline_noise(port, frames, "t6-schedule", SCHEDULE_HEADER_BOX, copies=2)
    swipe(port, "swipe 120 16 300 16")
    wait_state(port, page=6, app=APP["none"])
    dev.command(port, "page 5")
    wait_state(port, page=5, app=APP["none"])
    after = frames.shot(port, "t6-schedule-after.png")
    diff = changed_pixels(baseline, after, SCHEDULE_HEADER_BOX)
    if diff > noise + 2:
        raise RuntimeError("T6 FAIL: 课程表周标题/日期变化 diff=%d noise=%d"
                           "（滑动抬手点改了 selectedWeek）" % (diff, noise))
    reset_page(port)
    return {"diff": diff, "noise": noise}


def test_t7_schedule_course_block(port, frames, args):
    """T7（跳过）：课程块坐标随导入课表变化，需按当前设备截图选点；T6 覆盖同一手势路径。"""
    return {"skipped": True,
            "reason": "课程块坐标随课表数据变化；需要时先截图定位色块再手写 swipe 用例"}


def test_t8_calendar_cell(port, frames, args):
    """T8：App::Calendar 横滑、抬手落在日期格：不得选中日期（网格/选择条像素不变）。"""
    dev.command(port, "app calendar")
    wait_state(port, app=APP["calendar"])
    noise, baseline = baseline_noise(port, frames, "t8-calendar", CALENDAR_BOX, copies=2)
    dev.command(port, "swipe 40 100 200 100")
    time.sleep(0.6)
    check(port, app=APP["calendar"])
    after = frames.shot(port, "t8-calendar-after.png")
    diff = changed_pixels(baseline, after, CALENDAR_BOX)
    if diff > noise + 2:
        raise RuntimeError("T8 FAIL: 横滑抬手选中了日历日期 diff=%d noise=%d" % (diff, noise))
    reset_page(port)
    return {"diff": diff, "noise": noise}


def test_t9_wifi_list(port, frames, args):
    """T9：Wi-Fi 列表横滑、抬手落在“重新搜索”行：不得触发重新扫描（列表像素不变）。"""
    open_wifi_list(port)
    time.sleep(3.0)  # 进入配网页会自动 requestScan；等扫描结束、列表稳定
    noise, baseline = baseline_noise(port, frames, "t9-wifi-list", WIFI_LIST_BOX, copies=2)
    dev.command(port, "swipe 60 77 220 77")
    time.sleep(1.5)  # 若误触会立即进入“正在搜索…”，给 UI 时间变化
    check(port, app=APP["wifi"])
    after = frames.shot(port, "t9-wifi-list-after.png")
    diff = changed_pixels(baseline, after, WIFI_LIST_BOX)
    if diff > noise + 3:
        raise RuntimeError("T9 FAIL: 横滑误触了 Wi-Fi 列表按钮（重新扫描）diff=%d noise=%d"
                           % (diff, noise))
    reset_page(port)
    return {"diff": diff, "noise": noise}


def test_t10_keyboard(port, frames, args):
    """T10：Wi-Fi 键盘上从键“1”横滑到“7”：不得输入任何字符；T10+ 正常点击仍能输入。

    进入手动输入后默认焦点是密码框（password 模式圆点），用密码框像素裁剪做断言：
    先抓多帧建立光标闪烁噪声基线，滑动后变化不应超过基线；再正常点击两次键“1”，
    变化必须显著超过基线（防止把正常输入误杀成 PASS）。
    """
    open_wifi_list(port)
    time.sleep(3.0)
    dev.command(port, "tap %d %d" % WIFI_MANUAL_ENTRY)
    time.sleep(1.2)
    check(port, app=APP["wifi"])
    noise, baseline = baseline_noise(port, frames, "t10-keyboard", PASSWORD_BOX,
                                     copies=args.noise_copies)
    dev.command(port, KEY_SWIPE)
    time.sleep(0.8)
    after_swipe = frames.shot(port, "t10-keyboard-after-swipe.png")
    swipe_diff = changed_pixels(baseline, after_swipe, PASSWORD_BOX)
    if swipe_diff > noise + 8:
        raise RuntimeError("T10 FAIL: 键盘上滑动后密码框出现字符 diff=%d noise=%d"
                           % (swipe_diff, noise))
    for _ in range(2):  # T10+ 正向对照：两次正常点击，确保变化明显可判
        dev.command(port, "tap %d %d" % KEY_ONE)
        time.sleep(0.45)
    after_tap = frames.shot(port, "t10-keyboard-after-tap.png")
    tap_diff = changed_pixels(after_swipe, after_tap, PASSWORD_BOX)
    if tap_diff <= noise + 12:
        raise RuntimeError("T10+ FAIL: 正常点击键后密码框没有明显变化 diff=%d noise=%d"
                           % (tap_diff, noise))
    dev.command(port, "tap %d %d" % WIFI_RETURN)  # 返回 → 列表（不保存、不连接）
    wait_state(port, app=APP["wifi"])
    reset_page(port)
    return {"swipeDiff": swipe_diff, "tapDiff": tap_diff, "noise": noise}


def test_t11_post_swipe_tap(port, frames, args):
    """T11：滑动后立刻正常点击必须生效（swipeSeen_ 在新按下时清零，不得残留误吞）。"""
    reset_page(port)
    swipe(port, "swipe 290 70 230 70")
    wait_state(port, page=6, app=APP["none"])
    state = tap_until(port, 58, 77, app=APP["clock"])  # page 6 应用页时钟卡
    back_until(port, page=6, app=APP["none"])
    reset_page(port)
    return {"page": state["page"], "app": state["app"]}


def test_t12_vertical_scroll(port, frames, args):
    """T12：阅读详情内竖直滑动：不关详情、不切页（大号像素差=被关掉或换页）。"""
    dev.command(port, "page 3")
    wait_state(port, page=3, app=APP["none"])
    dev.command(port, "tap 120 70")  # 打开每日名言详情（reading_page.cpp:504）
    time.sleep(0.8)
    before = frames.shot(port, "t12-reading-detail.png")
    swipe(port, "swipe 160 80 160 220")  # 竖直拖动（不满足横滑判定）
    time.sleep(0.3)
    state = check(port, page=3, app=APP["none"])
    after = frames.shot(port, "t12-reading-detail-after.png")
    diff = changed_pixels(before, after)
    if diff > 6000:
        raise RuntimeError("T12 FAIL: 竖直滑动把详情页关掉/换页了 diff=%d" % diff)
    dev.command(port, "back")
    time.sleep(0.6)
    reset_page(port)
    return {"page": state["page"], "app": state["app"], "diff": diff}


def test_t13_transition_double_swipe(port, frames, args):
    """T13：过渡中连续两次 swipe 不崩溃、不重启，最终停在合理页。"""
    reset_page(port)
    before = dev.read_status(port)
    dev.command(port, "swipe 60 120 260 120")
    time.sleep(0.1)
    dev.command(port, "swipe 60 120 260 120")
    time.sleep(2.2)
    state = check(port)
    if state["page"] not in (1, 2):
        raise RuntimeError("T13 FAIL: 过渡中连续滑动后 page=%d（期望 1 或 2）"
                           % state["page"])
    if state["uptimeMs"] <= before["uptimeMs"]:
        raise RuntimeError("T13 FAIL: uptime 未增长（疑似重启）")
    reset_page(port)
    return {"page": state["page"], "uptimeDelta": state["uptimeMs"] - before["uptimeMs"]}


BRIGHTNESS_MIN, BRIGHTNESS_MAX = 10, 100
SLIDER_X0, SLIDER_WIDTH = 16, 288


def _brightness_x(value):
    ratio = (value - BRIGHTNESS_MIN) / float(BRIGHTNESS_MAX - BRIGHTNESS_MIN)
    return max(SLIDER_X0, min(SLIDER_X0 + SLIDER_WIDTH,
                              int(round(SLIDER_X0 + ratio * SLIDER_WIDTH))))


def test_t14_slider(port, frames, args):
    """T14（默认跳过）：设置页亮度滑杆拖动。

    拖动会持久化写 NVS（gift_ui.cpp levelEvent -> GiftData::setLevel），默认 SKIP；
    --allow-slider 时执行并尽力把亮度拖回初始值（±1），否则判定失败。
    """
    if not args.allow_slider:
        return {"skipped": True,
                "reason": "T14 会持久化改亮度（NVS）；默认 SKIP，需要时加 --allow-slider"}
    dev.command(port, "app settings")
    state = wait_state(port, app=APP["settings"])
    initial = int(state["brightness"])
    swipe(port, "swipe 40 63 280 63")  # 亮度滑杆 y≈60..67
    time.sleep(1.0)  # 等 dirty 落盘（总计已 ≥1.5s）
    changed = int(check(port, app=APP["settings"])["brightness"])
    restored = changed
    tap_x = _brightness_x(changed)
    for _ in range(8):
        restored = int(check(port, app=APP["settings"])["brightness"])
        if abs(restored - initial) <= 1:
            break
        # 用反馈步进而不是一次公式反推：滑杆坐标↔值有旋钮偏移，反馈几轮即收敛。
        step = int(round((initial - restored) * SLIDER_WIDTH /
                         float(BRIGHTNESS_MAX - BRIGHTNESS_MIN)))
        tap_x = max(SLIDER_X0, min(SLIDER_X0 + SLIDER_WIDTH, tap_x + step))
        dev.command(port, "tap %d 63" % tap_x)
        time.sleep(0.6)
    if abs(restored - initial) > 2:
        raise RuntimeError("T14 亮度未能还原（当前=%d，初始=%d）；请手动在设置页拖回"
                           % (restored, initial))
    reset_page(port)
    return {"initialBrightness": initial, "afterSwipe": changed, "restored": restored}


def test_t15_regression_reminder(port, frames, args):
    """T15：全量回归提醒；--full 时顺带跑设备 selftest（不改持久化状态）。"""
    notes = [
        "另跑: python hardware/scripts/inspect_device.py stress --duration 30 （全量导航回归）",
        "另跑: python hardware/scripts/check_gestures_device.py （手势路由，若已生成）",
    ]
    if args.full:
        lines = command_until(port, "selftest", ("SELFTEST PASS", "SELFTEST FAIL"), timeout=8.0)
        if not any("SELFTEST PASS" in line for line in lines):
            raise RuntimeError("T15 FAIL: 设备 selftest 未 PASS: %r" % lines[-5:])
        notes.append("selftest PASS")
    return {"skipped": not args.full, "notes": notes}


def test_t16_manual_two_finger(port, frames, args):
    """T16（人工）：串口 simulate 只能单点，双指/真实手指手感需人工验证。"""
    return {"skipped": True, "manual": [
        "双指同时按下并横滑：不得误翻页/误开 App/不崩溃",
        "真实手指从主页卡片起滑、松手仍在卡片内：只翻页，不打开 App",
        "Wi-Fi 键盘：从键上起滑横穿键盘：不输入任何字符（T10 的物理手指版）",
    ]}


def _brief(state):
    if not state:
        return None
    keys = ("version", "page", "app", "brightness", "volume", "wifi", "timeValid",
            "transitionActive", "uptimeMs", "resetReason", "minHeapFree")
    return {key: state[key] for key in keys if key in state}


def hold(port, x, y, seconds=1.2, interval=0.12):
    """用重复 tap 命令模拟“按住不放”。

    Touch::simulate 单次只维持约 220ms 按下窗口，短于 LVGL 400ms 长按阈值；连续发
    tap（同一坐标）会不断重开这个窗口，LVGL 侧就是持续按住。最后停发并等 release
    采样（touch.cpp 要求连续两个 release 采样）与事件派发。
    """
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        dev.command(port, "tap %d %d" % (x, y))
        time.sleep(interval)
    time.sleep(0.8)


def test_t17_long_press(port, frames, args):
    """T17（默认跳过，--long-press 才跑）：长按退格连删 + 长按模式按钮跳符号页。

    单次串口 simulate 的按下保持约 220ms < 400ms，无法触发长按；这里用 hold() 连续
    发 tap 维持按下。断言：
      1) 先点 10 次键“1”输入 10 个字符 → 长按退格 ~1.6s → 密码框清空；
      2) 长按模式按钮 → 键盘区变成符号页；再单击一次 → 回到小写页（证明长按抬手
         补发的 CLICKED 已被吞掉，单击循环语义未被破坏）。
    注意：这是串口模拟的近似，真机手感仍以 T16/T17 的人工清单为准。
    """
    if not args.long_press:
        return {"skipped": True,
                "reason": "长按无法用单次 simulate 复现（保持 ~220ms < 400ms）；"
                          "默认 SKIP，需要时加 --long-press（用重复 tap 维持按下）"}
    keyboard_crop = (0, 92, 320, 240)  # 命令行 + 键盘矩阵区
    open_wifi_list(port)
    time.sleep(3.0)
    dev.command(port, "tap %d %d" % WIFI_MANUAL_ENTRY)
    time.sleep(1.2)
    check(port, app=APP["wifi"])
    noise, empty_baseline = baseline_noise(port, frames, "t17-keyboard-empty",
                                           PASSWORD_BOX, copies=3)
    keyboard_noise, lower_baseline = baseline_noise(port, frames, "t17-keyboard-lower",
                                                    keyboard_crop, copies=2)
    for _ in range(10):
        dev.command(port, "tap %d %d" % KEY_ONE)  # 每次单击输入 1 个字符
        time.sleep(0.5)  # > simulate 的按下+释放周期（约 280ms），避免连成“按住”
    typed = frames.shot(port, "t17-keyboard-typed.png")
    if changed_pixels(typed, empty_baseline, PASSWORD_BOX) <= noise + 8:
        raise RuntimeError("T17 FAIL: 10 次单击没有输入字符（基线噪声=%d）" % noise)
    hold(port, 46, 105, seconds=1.6)  # 退格按钮中心 (10..82, 92..118)
    cleared = frames.shot(port, "t17-keyboard-cleared.png")
    cleared_diff = changed_pixels(cleared, empty_baseline, PASSWORD_BOX)
    if cleared_diff > noise + 8:
        raise RuntimeError("T17 FAIL: 长按退格没有连删干净 diff=%d noise=%d"
                           % (cleared_diff, noise))
    hold(port, 124, 105, seconds=0.9)  # 小写按钮中心 (88..160, 92..118) → 长按跳符号页
    symbols = frames.shot(port, "t17-keyboard-symbols.png")
    symbols_diff = changed_pixels(symbols, lower_baseline, keyboard_crop)
    if symbols_diff <= 500:
        raise RuntimeError("T17 FAIL: 长按模式按钮没有切到符号页 diff=%d" % symbols_diff)
    dev.command(port, "tap %d %d" % (124, 105))  # 符号页单击 → 应循环回小写页
    time.sleep(0.8)
    cycled = frames.shot(port, "t17-keyboard-cycled.png")
    cycle_diff = changed_pixels(cycled, lower_baseline, keyboard_crop)
    if cycle_diff > keyboard_noise + 20:
        raise RuntimeError("T17 FAIL: 长按后补发 CLICKED 未被吞/单击循环异常 diff=%d"
                           % cycle_diff)
    dev.command(port, "tap %d %d" % WIFI_RETURN)  # 返回列表（不保存）
    wait_state(port, app=APP["wifi"])
    reset_page(port)
    return {"clearedDiff": cleared_diff, "noise": noise,
            "symbolsDiff": symbols_diff, "cycleDiff": cycle_diff}


# 执行顺序 = 审计 02 §7 的 T0–T17；T7/T14/T17 默认 SKIP（原因见各自 docstring）。
TESTS = [
    ("t0_page_turn", test_t0_stress_page_turn),
    ("t1_home_clock_left", test_t1_home_clock_left),
    ("t2_home_clock_right", test_t2_home_clock_right),
    ("t3_home_weather_card", test_t3_home_weather_card),
    ("t4_home_week_card", test_t4_home_week_card),
    ("t5_home_calendar_card", test_t5_home_calendar_card),
    ("t6_schedule_week", test_t6_schedule_week),
    ("t7_schedule_course_block", test_t7_schedule_course_block),
    ("t8_calendar_cell", test_t8_calendar_cell),
    ("t9_wifi_list", test_t9_wifi_list),
    ("t10_keyboard", test_t10_keyboard),
    ("t11_post_swipe_tap", test_t11_post_swipe_tap),
    ("t12_vertical_scroll", test_t12_vertical_scroll),
    ("t13_transition_double_swipe", test_t13_transition_double_swipe),
    ("t14_slider", test_t14_slider),
    ("t15_regression_reminder", test_t15_regression_reminder),
    ("t16_manual_two_finger", test_t16_manual_two_finger),
    ("t17_long_press", test_t17_long_press),
]


def main():
    parser = argparse.ArgumentParser(description="Mistouch negative tests on the real device")
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--allow-slider", action="store_true",
                        help="执行 T14 亮度滑杆拖动（会持久化改亮度，尽力还原）")
    parser.add_argument("--full", action="store_true", help="T15 追加设备 selftest")
    parser.add_argument("--long-press", action="store_true",
                        help="T17 用重复 tap 模拟长按（退格连删/模式按钮跳符号页）")
    parser.add_argument("--noise-copies", type=int, default=5,
                        help="T10 光标闪烁噪声基线帧数（默认 5）")
    args = parser.parse_args()
    frames = FrameDir(args.out)
    results = []
    notes = []
    initial = None
    final = None
    started = time.monotonic()
    with dev.open_device(args.port) as port:
        time.sleep(0.3)
        port.reset_input_buffer()
        dev.warmup(port)
        initial = dev.read_status(port)
        try:
            for name, test in TESTS:
                try:
                    detail = test(port, frames, args)
                    results.append({"id": name, "ok": True, "detail": detail})
                    print("PASS " + name, flush=True)
                except Exception as error:  # noqa: BLE001 - 汇总所有用例，最后统一失败
                    results.append({"id": name, "ok": False, "error": str(error)})
                    print("FAIL %s: %s" % (name, error), flush=True)
        finally:
            try:
                dev.command(port, "page 0")
                time.sleep(0.5)
                check(port, page=0, app=APP["none"])
            except Exception as error:  # noqa: BLE001
                notes.append("cleanup: 未能回到 page 0: %s" % error)
                print("WARN cleanup:", error, file=sys.stderr)
            try:
                final = dev.read_status(port)
            except Exception as error:  # noqa: BLE001
                notes.append("final status 读取失败: %s" % error)

    failed = [item for item in results if not item["ok"]]
    if initial is not None and final is not None:
        if final["uptimeMs"] <= initial["uptimeMs"]:
            failed.append({"id": "device_alive", "ok": False,
                           "error": "uptime 未增长，疑似测试期间重启"})
        for key in ("brightness", "volume"):
            if not args.allow_slider and final[key] != initial[key]:
                failed.append({"id": "persisted_state", "ok": False,
                               "error": "%s 被测试改变：%s -> %s"
                                        % (key, initial[key], final[key])})
    duration = time.monotonic() - started
    report = {
        "passed": not failed,
        "port": args.port,
        "durationSeconds": round(duration, 1),
        "allowSlider": bool(args.allow_slider),
        "initial": _brief(initial),
        "final": _brief(final),
        "tests": results,
        "notes": notes,
    }
    args.out.mkdir(parents=True, exist_ok=True)
    report_path = args.out / "report.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    reminder = next((item for item in results if item["id"] == "t15_regression_reminder"
                     and item["ok"]), None)
    if reminder:
        for line in reminder["detail"].get("notes", []):
            print("NOTE " + line)
    if failed:
        print("FAIL: %d case(s) failed; report: %s" % (len(failed), report_path))
        for item in failed:
            print("  - %s: %s" % (item["id"], item.get("error")))
        return 1
    print("PASS: all mistouch checks passed; report: %s" % report_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
