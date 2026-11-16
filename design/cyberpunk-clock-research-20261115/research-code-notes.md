# 赛博朋克钟面 · 可落地性与代码参考调研

- 日期：2026-11-15（Asia/Shanghai，11:33 起）
- 目标硬件：ESP32-S3 N16R8 / LCDWiki ES3C28P / ILI9341 320×240 横屏 / Arduino-ESP32 2.0.17 / **LVGL 8.4.0** / TFT_eSPI 2.5.44
- 用途：给「依依的礼物」桌面时钟增加 2–3 款赛博朋克风格钟面（现有：GIF 时光动画、Pixel Clock 19×9、白色翻页 + Clock Dashboard 三款）
- 范围：只做调研，**未改动任何固件代码**

## 0. 方法与限制（请先读，影响下面所有结论的可信度）

| 项 | 说明 |
|---|---|
| 星数获取方式 | 通过 shields.io 的 GitHub stars 徽章 JSON 接口实时读取（`https://img.shields.io/github/stars/<owner>/<repo>.json`），**观察时间 2026-11-15 11:40–12:00 CST**，属"页面/接口观察值"，非爬取历史。个别仓库用 `api.ecosyste.ms` 镜像交叉核对（会注明同步日期） |
| 许可证获取方式 | 同上，用 `https://img.shields.io/github/license/<owner>/<repo>.json` + 仓库 README/LICENSE 原文 |
| **图片无法查看** | 本会话工具不能显示/下载图片（`image/*` 返回 unsupported content type，浏览器不可用）。**所有"视觉风格"判断来自 README 文字、目录名与仓库描述，未目视核验像素**。预览链接仅验证到"URL 可访问且返回 image/*" |
| 源码未逐行读 | 只读了 README、`platformio.ini`、LVGL 8.4 本地源码；未克隆或审阅任何第三方 C 代码 |
| LVGL 事实来源 | **本地实际使用的 LVGL 8.4.0 库源码**（`C:\Users\Tian\Documents\Arduino\libraries\lvgl`，version=8.4.0）+ v8.4.0 官方文档 raw 文件。比网页文档更可靠 |

## 1. 结论速览

1. **LVGL 8.4 没有"模糊/发光滤镜"**：全库只有 `lv_canvas_blur_hor()` / `lv_canvas_blur_ver()` 两个**软件模糊函数**（作用于 canvas 缓冲区，不是样式），且**没有** text shadow、没有对象级 filter、没有径向/锥形渐变、没有图片平铺。→ 霓虹光晕必须**离线烘进位图**或**分层描边 + 加色混合**做，不能"运行时开个 glow 属性"。
2. **但有 3 个够用的"赛博朋克原语"**：`LV_BLEND_MODE_ADDITIVE`（文字/弧线/线/矩形/图片全都支持）、`lv_meter`（刻度环 + 弧指示器，天然 HUD）、`LV_DRAW_MASK_TYPE_ANGLE`（锥形扫描遮罩）。这三样足以做出 90% 的赛博朋克观感。
3. **ESP32 上真实跑过赛博朋克钟面的项目存在，但没有 320×240 的**：最接近的是 240×240（CyberWatch、fbiego 系列表盘）与 240×135（cyber-clock）。**未找到任何 320×240 赛博朋克钟面项目**，也没有任何项目公开 320×240 的帧率数据。
4. **最值得参考的三个**：`fbiego/esp32-c3-mini`（LVGL 真机表盘工程范式）、`JnCrMx/cyberwatch`（唯一真正的 Cyberpunk 2077 风格 ESP32 手表固件，带 SDL2 主机模拟器）、`andjiang0083/cyber-clock`（MIT，ESP32-S3 桌面赛博朋克时钟，有实测资源数据）。
5. **风险排序**：全屏每秒重绘 ≫ 大图光晕 ≫ PSRAM 大图每帧读取 > 阴影开销。按"每秒变化面积 ≤8%"设计，2–3 款钟面可以把 app0 增量控制在 250–400 KiB、PSRAM 峰值 ≤200 KiB、秒刷新不卡。

## 2. 参考项目表

