# GitHub 时钟样式调研 · 2026-11-13

背景：桌面固件当前有三种钟面——时光动画（GIF）、Pixel Clock（19×9 方块）、白色翻页。app0 余量 2,099,408 B（约 2.00 MiB）。本轮在 GitHub 上按“能在 320×240 彩色屏上实现的钟面样式”方向做调研，只收集参考，不改固件、不刷设备。

调研方法：按 16 组关键词检索 GitHub，下载 22 个仓库源码与 README，提取预览图/帧拼成参考图；星数为 2026-11-13 通过 shields 接口查询的近似值。

参考图：`github-clock-styles.png`（本目录，9 类样式拼贴；按 320×240 画幅排版）。

## 一、可直接上屏的 9 类样式

| # | 样式 | 参考项目（星） | 实现方式 | 320×240 适配要点 | 估算 Flash |
|---|---|---|---|---|---|
| 1 | 翻页牌 · 蓝色翻板钟 | [ThomDyson/CYDFlipClock](https://github.com/ThomDyson/CYDFlipClock)（24★）；另有 [ESP32-Chipshift](https://github.com/reyco2000/ESP32-Chipshift)（1★）的 CYD Nixie Clock | LVGL 或 TFT_eSPI 画卡片 + 上下两半翻转动画 | 与 CYD 同为 320×240，可直接按 6 位数字 + 星期/日期两行排布 | 代码 40–70 KB；纹理 1–2 张小图 |
| 2 | 翻页牌 · 迷你出发板 | [dspl1236/GTA6-Countdown-Clock](https://github.com/dspl1236/GTA6-Countdown-Clock)（0★） | 字符格逐格翻动 + 顶部标题栏 | 320×240 上建议 4 组数字（时/分）或 3 行信息（星期/时间/倒计时） | 代码 50–80 KB |
| 3 | 辉光管 · 真管氛围 | [mkonhy/ESP32_NixieClock](https://github.com/mkonhy/ESP32_NixieClock)（3★） | 仅作视觉参考（真管需高压驱动，不适合本项目） | 取“橙色灯丝 + 玻璃反射 + 暗背景”氛围 | — |
| 4 | 辉光管 · LCD 拟真 | [AndroidCrypto/ESP32_S3_1_9_inch_ST7789_Display](https://github.com/AndroidCrypto/ESP32_S3_1_9_inch_ST7789_Display)（11★） | 代码绘制玻璃管 + 网点纹理 + 橙色发光数字 | 4 位数字占满屏宽时每位约 65px 宽，正好放得下 | 代码 60–100 KB；光晕/纹理 30–80 KB |
| 5 | LED 点阵 / 等离子 | [vladkorotnev/plasma-clock](https://github.com/vladkorotnev/plasma-clock)（41★）；[Resinchem/Matrix-Clock-ESP32](https://github.com/Resinchem/Matrix-Clock-ESP32)（16★）；[Keralots/AnimatedPixelClock](https://github.com/Keralots/AnimatedPixelClock)（95★） | 点阵字体/像素绘制；WLED 或自绘 | 可与现有 Pixel Clock 区分：做“圆形灯珠 + 辉光 + 滚动文字” | 代码 30–60 KB |
| 6 | 像素 & 复古 OLED | [richrd/esphome-clock-os](https://github.com/richrd/esphome-clock-os)（6★） | 单色像素小人/表情 + 小字时间 | 彩色屏上做“黑白像素滤镜 + 小黑像素形象”；像素素材很小 | 精灵图 20–80 KB |
| 7 | 天气场景钟 | [h6161236/tianqi-shizhong-dahuangban](https://github.com/h6161236-spec/tianqi-shizhong-dahuangban)（1★）；原版 [HoloCubic](https://github.com/peng-zhihui/HoloCubic)（6.8k★） | 天空渐变 × 一天时段 + 天气粒子/图标 | 12 天气组 × 11 时段关键帧；LVGL 渐变每帧开销大，建议静态底图 + 少量动态元素或 PSRAM 缓存 | 120–250 KB |
| 8 | 指针表盘 | [ZhouKes/ESP32Clock](https://github.com/ZhouKes/ESP32Clock)（0★）；[sl45sms/3dClock](https://github.com/sl45sms/3dClock)（0★，LVGL 9 + DSEG7） | LVGL 线条/图片表盘 + 指针旋转 | 240×240 表盘居中 + 右侧日期/天气；与现有三款差异明显 | 代码 30–60 KB；底图 30–80 KB |
| 9 | 极简翻页数字 | [limintao/vue-turn-clock](https://github.com/limintao/vue-turn-clock)（4★）；[QTTsurfing/Flip-Clock-App](https://github.com/QTTsurfing/Flip-Clock-App)（0★，Fliqlo 克隆网页） | 纯色背景 + 圆角数字卡片 + 翻动动画 | 现有“白色翻页”已覆盖类似方向，可做“黑底荧光/极简大卡片”变体 | 代码 30–50 KB |

## 二、补充样式（本次找到但没拼进参考图）

| 样式 | 参考项目 | 说明 |
|---|---|---|
| 文字时钟（Word Clock） | [ednieuw/Arduino-ESP32-Nano-Wordclock](https://github.com/ednieuw/Arduino-ESP32-Nano-Wordclock)（8★）、[fablabs-ch/wordclock](https://github.com/fablabs-ch/wordclock)（8★） | 汉字网格点亮当前读法，如「现在是 晚上 十点 零八分」。本项目已有 CJK 字体，做中文版很有礼物感。 |
| VFD 荧光管 | [sh1ura/WiFi-Sync-NTP-Clock-with-ESP32-and-VFD](https://github.com/sh1ura/WiFi-Sync-NTP-Clock-with-ESP32-and-VFD)（4★）、[tehniq3/NTPclock_ESP32C3_VFD_PT6302](https://github.com/tehniq3/NTPclock_ESP32C3_VFD_PT6302) | 青绿色七段 + 光晕 + 玻璃反光，代码绘制即可；真管硬件不适用。 |
| 七段数码管 / DSEG 字体 | [sl45sms/3dClock](https://github.com/sl45sms/3dClock) | DSEG7 字体（开源）做复古数码管，数字子集很小。 |
| 床头睡眠钟（灰度） | [llinzzi/slumbercube](https://github.com/llinzzi/slumbercube)（1★） | 低亮度、大时间、少干扰，适合夜间模式。 |
| 经典翻页（Fliqlo） | [QTTsurfing/Flip-Clock-App](https://github.com/QTTsurfing/Flip-Clock-App) | 黑底白字的经典翻页；与现有白色翻页同为翻页类。 |
| 亚克力/玻璃立体钟 | [sl45sms/3dClock](https://github.com/sl45sms/3dClock) | 屏幕模拟玻璃分层效果，成本偏高，优先级低。 |

## 三、与现有 3 种钟面的关系

- **时光动画（GIF）**：已有三组 GIF（约 938 KB），继续走 GIF 管线可加“生日/节日/季节”主题，但每个 100–500 KB。
- **Pixel Clock**：已有点阵方块风格；新的 LED 点阵/辉光管要避免与它太像（建议用“圆灯珠/光晕/玻璃管”区分）。
- **白色翻页**：已有翻页数字；再做翻页类建议换“黑色出发板/日历牌/机械质感”，否则重复。

## 四、实现建议（LVGL 8 / 320×240 / 2 MiB 余量）

1. **优先代码绘制**：辉光管、翻页牌、指针、VFD 都可用 LVGL 图元 + 少量纹理完成，单款 60–150 KB，三款合计约 0.3 MB；GIF 方案则每款 100–500 KB。
2. **字库**：DSEG7（七段）开源；中文文字钟直接复用现有 12/16px 字库，不新增全量字库。
3. **刷新策略**：表盘/翻页每秒最多重绘变化区域；GIF 走现有解码器（PSRAM 画布）；天气场景用静态底图 + 少量动态元素，避免每帧全屏渐变。
4. **按钮/入口**：沿用现有钟面切换逻辑（三个钟面 → 五个），手机配置页增加“钟面”选项即可。
5. **音量与提醒**：钟面切换不影响闹钟/倒计时提醒层；新增钟面要跑现有 host 回归与真实 LVGL 截图。

## 五、推荐

**第一梯队（3 款，约 0.3–0.5 MB）**
1. **辉光管（LCD 拟真）**：暖橙光晕最容易出片，夜里有氛围；代码绘制为主。
2. **翻页牌（蓝色翻板/出发板）**：复古日历感，能同时显示星期与日期；与现有白色翻页区分明显。
3. **指针表盘**：安静、耐看，和主页森林风最搭；实现简单。
4. 备选：**中文文字钟**（礼物感强）或 **天气场景钟**（视觉最丰富但开销最大）。

**不建议**：真辉光管/VFD/机械翻牌等需要额外硬件的方案；视频/3D/整屏序列帧（Flash 与刷新压力都不合适）。

下一步：确定 1–3 款后，按 320×240 出真实分辨率设计稿（沿用 `design/` 的排版流程），再决定代码绘制或 GIF 管线，并核算最终镜像余量。

## 六、第二批补充（2026-11-13 追加）

第一轮偏“硬件摆件”，第二轮重点找**表盘视觉风格**，找到两个高星参考：`fbiego/esp32-c3-mini`（551★，LVGL 8 手表工程）自带 20 张 240×240 表盘预览，风格跨度很大；`teojs/clock-dashboard`（746★）是简约天气大字钟。参考图见本目录 `github-clock-styles-2.png`，20 张表盘总览见 `watchfaces-overview.png`。

| # | 样式 | 来源（星数） | 适配建议 |
|---|---|---|---|
| 1 | 极简大字（黑底白字 + 侧边图标） | fbiego/esp32-c3-mini（551★） | 320×240 横屏最稳的排版：左侧大时间 + 右侧日期/图标 |
| 2 | 运动数据环（圆形进度 + 数据） | 同上 | 可放步数/喝水/专注数据，与健康类应用呼应 |
| 3 | 星空 / 星云（照片级背景 + 大数字） | 同上 | 夜间氛围首选；背景图 1 张 + 大数字，约 80–150 KB |
| 4 | 天文照片 + 天气（火星/月面等） | 同上 | 用“依依喜欢的风景照”当底图，最私人化 |
| 5 | 节日插画（圣诞夜景等） | 同上 | 生日/圣诞/新年可做 3 套主题；与现有 GIF 管线兼容 |
| 6 | 霓虹 / 极光渐变 | 同上 | 纯色渐变 + 发光数字，代码绘制即可 |
| 7 | 赛车 / 竞技红黑 | 同上 | 情绪强烈，适合偶尔切换 |
| 8 | 几何色块分割（国旗色） | 同上 | 可做“纪念日专属配色”变体 |
| 9 | 极简像素 / 点阵 | 同上 | 与现有 Pixel Clock 接近，需差异化 |
| 10 | 雷达 / 极客仪表 | 同上 | 极客风，可做“纪念日倒计时雷达” |
| 11 | 复古航空 / 琥珀光 | 同上 | 与 VFD/辉光管同一复古谱系，可归为一类 |
| 12 | 涂鸦 / 拼贴数字 | 同上 | 手账感，适合做生日特别版 |
| 13 | 简约天气大字钟 | [teojs/clock-dashboard](https://github.com/teojs/clock-dashboard)（746★） | 大号圆润时间 + 中文日期 + 天气行，实现成本最低、日常最耐看 |
| 14 | 像素角色陪伴钟 | [RenatBogdanov/ESP32-C3-Stopwatch](https://github.com/RenatBogdanov/ESP32-C3-Stopwatch)（0★） | 时间旁放像素小人（小黑像素化），和桌宠方向天然结合 |
| 15 | 二进制翻牌点阵 | [marcinsaj/Flipo-Binary-Clock](https://github.com/marcinsaj/Flipo-Binary-Clock-2x6-Flip-Disc-Display)（2★） | 实体翻转点阵；屏上可做“圆点翻牌二进制”极客变体 |
| 16 | 莫尔斯代码翻牌钟 | [marcinsaj/morsedot-clock](https://github.com/marcinsaj/morsedot-clock)（3★） | 实体翻转点阵；屏上可做摩尔斯/斐波那契等密码钟，趣味向 |
| 17 | 月相钟（圆屏） | [LazydaysCr/moon-phase-clock](https://github.com/LazydaysCr/moon-phase-clock)（10★） | 真月相 + 圆盘布局，可做“今晚月亮”夜间主题 |
| 18 | 蒸汽朋克钟 | [balassy/steampunk-clock](https://github.com/balassy/steampunk-clock)（0★） | 硬件外壳为主，屏上可模拟齿轮/黄铜质感 |
| 19 | 机械结构钟 | [IanDBallard/MechanicalClock](https://github.com/IanDBallard/MechanicalClock)（0★） | 实体机械翻页，仅作质感参考 |
| 20 | 沙漏 / 流沙钟 | [FluxSand/FluxSand](https://github.com/FluxSand/FluxSand)（12★） | 实体沙漏矩阵装置；屏上可做粒子流沙动画，开销较大 |

**第二批结论**：
- 礼物向最值得看的是 **节日插画（5）**、**星空/月相（3、17）**、**天文照片（4）**、**极简天气大字（13）**、**像素小黑陪伴（14）**——都偏“情绪 + 陪伴”，与现有三款钟面的差异也最大。
- 表盘合集是 240×240 方形，改成 320×240 横屏时建议：**左侧圆盘/插画 + 右侧大数字与日期天气**，或 **中心表盘 + 四角信息**。
- 实现上多为“1 张静态插画 + 少量动态元素”，单款 60–150 KB；如果全做成 GIF 主题，则每个 100–500 KB。

## 七、更新后的推荐组合

| 方向 | 内容 | 估算 |
|---|---|---|
| A 情绪向 | 节日插画钟（生日/圣诞/新年 3 套）+ 星空/月相钟 | 0.3–0.6 MB |
| B 日常向 | 极简天气大字钟 + 像素小黑陪伴钟 | 0.15–0.3 MB |
| C 复古向 | 辉光管（第一批）+ 翻页牌（第一批） | 0.2–0.4 MB |

建议先做 **B 的极简天气大字钟**（成本最低、每天都会看）和 **A 的生日插画钟**（时间敏感），辉光管/翻页牌作为“换口味”的复古分支第二批做。

## 八、第三批（2026-11-15 追加：实体钟 / 艺术钟 / 极简排版）

参考图 `github-clock-styles-3.png`；源码副本在 `build/research/clock-styles-2-20261115/src/`。

| # | 样式 | 来源（星数） | 实现方式 | 320×240 适配 / 成本 |
|---|---|---|---|---|
| 1 | ClockClock 24（24 个小表盘拼成数字） | [harry-roberts/clock-clock-clone](https://github.com/harry-roberts/clock-clock-clone)（5★）；实体版 [Vallasc/clockclock24-replica](https://github.com/Vallasc/clockclock24-replica)（166★） | LVGL 线条画 24 个双针表盘，按时间变化驱动 | 代码 40–80 KB；24 个对象按分钟批量重绘、测帧率 |
| 2 | 翻页点阵数字（flip-disc） | [Vallasc/clockclock24-replica](https://github.com/Vallasc/clockclock24-replica)（166★） | 圆点/翻片点阵构成数字 | 30–60 KB；橙/白配色出效果 |
| 3 | 极简大字 · 白（Padbury 风） | [Kamik423/Padbury-Clock-Revived](https://github.com/Kamik423/Padbury-Clock-Revived)（63★） | 白底 + 超细大数字 + 极简指针 | 20–40 KB + 细字体子集 10–30 KB |
| 4 | 极简衬线 · 黑 | 同上（Padbury 暗色） | 黑底白衬线大字 + 日期 | 20–40 KB + 字体子集 |
| 5 | 天文钟表盘 | [kshetline/prague-clock](https://github.com/kshetline/prague-clock)（57★） | 多层同心刻度盘、指针、月相 | 代码 80–150 KB，或静态底图 77 KB + 指针动画 |
| 6 | 复古卡片钟 | [bedimcode/retro-clock](https://github.com/bedimcode/retro-clock)（22★） | 米色卡片 + 圆角小表盘 + 数字日期 | 20–40 KB，纯代码绘制 |
| 7 | 罗马数字表盘 | [dakota2315/Analog-Clock](https://github.com/dakota2315/Analog-Clock)（0★） | 罗马数字 + 三指针 | 15–30 KB（矢量绘制） |
| 8 | 分形 / 几何艺术 | [cecton/egui-screensaver-fractal-clock](https://github.com/cecton/egui-screensaver-fractal-clock)（0★） | 分形线条/几何动画 | 20–40 KB；CPU 较重，需限帧或静态化 |
| 9 | 中文文字钟 | [mwestphal/chineseWordClock](https://github.com/mwestphal/chineseWordClock)（0★，自绘示意） | 汉字网格点亮“现在是晚上十点零八分” | 30–50 KB，复用现有中文字库，无素材 |
| 10 | 一年进度钟 | [ChiragChrg/DaysBeyond](https://github.com/ChiragChrg/DaysBeyond)（1★）、[QuinceyWu/TimeSlice](https://github.com/QuinceyWu/TimeSlice) | 年度百分比/进度条 + 剩余天数 | 15–25 KB |
| 11 | 斐波那契色块钟 | [artmaker43/A-Unique-Fibonacci-Clock](https://github.com/artmaker43/A-Unique-Fibonacci-Clock)（1★） | 5 个色块按斐波那契数值组合出时间 | 15–25 KB |
| 12 | 七段 LED 电子表 | 自绘示意（VFD/LED 系） | 红/绿七段数字 + 光晕 | 20–40 KB |

**第三批结论**：最值得做的是 **中文文字钟**（最私人、GitHub 上也没有现成中文实现）、**ClockClock 24**（最出片）、**Padbury 极简排版**（最耐看）、**七段 LED / 翻页点阵**（复古）。四款合计约 200–300 KB，1.68 MiB 余量可以轻松放下；ClockClock、分形、天文钟需要实机测帧率和内存。

## 九、附：本地参考素材

- 源码与 README：第一批 `build/research/clock-styles-20261113/src/`（32 个仓库）、第三批 `build/research/clock-styles-2-20261115/src/`（13 个仓库）
- 参考图：`github-clock-styles.png`（第一批 9 类）、`github-clock-styles-2.png`（第二批 16 类）、`github-clock-styles-3.png`（第三批 12 类）、`watchfaces-overview.png`（20 张表盘总览）
- 星数源：`https://img.shields.io/github/stars/<owner>/<repo>.json`（第三批为 2026-11-15 查询）

## 十、第四批（2026-11-15 追加：点阵 / 地图 / 人生 / 创意钟）

参考图 `github-clock-styles-4.png`；源码副本在 `build/research/clock-styles-3-20261115/src/`。

| # | 样式 | 来源（星数） | 实现方式 | 320×240 适配 / 成本 |
|---|---|---|---|---|
| 1 | 多米诺骨牌钟 | [AndroidCrypto/ESP32_CYD_Domino_Clock](https://github.com/AndroidCrypto/ESP32_CYD_Domino_Clock)（3★） | 骨牌点阵拼数字，CYD 同板 Arduino 实现 | 最接近本板，可直接参考移植；代码 30–60 KB |
| 2 | 点阵斑块钟 | [kainsteffen/dots-clock](https://github.com/kainsteffen/dots-clock)（34★，Flutter Clock 2019 全球前 30） | 圆点密度/斑块构成数字 | LVGL 圆点 + 抖动；40–80 KB，可做动态噪点 |
| 3 | 噪点立方钟 | 同上（cubic noise 变体） | 三维噪点投影 | 40–80 KB，CPU 稍重，限帧 |
| 4 | 点阵暗色钟 | 同上（dark 变体） | 深色底 + 白色点阵 | 较省电，30–50 KB |
| 5 | 世界昼夜地图钟 | [ACoci86/terrahour](https://github.com/ACoci86/terrahour)（263★） | 世界地图昼夜分界 + 时间线 | 底图 8-bit 约 77 KB + 时区计算；代码 40–80 KB |
| 6 | 世界时钟雷达 | 同上（radar） | 多时区列表 + 雷达图 | 30–60 KB |
| 7 | 人生周历钟 | [PrEvAl85/obsidian-life-calendar](https://github.com/PrEvAl85/obsidian-life-calendar) | 每周一格的人生日历，已过/未来不同色 | 20–30 KB，纯绘制；情绪感强 |
| 8 | ASCII 字符钟 | [cmpbl/ascii_clock](https://github.com/cmpbl/ascii_clock)、[kristopolous/aclock](https://github.com/kristopolous/aclock) | 字符画点阵数字 | 20–30 KB，现有中文点阵字体可做 |
| 9 | 十六进制颜色钟 | [RobinMesaMarin/TimeColor](https://github.com/RobinMesaMarin/TimeColor)（1★） | `#HHMMSS` 作为整屏背景色，文字显示时间/色值 | 15–25 KB，极简独特 |
| 10 | 盲文点阵钟 | [jangjia01234/Time-to-Dot](https://github.com/jangjia01234/Time-to-Dot)（WWDC24 获奖） | 6 个盲文格表示时/分/秒 | 15–25 KB |
| 11 | 睡眠周期钟 | [Ahmed-Dlshad1212/sleepyclock](https://github.com/Ahmed-Dlshad1212/sleepyclock) | 入睡时间 + 90 分钟周期起床建议 | 20–30 KB，实用小功能 |
| 12 | 故障艺术 / VHS 钟 | 自绘（VHS/glitch 美学） | RGB 错位 + 扫描线 + 噪声块 | 15–25 KB |

**第四批结论**：
- 唯一**同板同分辨率**可借鉴的是 **多米诺骨牌钟**（ESP32 CYD 320×240，Arduino）；
- 视觉上最出彩的是 **点阵噪点钟**（dots-clock）和 **世界昼夜地图钟**（terrahour 263★）；
- 情绪价值最高的是 **人生周历钟** 和 **睡眠周期钟**（适合放在礼物里）；
- 其余多为网页/App/终端风格，需按 LVGL 8 重画，但单款代码都在 15–80 KB，12 款合计约 300–400 KB，1.68 MiB 预算足够。**时钟样式参考图**：`github-clock-styles-4.png`（12 类，其中 5 类为自绘示意）。

## 十一、礼物级钟面：Flutter Clock 大赛获奖/入围设计（2026-11-15）

前几批混入了盲文、ASCII、十六进制等极客趣味，已被否定。这一批只收**专业设计过、好看、耐看**的钟面，来源是 Google/Lenovo 的 Flutter Clock 设计大赛获奖与入围作品（26 个），加一个中式时辰钟。参考图 `gift-clock-faces-2.png`。

| # | 样式 | 来源 | 实现方式 | 320×240 适配 / 成本 |
|---|---|---|---|---|
| 1–2 | 星空天气钟（夜/晴） | [CiriousJoker/star_clock](https://github.com/CiriousJoker/star_clock)，Flutter Clock 入围 | 星空/山景背景 + 星点组成数字 + 天气 | 背景插画 77–150 KB + 代码 20–40 KB；按时段/天气换色 |
| 3–4 | 风景钟（清晨/夜晚） | [Dwite/Scenery-FlutterClock](https://github.com/Dwite/Scenery-FlutterClock)，入围 | 同一风景插画按时段变色 + 大数字 | 2–4 张时段底图 77 KB/张 + 代码 20–30 KB |
| 5 | 纸艺钟 | [ryanjacktaylor/paper_clock](https://github.com/ryanjacktaylor/paper_clock)，入围 | 纸雕风景 + 温度/大字 | 静态插画 77 KB + 20–30 KB |
| 6 | 蒸汽朋克钟 | [tsinis/flutter_clock](https://github.com/tsinis/flutter_clock)，Google & Lenovo 奖 | 暗色书房 + 铜质表盘 | 底图 77–150 KB + 指针动画 30 KB |
| 7 | 城市 Lo-Fi 钟 | [nosliwmichael/flutter_clock](https://github.com/nosliwmichael/flutter_clock)，入围 | 霓虹城市夜景 + 数字牌 | 底图 77–150 KB + 代码 20–30 KB |
| 8 | 粒子流体钟 | [miickel/flutter_particle_clock](https://github.com/miickel/flutter_particle_clock)，大奖 | 粒子流动组成时间 | 40–80 KB 代码，CPU 重，需限帧/减粒子 |
| 9–10 | 手绘积木钟（浅/深） | [ThinkingEngine-net/hand-drawn-clock-flutter](https://github.com/ThinkingEngine-net/hand-drawn-clock-flutter)，入围 | 纸片/积木拼数字 | 20–30 KB，纯绘制，可做浅/深两套 |
| 11 | 日落倒影钟 | [vmorilla/sunset_reflections_clock](https://github.com/vmorilla/sunset_reflections_clock)，Idea Award | 霓虹日落 + 水面倒影 + 月相 | 底图 77–150 KB + 代码 30–40 KB |
| 12 | 中式时辰钟 | [eevil505/ziwuliuzhu-clock](https://github.com/eevil505/ziwuliuzhu-clock) | 十二时辰/经络/五行，米白墨色 | 静态页面 77 KB + 代码 30–50 KB；国风独特 |

**推荐**：星空天气钟（夜里最美）、风景钟（白天温柔）、城市 Lo-Fi 钟（夜间氛围）、手绘积木钟（可爱耐看）四款先做；粒子钟和日落倒影作为“艺术彩蛋”；中式时辰钟做“国风特别版”。这些是 Flutter UI，不直接移植，按“背景插画 + 数字/指针”在 LVGL 8 里重画，单款 50–200 KB，余量 1.68 MiB 够用。

## 十二、Flutter Clock 大赛精选 15 款（2026-11-15）

参考图 `gift-clock-faces-3.png`；源码副本在 `build/research/clock-winners-20261115/src/`。26 个获奖/入围作品全表见 `flutter/website` 博客 “It’s Time: The Flutter Clock Contest Results”。

| # | 样式 | 来源 | 感觉 |
|---|---|---|---|
| 1–2 | Agent Clock（点阵字符） | [kruegerrobotics/flutter_agent_clock](https://github.com/kruegerrobotics/flutter_agent_clock) | 极简、科技感但克制 |
| 3–4 | Animal Clock（小动物表情） | [seiwonlee/aninmal_clock](https://github.com/seiwonlee/aninmal_clock) | 可爱治愈 |
| 5 | Ants Clock（树叶与蚂蚁） | [StuartApp/Flutter-clock-challenge](https://github.com/StuartApp/Flutter-clock-challenge) | 自然、清新 |
| 6 | Boating Clock（湖面小船） | [SpiciedCrab/Boating](https://github.com/SpiciedCrab/Boating) | 安静、有故事感 |
| 7–8 | Iso Clock（等距积木数字） | [cookmscott/FlutterClockChallenge](https://github.com/cookmscott/FlutterClockChallenge) | 现代、有设计感 |
| 9–10 | Freehand Clock（手绘极简） | [thayesx/freehand_clock](https://github.com/thayesx/freehand_clock) | 黑白极简、留白 |
| 11 | Generative Clock（生成艺术） | [Fabian-Stein/flutterClock](https://github.com/Fabian-Stein/flutterClock) | 艺术、抽象 |
| 12 | SLA Clock（点线排版） | [sla-000/flutter_clock](https://github.com/sla-000/flutter_clock) | 排版感、克制 |
| 13 | BarBar Clock（色条构成） | [aednlaxer/flutter-clock-challenge-barbar](https://github.com/aednlaxer/flutter-clock-challenge-barbar) | 色带、现代 |
| 14–15 | 极简指针（浅/深） | [meszarosdezso/flutter_clock](https://github.com/meszarosdezso/flutter_clock) | 经典指针、耐看 |

未入选/仅备查的入围作品：DJ Clock（黑胶唱机，预览文件是模板通用图）、Matches Clock（火柴数字，素材为长条动画）、Snake/Tetris Clock（游戏化，偏玩具）、Circle/Helg9000/Infinity（缺预览）、Dots Clock（已在第四批用过）。

**最终候选池**：第二辑 12 款 + 本辑 15 款，共 27 款礼物级钟面；建议先做“星空天气、风景晨昏、城市 Lo-Fi、纸艺、Animal、Iso、极简指针”里挑 3–4 款出设计稿。

## 十三、30 款钟面总选单（2026-11-15）

两张总图：
- `gift-clock-faces-4-1.png`：1–15 日常与氛围（极简白/黑、天气大字、辉光管、翻页牌、星空、节日插画、复古卡片、星空天气、风景晨昏、纸艺、城市 Lo-Fi、手绘积木、中式时辰）。
- `gift-clock-faces-4-2.png`：16–30 设计与新发现（粒子、日落倒影、蒸汽朋克、Agent、Animal、Ants、Boating、Iso、Freehand、BarBar、Amazfit·ara/asterisk/astro/atan2/circlock）。

本轮新来源：
- **Amazfit 表盘合集**：[novvember/amazfit-watchfaces](https://github.com/novvember/amazfit-watchfaces)（ZeppOS 圆表盘，选 5 款：ara、asterisk、astro、atan2、circlock）。
- **CSS Clocks**：[alvaromontoro/CSS-Clocks](https://github.com/alvaromontoro/CSS-Clocks)（纯 CSS 时钟，本地已用 headless Edge 渲染出经典指针/文字钟/径向文字等 9 张，可作备选）。
- **Five Hundred Clocks**：[tkgally/500-clocks](https://github.com/tkgally/500-clocks)（500 种“怎么读时间”的概念钟画廊，适合继续挖创意）。
- **Flutter Clock 26 个获奖/入围**：已全部下载，15 款进图。

**总选单里值得先做的 6 款**：极简天气大字（日常）、星空天气钟（夜晚）、纸艺钟（治愈）、中式时辰钟（国风）、辉光管（氛围）、极简指针（经典）。

## 十四、30 款全新钟面（2026-11-15，全部无重复）

来源：[tkgally/500-clocks](https://github.com/tkgally/500-clocks) —— **CC0 1.0 公共领域**的“500 种时钟”画廊（每个时钟是一种独立的时间读法/艺术形式），本轮用 Playwright 渲染并逐一截图，挑出 30 款此前从未出现过的设计：

- 上：`new-clocks-30-a.png`（1–15 艺术与自然）：千花玻璃镇纸、水彩、梭结蕾丝、木版画、禅意圆相、夜间楼群、木片镶嵌、新造型主义、花道、剪纸窗花、晨鸟合唱、荧光海湾、树的年轮、星位图、鹦鹉螺。
- 下：`new-clocks-30-b.png`（16–30 文字/历史/机械/日常）：蒲公英、向日葵、排列硅藻、汉字时间、号外！号外！、龙火钟、大象钟、更夫、法国大革命十进制时间、香篆钟、游标卡尺、饮水鸟、沙漏、生日蛋糕、灯塔。

CC0 意味着这些设计可以自由借鉴、改编甚至直接使用（不要求署名）。其中 **生日蛋糕、汉字时间、号外、灯塔、蒲公英、沙漏** 最适合做礼物向钟面；后续可按 320×240 重画，单款风格成本约 50–150 KB。
