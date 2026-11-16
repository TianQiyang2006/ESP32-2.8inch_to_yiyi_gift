# 在线 FM / 网络电台 GitHub 调研 · 2026-11-13

用户希望在现有小黑桌面中增加在线 FM，本轮查找和核验现成开源项目，不改固件、不刷机。通过 GitHub 仓库搜索、仓库页面及 raw 源文件核验。以下兼容性结论属于源码与文档检查，未在本工程编译或实板播放验证。

## 当前工程边界

- LCDWiki ES3C28P，ESP32-S3 N16R8，ILI9341，FT6336 电容触摸，ES8311 音频。
- Arduino ESP32 core 2.0.17-cn / ESP-IDF 4.4 系列，LVGL 8.4，横屏 320×240。
- 当前 0.17.6-clock-themes 桌面镜像 7,146,816 B，app0 8,912,896 B，剩余 1,766,080 B，约 1.684 MiB。
- 桌面音乐已有 HTTP、32 kHz 单声道 IMA ADPCM、256 KiB PSRAM 环缓和 ES8311/I2S 输出；目前不是通用 MP3/AAC 直播播放器。
- 最近桌面核验记录内部空闲堆约 79–97 KB，空闲 PSRAM 约 7 MiB 以上；不是新增解码器运行时测量。

## 最贴近：dominikwojcicki/Esp32-WiFi-Radio

仓库：https://github.com/dominikwojcicki/Esp32-WiFi-Radio

- README 与 `docs/board-targets.md` 明确以同款 LCDWiki ES3C28P / ESP32-S3 N16R8 为目标，屏幕、触摸和 ES8311 均一致。
- `shared/lcdwiki_es3c28p/lcdwiki_es3c28p_config.h` 音频脚与本工程一致：MCLK=4、BCLK=5、LRCK=7、DOUT=8、DIN=6，功放 GPIO1 低电平启用，I2C 地址 0x18。显示与触摸引脚也一致。
- 功能：最多 50 个电台、触摸选台、播放/停止、音量、节目元数据、断流重连、网页设置、Wi-Fi 配网、NTP 时钟与电池状态。
- `ESP32WiFiRadio/radio_app.cpp` 有真实的 `initEs8311()`、`audio.connecttohost()`、`audio.loop()` 和停流后重连逻辑；通过 ESP32-audioI2S 在设备端接入电台，架构不要求自建转码 VPS。
- 差异：使用 LVGL 9、Arduino_GFX，240×320 竖屏，依赖 SD 卡保存电台与配置；本工程是 LVGL 8.4 / TFT_eSPI、320×240 横屏、内部存储。适合提取播放器和选台逻辑，再接入现有驱动与页面。
- 源码还含新版 ESP-NOW 接收类型等接口，没有在该项目文档中找到锁定的 Arduino core 版本，不能视为可直接在当前 core 2.0.17 编译。
- 许可证：GPL-3.0-or-later。

证据：[README](https://github.com/dominikwojcicki/Esp32-WiFi-Radio/blob/main/README.md)、[板卡说明](https://github.com/dominikwojcicki/Esp32-WiFi-Radio/blob/main/docs/board-targets.md)、[引脚](https://github.com/dominikwojcicki/Esp32-WiFi-Radio/blob/main/shared/lcdwiki_es3c28p/lcdwiki_es3c28p_config.h)、[主实现](https://github.com/dominikwojcicki/Esp32-WiFi-Radio/blob/main/ESP32WiFiRadio/radio_app.cpp)、[编译依赖](https://github.com/dominikwojcicki/Esp32-WiFi-Radio/blob/main/docs/build-and-upload.md)。

## 其他候选

| 仓库 | 已核验能力 | 对本工程的用途与限制 |
|---|---|---|
| [e2002/yoradio](https://github.com/e2002/yoradio) | README 当前版本 0.9.720；明确有 ESP32-S3、I2S、ILI9341 支持及 Arduino core <3 兼容修复记录；含网页电台管理、元数据、MQTT 与 HLS 修复记录；GPLv3 | 适合借用更完整的电台管理和直播处理。显示走 Adafruit 库；没有据此确认同款 ES8311 初始化，不是可直接加入现有桌面的插件。 |
| [schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) | 音频底层库，支持 MP3、AAC、FLAC 等，支持 m3u8/HLS 与 ICY；当前要求 PSRAM；GPLv3 | 适合评估本机直连直播。当前 master 的新版 I2S API 不适用于 core 2.0.17；旧 tag 3.0.12 仍有 IDF4/5 分支，是锁版本候选。须协调它与现有 AudioBoard 对 I2S/采样率的所有权。 |
| [earlephilhower/ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio) | 同时面向 ESP8266/ESP32 的音频库，提供 MP3/AAC 解码与输入/输出抽象 | 适合只引入需要的解码模块并接已有 AudioBoard。当前 2.4.1 默认 ESP32 I2S 输出使用新驱动；旧 tag 1.9.9 有旧 I2S 和 IDF4.4/MCLK 分支。尚未验证完整工程的兼容与占用。 |
| [marcinozog/AtlasCube](https://github.com/marcinozog/AtlasCube) | ESP32-S3、LVGL、ILI9341、MP3/AAC/FLAC/HLS、网页管理与时钟；MIT | 可参考时钟与电台的交互。README 明确 GitHub 保留至 v0.51.0 的归档，后续开发迁站；依赖 ESP-IDF 5.5.4 + ESP-ADF，整套移入当前 Arduino 工程成本较高。 |
| [Edzelf/ESP32Radio-V2](https://github.com/Edzelf/ESP32Radio-V2) | 完整网络电台，Apache-2.0；README 将 S3 支持标为实验性，明确测试的是 SP/DIF | 可参考协议和预设管理，不能视为已验证的 S3+ES8311 触屏方案。 |

旧版本源码证据：

- [ESP32-audioI2S 3.0.12 / Audio.h](https://github.com/schreibfaul1/ESP32-audioI2S/blob/3.0.12/src/Audio.h)：按 `ESP_IDF_VERSION_MAJOR == 5` 选择 `driver/i2s_std.h`，否则 `driver/i2s.h`。
- [ESP8266Audio 1.9.9 / AudioOutputI2S.cpp](https://github.com/earlephilhower/ESP8266Audio/blob/1.9.9/src/AudioOutputI2S.cpp)：旧 I2S 路径和 IDF4.4/MCLK 适配分支。
- 这些源码分支仅证明有旧框架的适配路径，不等于已在本工程构建通过。

## 建议与空间结论

硬件适配优先参考同款板 Esp32-WiFi-Radio；电台列表、元数据与直播处理可对照 yoRadio。保留当前 LVGL 8.4 页面和 ES8311 驱动，单独评估与 core 2.0.17 兼容的解码版本，是设备直连路线的合理起点。

本机直连需要新增 MP3/AAC 解码及通用直播接入，能否放进剩余 1.68 MiB，应在锁定库、裁剪功能并完成最小集成后编译测量，还要检查内部 RAM 峰值、切台/断流、闹钟音频抢占和长时间播放。不能把整机项目的分区尺寸当作给现有固件增加的体积。

上一轮 50–200 KiB 是“VPS 统一转为 IMA、设备复用已有音乐播放”的方案预算，不适用于直接引入完整 MP3/AAC 库。现成项目证明设备直连有可参考实现，尚未证明移植后的空间和稳定性。