> 难度：**A = 可直接移植/直接用**；**B = 需要按 LVGL 8.4 重写但结构与算法可用**；**C = 只能借风格/配色/构图**。
> 星数为 2026-11-15 观察值；"≈"表示徽章取整（如 1.2k）。

| # | 项目 | 链接 | 星 / 许可 | 技术栈 | 能借鉴什么 | 难度 |
|---|---|---|---|---|---|---|
| 1 | **fbiego/esp32-c3-mini** | https://github.com/fbiego/esp32-c3-mini | **551★** / **MIT** | PlatformIO；LVGL **8 分支与 9 分支都有**；240×240 圆形/方形板（C3 mini、M5Dial、Waveshare S3…）；可原生 SDL 构建 | 最成熟的"LVGL 手表/表盘"工程结构：一个 `ui_home` + 表盘选择器 + 每个表盘独立 root obj；42 款表盘（含 `neon_360`、`glow_360`、`terminal_360`、`radar`）；表盘本体基本都是**整屏图片资源 + 少量动态 widget**，恰好匹配本项目的"整屏背景 + 数字层"思路 | **A**（资源做法/页面结构） **B**（代码） |
| 2 | **fbiego/esp32-lvgl-watchface** | https://github.com/fbiego/esp32-lvgl-watchface | **75★** / **MIT** | Kotlin `bin2lvgl.jar`：把表盘二进制转成 LVGL C 代码；240×240 | **工具链**：把设计师给的整屏表盘图转成 LVGL 资源；README 明确"能编译多少表盘受 Flash 限制"——正是本项目要面对的取舍 | **B**（工具思路，Java 产物不必用） |
| 3 | **JnCrMx/cyberwatch** | https://github.com/JnCrMx/cyberwatch | **3★** / **无 LICENSE（GitHub 显示 not specified）** | **PlatformIO + Arduino + LVGL 9.2.2** + LilyGoLib 0.1.0；LILYGO **T-Watch S3 / S3 Plus，240×240**；带 `env:emulator`（SDL2，240×240）主机模拟 | 唯一真正"Cyberpunk 2077 风格"的 ESP32 手表固件：霓虹 app 网格、暗色调、五栏布局、Main Clock/Settings/Tools；**主机 SDL 模拟器工作流**与本项目 host LVGL 一致，可在 PC 上先迭代钟面 | **C**（无许可证，**不能抄代码**；且 LVGL 9 API 与 8.4 不兼容，`lv_meter` 在 9.x 已换成 `lv_scale`） |
| 4 | **andjiang0083/cyber-clock** | https://github.com/andjiang0083/cyber-clock | **0★** / **MIT** | PlatformIO；Arduino 3.3.9；M5Unified/M5GFX（**非 LVGL**）；M5StickS3 / Cardputer，**240×135**；sprite 双缓冲 | ESP32-S3 上真机跑过的赛博朋克桌面钟：**列式数字雨（55 滴、6 字符拖尾、头亮尾暗衰减）**、6 套霓虹配色（Neon Cyan `#00FFFF`/`#FF00FF`、Matrix Green `#00FF41`、Aurora Purple、Ember Orange、Blood Red、Night Red）、40 段麦克风频谱、摇一摇换主题、三段省电；**有实测资源数据**（Flash 1,176,559 B / 3,342,336；RAM 52,872 B / 327,680；33 fps，idle 1 fps，`delay(30)` 封顶） | **B**（雨算法/配色/省电策略重写成 LVGL） |
| 5 | **sebyx07/cybercore-css** | https://github.com/sebyx07/cybercore-css | **82★** / **MIT** | 纯 CSS/SCSS 框架（无 JS）；CSS Layers/container queries | **技法配方库**：glitch（色差 + 扫描线 + 抖动）、neon border（多层 box-shadow 发光）、scanlines、noise、datastream（光扫）、holo card、terminal；配色 token：Cyan `#00f0ff`、Magenta `#ff2a6d`、Yellow `#fcee0a`、Green `#05ffa1`、Void `#0a0a0f`。可直接当"设计规范"抄数值 | **C → B**（CSS 效果翻译成 LVGL 分层绘制） |
| 6 | **keshikan/DSEG** | https://github.com/keshikan/DSEG | **≈1.2k★** / **SIL OFL 1.1** | 七段/十四段 LCD 风格字体（DSEG7 / DSEG14 / DSEGWeather），TTF/WOFF/SFD | **合法的七段数字字体**（OFL 1.1，商用可）；仓库 README 明确"v0.40 起改为 OFL 1.1"。转成 LVGL 字体后即可做"数字管"钟面；冒号与空格等宽、句点零宽，排版要注意 | **A**（TTF → LVGL font converter） |
| 7 | **xxjun9527/jarvis-holographic** | https://github.com/xxjun9527/jarvis-holographic | **45★** / **MIT** | HTML/CSS/JS，"钢铁侠 HUD"风格 AR/3D 界面 | HUD 环/刻度/扫描线/半透明面板的**构图语言**（三层：外环刻度 + 中环数据 + 中心主数字）；只借版式，不借实现 | **C** |
| 8 | nickluong-dev/Cyber-Clock | https://github.com/nickluong-dev/Cyber-Clock | **0★**（ecosyste.ms，同步于 2023-08-02）/ **无许可证** | 纯 JS + Canvas，2020 年作品 | 参考价值低（无许可证、无维护、无真机）；**不建议**投入时间 | **C（可跳过）** |

