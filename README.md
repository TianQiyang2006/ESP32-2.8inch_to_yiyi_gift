# 给依依的礼物 · ESP32-S3 桌面小黑（公开源码版）

一个送给女朋友的桌面小礼物：LCDWiki ES3C28P（ESP32-S3 N16R8，16 MiB Flash，240×320 IPS 电容触摸），双应用结构——自研 LVGL 8 桌面 + 小智 2.5.3 / GrokBot 语音应用。

**最终固件版本：`0.19.6-barbar-clock`**（2026-11-15 实机部署并通过 49+7 项设备检查）。

## 仓库内容

| 路径 | 内容 |
| --- | --- |
| `hardware/arduino/yiyi_desk/` | 桌面固件（Arduino + LVGL 8.4，七页桌面、时钟、天气、课程表、闹钟/番茄/倒计时、倒数日、音乐、在线 FM、10 款游戏、手机热点配置页） |
| `hardware/arduino/yiyi_desk/README.md` | 主功能说明、版本记录、构建与验证命令（最完整的项目文档） |
| `hardware/esp-idf/`、`xiaozhi-esp32-2.5.3-dev-grokbot/` | 小智 / GrokBot 独立应用（ESP-IDF，app1 分区） |
| `hardware/scripts/` | 构建、烧录、主机回归、界面/设备验证脚本 |
| `hardware/*.md` | 各功能实现与验证记录（时钟、游戏、音乐、FM、电池等） |
| `design/` | 设计资料、UI 稿与版本说明 |
| `hardware/services/` | 音乐陪伴站/在线 FM 配套服务 |

## 构建要点

- 桌面：ESP32 Arduino core **2.0.17-cn**、LVGL **8.4.0**、ArduinoJson 7.4.3、TFT_eSPI 2.5.44、fixed-point Opus 1.3.1；
  FQBN `esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB`，分区表见 `hardware/arduino/yiyi_desk/partitions.csv`。
- 小智：ESP-IDF 6.0.1 / LVGL 9，构建脚本见 `hardware/scripts/build_grokbot_firmware.ps1`。
- 详细命令见 `hardware/arduino/yiyi_desk/README.md`。

## 未包含在本仓库（隐私 / 版权 / 体积原因）

- 个人照片与礼物文案素材、设备个人数据（NVS 设置、Wi-Fi、留言/日记）、设备整片 Flash 备份、成品发布 ZIP；
- 第三方工具链、厂商资料与部分第三方参考工程；
- 因此本仓库**不能直接重建与实机完全一致的固件**（缺少个人素材），但代码、文档与设计可完整追溯。

## 说明

- 本仓库为公开源码版；完整发布包（含固件镜像/证据备份/个人数据）保留在私有仓库，不在此公开。
- 代码与文档为个人项目，仅供学习参考；引用的第三方库/字体保留其原始许可（见各目录内 LICENSE / 说明）。
