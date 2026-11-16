# 依依桌面 ES3C28P 小智板型

板型和发行名称均为 `yiyi-es3c28p`，目标为 ESP32-S3，16 MiB Flash、8 MiB 八线 PSRAM。使用 ESP-IDF 6.0.1 或更新版本。本板与 Arduino 桌面程序共存，小智使用 LVGL 9，桌面使用 LVGL 8，两者通过重启切换。

## 硬件

引脚初始化在 `yiyi_board.cc` 中；桌面端对应 `hardware/arduino/yiyi_desk/board_config.h`。

| 外设 | 参数 / GPIO |
| --- | --- |
| ILI9341 | 横屏 320×240，SPI 20 MHz；CS 10、DC 46、CLK 12、MOSI 11、MISO 13 |
| 背光 | 45 |
| FT6336G | I²C 地址 0x38；SDA 16、SCL 15、RESET 18 |
| ES8311 | 与触摸共用 I²C；16 kHz；MCLK 4、BCLK 5、LRCK 7、DOUT 8、DIN 6 |
| 功放使能 | 1，低电平有效 |

Wi-Fi 凭据及触摸校准从桌面既有 NVS 读取；退出按钮通过主任务调度切回 `app0`。仅 `yiyi_board.cc` 导出 `DECLARE_BOARD`。

## 构建与分区

在整个礼物项目根目录运行：

```powershell
./hardware/scripts/build_official_xiaozhi.ps1
```

脚本同步到 ASCII 路径并调用 `scripts/build.py` 的单板构建入口，输出到 `build/official-xiaozhi`。选板链为 `config.json` → `BOARD_TYPE_YIYI_ES3C28P` → `main/CMakeLists.txt` → 本目录。

使用 `partitions/yiyi.csv`：桌面 `app0` 为 0x10000 / 0x980000，小智 `app1` 为 0x990000 / 0x300000，资源为 0xc90000 / 0x280000，LittleFS 为 0xf10000 / 0xf0000。两套程序必须使用相同分区表。

双应用安装入口为根目录的 `hardware/scripts/flash_dual_app.ps1`；它校验分区表与镜像容量，并备份 NVS / OTA 和 LittleFS。IDF 自动生成的普通 `idf.py flash` 参数会把小智放到首个应用分区，不适用于此双应用布局。

## 验证

在本小智工程目录执行 `python -m unittest scripts.tests.test_build.BoardMenuTests -v` 检查选板菜单；完整测试入口为 `python -m unittest discover -s scripts/tests -v`。

编译后仍需上板检查触摸四角与滑动、背光、唤醒 / 录音 / 播放、联网重连，以及桌面 → 小智 → 退出返回桌面的流程。