### 预览图（raw URL，**未下载、仅验证可访问**）

| 项目 | 预览 | 验证情况 |
|---|---|---|
| cyberwatch | https://raw.githubusercontent.com/JnCrMx/cyberwatch/main/docs/demo.webp | 返回 `image/webp`（HTTP 200），为动图 |
| cyber-clock | https://raw.githubusercontent.com/andjiang0083/cyber-clock/main/docs/images/cyber-clock-sticks3-matrix-green.jpg | 返回 `image/jpeg`（HTTP 200） |
| esp32-c3-mini · neon | https://raw.githubusercontent.com/fbiego/esp32-c3-mini/master/src/faces/neon_360/watchface.png | 返回 `image/png`（HTTP 200） |
| esp32-c3-mini · glow | https://raw.githubusercontent.com/fbiego/esp32-c3-mini/master/src/faces/glow_360/watchface.png | 返回 `image/png`（HTTP 200） |
| esp32-c3-mini · terminal / radar | 同目录 `terminal_360/`、`radar/` 下的 `watchface.png` | **未逐个验证**（目录名来自 `src/faces/README.md`，该文件列出 42 款） |
| cybercore-css · glitch | https://raw.githubusercontent.com/sebyx07/cybercore-css/main/assets/img.png | 返回 `image/png`（HTTP 200） |
| DSEG 样张 | https://raw.githubusercontent.com/keshikan/DSEG/master/sample/sample_image.png | **未验证**（来自 README 相对路径） |

## 3. 技法 → LVGL 8.4 对应表

> "本机"= 本项目固件（`hardware/arduino/yiyi_desk/`）。"LVGL 8.4"= 本地库源码事实。

