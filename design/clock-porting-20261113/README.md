# 本开发板可移植时钟项目 · 2026-11-13

本板：**LCDWiki ES3C28P** — ESP32-S3 N16R8、2.8 寸 IPS 240×320（ILI9341，SPI）、FT6336G 电容触摸（I2C）、ES8311 音频、16 MB Flash、8 MB OPI PSRAM；横屏 320×240。
固件环境：Arduino-ESP32 **2.0.17-cn**、**LVGL 8.4.0**、TFT_eSPI **2.5.44**；双应用布局，桌面 app0 8.5 MiB，当前余量 **2,099,408 B（约 2.00 MiB）**。

移植门槛：
1. 框架为 Arduino（或绘制代码可拆出来）；
2. 显示可改为 4 线 SPI ILI9341、240×320 原生/320×240 横屏；
3. 不依赖本板没有的外设（按键、RTC、传感器可省略；摄像头、TF 卡、点阵屏、舵机、高压管不可用）；
4. Flash 增量不超过约 2 MB，RAM 不爆（当前静态 RAM 108,148 B，PSRAM 充足）；
5. **许可证允许**使用（MIT/Apache 可直接改；无 LICENSE 默认保留版权，只能参考重写）。

本目录的源码副本：`build/research/clock-porting-20261113/`（ESP32_Display_Panel）与 `build/research/clock-styles-20261113/src/`（其余仓库）。

## 一、A 级：可以实际移植（推荐先做）

