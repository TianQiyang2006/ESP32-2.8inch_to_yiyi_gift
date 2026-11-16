# 1.68 MiB 空间预算与 GitHub 可放内容 · 2026-11-13

当前固件：`0.17.6-clock-themes`，桌面 app0 8.5 MiB（8,912,896 B），镜像 7,146,816 B，**余量 1,766,080 B ≈ 1.68 MiB**；静态 RAM 118,836 B（内部 SRAM 320 KB，PSRAM 8 MiB）。已有：七页桌面、四页应用（含 10 款游戏）、四种钟面（艺术 GIF／Pixel／白色翻页明暗／天气看板）、闹钟／番茄／倒计时／课程表／天气／音乐／小智等。本目录按“还能加什么、各自占多少”整理，只做调研。

## 一、空间单价（按本工程实测/同类项目折算）

| 类型 | 占用 | 说明 |
|---|---|---|
| 纯代码功能/页面 | 20–80 KB | 现有页面源码 20–37 KB；四款游戏用矢量绘制，几乎不加素材 |
| 全屏图片（320×240，8-bit 索引） | 76.8 KB | 现有钟面/pages 素材口径 |
| 全屏图片（RGB565） | 153.6 KB | 带 alpha 约 230 KB |
| 小图标/装饰图 | 2–45 KB | 现有 gift_* 图标 2–45 KB |
| 照片 JPEG（320×240，质量 80/90） | 24 / 35 KB | 实测生成样本；可直接放 Flash 或 LittleFS |
| GIF 钟面/动画 | 单个 220–470 KB | 现有 3 个 GIF 共约 938 KB；120×120 表情类约 85 KB |
| 新增一档全 CJK 字库 | 300 KB–1 MB | 不划算（现有 12/16 px 已覆盖基本汉字） |
| 数字/像素字体子集 | 5–80 KB | DSEG7、ZhengGeDianHei 子集等 |
| 16 kHz 单声道音频 | IMA 约 8 KB/s（1.68 MiB ≈ 3.6 分钟）；MP3 32 kbps ≈ 4 KB/s（≈7.4 分钟）；48 kbps ≈ 6 KB/s（≈4.9 分钟） | 可直接放 Flash；也可放 LittleFS |
| LittleFS 数据区 | 960 KB | 目前存日记/课表/设置；可挪给照片/录音约 900 KB |

## 二、GitHub 上找到的候选（按“能放几个”）

