# yiyi_music —— 双应用布局中的“音乐小智” app1

本目录是 [Maggotxy/xiaozhi-esp32-music](https://github.com/Maggotxy/xiaozhi-esp32-music) 的本地移植副本，作为“给依依的礼物”双应用布局的 **app1**，替代原来的官方小智 2.5.0。

- 上游 fork：`Maggotxy/xiaozhi-esp32-music`，main 分支快照 2025-12-18（最后一次提交：Merge PR #13）
- fork 自身基线：`78/xiaozhi-esp32` **v1.8.5**（fork 内 `PROJECT_VER` 原为 `1.8.5-m2`）
- 本副本工程版本：`2.5.0-music`（桌面 `xiaozhi_voice.cpp` 只认 `project_name=="xiaozhi"` 且版本以 `2.5.0` 开头）
- 构建 SDK：**ESP-IDF v5.4.3**（该 1.8.5 代码不能用 IDF 6.0.1 编译）

## 相对 fork 原版的改动

| 文件/目录 | 改动 | 原因 |
| --- | --- | --- |
| `CMakeLists.txt` | `PROJECT_VER` → `2.5.0-music` | 桌面识别 app1 已安装（版本前缀 2.5.0） |
| `main/Kconfig.projbuild` | 新增 `BOARD_TYPE_YIYI_ES3C28P` | 选板链：config.json → Kconfig → CMake → 板级目录 |
| `main/CMakeLists.txt` | 新增 yiyi 板型分支 | 同上 |
| `main/boards/yiyi-es3c28p/` | 新增板级：`config.h`、`config.json`、`yiyi_board.cc`、`desktop_bridge.cc`、`yiyi_exit_font_20.c` | ILI9341 320×240 自定义初始化、FT6336G 触摸 + NVS 仿射校准、ES8311、退出按钮、USB 串口诊断 |
| `main/boards/common/desktop_bridge.h` | 新增头文件（放在 common 以便 main.cc include） | 桌面 NVS 身份/Wi-Fi 导入、退出回 app0 |
| `main/boards/common/yiyi_touch_transform.h` | 从官方 2.5.0 工程复制 | 复用已验证的触摸仿射映射 |
| `main/main.cc` | 启动时 `ImportDesktopIdentity()`；NVS 损坏时**不再整片 erase** | 双应用共享 NVS，app1 不能清桌面设置 |
| `main/application.cc` | `allow_firmware_upgrade=false`（保留 CheckVersion 获取协议/激活配置，但禁止 OTA 写另一个槽）；离线重试改为 2 次、每次 2 s，失败后 `SetDeviceState(kDeviceStateIdle)` | 双应用布局 OTA 会写 app0（桌面）；离线时不能长时间阻塞主循环（否则退出按钮无响应） |
| `main/boards/common/wifi_board.h/.cc` | 新增 `UsesDesktopWifi()`、桌面 Wi-Fi 提示；桌面模式不启动 AP 配网，等待 15 s 超时后继续；`GetBoardJson`/`GetDeviceStatusJson` 的 `GetRssi()/GetChannel()` 增加 `IsConnected()` 保护 | Wi-Fi 凭据由桌面唯一维护；fork 原版离线时 `GetRssi()` 内 `ESP_ERROR_CHECK` 会 panic 重启 |
| `main/boards/common/esp32_music.cc` | 点歌地址改为从 NVS `music` / `server_url` 读取，缺省仍为 fork 原地址 | 用户自建 VPS 后无需重新编译 |
| `partitions/yiyi.csv` | 新分区：app0 7.5 MiB / app1 5.0 MiB / model 2.5 MiB / spiffs 960 KiB | fork app 实测 4,064,320 B，3 MiB 装不下 |
| `sdkconfig.yiyi.append` | 选板、自定义分区表、USB Serial JTAG 控制台、LVGL canvas | 与本板/双应用一致；诊断串口走 USB |
| `dependencies.lock` | 固定组件版本（IDF 5.4.3 解析结果） | 可复现构建 |

## 构建与烧录

```powershell
# 构建（产物输出到 build/music-firmware/）
./hardware/scripts/build_music_firmware.ps1
# 烧录到 app1（自动选择新布局，保留桌面/NVS/LittleFS）
./hardware/scripts/flash_music_app.ps1 -SkipBuild
```

首次构建需要访问 `components.espressif.com` 下载组件（约 450 MB，缓存于 `%USERPROFILE%\.cache\yiyi-xiaozhi\music-yiyi-build\managed_components`）。

## 串口诊断（app1 运行时，USB 115200）

`status`、`back`、`talk`、`tap <x> <y>`、`music <歌名>`、`musicstop`、`musicmode lyrics|spectrum`、`musicurl <url>|default`、`help`。

音乐服务端接口见 [`hardware/音乐服务接口约定.md`](../../../音乐服务接口约定.md)。

## 已知限制/未验证

- 本机测试环境只有 5 GHz 手机热点（ESP32-S3 仅 2.4 GHz）与不可靠的 Windows 热点转发，**Wi-Fi/语音云端链路未能在本机验证**；需在 2.4 GHz 路由器环境复测。
- 音乐服务端（原作者服务器）已停服；需用户 VPS 实现接口后验证真实播放/歌词/频谱。
- fork 的 `lcd_display.cc` 频谱绘制宽度硬编码 240 px，在 320×240 屏幕上只画左侧 240 px（不影响启动，属显示瑕疵）。
- fork 的音乐播放只在 idle 状态出声，任何语音/状态切换会停播且不续播（上游行为）。
