# LVGL 桌面实板验证记录

日期：2026-10-06。设备：LCDWiki ES3C28P / ESP32-S3 revision v0.2，USB Serial/JTAG，COM8。工程：`arduino/yiyi_desk/`，LVGL 8.3.11，Arduino ESP32 2.0.17-cn。

## 当前状态

2026-10-07 抓帧通道修复：此前 `inspect_device.py` 对所有帧报 `Frame CRC mismatch` 并拒绝保存，导致界面无法远程验证。已定位为**两个独立原因**，均已修复。现在 `sweep` 可稳定抓取五页与全部应用共 14 帧，输出位于 `../build/verify-captures/`，且 `page-1`（此前疑似渲染损坏）确认为正常。LVGL 已同时升级到 8.4.0（AiDoGUI 要求 ≥8.4.0），应用镜像 4,626,544 字节。

2026-10-07 实物确认：用户查看实板后确认**默认横屏显示方向正常**，此前的镜像问题已由 `MADCTL={0x08,0x68,0xC8,0xA8}` 配合 `LcdRotation=1` 修好。该项此前只能靠抓帧间接推断，现为实物人眼确认。同时反馈**电容触摸坐标未完全校准**，四角可点、功能可用，但存在位置偏移，校准待做。方向已确认，重构时不得改动已验证的 MADCTL 参数。

当前检查点已完成：LVGL 桌面固件已重新完整烧录到 COM8，启动正常。针对实物出现的镜像问题，已按 LCDWiki 官方方向表修正 ILI9341 MADCTL：默认横屏 rotation 1 使用 `0x68`，并同步保留 FT6336G 默认触摸映射。之前约 8% 中断导致的黑屏固件已被完整覆盖。

## 已完成

