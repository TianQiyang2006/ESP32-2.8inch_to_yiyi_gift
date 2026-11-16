# LVGL 七页移植与验证 · 2026-10-08

目标板：LCDWiki ES3C28P，ESP32-S3 N16R8，原生 240×320 ILI9341 IPS/CTP，320×240 横屏；串口 COM8，USB VID 303A / PID 1001。七页移植版本 `0.2.0-seven-ai`，显示/触摸修正版本 `0.2.1-panel-fix`。

## 设计还原

依据 `design/mockups/` 已确认的八张界面图及其 HTML/CSS，恢复邮票时钟、天气大图标和七天预报、卷纸日历与节日数字栏、名言与楷体诗句、心情圆圈与本周记录、木鱼纹理/槌子/反馈、饮水卡片、倾斜便签、白色应用图标卡、细滑杆和三种钟面。

图片、SVG 图标和字体独立转换，原始素材和 GIF 保持原样；时间、星期、天气、正文及所有计数均为真实 LVGL 控件，没有用整页截图代替活数据。

逐页对照中修复了压缩字体未开启、透明旋转文字不显示、图标缺少 CSS 描边、诗句重复换行、中文标点缺字、装饰面板吞掉点击等问题。主机对照图为 `build/lvgl-port-review/visual-comparison.png`。

## 主机验证

- `hardware/scripts/test_native_lvgl.py`：真实 LVGL 8.4.0、字体、共享数据及文件提交，保留有效断言 `-UNDEBUG`。通过七页、真实指针点击、图标入口、拼音 ni → 你、键盘/候选区域边界、历史日记、未来日期禁用、保存失败回滚、饮水撤销、木鱼、留言详情、三种钟面、语音按钮、真实滑块事件、20% 电量策略、星期/跨午夜和文件重启读取。
- `hardware/scripts/test_voice_protocol.py`：编译实际 Opus 1.3.1 fixed-point，校验官方 v1/v2/v3 字节序、长度及截断拒绝，并在 8/12/16/24/48kHz 真实编码和解码 60ms 音频。通过。
- `tools/design-review/check-live-data.cjs`：HTML 共享天气和电池、跨日/月/年、低电量与常接电、草稿保持、旧请求拒绝及离线导出。通过，已重新生成 designer-bundle.js。
- `tools/design-review/check-firmware-portal.cjs`：实际嵌入式手机 HTML，隔离模拟设备 API，检查设备日期日记、草稿、失败保存、文字安全、亮度/静音、Wi-Fi 选择、共享地区、GPS 与无密码/令牌导出。通过；该测试没有向实板发送模拟数据。

主机截图含测试日期和天气。实板目前没有配置 Wi-Fi，截图按实际状态显示等待校时、未设置城市及 `--`。

## 备份、编译与实板

整片 Flash 已通过 ROM 分块读取，拼接为完整 16,777,216 字节，并核验 SHA-256：

`build/backups/esp32s3-before-seven-pages-20261008-16mb.bin`

同目录 `.sha256` 是校验文件，`before-seven-pages-parts/` 保留各块及校验。长单次 ROM 读取曾因 USB 中断失败，stub 读取曾出现短帧，均没有生成可用的完整备份；最终使用 `hardware/scripts/backup_current_device.py` 完成分块备份后才写入。

最终编译产物位于 `build/seven-ai-final-output/`，编译日志 `build/seven-ai-release.log`。实际 LVGL 为 8.4.0，Arduino core 为 2.0.17-cn。应用约 7.58MB，占两个 7.5MiB 应用槽中单槽容量的 96%；静态内存约 74KB。

写入 bootloader、分区表、OTA 初始化数据与 app0，没有写入 NVS 区。新版文件系统偏移为 0xf10000，旧完整 Flash 备份已保留。esptool 写入后哈希校验通过。

实板状态与自检：