| 候选 | GitHub | 星/许可 | 占用 | 1.68 MiB 能放 |
|---|---|---|---|---|
| 相册/回忆（JPEG 解码） | [lvgl/lv_lib_split_jpg](https://github.com/lvgl/lv_lib_split_jpg) | 37★ / MIT | 源码约 75 KB → 编译约 30–40 KB；每张照片 24–35 KB | 解码器 + 30–60 张照片（Flash）或全部走 LittleFS |
| 照相机式相册/网页上传参考 | [adgsenpai/ESP32DigitalPhotoFrame](https://github.com/adgsenpai/ESP32DigitalPhotoFrame) | 0★ | 参考实现（ESP-IDF，非直接移植） | 借网页上传/幻灯片的流程 |
| 天气图标（WMO 26 个） | [meshosk/weather-icons](https://github.com/meshosk/weather-icons) | 3★ / Apache-2.0 | 全 26 个 PNG 共 270 KB；挑 8–12 个转 LVGL 约 30–80 KB | 1–2 套 |
| 中文像素字体 | [yzdnn/ZhengGeDianHei-16](https://github.com/yzdnn/ZhengGeDianHei-16) | 56★ / OFL | TTF 4 MB；子集 500 字约 30–80 KB | 做一款像素风钟面/标题 |
| 离线小语音合成（英文） | [0xD34D/SAM-ESP32](https://github.com/0xD34D/SAM-ESP32) | 4★ / 无 LICENSE | 源码约 100 KB → 编译约 30–50 KB | 1 个（仅英文复古音，中文不行） |
| 二维码（分享 Wi-Fi/留言） | LVGL 内置 `lv_qrcode`（本地 LVGL 源 8–17 KB） | LVGL MIT | 编译约 8–12 KB | 1 个 |
| 统计图表（番茄/心情/饮水周报） | LVGL 内置 `lv_chart`（本地源 55 KB） | LVGL MIT | 编译约 15–25 KB | 1 个 |
| 语音信/白噪音 | 复用现有 IMA/MP3 音频通道；素材可自录或用 VPS | — | 代码 20–40 KB + 音频 | 音频 3–7 分钟（见单价） |
| 桌宠/小黑动画 | [cifertech/TamaFi](https://github.com/cifertech/TamaFi)、[0015/lvgl_kawaii_face](https://github.com/0015/lvgl_kawaii_face) | MIT | 素材 200–400 KB | 1 只 |
| 生日彩蛋/纪念日 | [love-page](https://github.com/funnyzak/love-page)、[zry-birthday](https://github.com/Yuziki02/zry-birthday) | 参考 | 代码 20–40 KB + 插画 150–300 KB | 1 套 |
| 新钟面（代码绘制） | 见 `design/clock-styles-survey-20261113/` | MIT/Apache 居多 | 辉光管约 100 KB、VFD 约 80 KB、文字钟约 60 KB、指针约 70 KB、翻页牌约 110 KB | 5–10 款 |
| GIF 主题钟面 | 见上一轮调研 | 视素材 | 单个 220–470 KB | 3–7 个 |

## 三、三套可落地的预算方案

**方案 A：日常陪伴（推荐）**
- 相册/回忆：解码 + UI + 网页上传 ≈ 60 KB；照片走 LittleFS（约 30 张）或 VPS
- 纪念日/在一起天数：20 KB
- 辉光管钟面：100 KB
- 天气图标升级：50 KB
- 语音信/白噪音音频：1.0 MB（约 2 分钟 IMA 或 4 分钟 MP3）
- 合计 ≈ 1.23 MB，余 ≈ 0.45 MiB

**方案 B：钟面收藏**
- 辉光管 100 KB + VFD 80 KB + 文字钟 60 KB + 指针 70 KB + 翻页牌 110 KB ≈ 420 KB
- 再加 2 个 GIF 主题（约 600 KB）
- 合计 ≈ 1.02 MB，余 ≈ 0.65 MiB

**方案 C：生日彩蛋**
- 生日插画/动画：250 KB
- 桌宠素材：300 KB
- 生日祝福音频（自录/合成）：600 KB
- 代码：60 KB
- 合计 ≈ 1.21 MB，余 ≈ 0.46 MiB

## 四、不建议

- 再加游戏包（已有 10 款，且单款新增素材/规则代码约 50–150 KB，收益低）；
- 视频/大分辨率照片墙（Flash 放不下，PSRAM 解码压力大）；
- 新增全量中文字库（300 KB–1 MB，现有字库已够用）；
- 升级 LVGL 9 或换显示框架（风险远大于收益）；
- 离线中文 TTS（现有开源方案体积/效果都不合适，继续用 VPS 预生成）；
- 本地大模型/语音识别（远超预算）。

## 五、第二批候选（2026-11-13 晚追加）

| 候选 | GitHub | 星 / 许可 | 占用 | 说明 |
|---|---|---|---|---|
| PNG 解码 | [lvgl/lv_lib_png](https://github.com/lvgl/lv_lib_png) | 69★ / MIT | lodepng 源码 264 KB → 编译约 40–60 KB | 照片/截图/图标可走 PNG |
| JPEG 解码 | [lvgl/lv_lib_split_jpg](https://github.com/lvgl/lv_lib_split_jpg) | 37★ / MIT | 编译约 30–40 KB | 相册首选 |
| LVGL 小应用示例库 | [100askTeam/lv_lib_100ask](https://github.com/100askTeam/lv_lib_100ask) | 139★ / MIT | 单个示例 1–16 KB 代码 | 计算器、页面管理器、文件浏览器、NES 等，挑着借；NES + ROM 不建议 |
| 计算器 | [ThingPulse/esp32-color-kit-calculator](https://github.com/ThingPulse/esp32-color-kit-calculator) | 2★ / 无 LICENSE | 代码约 50 KB + 大字号字体 65 KB | 可精简到 20–40 KB；无许可证，仅参考重写 |
| 音乐频谱/示波器 | [kosme/arduinoFFT](https://github.com/kosme/arduinoFFT)（665★ / GPL-3.0）、[ESP32TFTOscilloscope](https://github.com/siliconvalley4066/ESP32TFTOscilloscope)（27★）、[ESP32-Freq-Analyzer](https://github.com/forzub/ESP32-Freq-Analyzer)（1★ / 无 LICENSE） | — | FFT 约 15 KB + 绘制 20–30 KB | 加到音乐页做频谱/波形；注意 GPL 与无许可证项目只参考 |
| 农历/黄历 C 库 | [vincascm/cdate](https://github.com/vincascm/cdate) | 7★ / 无 LICENSE | 源码 14 KB → 编译约 10 KB | 现有 gift_lunar 已有农历基础，做“宜忌/节气”页需再加数据表 |
| 更多表盘代码 | [ljelen/esp32-c3-mini-lvgl](https://github.com/ljelen/esp32-c3-mini-lvgl) | 1★ / MIT | 表盘代码每个 5–30 KB | 与 fbiego 同源，可补充表盘样式 |
| 数据看板 | [Neko-233/ESP32-GitHub-Stars-Display](https://github.com/Neko-233/ESP32-GitHub-Stars-Display) | 5★ / MIT | 代码约 30 KB + 资源 | CYD 320×240 显示 GitHub 星数；可改成“粉丝/纪念日/数据看板” |
| 屏保/粒子特效 | WLED 粒子系统（[wled/WLED](https://github.com/wled/WLED) PR #4506） | 参考 | 自绘 20–60 KB/款 | 星空、雨窗、萤火、烟花；LVGL 没有现成水族箱项目，需自写 |
| 音频素材 | GitHub 不适合放大音频；白噪音/祝福音建议自录或用 CC0 素材站 | — | 1.68 MiB ≈ 3.6 分钟 IMA / 7.4 分钟 MP3@32k | 代码复用现有音频通道 |

## 六、把 1.68 MiB 用完的四种组合

留 200 KB 机动，实际可用约 1.49 MiB（1,566,080 B）。

| 组合 | 内容 | 合计 | 余量 |
|---|---|---|---|
| A 相册包 | JPEG+PNG 解码 100 KB + 相册/上传 UI 30 KB + 45 张照片×24 KB（放 Flash）≈ 1.08 MB | ≈ 1.21 MB | ≈ 0.35 MiB |
| B 影音包 | 频谱/波形 40 KB + 白噪音/睡眠 UI 25 KB + 节拍器 20 KB + 音频 1.2 MB | ≈ 1.28 MB | ≈ 0.27 MiB |
| C 小工具包 | 计算器 35 KB + 黄历/节气 45 KB + 世界时钟 15 KB + 待办/倒数日 30 KB + QR 10 KB + 图表周报 25 KB | ≈ 0.16 MB | ≈ 1.33 MiB（还能再放屏保/音频） |
| D 屏保包 | 3 款自绘屏保 75 KB + 2 个 GIF 主题 500 KB + 1 个节日 GIF 250 KB | ≈ 0.83 MB | ≈ 0.66 MiB |

**推荐**：A（相册，照片放 LittleFS 可再省 1 MB）+ 辉光管钟面 100 KB + 纪念日 20 KB + 语音/白噪音 800 KB + 频谱 40 KB ≈ **1.08 MB，余约 0.47 MiB**——既有回忆、有新钟面、有声音，性价比最高。

## 七、结论

1.68 MiB 足够做 **“相册 + 一个代码绘制钟面 + 纪念日 + 2–4 分钟语音”**，或者 **“5 款新钟面 + 2 个 GIF 主题”**，也可以拆成 **“小工具包 + 屏保包”**。照片和音频优先放 LittleFS（960 KB）或 VPS，Flash 留给代码与素材；若还缺空间，可对现有 3 个 GIF（938 KB）做重编码/减帧，通常能再腾 100–300 KB。

许可证提醒：本批里 `lv_lib_png`、`lv_lib_split_jpg`、`lv_lib_100ask`、`Neko-233`、`ljelen` 是 MIT；`arduinoFFT` 是 GPL-3.0；计算器、cdate、Freq-Analyzer 无 LICENSE，只适合参考重写。