| 赛博朋克技法 | 常见 CSS/前端做法 | LVGL 8.4 可行做法（具体 API） | 成本 / 结论 |
|---|---|---|---|
| **霓虹文字发光** | `text-shadow: 0 0 6px #0ff, 0 0 18px #0ff` | ① **多层 label 叠加**：同文本 2–3 份，副本放大 1–2px、`text_opa` 30–60、亮色，`lv_obj_set_style_blend_mode(o, LV_BLEND_MODE_ADDITIVE, 0)`；② 把文字画进 `lv_canvas`，用 `lv_canvas_blur_hor/ver()` 模糊后当底层，再叠清晰层 | **无原生 shadow**：`lv_draw_label_dsc_t` 只有 `opa`，**没有** shadow/recolor（`src/draw/lv_draw_label.h`）。官方论坛（v8 标签）给的也是"两个 label 叠加"方案 |
| **七段数码字体** | `@font-face` DSEG7 + `text-shadow` | ① DSEG7/DSEG14（OFL）→ LVGL font converter → 4bpp 字体；② **更省**：自绘七段 = 7 个圆角矩形（`lv_obj` 或 draw 事件里的 `lv_draw_rect_dsc_t`），发光用"外扩半透明层 + 实心层" | 字体法 ≈ 6–12 KB Flash（估算）；自绘法 **0 Flash**、可任意加光晕，推荐用于主钟面 |
| **Glitch 色差（chromatic aberration）** | 伪元素 `::before/::after` + `text-shadow: -2px 0 red, 2px 0 cyan` + clip 动画 | 同一数字/标题画 2 份副本，`lv_obj_set_style_img_recolor()` 或 `text_color` 分别设品红/青，`blend_mode=ADDITIVE`，`x` 偏移 ±1–2px，用 `lv_anim` 每 0.5–1 s 随机跳 | **小区域可行**（数字 120×80 → 每次 ≈9.6 KB 上屏）。**不要全屏 glitch** |
| **扫描线 / CRT 线** | `repeating-linear-gradient` 叠加层 | **LVGL 8.4 没有图片平铺**（全库无 tile API）。→ **把扫描线直接烘进背景图**（`LV_IMG_CF_INDEXED_8BIT`）；若要"移动亮带"，做一条 320×2 的 obj 沿 y 移动，4–8 fps | 烘进背景 = 0 运行时成本，**强烈推荐** |
| **HUD 圆环 / 刻度** | SVG `stroke-dasharray` + `conic-gradient` | `lv_meter`：`lv_meter_add_scale` + `lv_meter_set_scale_ticks` + `lv_meter_set_scale_major_ticks` + `lv_meter_add_arc`（半径用 `r_mod` 调）；或 `lv_arc` | **LVGL 8.4 原生支持**（`LV_USE_METER=1`、`LV_USE_ARC=1` 默认开）。锥形渐变没有 → 用"角度遮罩 `LV_DRAW_MASK_TYPE_ANGLE` + 纯色"或分段多色弧 |
| **渐变填充** | `linear-gradient` / `radial-gradient` | 仅 `LV_GRAD_DIR_VER` / `_HOR`（**没有径向/锥形**）；`LV_DITHER_GRADIENT` 本项目已开 | 径向光晕请直接烘图 |
| **全息网格 / 透视地板** | CSS 3D transform + 背景图 | 烘成 320×240 背景图（见资源估算），或用 draw 事件逐线画（每帧 ~20 条线，仅静态时可接受） | 静态用图最省 |
| **城市天际线** | 背景图 + 灯点动画 | 背景图下半部烘进 `INDEXED_8BIT` 图；灯点用 2–4 个小矩形 + `blend_mode=ADDITIVE` 做闪烁 | 图 76.8 KB/张；灯点几乎零成本 |
| **数字雨** | canvas 逐帧字符 | 只用左右两条窄带（各 40–48 px 宽），字符用预生成 `ALPHA_8BIT` 小蒙版（`img_recolor` 染色），每列 200–400 ms 变一次 | 失效面积 ≈15–20% 屏；**全屏雨不可行**（见风险 1） |
| **加色混合发光** | `mix-blend-mode: screen/plus-lighter` | `LV_BLEND_MODE_ADDITIVE / SUBTRACTIVE / MULTIPLY`（**没有 DIFFERENCE**）；`lv_draw_sw_letter.c:188`、`lv_draw_sw_arc.c:73`、rect/line/img 都把 `blend_mode` 传给混合器 | **可用且便宜**，是"发光感"的主力 |
| **半透明遮罩/异形切角** | `clip-path: polygon()` | `LV_DRAW_MASK_TYPE_POLYGON / LINE / RADIUS / ANGLE / FADE / MAP`（`src/draw/lv_draw_mask.h`） | 可用于切角面板、扫描扇形 |

## 4. LVGL 8.4 关键事实核验（逐条给证据）