- `0.2.0-seven-ai`，16MB Flash，8MB PSRAM。
- `audioReady=true`，`audioError` 为空：ES8311 I²C 初始化、I²S、Opus、队列与任务启动成功。
- 原亮度设置迁移为 79%，音量 45%，供电配置为外部电源/未安装电池。
- 空闲页可用内部堆约 143KB、PSRAM 约 7.8MB，运行截图期间未见重启。
- `logic / touch / screens / uiExport / messages / widgets / text` 均 PASS。
- 两轮共 28 次模拟滑动、12 个应用入口、日历和设置嵌套返回，通过；15 秒连续运行与 65 秒无操作回主页，通过，记录 `build/device-seven-stress.json`。

实板帧截图：`build/device-seven-review/seven-pages-contact.png`。每次截图校验 320×240 RGB565 长度和 CRC32，单页及设置/时钟/小智截图也在该目录。

## 验证边界

串口模拟触摸通过真实 LVGL 输入通路，帧截图来自传给 LCD 的像素；它们不能证明玻璃上的颜色、物理触摸边缘或听觉质量。本轮已通过用户实物反馈确认测试图颜色/边界及完整固件第五页的画面、点击和木鱼敲击声。其余页面未逐一获得用户实物验收，麦克风音质、语音播放质量、官方云端账号绑定与完整语音往返仍需相应实物及联网条件。

当前开发板未配置 Wi-Fi、未联网校时、未设置天气地区，未插入 TF 卡。固件没有伪造实时值。用户可从设置直接配网，连接手机修改全部设置，首次打开小智后按绑定码绑定。

TP4054 CHRG 和 USB VBUS 没有接至 MCU；插电和安装电池由设置指定，ADC 只用于已安装电池的电压估算，固件没有声称自动检测充电。手动对话已经接入，唤醒词和 AEC 未实现。设备 HTTP 门户不支持浏览器直接 GPS，须使用可信 HTTPS 或搜索城市/填写坐标。


## 2026-10-08 · 实屏反馈后的驱动复核

用户报告原生面板是 240×320 ILI9341 IPS，CTP 点击错位；照片存在偏蓝，随后确认即使 0.2.1 恢复 bgr=true/touch=0，仍有间隔色块和错位。0.2.1 的参数迁移、自检与真实触摸计数不构成实屏驱动通过，此次反馈为失败记录。

对照 GitHub：

- 用户提供的 `Dreams-Possible/ILI9341Driver`（9f608f11cd0b41912546d3982a8272ec1086c186）：根目录是 IO/SPI 函数仍待实现的模板；old 中有 ESP-IDF 端口，但 GPIO 与本板不一致，也不含 CTP 驱动，不能原样替换。
- `mrcyclo/ES3C28P`（e5ebddaf1d0cdcc47af48c74c3741eacd42606b4）：同型号、相同 ILI9341/FT6336 与引脚，使用 TFT_eSPI，并明确提示不要覆盖其 B6 扫描控制。
- `ngttai/esp32_s3_es3c28p`：同板 ESP-IDF BSP，使用独立 LCD 与 FT5x06 兼容触摸组件，BGR/IPS 开启。
- `Bodmer/TFT_eSPI` 固定到 16e37595040eac69cd628e4bffb56fc30cad6299（2.5.44），位于工程 tools/panel-libraries/，未修改用户全局 Arduino 库。

0.2.2 采用 TFT_eSPI 的 ILI9341 旋转/扫描表，20MHz SPI、6MHz 读回，对齐的内部 RAM 行缓冲发送 native RGB565，不再混写旧 B6/MADCTL 初始化。完整固件编译通过：7,587,261 字节，静态 RAM 74,448 字节，产物 build/tft-panel-output/。

独立 panel_probe 已烧录 app0，保留分区、用户 NVS 及日记文件系统。用户确认新测试图颜色和边界正常；原生单像素网格 GRAM 读回 200/200。首次 24 像素边缘五点测量没有通过中心点校验，未保存；改为更内侧的靶点及稳定按压采样继续校准。校准及完整七页的实屏结果以之后的记录为准，不能把测试图通过直接当成完整产品验收。


