# GitHub「倒数日 / LVGL」搜索结论 · 2026-11-15

## 一、搜索结论

GitHub 上没有可直接用的 **LVGL 倒数日组件或 ESP32+LVGL 倒数日项目**。按 `倒数日 lvgl`、`纪念日 lvgl`、`countdown days lvgl`、`倒数日 esp32` 等关键词检索，结果集中在四类：

- Android / 手机 App（Kotlin/Compose、Flutter 等）；
- Electron/Vue 桌面应用；
- 墨水屏（e-paper）或 OLED 小屏设备；
- 网页/服务端卡片生成。

LVGL 侧能直接用的只有内置组件：`lv_calendar`（日历选择）、`lv_label`、`lv_obj` 动画和日期计算，倒数日需要自己组合实现——**代码量约 30–50 KB，1.68 MiB 预算完全够**。

## 二、可借鉴的项目

| 项目 | 星 / 许可 | 类型 | 可借鉴的点 |
|---|---|---|---|
| [LaoYinBai/Countdown-App](https://github.com/LaoYinBai/Countdown-App) | 4★ / 无 LICENSE | Android（Kotlin + Compose） | 功能最完整：阳历/农历日期、普通倒数日、**每年重复纪念日**、置顶、左滑删除/右滑置顶、多选批量删、自定义卡片背景、卡片图片导出、按背景自动调文字色。仅参考交互与信息层级，不能复制代码 |
| [miku1130/study-desk](https://github.com/miku1130/study-desk) | 22★ / MIT | Electron + Vue 学习桌面 | 倒数日卡片 UI（`CountdownView.vue` 8.5 KB）与状态管理（`countdowns.ts` 1.7 KB）；和番茄钟/课表/白噪音组合的方式可参考 |
| [wmz1024/daysmatter-image-api](https://github.com/wmz1024/daysmatter-image-api) | 11★ / MIT | PHP + Deno，服务端 | DaysMatter 风格卡片：标题 + 大号天数 + 目标日期 + 蓝/橙背景（过期变橙）。可在 VPS 生成分享卡片，也可只借配色和排版 |
| [aaldrich29/E290-Date-Countdown](https://github.com/aaldrich29/E290-Date-Countdown) | 0★ / 无 LICENSE | ESP32-S3 + 墨水屏（单文件 .ino 27 KB） | 设备端逻辑最接近：3 个日期、剩余天数、**pin 置顶**、年度/一次性事件、午夜刷新、WiFi 配置。仅参考算法与状态设计 |
| [Work-KewalShah/Countdown-Timer-V1](https://github.com/Work-KewalShah/Countdown-Timer-V1) | 2★ / MIT | ESP32 Arduino + 3D 打印外壳 | 多目标倒计时、秒表、**断电保存状态**；偏倒计时器，不是日期倒数日 |
| [lvgl/lvgl · lv_calendar](https://github.com/lvgl/lvgl/blob/master/docs/widgets/calendar.md) | 25k★ / MIT | LVGL 内置 | 日期选择/月历控件，可做“选日期”入口；LVGL 8.4 已内置 |

补充：我们工程里已有「放假倒计时」页、日历应用中的自定义倒计时、手机端“纪念”字段（含日期），所以这不是从零做，而是把三处能力整合成一个「倒数日」应用。

## 三、建议实现方案

**功能**
- 列表页：卡片显示「还有 N 天 / 已经 N 天 / 就是今天」+ 标题 + 日期（可选农历）+ 置顶标记；
- 详情页：大号天数、目标日期、农历、备注、剩余/已过进度；
- 排序：置顶优先 → 最近日期优先；年度重复事件按今年/明年推算；
- 操作：点击详情；长按菜单置顶/删除（避免和现有左右滑手势冲突）；
- 首页可选显示置顶的一条（小卡片）；
- 手机配置页增加「倒数日」编辑（增删改、年度重复、置顶、背景色），与现有留言/纪念同步机制一致。

**数据**
- NVS 双槽 CRC 记录（沿用闹钟/番茄的做法），每条：`{title, date, yearly, pinned, background, note}`，上限 20 条；
- 日期用 `YYYY-MM-DD`，未校时显示「待校时」，不猜测日期。

**复用**
- `gift_lunar`（农历）、现有日期/时间源、手机配置 JSON 同步、详情页框架、NVS 双槽保存、应用入口/导航手势框架。

**体积**
- 代码 30–50 KB；可选 2–3 张卡片背景（8-bit 小图约 20–60 KB）；不需要新字库。
- 对比 1,766,080 B 余量：占用 < 5%，可以顺手再加“生日彩蛋/分享卡片”。

**验证要点**
- 跨年、闰年 2 月 29 日、年度重复（2/29）、时区/午夜刷新、未校时、列表为空；
- 手机保存失败回滚、双槽损坏恢复、20 条上限、置顶排序；
- 真实 LVGL 指针交互 + 主机回归 + 实板截图。

## 四、结论

GitHub 上没有现成的 LVGL 倒数日项目，**自己写是正确路线**，代码量 30–50 KB。设计上参考 Android 的 Countdown-App（信息层级最完整）和 daysmatter-image-api（卡片配色），逻辑参考 E290（年度重复/pin/午夜刷新）；全部为参考思路，注意 Countdown-App 与 E290 没有 LICENSE，不要直接复制代码。