| 问题 | 结论 | 证据 |
|---|---|---|
| **是否支持模糊 / 发光滤镜？** | **不支持对象级/样式级模糊与发光**。全库只有 `lv_canvas_blur_hor(canvas, area, r)` / `lv_canvas_blur_ver(...)` 两个**软件模糊**，作用于 canvas 像素缓冲，需自己做（一次性）。SDL 后端内有 stack blur，但仅在 PC 模拟器生效 | `src/widgets/lv_canvas.h:168/176`；全库 grep `blur` 仅 canvas + 内部阴影上采样 + sdl 后端 |
| 文字有没有 shadow？ | **没有**。`lv_draw_label_dsc_t` 字段只有 `opa`、`color`、`font`、`align`、`blend_mode` 等，无 shadow；v8.4.0 官方 `docs/widgets/core/label.md` 也无 "text shadow" 小节（8.3 文档曾有，建议"两层 label 叠加"） | `src/draw/lv_draw_label.h`；https://raw.githubusercontent.com/lvgl/lvgl/v8.4.0/docs/widgets/core/label.md ；官方论坛 https://forum.lvgl.io/t/how-set-a-shadow-to-a-text/14043 |
| `shadow_*` 样式能作用在文字/图片 alpha 上吗？ | **不能**。`draw_shadow()` 的输入是对象的**圆角矩形区域**，先把该矩形模糊成阴影，再用半径遮罩挖掉本体；与字形/图片透明度无关 | `src/draw/sw/lv_draw_sw_rect.c:427–560`（`shadow_draw_corner_buf` 生成角块） |
| 阴影成本？ | 角块缓冲 = `(shadow_width + radius)² × 2 B`，**默认 `LV_SHADOW_CACHE_SIZE=0` 即每帧重算**（本项目未覆盖该默认值）。大 `shadow_width`（如 20–30 px）对大矩形会很贵 | `lv_draw_sw_rect.c:479–502`；`src/lv_conf_internal.h` `LV_SHADOW_CACHE_SIZE 0` |
| 渐变方向？ | 仅 `NONE / HOR / VER`，**无径向、无锥形** | `src/misc/lv_style.h:127–129` |
| 混合模式？ | `NORMAL / ADDITIVE / SUBTRACTIVE / MULTIPLY` 四种；**无 DIFFERENCE**；text/arc/line/rect/img 全部支持 | `src/misc/lv_style.h:88–90`；`src/draw/sw/lv_draw_sw_letter.c:188,258,451`；`lv_draw_sw_arc.c:73` |
| 图片能否染色？ | 可以。`lv_obj_set_style_img_recolor()` + `img_recolor_opa`；**`LV_IMG_CF_ALPHA_8BIT` 走最快路径，颜色直接取 `recolor`**（不受 `recolor_opa` 影响；对象自身 `opa` 仍生效），1 B/px | `src/draw/sw/lv_draw_sw_img.c:71–82` |
| 4bpp alpha 蒙版是否也快？ | **不**（`LV_IMG_CF_ALPHA_4BIT` 无专用快速路径，落到逐像素通用分支）。`LV_IMG_CF_RGB565A8`（3 B/px）有快速路径 | `lv_draw_sw_img.c:65–96` |
| 图片能否平铺？ | **不能**（无 tile API） | 全库 grep `LV_IMG_TILE/tile_mode` 无匹配 |
| 旋转/缩放图片代价？ | 变换走逐像素路径；**本项目 `LV_IMG_CACHE_DEF_SIZE=0`**，无缓存 → 每帧重新采样 | `lv_draw_sw_img.c:54`；`yiyi_desk/lv_conf.h:34` |
| `lv_meter` 可用性 | 可用（刻度/主刻度/弧/指针/刻度线改色），并支持 `LV_EVENT_DRAW_PART_BEGIN` 自定义每个刻度 | `src/extra/widgets/meter/lv_meter.h`；https://raw.githubusercontent.com/lvgl/lvgl/v8.4.0/docs/widgets/extra/meter.md |
| canvas 可用性 | 可用：`lv_canvas_set_buffer/set_px_color/draw_rect/draw_text/draw_line/copy_buf/transform/fill_bg/blur_hor/blur_ver` | `src/widgets/lv_canvas.h` |

> 补一条版本风险：LVGL **9.3 起**才有 drop shadow（PR #9331 标题 `feat(dropshadow): add drop shadow support`）。**9.x 更高版本是否新增 blur 我未核验，按"没有"处理**。本项目的 8.4 无论如何都用不上。