第三轮五点校准已通过并保存，用户回复“弄好了”。四个角点参与拟合，中心点独立验证；实测最大误差 5.38 像素。原始点、矩阵和逐点误差见 build/tft-panel-review/calibration-final.json。实测 y 轴比例约 -1.263，证明原来仅假设 `239-rawX` 的映射不足；完整固件从 NVS 加载本次原始 CTP 仿射矩阵，独立于模拟输入。


完整七页 0.2.2-tft-panel 已重新写入 app0，esptool SHA/MD5 写入校验通过。重启后的实板状态确认 `displayDriver=TFT_eSPI`、`spiMHz=20`、`touchCalibrated=true`、`touchMode=0`、`bgr=true`、`inverted=true`，亮度 79%、音量 45% 和供电策略保留；全部七模块自检 PASS。记录 build/tft-panel-review/device-status.log、device-selftest.log，主页与第五页帧为 home.png、daily.png。随后用户实物验收结果见下文；尚未配置 Wi-Fi/NTP，当前按日记录没有有效日期。

### 最终实物反馈（2026-10-08）

用户对“第五页画面是否正常、橙色卡片的‘更多’是否能进入木鱼详情、点击木鱼是否能听到敲击声”回复：**“画面、点击和声音都正常”**。据此确认本轮完整固件第五页的显示、实际触摸入口及木鱼声音通过；不是以帧截图、自检或串口模拟点击替代验收。

收到反馈前的串口复核显示 `version=0.2.2-tft-panel`、`page=4`、`touchCalibrated=true`、`physicalTouchPresses=179`，TFT_eSPI/20MHz、亮度 79%、音量已由用户调为 93%。本轮未再次烧录，未清除校准或更改音量、亮度和供电设置。保留通过实物验证的驱动及 NVS 五点仿射矩阵。本轮显示/触摸修复完成，不扩展为其余页面或云端小智功能全部验收通过。

## 2026-10-08 · 小智握手与页面切换优化

已编译并仅写入 app0：`0.2.3-voice-smooth`。最终固件占用 7,593,161 字节（96%），静态 RAM 79,040 字节，esptool 写入校验通过。固件 `build/voice-smooth-output/yiyi_desk.ino.bin`，SHA-256 `F1A067CE3103A3ADA11AAD39E70F7F8E3D6C0CC6604AA0010A0592C0B640F18F`。保留 ILI9341/TFT_eSPI 20MHz 配置、真实五点仿射校准、Wi-Fi、个人设置及日记。修改前源码快照 `build/snapshots/voice-smooth-before-20261008-152052/`。

### 握手修复

原固件用一个 12s 时限覆盖网络和应用 hello，而且发送失败未直接报告，服务器的空关闭帧也被忽略。实测发现 Arduino 大写 MAC 与官方小写设备 ID 不一致：大写身份下服务器连接后立即关闭，统一激活/绑定/WS 的小写格式后，收到真实 hello。IDF 4.4 的接收任务持有递归锁，hello 改为在连接回调内先发送，成功后通知工作任务；停止和销毁仍仅在工作任务执行。

新增独立连接阶段计时（网络 40s、hello 成功发送后 10s）、一次有限自动重试、发送结果检查和关闭帧报告。状态诊断不包含令牌、完整 URL 或对话文本。最终实板自动验证 `build/voice-smooth-review/connection-final.json`：首次尝试成功，网络 7906ms，hello 2151ms，`voiceConnected=true`、`voiceStage=Listening`，协商 24kHz 输出，收到服务器 MCP 初始化/工具消息。检查脚本立即打断并关闭录音，未要求服务器生成回答。

先前同样小写身份的实测为网络 8191ms + hello 4000ms，超过旧 12s 总限额。最终全部七项自检 PASS。云端握手已实测；真实一句话的 STT/TTS 和扬声器听感仍待用户反馈，不能以 Listening 状态替代完整语音验收。

### 页面与刷新