| 检查 | 结果和范围 |
|---|---|
| 芯片和存储 | esptool 识别 ESP32-S3，16MB Flash、内置 8MB PSRAM；固件分配双显示缓冲成功 |
| 构建 | 最新修正版成功编译；应用 4,623,625 字节，占 7MB 应用槽 62%；静态全局 67,132 字节 |
| 原固件备份 | `../build/backups/esp32s3-before-lvgl-first4mb-20261006.bin`，旧分区实际使用前 4MB |
| LVGL 启动崩溃修复 | 将触摸 `lv_indev_drv_t` 从 setup 局部变量改为长期保留结构；LVGL 8 注册接口保存原指针，局部变量返回后失效 |
| 固件自检 | 修复版板上返回 `SELFTEST PASS`：坐标转换、五页方向、日期边界、闰年、自然排序、UTF-8 等 |
| 最新方向修复 | `desk.cpp` 使用 LCDWiki 官方 `MADCTL={0x08,0x68,0xC8,0xA8}` 方向表；默认 `LcdRotation=1`，避免 `setRotation(1)` 后被错误的 `0x20` 覆盖 |
| 最新烧录 | bootloader、分区表、应用镜像均写入完成并通过 Hash 校验；应用镜像约 4.62MB，设备自动复位后 `resetReason=0` |
| 最新运行状态 | 固件 `0.1.0-lvgl`，16MB Flash、约 8MB PSRAM，FT6336G 在 `0x38` 响应，`SELFTEST PASS` |
| 无卡运行 | 自检和页面检查期间持续处理每 5 秒的无卡扫描，未再次观察到原有启动崩溃；无卡 `0x107` 为 SD 初始化超时 |
| 界面初步抓帧 | 五页及相册、留言、天气、日历、设置、关于、颜色触摸页已读取；主页、留言、倒计时、应用网格及主要应用文本可见 |
| Wi-Fi 离线图标 | 主页及应用页右上角显示 Wi-Fi 图标叠加红色 ×；设备状态 `wifi=false` |
| 未校时状态 | 显示 `--:--` 和等待联网校时；日历“今天”禁用，不显示假时间 |
| 显示方向（实物人眼） | 2026-10-07 用户查看实板确认：默认横屏方向正常，无镜像。抓帧无法验证面板呈现，此项以实物为准 |
| 触摸精度（实物） | 四角可点、功能可用，但坐标未完全校准存在偏移；`0x38` 应答不等于映射正确 |
| 抓帧通道修复 | 帧 CRC 从不匹配的根因有两条：① 固件 `~esp_rom_crc32_le(~0u,…)` 并非标准 CRC-32（实测该 ROM 接口的 seed 参数不作标准 init 用，等价于 init 0 且无最终取反），已改为表驱动标准 CRC-32；② 无卡时每 5 秒的 `sdmmc_init_ocr 0x107` 错误日志与 153600 字节帧流共用 USB CDC，日志文本插入帧中。已用 `esp_log_level_set` 静音 sdmmc/vfs 日志标签，卡状态改由 `status`/`cardtest` 的结构化字段汇报 |
| 抓帧稳定性 | 连续两次抓同一静态页，payload 153600 字节完全一致，证明像素数据从未损坏，此前是完整性闸门误杀 |
| page-1 澄清 | 此前 `device-captures2/page-1.png` 呈彩虹文字与土褐配色，经字节交换可还原，属旧抓帧产物；修复后重新抓取为正常界面，非固件渲染缺陷 |
| SD 挂载诊断 | 新增 `sdStage`/`sdKhz`/`sdPhotosDir`/`sdMessagesFile` 与 `cardtest` 串口命令；`rescan` 改为深度探测（20MHz 失败后降 400kHz）。当前实测：无卡，20MHz 挂载失败，未做低频探测 |
| 日历崩溃修复 | `stress` 首次完整执行时在"日历选日"处 panic：`LoadProhibited`，`EXCVADDR 0x0`，回溯落在 `lv_calendar_get_pressed_date`。根因是日历的 day btnmatrix 带 `LV_OBJ_FLAG_EVENT_BUBBLE`，回调里 `lv_event_get_target()` 取到的是 btnmatrix 而非日历，被当作 `lv_calendar_t*` 读取 `->btnm` 得到空指针。改用 `lv_event_get_current_target()` 并校验事件源确为 day matrix。已确认 8.3.11 同样存在该标志，属长期潜伏缺陷而非 8.4 升级引入 |
| 压力测试 | 首次通过：导航循环 ×2、日历与嵌套返回路径、连续运行、闲置回主页全部 PASS |
| 触摸模块抽取 | 触摸硬件访问、方向、手势识别迁至 `src/touch.cpp`；导航逻辑通过回调留在 UI 层。重构后重新抓取 14 帧，文件尺寸与重构前逐字节一致 |
| 触摸仿射校准 | 此前 `touchToLandscape` 只做四种旋转、无缩放偏移，这是"能用但未完全校准"的原因。新增两点仿射校准，存入 NVS，硬复位后回读一致；`touchcal` 查状态、`touchraw` 输出原始坐标、`touchset <xMin> <xMax> <yMin> <yMax>` 写入。**尚未用实测四角数据标定**，当前为 identity 映射 |
| 空闲计时溢出修复 | `deskLoop` 在循环开头取 `now=millis()`，随后 `pollSerial()` 里的 `serialCommand` 又设 `lastInputAt=millis()`（更晚的值）。`now-lastInputAt` 在 uint32 下溢成约 2^32，恒 ≥ `IdleMs`，于是**任何串口命令之后立即触发"闲置回主页"**，page 与 app 同时清零。这一直是"抓帧时好时坏""view 命令不生效"的真正原因：帧回落为主页（CRC `7704EEAC`），且无重启、uptime 单调递增。修复方式是在 `pollSerial()` 之后重新读取 `now`。修复后连续 5 次导航命令全部稳定生效 |
| AiDoGUI 接入缝 | 新增 `src/screens.h/.cpp`：槽位枚举 `Screens::Slot`、`Screens::Provider` 回调、`dispatch()` 与 `activateBuiltIn()`。内置界面仍为默认，生成代码按需接管单个槽位；被接管时内置渲染整体跳过，未被接管时先恢复内置屏幕再渲染 |
| 接入缝验证 | `src/ui_export/ui_demo.cpp` 是参考适配器，行为刻意模仿生成代码（自建屏幕 + `lv_scr_load`）。实测：`uiexport off` 时天气槽=内置天气页（11,422 B）；`uiexport on` 时同一槽=自建屏幕（15,304 B）；设置页不受影响（9,405 B）。`selftest` 输出 `logic=PASS touch=PASS screens=PASS uiExport=PASS` |
| 串口未知命令上报 | 新增 `UNKNOWN "<原文>"` 输出。此前无法区分"命令没送达"与"固件分支没匹配"，是本次定位耗时较久的原因之一 |
| 抓帧工具加固 | `inspect_device.py` 增加开端口后的 `warmup()`（等设备应答，避免首条命令丢失）、抓帧失败重试 3 次、view 命令后延时 0.3→0.8 秒。sweep 每帧延时 0.2→0.6 秒 |
| 本轮回归 | sweep 14 帧全部通过且尺寸与既有基准一致（page-0 31,175 / page-1 7,319 / page-2 11,626 / page-3 10,782 / page-4 7,426 / app-weather 11,422 / app-settings 9,405）；`stress --duration 30` 全部 PASS；`resetReason=0` |
| 显示驱动模块抽取 | 面板、背光、LVGL display 驱动、帧镜像缓冲与截图发送迁至 `src/display.cpp`。MADCTL 方向表与 IPS 反色**逐字照搬**，未作任何改动。抽取后重新抓帧：`page-0`、`page-2`、`app-settings` 与基准**逐字节一致**，其余帧尺寸一致；`selftest` 四项全 PASS；`stress` 全 PASS |
| 留言服务模块化 + 远程来源 | 留言校验/解析迁至 `src/messages.cpp`，TF 卡与远程 URL 共用同一套规则（全有或全无：任一字段类型错误、id 重复或超限都返回失败并**保留旧列表**）。新增 `Messages::fetchRemote()`：`http://` 用普通 client，`https://` 按 ISRG Root X1 校验（Let's Encrypt 证书可过，自签证书会按设计拒绝）。地址存 NVS，可经配网页 `/api/messages` 或串口 `msgurl` 设置 |
| 留言校验自检 | 新增 `Messages::selfTest()`，覆盖合法解析、id 重复、非数组、缺字段、类型错误、空 id、签名类型错误、标题超 40 字、空数组清空列表、超长拒绝，以及**失败时调用方列表不被修改**。实板 `selftest` 输出 `messages=PASS` |
| 留言地址实测 | `msgurl` 空值→拒绝非法协议→接受合法地址→设备未联网时给出"Wi-Fi 未连接，无法更新留言"而非崩溃→硬复位后地址仍保留→`msgurl clear` 恢复以 TF 卡为来源。**实际 HTTP 拉取尚未验证**（设备未配网）。留言页刷新按钮在有地址时显示"从服务器更新" |
| 时间有效性阈值统一 | `DeskLogic::timeValid(epoch)` 成为唯一判定点，`desk.cpp` 与 `messages.cpp` 共用，消除重复常量 |
| SD 挂载阶梯与 exFAT 结论 | `mountCard()` 深度探测扩展为四种组合：4-bit/1-bit × 20MHz/400kHz。判读方式：4-bit 失败但 1-bit 成功→D2/D3（GPIO48/47）接触或接线问题；20MHz 失败但 400kHz 成功→信号完整性问题；全部失败→无卡、卡损坏、非 FAT 格式或供电问题。另新增 `sdDeepStage` 保存最近一次**深度**探测结果，避免被每 5 秒的后台浅探测覆盖；`requestScan` 允许深度探测不被已挂起的后台扫描吞掉。**重要结论**：本 core（Arduino ESP32 2.0.17-cn）的 `libfatfs.a` 中 exFAT 相关符号出现 **0 次**，`tools/sdk/esp32s3/sdkconfig` 的 FATFS 段也没有任何 exFAT 选项，因此**exFAT 格式的卡无法挂载**（64GB 以上卡出厂默认 exFAT，Windows 图形界面也无法把 >32GB 格成 FAT32）。工程对卡只读：`format_if_mount_failed=false`，且无任何写入调用 |
| GIF 动图接入 | 打开 `LV_USE_GIF`，新增素材脚本 `hardware/scripts/prepare_gifs.py`：自动按内容包围盒裁剪 260×568 竖屏素材、缩放到目标方形、将近黑背景抠成 GIF 透明、输出 C 字节数组与 3× 预览。首个成品 `src/assets/gif_birthday.c`（120×120、16 帧、60ms、**85,371 字节**）放在主页右侧。`lv_gif` 需要 `lv_img_dsc_t` 包装（LVGL 依首字节判定源类型，`LV_IMG_CF_RAW` 即 1，落在 VARIABLE 分支）；因固件里数组声明为不完整类型，生成器额外输出 `<name>_len` 供 `data_size` 使用 |
| GIF 实测与基准变化 | 连续三次抓 `page 0` 得到三个**不同**哈希与不同大小（36,144 / 36,045 / 36,202 B），证明动画在推进；画面为透明背景的生日蛋糕小黑叠在森林美术上，时钟区与天气条未被遮挡。**注意：`page-0` 因此不再是逐字节可复现的回归基准**，其余 13 帧仍与基准完全一致。应用镜像 4,733,325 B（64%），仍余约 2.5 MiB |