## 5. 本项目已有能力（可直接复用，别重复造）

| 能力 | 位置 | 对赛博朋克钟面的价值 |
|---|---|---|
| 整屏 8-bit 索引背景（320×240，`data_size = 77,824`） | `src/gift_assets/gift_clock_day.c:2436` 等 6 张 | 城市天际线/全息网格/扫描线**直接烘进背景**，零运行时成本 |
| 数字蒙版 + 染色 + 旋转缩放（RGB565A8 canvas 在 PSRAM） | `src/pages/dashboard_clock_page.cpp:179, 394` | 现成的"霓虹数字"管线：把蒙版换成带发光的蒙版即可 |
| **自定义绘制事件**（直接 `lv_draw_rect_dsc_t` / `lv_draw_line_dsc_t` / `lv_draw_label_dsc_t`） | `src/pages/dashboard_clock_page.cpp:206–227` | 一个回调里画环、刻度、发光描边，比几十个 obj 便宜；**HUD 环首选此路** |
| 帧差上屏 + PSRAM 快照 + 内部 RAM 320×24 条带 | `src/display.cpp:261–275, 291`；README「完整帧差分上屏」 | 局部刷新已优化好；赛博朋克只需控制"失效面积" |
| 主机 LVGL 截图/回归框架 | `hardware/scripts/verify_*_ui.py` | 钟面先在 PC 上对帧，再上板 |
| 钟面选择页 + 预览图机制 | `src/pages/*` + `gift_clock_preview_*.c`（29–43 KB/张） | 新增 3 款要各配一张预览图 |

## 6. 三款钟面建议方案（含资源与帧率估算）

> 估算基准（均为**估算**，非实测）：SPI **40 MHz**（`hardware/scripts/panel_build_flags.ps1` 的 `SPI_FREQUENCY=40000000`；README 历史条目写的 20 MHz 是旧版记录）。整帧 320×240×2 = **153,600 B**，40 Mbit/s 下理论 **30.7 ms**，实测含命令开销约 **33–40 ms** → 纯传输上限约 **25–30 fps**，且这还没算渲染。本项目 `LV_DISP_DEF_REFR_PERIOD` 保持默认 **30 ms**（≤33 fps）。

### 方案 A｜NEON-7「霓虹七段 HUD」（推荐首发）
- 构图：上 2/3 黑色 + 全息网格背景，中央 4 位七段数字（自绘矩形），下方日期条，外圈 `lv_meter` 刻度环（秒进度），四角小状态块。
- 发光做法：每个数字 = 外层 2 圈半透明描边（`blend_mode=ADDITIVE`）+ 实心层；不用滤镜。
- 资源：背景 1 张 77 KB + 0 字体 + 0 大图 ≈ **80 KiB Flash**；PSRAM 仅复用现有帧缓冲。
- 变化面积/秒：4 位数字 ≈ 260×90 px ≈ 23 KB → 单次上屏 ~5–6 ms（传输）+ 渲染；**可做到每秒刷新不掉帧**。
- 帧率风险：低（只有数字区失效，环每 250 ms 更新一次也可只失效环带）。

### 方案 B｜GLITCH-88「故障大字 + 城市天际线」
- 构图：底部 60 px 天际线（烘进背景），中部 88 px 超大数字，2–3 层品红/青色差副本，随机 1–2 s 抖一次；上部细扫描线（烘进图）。
- 资源：背景 77 KB + 数字蒙版（ALPHA_8BIT，10 字形 ≈ 40–50 KB）+ 可选第二套配色蒙版 ≈ **120–170 KiB**。
- 变化面积：数字区 300×110 ≈ 66 KB → 单次 12–15 ms；**glitch 只在"抖"的那几帧全字重绘**，每秒 1–2 次，可接受；平时只重绘秒的个位（窄条）。
- 帧率风险：中。要点：色差副本只做 ±2 px 位移、**不做 zoom**（无 image cache，缩放很贵）。