七页、应用和详情入口统一使用 220～260ms smoothstep 缓动的渐变揭示。目标 UI 先完整绘制进 PSRAM，保留旧帧，通过对齐内部 RAM 的 8 行缓冲只发送变化的窄带，保持已有 20MHz 原生 RGB565 与布局。过渡期间禁用新页面输入，过渡中按住的手指必须松开再点击，防止误触。截图命令先完成过渡，后台数据变化最终完整补刷。

天气图标、心情图标、月历颜色/高亮、点阵钟面和显隐状态只有变化时才更新。主机真实 LVGL 测试新增七页静态数据连续刷新无 LCD flush 的断言，全部通过。`test_display_transition.py` 编译实际 `display.cpp` + 真实 LVGL，仅以记录像素的 TFT/SPI 接口替代硬件，验证分区像素、双向过渡、部分重绘与抓帧完成；`test_panel_driver.py` 验证动画期间按压不会在新页误点击，均通过。独立协议测试覆盖两阶段时限、计时回绕和真实 Opus，通过。

`build/voice-smooth-review/navigation.json` 实板 28 次模拟滑动、12 个入口、嵌套返回与 65s 空闲回主页通过，75 次状态采样未见重启；每次过渡 8～12 次窄带更新，实际耗时 228～282ms。亮度 79%、音量 93% 与真实校准保持。该测试证明切换完成和导航正确，用户对顺滑感的实物反馈仍待接收。


## 2026-10-08 · 0.2.4 心情与中文清晰度优化

最终固件 `build/clarity-output/yiyi_desk.ino.bin`，文件 7,345,168 字节；Sketch 7,344,805 字节（93%），静态 RAM 78,920 字节。SHA-256 `3D47939E70D1076E0ADACC479473B510AFF79B67879F8CBD4FE41032385EB5A2`。日志 `build/clarity-review/build-final.log`、`upload-final.log`。只写入 `0x10000` 的 app0，未写 NVS、分区表、文件系统。实板启动确认版本、20MHz 原生面板配置、真实触摸校准与音量/亮度不变。

本轮完成心情点选代替日记、六表情与居中加号、全屏三种秒钟、新月夜数码样式及缩略图、双向滑回预设、无边框留言板和设置里的留言编辑入口。网页与实机按原生320×240布局；原始素材保留。共享22px完整CJK 4bpp字体，经面积采样生成其他字号，阅读详情22px，中文换行按实际字宽避开行首句号和右引号；数据不变时不重排文本。小智按本目录官方2.1.0 LCD源码的左右气泡区分用户和回答，STT更新用户文本，TTS逐句累加；跟随底部与手动查看旧内容分别处理。

验收：

- `python hardware/scripts/test_native_lvgl.py` 通过真实LVGL七页、六种心情、历史日期/旧文本保留、秒数强制开启、字体和标点换行、语音气泡和长回复滚动、静态数据不重复LCD刷新。画面 `build/host-lvgl/`；语音文本来自隔离测试桩，不是云端实测。
- `node tools/design-review/check-clarity-ui.cjs` 通过加号、6PNG、日期保存、跨午夜、全屏布局、秒数增量DOM、左右滑返回、22px详情、设置入口、语音演示气泡与离线素材；`build/clarity-review/browser/report.json`。测试等待滑动后的点击抑制窗口结束，未删除真实防误触逻辑。
- `node tools/design-review/check-firmware-portal.cjs` 已通过实际嵌入门户的心情接口、旧数据保留、强制秒数与保存失败/草稿/设置测试。
- `python hardware/scripts/check_clarity_device.py` 最终通过实板心情选择入口、留言/阅读详情、三钟面、秒数两帧变化、左右滑回父菜单及个人设置保留。七项自检全PASS。切换完成后采样到222～273ms的过渡，报告 `build/clarity-review/device/report.json`。脚本对异步过渡等待完成，不能以固定0.7s读取替代完成状态。