初步抓帧输出位于 `../build/device-captures2/`。截图协议已增加 CRC32、结束标记校验和 CDC 分块发送；最新一次截图命令受 USB CDC/后台无卡扫描影响未返回帧，因此仍不把串口抓帧当作 LCD 实物呈现的替代验证。

## 待运行

以下命令已于 2026-10-07 全部执行并通过，结果见 `../build/verify-captures2/` 与 `../build/device-stress.json`：

```powershell
python hardware/scripts/inspect_device.py status
python hardware/scripts/inspect_device.py selftest
python hardware/scripts/inspect_device.py sweep --out build/verify-captures2
python hardware/scripts/inspect_device.py stress --duration 30 --out build/device-stress.json
```

`stress` 用同一串口验证真实 LVGL 输入流程：20 次滑动、12 次应用启动与返回、日历选日和子页返回、连续运行，以及无操作自动回主页，全部 PASS。模拟输入送入同一个 LVGL 触摸处理流程，但不代替物理电容触摸检查。

## 待实物确认

- 屏幕红、绿、蓝、白、黑测试块（色彩呈现）。显示方向已于 2026-10-07 由用户实物确认为正常。抓帧只验证发出的像素，不验证面板呈现。
- 四角电容触摸坐标的精确校准；当前可点但有偏移，需要实物标定后再修正映射。
- 插入 FAT32 TF 卡后的挂载、JPEG 解码、留言加载、拔插和轮播。目前无卡，不能宣称卡功能已实测通过。
- 连接 2.4GHz Wi-Fi 后 × 消失、断开后恢复，以及 NTP、天气查询和手机配置页。尚未提供网络配置，联网流程未实测。
- 相册、设置等界面在真实屏幕上的触控舒适度和长时间稳定性。

## 参考资料

- 官方目录 `../2.8inch_IPS_ESP32-S3_ILI9341V_ES3C28P_ES3N28P_V1.0/1-示例程序_Demo/Arduino/Demo/Example_08_LVGL_Demos/`：静态显示及输入驱动、FT6336 触摸。
- 同一官方目录下 `Example_05_show_SD_jpg_picture/`：四线 SD_MMC 引脚和初始化；不据无卡超时改写 Slot 配置。
- 工程保存的 `lcdwiki/ILI9341V_Init.txt`：IPS 初始化、BGR、反色和 gamma。
- [LCDWiki 产品资料](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display)。
- [ES3C28P GitHub 参考](https://github.com/ngttai/esp32_s3_es3c28p)。
- `../小智例程源码/`：1.7.6、2.0.2、2.1.0；按用户要求暂不集成语音。