### 方案 C｜RAIN-HUD「数字雨 + 环」 HUD
- 构图：左右各 44 px 数字雨窄带（可关）+ 中央圆环 HUD（`lv_meter` 两圈）+ 中央时间。
- 资源：背景 77 KB + 12 个字符小蒙版（每个 24×24 ALPHA_8BIT ≈ 0.6 KB）≈ **85 KiB**；RAM 另需 2×44×240 的滚动缓冲（≈21 KB PSRAM）。
- 刷新：雨 4 fps、每列独立 200–400 ms；失效面积 ≈ 2×44×240 = 21 KB/帧 → 4 fps 下总带宽 ≈ 84 KB/s（相对整帧 153.6 KB，即 0.55 帧/s 等效），**完全够**。
- 帧率风险：低–中；风险点是雨滴"整列重绘"（44×240 一列 ≈ 21 KB/次）要合并成"整条窄带一次失效"，避免每列一个 obj 各失效一次。

### 资源汇总估算

| 项 | 单款 | 3 款合计 | 说明 |
|---|---|---|---|
| Flash 背景 | 77 KiB | 231 KiB | `INDEXED_8BIT` 320×240 = 76,800 B + 1,024 B 调色板 |
| Flash 数字/字符 | 0–50 KiB | 0–130 KiB | 自绘七段为 0；蒙版按 10 字形 ALPHA_8BIT 估 |
| Flash 预览图 | 30–43 KiB | 90–130 KiB | 与现有 `gift_clock_preview_*` 同量级 |
| **Flash 合计** | **80–170 KiB** | **≈250–490 KiB** | **在 0.86 MiB（README 记录）或 2.0 MiB（任务给定）内都放得下** |
| PSRAM 峰值 | 0–150 KiB | 逐页释放，不同时存在 | 数字 canvas / 雨缓冲；退出钟面必须释放（现有页面已有该习惯） |
| 内部 RAM | 0 | 0 | 不要新增 draw buffer |
| 代码量 | 500–900 行 | **1,800–2,600 行 C++**（含选择页/设置项）+ 资源生成脚本 | 参照 `dashboard_clock_page.cpp`（31.9 KB ≈ 900 行） |

## 7. 风险与规避（按严重度）

1. **每秒全屏重绘 = 硬上限**。整帧 153.6 KB / 40 MHz ≈ **31–40 ms**，加上 PSRAM 帧差扫描（76,800 px 比较，约 2–4 ms），全屏动画最多 15–20 fps 且吃满 CPU，会让触摸/其他页面调度变卡。
   - 规避：扫描线、网格、天际线**全部烘进背景**；只有数字区/环带/窄条雨带失效；目标"每秒失效面积 ≤8%"。
2. **大图光晕 / 每帧模糊**。LVGL 8.4 没有模糊滤镜；`lv_canvas_blur_*` 是纯软件两趟模糊，320×240 一次 ≈ 数万次像素运算（估 10–30 ms），**每帧做必爆**。
   - 规避：① 离线把发光烘进蒙版 PNG；② 运行时用"2–3 层描边 + ADDITIVE"模拟；③ `lv_canvas_blur` 只在**进入页面时**做一次并缓存。
3. **PSRAM 读取速度**。本项目已经把 LVGL draw buffer 放**内部 RAM 320×24 条带**（`display.cpp:265`），PSRAM 只放快照帧。新钟面别把 canvas 当每帧读写的中转——尤其别把 draw buffer 改回 PSRAM。
4. **阴影开销**。`LV_SHADOW_CACHE_SIZE=0`（本项目未改）→ 每次重绘重算角块；对 28 个七段矩形各加 `shadow_width=20` 会明显变慢。
   - 规避：七段不要用 shadow 做发光（用分层矩形）；若必须用，先开 `LV_SHADOW_CACHE_SIZE`（例如 4096）并实测。
