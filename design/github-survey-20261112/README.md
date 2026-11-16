# GitHub 调研 · 2026-11-12

背景：桌面固件当前为 `0.11.1-no-arcade`，app0 余量 2,099,408 B（约 2.00 MiB），小智 app1 余 756,800 B。目标是给依依的生日礼物找可加的功能；本轮只做调研，不改固件、不刷设备。

星数均为 2026-11-12 通过 GitHub badges 接口查询的近似值，按项目当前状态记录。

## 1. 情侣 / 纪念日桌面摆件（同品类直接参考）

| 项目 | 星 | 看点 | 可借用的点 |
|---|---|---|---|
| [Commander-bao/MiniTV-for-couples](https://github.com/Commander-bao/MiniTV-for-couples) | 70 | 基于 ESP32 的情侣小电视；天气时钟、电量、纪念日页、情侣互动、动画切换、亮度、快速配网；PCB/外壳/固件全开源 | 双击发送“想你啦”的互动协议；纪念日页信息层级；配网流程 |
| [qingehao/LizCubic](https://github.com/qingehao/LizCubic) | 59 | “异地恋神器”，4G 联网；时钟天气、外出建议、音乐频谱、纪念日、HeartBeat 两台设备联动表达思念；作者自述“给女朋友的一件礼物” | HeartBeat 思路（本机只有一台设备，可改成设备↔手机/VPS）；纪念日 UI；频谱页 |
| [ClimbSnail/HoloCubic_AIO](https://github.com/ClimbSnail/HoloCubic_AIO) | 1.4k | 稚晖君 HoloCubic 的多功能固件：天气时钟、相册、特效、视频、电脑投屏、Web 设置、B 站粉丝、2048；含“纪念日”和“心跳”App | 网页上传文件到设备、Web 配置页做法；“纪念日/心跳”App 形态 |
| [peng-zhihui/HoloCubic](https://github.com/peng-zhihui/HoloCubic) | 6.8k | 原版分光棱镜小电视，生态起点 | 硬件/结构创意参考，与本项目形态不同 |

## 2. 桌宠与表情动画

| 项目 | 星 / 许可 | 看点 | 可借用的点 |
|---|---|---|---|
| [cifertech/TamaFi](https://github.com/cifertech/TamaFi) | 483 / MIT | ESP32-S3 桌宠：6 项数值（饥饿/精力/快乐/健康/好奇/舒适）、成长阶段、动作决策、成就、NVS 存档；脸完全用代码画，不占图片素材 | 养成规则与数值设计；代码画脸省 Flash 的做法 |
| [tagazok/cloudagotchi](https://github.com/tagazok/cloudagotchi) | 11 / MIT | 1.8 寸 AMOLED 桌宠，表情在设备、状态和每日播报（Bedrock+Polly）在云端，MQTT 同步 | “设备只做表情和播放、逻辑放 VPS”的架构——我们已有 VPS，可低 Flash 实现“有记忆的宠物” |
| [0015/lvgl_kawaii_face](https://github.com/0015/lvgl_kawaii_face) | 36 / MIT | LVGL 9 canvas 代码画脸：17 种表情、自动眨眼、平滑过渡、可随父容器缩放 | 黑猫表情系统直接借鉴（注意：本项目桌面是 LVGL 8，需移植 canvas 调用） |
| [txp666/otto-emoji-gif-component](https://github.com/txp666/otto-emoji-gif-component) | 36 / MIT | 21 个程序化生成的 GIF 表情（240×240，Pillow 脚本生成，共约 940 KB），文件名对齐小智 EmojiCollection，可用于小智自定义表情资源 | 生成脚本改画小黑；单表情约 45 KB，选 8–12 个约 300–500 KB |
| [sytnocui/ElectronBili](https://github.com/sytnocui/ElectronBili) | 5 | RTT+LVGL 电子宠物表情系统；记录了“整帧 PNG 序列太大 → 拆成 label/小图分开动”的踩坑 | 别做整屏序列帧；拆分元素动画更省 Flash |

## 3. 小智生态（不加/少加桌面 Flash 就能扩展能力）

| 项目 | 星 / 许可 | 看点 | 可借用的点 |
|---|---|---|---|
| [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) | 33k | 小智固件本体；工作区已有 2.5.3 分支源码 | 设备端 MCP 工具框架（`boards/*` 下 100+ 处 `AddTool` 示例），可在 app1 加自定义工具 |
| [xinnan-tech/xiaozhi-esp32-server](https://github.com/xinnan-tech/xiaozhi-esp32-server) | 11k / MIT | 可自托管的小智后端：MQTT+UDP/WebSocket、声纹识别、知识库、MCP 接入点、短期记忆、插件、指令下发；TTS 支持本地 GPT-SoVITS / FishSpeech / CosyVoice，即“自定义音色” | 若想要“他的声音”当小智嗓子、认人说话、长期记忆，可评估自托管；成本是要从官方云切到自建服务 |
| [78/xiaozhi-assets-generator](https://github.com/78/xiaozhi-assets-generator) | 164 | 浏览器生成小智 assets.bin：自定义唤醒词（S3 支持 MultiNet 中文命令词）、21 个表情（GIF/PNG）、字体、聊天背景 | 礼物感最强的一步：唤醒词改成“小黑/依依”，小智表情换小黑风；需先验证 GrokBot app1 资源包兼容性并整片备份 |
| [toddpan/xiaozhi-esp32-mcp](https://github.com/toddpan/xiaozhi-esp32-mcp) | 73 / GPLv3 | Arduino 版 MCP 客户端库（WebSocketMCP，注册工具给小智云调用） | 思路参考；本项目 app1 已有更完整的 MCP 框架，不必引入 |

## 4. 生日 / 纪念日网页与彩蛋（可部署到手机侧或 VPS）

| 项目 | 星 / 许可 | 看点 | 可借用的点 |
|---|---|---|---|
| [funnyzak/love-page](https://github.com/funnyzak/love-page) | 4 / AGPL-3.0 | 情侣纪念日站点：在一起年/天/时/分/秒、里程碑时间线、纪念日倒计时+当天全屏庆祝、纪念卡、照片墙、背景音乐 | 把“里程碑时间线”“当天全屏庆祝彩蛋”搬进设备；或直接搭一个手机访问的纪念日站点，不占设备 Flash |
| [Yuziki02/zry-birthday](https://github.com/Yuziki02/zry-birthday) | - | 生日祝福网页：倒计时、三段滚动分镜、互动纸条、五彩纸屑/气球/蛋糕动画、测试按钮 | 生日当天设备彩蛋的分镜与动画节奏参考 |
| [VignaChu/particle-cake](https://github.com/VignaChu/particle-cake) | - | three.js 旋转粒子蛋糕 + 自定义祝福语脚本 | 网页端彩蛋参考；可导成 GIF 放设备 |

## 5. 照片 / 回忆

| 项目 | 星 / 许可 | 看点 | 可借用的点 |
|---|---|---|---|
| [dai-hongtao/InkTime](https://github.com/dai-hongtao/InkTime) | 669 | 自托管墨水屏电子相框：AI 分析照片库打分，按“历史上的今天”每天推一张最有回忆价值的照片 | “每天推一张回忆照片”的内容策略；设备端可换成 VPS 拉图 |
| [ClimbSnail/HoloCubic_AIO](https://github.com/ClimbSnail/HoloCubic_AIO) | 1.4k | Web 页上传照片到设备 TF 卡、相册轮播、MJPEG 视频播放 | 网页上传方式（我们无 TF 卡，可改 LittleFS 或 VPS） |
| [goldmorningsmart/EPD-PhotoAlbum](https://github.com/goldmorningsmart/EPD-PhotoAlbum) | - | ESP32-C3 三色墨水屏无线相册，Arduino 端 + Java 上位机，Wi-Fi 传图 | 传图协议参考 |

## 6. 语音 / 声音彩蛋

| 项目 | 星 | 看点 |
|---|---|---|
| [RVC-Boss/GPT-SoVITS](https://github.com/RVC-Boss/GPT-SoVITS) | 65k | 少量样本的声音克隆 TTS（中文效果好，社区成熟） |
| [fishaudio/fish-speech](https://github.com/fishaudio/fish-speech) | 34k | 高质量开源 TTS/声音克隆 |
| [FunAudioLLM/CosyVoice](https://github.com/FunAudioLLM/CosyVoice) | 24k | 多语言 voice clone TTS，小智服务端可直接接入 |
| [Finb/Bark](https://github.com/Finb/Bark) | 9.5k | iOS 推送服务器，“想你啦”可推到手机 |

落地建议：声音不必在设备上合成。用上述工具**预生成 16 kHz 单声道 MP3/IMA**，放 VPS，设备复用现有音乐通道按日期/按钮播放；桌面 Flash 只加播放 UI。

## 7. 天气 / 时钟 UI 与动画参考

| 项目 | 星 | 看点 |
|---|---|---|
| [h6161236-spec/tianqi-shizhong-dahuangban](https://github.com/h6161236-spec/tianqi-shizhong-dahuangban) | 1 | 同为 320×240 触摸桌面摆件（ESP32-2432S028）：天气场景引擎（12 天气组 × 11 时段天空渐变、月相）、4 帧像素精灵跑步+点击跳、BLE 音乐元数据/歌词/频谱、农历+节气、Web 预览自检。注意其用 TFT_eSPI/u8g2，不是 LVGL |
| [Elecrow-RD/ESP32-LVGL-DESK-CLOCK](https://github.com/Elecrow-RD/ESP32-LVGL-DESK-CLOCK) | - | LVGL 桌面时钟成品工程，可看目录组织与时钟 UI |

## 8. 与本项目结合的落地方案（按优先级）

| 方案 | 内容 | 桌面占用 | 风险 / 前置 |
|---|---|---|---|
| A 桌宠表情系统 | 借鉴 kawaii_face 的代码画脸 + otto 的表情生成脚本，做小黑 8–12 个表情（眨眼/开心/委屈/害羞/睡觉/爱心眼），点击有反应，偶尔冒一句情话 | 300–500 KB | LVGL 8 canvas 移植；表情生成脚本改画小黑 |
| B “想你啦”心跳互动 | 设备上按钮/爱心 → VPS → Bark 推送到手机；手机网页点一下 → 设备弹心跳动画+声音 | 30–60 KB + VPS 小接口 | 需要一台常开 VPS（已有）与推送令牌 |
| C 声音彩蛋 / 语音信 | 用 GPT-SoVITS 之类预生成“他的声音”的晚安/生日祝福/语音信，放 VPS，按日期或按钮播放 | 40–80 KB（音频 0 Flash） | 需要电脑侧 TTS 环境；仅自用注意声音授权 |
| D 生日彩蛋 + 纪念日 | 生日倒数解锁、生日当天全屏彩蛋（动画+蛋糕+生日歌+一封信）、在一起天数、时光机小结 | 150–300 KB + A 的可复用素材 | 无；情绪价值最高 |
| E 小智个性化 | 唤醒词改“小黑/依依” + 21 个表情换小黑风（assets 生成器） | app1/assets 侧 | 兼容性与分区写入风险，需整片备份后小步验证 |
| F 自托管小智服务端 | 自定义音色、声纹识别（认人）、长期记忆、知识库 | 0 桌面 Flash | 要从官方云迁移、VPS 性能与稳定性评估；工作量大 |

建议顺序：D（生日场景，时间敏感）+ A（桌宠）+ B（互动）先做，合计约 0.5–0.9 MB，仍留 1 MB 以上机动；C 可与 D 合并做语音信；E/F 单独立项评估。

## 9. 版权与许可

- 罗小黑为版权角色，官方素材不可直接搬运；本项目现有插画为自绘，继续沿用这条路，程序化生成表情最安全。
- MIT 项目（TamaFi、cloudagotchi、kawaii_face、otto-emoji、小智服务端）代码可参考/复用，保留版权声明。
- GPL/AGPL（LizCubic、love-page、toddpan/xiaozhi-esp32-mcp）只参考思路；自用礼物虽不涉及分发，仍建议重写而非整段复制。
- 本项目为自用礼物、不销售，但仍不要把他人账号、密钥或私有数据带进仓库。

## 10. 结论

GitHub 上同品类最值得抄的三件事：**桌宠表情系统（代码画脸/程序化生成表情，省 Flash）**、**情侣互动的“想你啦/心跳”**、**小智侧的个性化（自定义唤醒词与表情）**。照片/视频/投屏类实现多依赖 TF 卡或大缓冲，与本项目无 TF、仅 2 MiB 余量、双应用分区的约束不匹配，优先级放低。