| 项目 | 星 / 许可 | 技术栈 | 与我们的差异 | 需改动 | 工作量 |
|---|---|---|---|---|---|
| [esp-arduino-libs/ESP32_Display_Panel · squareline_wifi_clock](https://github.com/esp-arduino-libs/ESP32_Display_Panel/tree/main/examples/arduino/gui/lvgl_v8/squareline_wifi_clock) | Apache-2.0 | Arduino + LVGL **8.4.0** + SquareLine UI，UI 按 **320×240** 设计 | 整库要求 arduino-esp32 **≥3.1.0**，本板是 2.0.17-cn；但 UI 只是 LVGL 8 代码，**可只搬 `ui/` 文件夹**，显示/触摸用我们现有的 | 拷 `ui/`；把示例的 NTP 校时接到现有时间源；天气从 OpenWeather 换成现有 UApi 天气；退出时销毁页面；跑 host LVGL 截图回归 | 1–2 天 |
| [fbiego/esp32-c3-mini](https://github.com/fbiego/esp32-c3-mini)（`lvgl_8` 分支） | 551★ / MIT | PlatformIO + **LVGL 8.3**；240×240 表盘合集（20 款） | 屏幕 240×240 方形，本板 320×240 横屏；表盘 `.c` 为 bin2lvgl 生成的 LVGL 8 代码 | 加 320×240 板级配置；挑 1–2 款表盘，把 240×240 坐标改成居中/右侧信息区；资源图按需裁剪 | 1–2 天/款 |
| [reyco2000/ESP32-Chipshift · CYD_Clock](https://github.com/reyco2000/ESP32-Chipshift) | 1★ / MIT | Arduino + TFT_eSPI，辉光管字体（nixie），**320×240** | CYD 用 ST7789，本板 ILI9341；引脚不同；代码直接 TFT 绘制、不走 LVGL | 换 TFT_eSPI 驱动与引脚；绘制包成 LVGL 页面（canvas 或全屏刷新），对接现有钟面切换 | 0.5–1.5 天 |

**结论**：这三个都能真正落到本板。
- 想“整套页面级 UI” → 先搬 `squareline_wifi_clock` 的 `ui/`（Licensed、LVGL 8.4.0、320×240，几乎现成）。
- 想“单个新钟面、体量小” → 先做 CYD_Clock 的辉光管（MIT，代码量约 200 行）。
- 想要“表盘样式库” → 用 fbiego 的 20 款表盘代码按需移植。

## 二、B 级：技术接近，但许可证/框架/驱动需处理

| 项目 | 许可 | 技术栈 | 卡点 |
|---|---|---|---|
| [ThomDyson/CYDFlipClock](https://github.com/ThomDyson/CYDFlipClock) | **无 LICENSE** | Arduino + TFT_eSPI，240×320，`ILI9341_2_DRIVER` 已被注释、默认 ST7789 | 技术上最容易（同 ILI9341、TFT_eSPI、横屏 320×240），但无许可证，建议参照重写或联系作者 |
| [h6161236-spec/tianqi-shizhong-dahuangban](https://github.com/h6161236-spec/tianqi-shizhong-dahuangban) | **无 LICENSE** | PlatformIO + TFT_eSPI（ILI9341_2）+ u8g2，320×240，带 BLE | 同款 ILI9341 屏；BLE 音乐元数据可砍；天气场景引擎值得参考；无许可证，建议重写 |
| [LazydaysCr/moon-phase-clock](https://github.com/LazydaysCr/moon-phase-clock) | **无 LICENSE** | **LVGL 8.3.11** + TFT_eSPI，GC9A01 240×240 圆屏 | LVGL 版本最接近；把圆屏 240×240 改成 320×240 横屏布局即可；月相算法可参考；无许可证 |
| [sl45sms/3dClock](https://github.com/sl45sms/3dClock) | **无 LICENSE** | Arduino + **LVGL 9**，ST7789 240×240，DSEG7 字体 | LVGL 9 → 8 需要降级 API；代码很小，可直接借鉴重写 |
| [ZhouKes/ESP32Clock](https://github.com/ZhouKes/ESP32Clock) | **无 LICENSE** | ESP-IDF + LVGL/TFT_eSPI，240×240 方屏，指针表盘素材 | 框架是 IDF；指针表盘/背景素材可重绘；无许可证 |
| [AndroidCrypto/ESP32_S3_1_9_inch_ST7789_Display](https://github.com/AndroidCrypto/ESP32_S3_1_9_inch_ST7789_Display) | 11★ / MIT | Arduino + LovyanGFX/ST7789，1.9 寸 170×320，辉光管拟真 | 显示库/分辨率都要换；辉光管绘制思路可搬；MIT 可用 |
| [RenatBogdanov/ESP32-C3-Stopwatch](https://github.com/RenatBogdanov/ESP32-C3-Stopwatch) | 0★ / MIT | Arduino + SSD1306 128×64，像素角色 + 星空 | 屏幕完全不同；像素小黑/星空动效可参考重画 |

## 三、C 级：不能移植（只能借样式）

| 类别 | 项目 | 原因 |
|---|---|---|
| 实体辉光管 | mkonhy/ESP32_NixieClock、JebKerman91/Nixie-ESP32 | 需要高压驱动板与真管；本板没有 |
| 实体 VFD | sh1ura/…VFD、tehniq3/…VFD_PT6302 | 需要 VFD 玻璃管与 25 V 驱动 |
| 实体翻牌点阵/机械 | marcinsaj/Flipo-Binary-Clock、morsedot-clock、IanDBallard/MechanicalClock、FluxSand | 需要翻转点阵/机械结构 |
| HUB75 点阵屏 | vladkorotnev/plasma-clock、Keralots/AnimatedPixelClock | 需要外接 HUB75 矩阵，接线与刷新都不属于本板 |
| 小尺寸 OLED | esphome-clock-os（128×64，ESPHome）、GTA6-Countdown-Clock（SSD1309）、slumbercube（SSD1322，ESP-IDF+LVGL9） | 分辨率/驱动/框架都不同，只能借风格 |
| Web / App | clock-dashboard、vue-turn-clock、Flip-Clock-App、love-page | 网页设计，需要在我们 LVGL 里重画 |
| 其他板型方案 | HoloCubic_AIO（依赖 TF 卡、不同板型/框架）、steampunk-clock（ESP8266 + 舵机 + LED） | 硬件不同，移植成本高于重写 |

## 四、推荐落地顺序

1. **今天就能开始**：把 `squareline_wifi_clock` 的 `ui/` 目录单独编译进一个测试页面，验证在我们 320×240 + LVGL 8.4 上显示是否正常（不碰正式固件）。
2. **第一块正式新钟面**：移植 CYD_Clock 的辉光管（MIT、代码量小、与现有三款钟面差异大）。
3. **样式库备选**：从 fbiego 的 20 款表盘里挑“极简大字”“节日插画”“星空”各一款，按 320×240 重排。
4. 无许可证的 CYDFlipClock / tianqi / moon-phase 只做参考；如果确定要整段使用，先联系作者取得许可。

## 五、移植时的共同注意点

- **显示层**：本项目 LVGL 通过 TFT_eSPI flush 输出，第三方 TFT_eSPI 直绘项目要改成 LVGL 页面（或用 LVGL canvas 包一层），否则会和桌面页刷新冲突。
- **坐标**：所有 240×240 素材按“居中 + 两侧留白/信息区”改 320×240；横屏时注意 TFT_eSPI rotation 与触摸映射。
- **字体**：现有 12/16px 中文字库可直接复用；DSEG7/辉光管/像素字体是数字子集，体积很小。
- **时间源**：统一用现有 NTP/系统时间，不要各页面自己起 NTP 客户端。
- **天气**：统一走现有 UApi 通道，避免再引入 OpenWeather key。
- **内存**：大图/多帧资源放 PSRAM 或 flash；单款钟面 Flash 目标 ≤150 KB，为 2 MiB 余量留出空间。

## 六、结论

**能在这块板子上真正跑起来的**，优先 `ESP32_Display_Panel` 的 `squareline_wifi_clock`（Apache-2.0，LVGL 8.4.0，320×240 UI）与 `ESP32-Chipshift` 的辉光管 CYD 时钟（MIT，320×240，TFT_eSPI）；`fbiego/esp32-c3-mini`（MIT，LVGL 8）提供大量可直接移植的表盘代码。其余项目多数受限于“实体外设 / 不同屏幕 / ESP-IDF / LVGL 9 / 无许可证”，只适合做样式参考。