5. **无 image cache 的缩放/旋转**。`LV_IMG_CACHE_DEF_SIZE=0`，`lv_img_set_zoom/angle` 每帧重新采样。glitch 只用位移，不要缩放。
6. **字体许可**。DSEG（OFL 1.1）可用；**Digital-7（Style-7）不是 OFL，官方说明为免费个人使用、商用需授权**（我未核验到官方许可页，**标记不确定**）→ **不要嵌入固件**。也可完全不用字体（自绘七段）。
7. **第三方代码许可**。`JnCrMx/cyberwatch` **无 LICENSE**（GitHub 徽章 `not specified`）→ 默认保留一切权利，**只能参考风格，不能复制代码**。可安全借鉴代码的是 MIT 的 `andjiang0083/cyber-clock`、`fbiego/*`、`cybercore-css`、`jarvis-holographic`，以及 OFL 的 DSEG。
8. **LVGL 版本错配**。网上大量赛博朋克 LVGL 示例是 9.x（`lv_scale`、`lv_obj_set_style_*_filter` 等），8.4 无这些 API；`lv_meter` 在 9.x 已被 `lv_scale` 取代。抄示例前先确认版本。
9. **Flash 余量口径不一致**。任务给的是"app0 剩余约 2.0 MiB"；仓库 README 最新记录为镜像 `8,015,760 B`、**余 897,136 B（≈0.86 MiB）**。若按后者，3 款钟面（250–490 KiB）仍可放下但**余量只剩 ~400–650 KiB**，建议先确认口径再定资源预算。
10. **`LV_COLOR_SCREEN_TRANSP=0`**：屏幕级逐像素透明不可用，别指望"整层半透明叠加"；半透明只能靠 obj 级 `opa`（会走混合，成本正常）。

## 8. 未核验 / 不确定（明确标注）

- **所有项目的实际视觉效果**：未看任何图片/视频（工具限制）。"霓虹""HUD""glitch"等描述来自 README 文字与目录名。
- **Vetro**：任务提到的 "Vetro" 未能核实——多轮搜索未命中对应的 ESP32 手表/表盘项目，**不作任何结论**（可能记错名字或项目已改名）。
- **CyberWatch 具体数值**：未公开 Flash/RAM/帧率；仅在 `platformio.ini` 确认 `lvgl @ 9.2.2`、`lilygo-t-watch-s3`、SDL 模拟器 240×240。
- **LVGL 9.3+ 是否新增 blur**：未核验。
- **Digital-7 许可**：未核验到官方许可页。
- **`esp32-c3-mini` 的 LVGL 8 分支细节**：README 指向 `tree/lvgl_8` 分支，未审阅代码。
- **所有资源/帧率数字**：均为按本项目结构推算的**估算**，未在实板测量。

## 9. 参考链接

- fbiego/esp32-c3-mini — https://github.com/fbiego/esp32-c3-mini ；表盘目录 https://github.com/fbiego/esp32-c3-mini/blob/master/src/faces/README.md
- fbiego/esp32-lvgl-watchface — https://github.com/fbiego/esp32-lvgl-watchface
- JnCrMx/cyberwatch — https://github.com/JnCrMx/cyberwatch ；Open Hardware Directory 条目（第三方描述，称 PlatformIO/Arduino/LVGL）https://openhardware.directory/projects/cyberwatch-cyberpunk-themed-t-watch-s3-firmware
- andjiang0083/cyber-clock — https://github.com/andjiang0083/cyber-clock
- sebyx07/cybercore-css — https://github.com/sebyx07/cybercore-css
- keshikan/DSEG — https://github.com/keshikan/DSEG
- xxjun9527/jarvis-holographic — https://github.com/xxjun9527/jarvis-holographic
- LVGL v8.4 meter 文档 — https://github.com/lvgl/lvgl/blob/v8.4.0/docs/widgets/extra/meter.md
- LVGL 论坛「How set a shadow to a text」(v8) — https://forum.lvgl.io/t/how-set-a-shadow-to-a-text/14043
- LVGL dropshadow PR（9.x 才加入）— https://github.com/lvgl/lvgl/pull/9331 （页面加载超时，仅标题来自搜索结果，未读到正文）
- 本地 LVGL 8.4 证据：`src/misc/lv_style.h:88-90,127-129`、`src/draw/lv_draw_label.h:41-46`、`src/draw/sw/lv_draw_sw_img.c:65-96`、`src/draw/sw/lv_draw_sw_rect.c:427-560`、`src/widgets/lv_canvas.h:163-176`、`src/extra/widgets/meter/lv_meter.h`
