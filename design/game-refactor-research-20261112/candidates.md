# 游戏 App 重构：GitHub 候选清单

检索日期：2026-11-12。当前阶段为用户选型，尚未改动游戏源码或烧录设备。

目标工程：`hardware/arduino/yiyi_desk`，ESP32-S3、320×240 横屏、触摸输入、Arduino ESP32 2.0.17、LVGL 8.4。现有游戏厅含15款，俄罗斯方块另有独立应用。

下表的难度是针对当前工程的初步适配判断，不是已实测结果。MIT 指已读取仓库对应 LICENSE；“未找到许可证”指已检查的仓库根目录及源码材料中没有找到明确的项目级许可，不把仓库公开直接等同于允许复用。

## 可新增的玩法

| 编号 | 游戏 | GitHub 来源 | 已核实信息 | 适配判断 | 许可 |
|---|---|---|---|---|---|
| 1 | 单指跳跃跑酷 | [CrowPanel Pocket Arcade](https://github.com/lepczynski-cloud/crowpanel-pocket-arcade) / Star Pod Sprint | 原生320×240、Arduino/TFT_eSPI；点击跳跃、障碍、金币、三档速度；已读取 RunnerGame.cpp | 中：规则可复用，渲染和输入接入现有LVGL | MIT |
| 2 | 拼块逃脱／空间华容道 | [CrowPanel Pocket Arcade](https://github.com/lepczynski-cloud/crowpanel-pocket-arcade) / Shift Vault | 6×6棋盘、多种形状、四向滑动、运行时生成可达棋局；已读取 SlidingGame.cpp | 低至中：适合触屏；需要避免6×6棋盘控件过小 | MIT |
| 3 | 反应点击／打靶 | [CrowPanel Pocket Arcade](https://github.com/lepczynski-cloud/crowpanel-pocket-arcade) / Burst Hunt | 原生320×240；点安全目标、躲危险目标、连击、三档难度；已读取 ReactionGame.cpp | 低至中：最适合短时游玩，图形可用LVGL绘制 | MIT |
| 4 | 切水果 | [ESP32-CYD-Fruit-Ninja](https://github.com/nikoloskistefan-cell/ESP32-CYD-Fruit-Ninja) | 320×240、划动切水果、炸弹、生命、分数、划痕；已读取 fruit_ninja.ino | 中：原版Adafruit GFX，需要重做LVGL渲染并验证连续触摸 | MIT |
| 5 | 足球点球 | [ESP32-CYD-Penalty-Shootout](https://github.com/nikoloskistefan-cell/ESP32-CYD-Penalty-Shootout) | 320×240、五次射门、守门员移动和扑救；已读取 penalty_shootout.ino | 中：原版Adafruit GFX，需接入LVGL动画和现有音频 | MIT |
| 6 | 猜色密码 Mastermind | [ESP32-S3-Mastermind](https://github.com/VaAndCob/ESP32-S3-Mastermind) | 4个颜色组成密码、12次机会、每步计时；sketch/README.md明确使用LVGL8.4和Arduino ESP32 2.0.17+ | 中：原版800×480，需为320×240重新排布历史记录 | MIT |
| 7 | 打地鼠 | [Whac-A-Mole-game-LVGL](https://github.com/Elecrow-RD/Whac-A-Mole-game-LVGL) | Arduino、LVGL、LovyanGFX；已查看 custom.c、setup_scr_screen.c 等，原版背景480×320 | 低至中：玩法和定时机制简单，需重排UI及素材 | 未找到许可证；先作玩法／结构参考 |
| 8 | 打飞机／飞行射击 | [LVGL_Game_on_ESP32](https://github.com/LHYhorion/LVGL_Game_on_ESP32/tree/main/lvgl_game/airplane_shooting_project) | 仓库实际含 airplane_shooting_project/main/gui/flygame.c、敌机和爆炸资源，目标为ESP32-S3-BOX-3 | 中至高：适合横屏内小战场；需评估对象数、弹幕和刷新开销 | 未找到许可证；先作玩法／结构参考 |
| 9 | 轻量塔防 | [LVGL_Game_on_ESP32](https://github.com/LHYhorion/LVGL_Game_on_ESP32/tree/main/lvgl_game/pvz_project) | 仓库含植物大战僵尸式LVGL示例 | 高：小屏选卡和战场空间紧张；若选择，建议简化为少路数、少单位并制作原创美术 | 未找到许可证；先作玩法／结构参考 |
| 10 | 迷你高尔夫 | [infinite-golf](https://github.com/MikeWilson/infinite-golf) | ESP32-S3、LVGL canvas、程序生成球场、物理滚动、18洞、2–4人轮流；main/main.c已读取；有纯触摸拖拽击球模式 | 高：原版368×448、ESP-IDF5.5+，需横屏重排和拆分渲染／物理／音频 | 根目录LICENSE未找到；先作玩法参考，复用前继续核实 |
| 11 | 滚珠收集 | [Marble Roller Game](https://github.com/OzInFl/Waveshare-ESP32-S3-2inch-Capacitive-Touch-Display-Marble-Roller-Game) | ESP32-S3、240×320、LovyanGFX；倾斜设备收集宝石，使用QMI8658 IMU | 中；原版必须有IMU。当前工程未确认有对应传感器，可改为触摸拖动控制 | MIT |

同一仓库中不同玩法可以单独提取规则；不需要把其硬件驱动和整个启动器一起替换到本项目。

## 现有游戏的重构来源

| 编号 | 当前游戏 | GitHub 来源 | 值得引入的部分 | 适配判断 | 许可 |
|---|---|---|---|---|---|
| 12 | 2048 | [lv_100ask_2048](https://github.com/100askTeam/lv_lib_100ask/tree/master/src/lv_100ask_2048) | 独立LVGL控件、可调大小、手势／按键、分数和状态接口；已读取实现源码 | 低至中：核对LVGL8 API后可接现有大厅和存档 | MIT |
| 13 | 记忆翻牌 | [lv_100ask_memory_game](https://github.com/100askTeam/lv_lib_100ask/tree/master/src/lv_100ask_memory_game) | 可自定义关卡、样式、背景；已读取实现源码 | 低至中：适合换成项目自己的角色／图案 | MIT |
| 14 | 数独 | [Sudoku_ESP32-S3-TOUCH-LCD-4](https://github.com/Dogfight84/Sudoku_ESP32-S3-TOUCH-LCD-4) | 纯C++求解／生成／盘面／会话模块；唯一解生成、撤销、笔记、按难度计时、NVS；已读取lib/sudoku与测试源码 | 中：原版480×480，纯规则与UI已分离；当前C++11工具链还需核对其C++17使用情况 | 主项目MIT，硬件port和字体另有许可 |
| 15 | 黑白棋 | [ESP32_CYD_Othello_Game](https://github.com/AndroidCrypto/ESP32_CYD_Othello_Game) | Minimax人机对手、可调搜索深度；已读取主ino；README提示深度升高会明显变慢 | 中：替换棋力引擎有价值；需限制每次思考预算，保留UI响应 | 主仓库MIT；Minimax依赖需一并保留相应许可 |

## 选型建议

若目标是让游戏厅更好玩且适合单指触摸，优先考虑1、2、3、4、6，再按偏好加入5。若目标是把已有游戏做完整，优先12、14、15。7、8、9、10的可见玩法值得参考，但本轮没有确认可直接复用的项目级许可。

用户可直接按编号选多款，同时指定现有游戏的保留／删除范围。选定后再做320×240布局和操作方案确认，随后重构规则、渲染、输入、暂停、存档和资源，不先把未选游戏全部写入固件。

## 当前容量与实施约束

根据`hardware/游戏厅实现验证-20261112.md`：当前桌面镜像8,911,824 B，分区8,912,896 B，剩余1,072 B；推箱子12,611关共1,198,045 B，游戏封面和缩略图780,960 B。它们是可重新规划的资源空间，不能把当前余量当成可直接叠加新游戏的容量。

实际重构应先按选定游戏清理不再需要的关卡／封面，再核算最终镜像；容量评估以本次构建的bin为准。当前本轮未构建、未刷机、未验证候选项目在本机硬件上的实际帧率和触摸体验。

## 查看玩法的已核实入口

- CrowPanel三款的[玩法视频](https://www.youtube.com/watch?v=BkuglAy6qOQ)，仓库README含两版GIF。
- 猜色密码的[演示视频](https://youtu.be/52tzvPDqIfw)。
- 足球点球的[演示视频](https://youtube.com/shorts/kxJehX_gJHs)。
- 百问网2048与记忆游戏的仓库子目录包含GIF和PNG预览。
- 数独仓库的SVG图明确标注为界面示意图，并非实板截图。

## 检索与核实范围

通过GitHub仓库检索、raw文件、源码压缩包及浅克隆进行只读检查。API额度限制后改读公开仓库文件；未把星数作为成熟度或可移植性保证。部分归档下载出现网络错误时改读具体源码文件。候选评估基于已取得的README、许可证、依赖配置和代表源码，不等同于整仓审计或移植验收。