当前网络与验收边界：最终固件复测时Wi-Fi未连接，小智显示“请先进入设置连接 Wi-Fi”，没有发起握手，故 `build/clarity-review/connection-final.json` 为未通过。此前本轮较早的实际云端Hello记录 `connection.json` 已成功（网络9248ms、Hello3992ms）；此前7页压力报告 `navigation.json` 也已通过。不能把旧固件记录当成最终版本完整语音往返验收，真人STT/TTS、音质和玻璃上的观感仍须用户实物反馈。此轮帧截图证明渲染与串口LVGL输入通路，显示驱动继续沿用已由用户确认颜色、点击与木鱼声正常的配置。
## 2026-10-08 · 0.2.5 连续对话与音频缓存

最终固件为 build/continuous-output/yiyi_desk.ino.bin，版本 0.2.5-continuous-voice；最终编译占用 7,348,773 字节（93%），静态 RAM 79,096 字节。固件已烧录 COM8，保留真实面板参数、五点 CTP 仿射校准、NVS 设置和文件系统；烧录过程的分块哈希校验通过。

小智页现在一次点击“开始对话”后进入连续会话：麦克风以 RMS VAD 检测约 180ms 起始，静音约 900ms 自动发送，保留约 240ms 预录音避免吞掉开头；回答播放结束后约 220ms 自动重新监听。连续会话无语音 60 秒进入休眠，停留在小智页面并显示休眠提示；“打断”会结束会话并关闭连接。小智页面不参与自动回主页计时，因此连续监听、回答和休眠均不会跳回主页。音频播放使用 PSRAM 有界环形缓存，网络、事件队列、麦克风采集、编码、解码、播放输出、内存和协议错误分别显示可操作提示，并在诊断接口提供缓存量、队列量、VAD 电平和空闲秒数。

验证结果：

- python hardware/scripts/test_continuous_voice.py 通过 VAD 起止、噪声/短暂停顿边界、连续重监听、60 秒休眠、打断、millis 回绕、36 秒 Opus 突发和 40,000 次可变长度音频环形缓存操作。
- python hardware/scripts/test_voice_protocol.py、python hardware/scripts/test_native_lvgl.py 和 node tools/design-review/check-clarity-ui.cjs 均通过。
- 实板状态确认 audioReady=true、audioError=""、touchCalibrated=true、TFT_eSPI/20MHz、无重启；过渡状态为约 268ms/12 帧。
- 实板小智握手曾成功一次：build/continuous-review/connection.json 记录网络 6963ms、Hello 2144ms，收到服务端 hello 与 MCP 初始化并进入“正在听”，随后由测试主动中断。之后再次测试时激活服务返回 HTTP -1，固件正确显示“服务连接失败”并保持在小智页；这是网络服务侧失败，不改变已通过的握手记录。

真实用户语音的 STT/TTS 往返、VAD 对不同说话音量的阈值和扬声器听感仍需在用户实际说话时确认；自动化测试没有伪造云端回答或把网络失败记为通过。

## 2026-10-08 · 双应用官方小智验收补充

最终双应用刷写完成。实板从全部功能页小智图标进入 app1（官方 2.1.0），官方初始化页截图 `build/official-device-review/official.png`；新增退出按钮经真实 LVGL 触摸事件返回 app0 的全部功能页，`returned-apps.png` 和 `desktop-return.json` 已保存。官方入口、桌面启动稳定性、天气温度排版、无打断 UI 和网页检查均通过。

官方音频计数曾出现 input 2910、encoded 1093、decoded 77、played 76，并记录两段真实服务端回答文本；这证明官方采集/编码/解码/播放链路运行过，但未把电脑外放未识别或服务侧偶发连接失败记作真人语音验收通过。

## 2026-10-09 · 桌面统一 Wi-Fi 配置与官方重连

用户要求只在外层桌面连接网络。双应用切换会重启无线驱动，官方 app1 自动读取桌面 `yiyi-desk` 保存的 SSID/密码并重新连接，无需用户再次配置。导入会替换旧官方网络或清除已被桌面删除的网络，完全相同时不写 NVS；已有官方 UUID 不覆盖。

桌面入口新增联网检查，离线时留在桌面提示进入设置，未写共享参数、切换分区或重启。Yiyi 官方板禁止无凭据、60 秒超时和显式请求进入独立配网；扫描重试为 5～15 秒，连接超时保留 station 重连，Connected/timeout 竞态和异步提示执行时再次检查状态。其他官方板默认配网流程仍由主机测试覆盖。

第一版 Wi-Fi 修复实板运行发生一次 MQTT hello 超时后的唤醒词编码 `bad_alloc/abort`，记录 `build/desktop-wifi-review/pre-wake-fix-crash.txt`。编码任务遍历的 PCM 容器被检测线程同时修改，现已在锁内交换成任务独占缓存，队列操作加锁，等待前次静态任务删除后再复用栈；自动唤醒仅在语音通道打开成功后编码。相同多线程测试对旧生产源失败、修复源通过，控制日志 `wake-before-control.log`。右上角退出按钮的 basic 字库没有“退”，最后改为官方完整 20px 字库，未重做官方 UI。

最终固件：

| 应用 | 文件 | 字节 | SHA-256 |
| --- | --- | ---: | --- |
| 桌面 app0 | `build/yiyi_desk/yiyi_desk.ino.bin` | 7,252,304 | `CE5089AC8AB5DEB68ADEF9B4D07C76CA50AC1973FAA69CAB47EE8D559B6D7C34` |
| 官方 app1 | `build/official-xiaozhi/xiaozhi.bin` | 4,010,816 | `BDF7F04C496FEA81B43BBC12313CC860659A01576AE4C3B55C44E8F0DD64D9AC` |

官方 app1 容量 0x500000，最终仍有 1,232,064 字节空间。最后仅刷 app1 的日志 `build/desktop-wifi-flash-exit-font.log` 确认 `Hash of data verified`；没有刷写 NVS 或文件系统。桌面已在本轮前一次双应用更新时完成烧录，日志 `build/desktop-wifi-flash.log`。改动前七个生产文件快照 `build/snapshots/desktop-wifi-before-20261009/`，完整本轮差异 `build/desktop-wifi-review/source.patch`。

验证结果：

- `python hardware/scripts/test_desktop_wifi.py` 编译未改写的实际 Wi-Fi、桥接、桌面入口和 AFE 生产源文件，仅替换硬件接口。三组 PASS 包括凭据替换/删除、NVS 免重复写、UUID 保留、Connected/timeout 竞态、重连、普通官方板配网、离线入口、取消和启动失败、设置同步、真实线程并发 PCM 和静态任务栈复用。日志 `build/desktop-wifi-host-test.log`。
- 实板桌面离线入口不重启，显示正确设置提示并可返回；`build/desktop-wifi-review/offline-entry.json`、`offline-entry.png`。
- 含最终网络和音频修复的官方固件联网连续 45 秒通过，随后关闭 AP 并等待真正断链，持续离线 80 秒保持 `wifiConfigMode=false`、Idle 且 uptime 递增，无重启；`final/official-stable.json`、`final/official-offline.json`。恢复同一热点后自动联网，`final-recovery/official-online.json` 记录 uptime 266947ms、`wifi=true`、`wifiConfigMode=false`。这次长测在最后只替换退出字库之前，报告已区分版本。
- 最后更换字库、重新构建烧录后，从全部功能页原有小智图标进入，自动连接桌面网络，再连续运行 30 秒通过；`final-font/official-stable.json` 最后状态 Listening、输入 977、编码 502、解码/播放各 18 帧，没有重启或配网。`final-font/official-connected.png` 视觉确认“退出”两字完整。
- 最终通过实际 LVGL 输入回调模拟点击“退出”，返回 app0/page6，桌面自动恢复 Wi-Fi；`final-font/desktop-return.json`、`final-return/desktop-online.json`。亮度 79%、音量 53%、触摸校准和 audioReady 保留。该测试验证真实 UI 事件通路，不等同于用户手指触摸验收。

上述证据汇总于 `build/desktop-wifi-review/report.json`。完整真人语音 STT/TTS 和扬声器听感尚未验收，不能把 Listening 或音频帧计数当作完整对话通过。设备已退出回桌面，电脑现有热点保持开启；小智继续通过全部功能页进入。

## 2026-10-09 · 官方小智 v2.5.0 最终实板验收

官方上游版本为 Xiaozhi v2.5.0，commit `ac6deed3d8e75348475364bf40ad953c6cd48054`，使用 ESP-IDF 6.0.1。桌面 app0 保留原有“全部功能”入口；app1 使用官方界面、激活和服务流程，关闭打断路径，并额外提供右上角“退出”。实测发现官方 `SetupUI()` 完成前创建的退出控件会被状态栏层级盖住，现由板级显示类在官方 `SetupUI()` 返回后添加控件，官方页面其余内容未改。

本轮只更新 app1，设备设置和文件系统未重刷。分区表哈希 `EE3D0E59DE2A1079D124E97E48E4E84C9E8420DF6DEFFE6A32F5145D857CCDDE` 与桌面构建一致，三个镜像均未超过现有分区：

| 镜像 | 字节 | 分区容量 | SHA-256 |
| --- | ---: | ---: | --- |
| 桌面 app0 | 7,252,928 | 7,864,320 | `702EAA78A2A9E029CB166EC7056AE48A6657D9B9C6658A6EBA8FC42BC361ACFA` |
| 官方 app1 | 2,722,064 | 5,242,880 | `E47AD1E3FBD0CB06DFD8730CDC9ECA97DD16DE04C703F5312420BF5165398BB0` |
| 官方资源 | 1,664,265 | 2,621,440 | `8D4864439ADFD61E3AFA044F36E3E925E12FDC5098FAD30D0773756C5011B710` |

实板证据：

- `python hardware/scripts/check_official_device.py enter` 从 page6 原图标启动 `official-2.5.0`；Wi-Fi 自动连接桌面保存的网络，期间 `wifiConfigMode=false`。官方界面及退出按钮截图为 `build/official-device-review/final/official.png`。
- `python hardware/scripts/check_desktop_wifi_device.py official-stable --seconds 60` 连续 60 秒通过，30 个采样均联网、未配网且 uptime 递增。关闭原有桌面热点后执行 `official-offline --seconds 80`，40 个采样保持离线、不进入配网、不重启；恢复热点后 `official-online` 自动联网。样本见 `build/desktop-wifi-review/official-stable.json`、`official-offline.json`、`official-online.json`。
- 通过相同 LVGL 输入回调点击截图中的“退出”，设备返回 app0/page6，随后桌面自动连接网络。亮度 79、音量 53、触摸校准和 audioReady 均保留；`build/official-device-review/desktop-return.json`、`returned-apps.png`、`build/desktop-wifi-review/desktop-online.json` 可查。此自动输入路径不等同于手指触摸验收。
- app1 单分区刷写日志 `build/xiaozhi-2.5.0-exit-overlay-flash.log` 含 `Hash of data verified`。随后 `build/xiaozhi-2.5.0-verify-flash.log` 用 `esptool verify-flash` 分别读回 app0、app1 和资源，三个摘要全部匹配。
- `python hardware/scripts/test_native_lvgl.py` 通过主页温度边界、℃单位不重叠、未知天气，以及七页原生 LVGL 行为。实板主页截图 `build/official-device-review/home.png` 当前显示“未设置城市/暂无天气”，故没有实时天气值可供实屏比对。

构建日志为 `build/xiaozhi-2.5.0-exit-overlay-build.log`，汇总和升级源码差异为 `build/xiaozhi-2.5.0-review/report.json`、`upgrade.patch`。完整真人 STT/TTS 对话和扬声器听感仍未验收；Listening 状态及音频计数不作为完整真人语音验收依据。
