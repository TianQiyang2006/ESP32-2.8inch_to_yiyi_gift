# Clock Dashboard 时钟移植与布局优化验证

最新`0.17.2-clock-layout`按用户参考图重新安排三款布局，并替换圆体及提高字形生成精度，见[时钟参考布局与圆顶修复](时钟参考布局与圆顶修复-20261113.md)。

后续`0.17.1-clock-readability`扩大数字图像容器、修复字形边缘并统一加粗文字，最新结果见[时钟容器与文字清晰度修复](时钟容器与文字清晰度修复-20261113.md)。下文保留0.17.0首次移植记录。

日期：2026-11-13。固件版本：`0.17.0-dashboard-clocks`。

本轮将[teojs/clock-dashboard](https://github.com/teojs/clock-dashboard)提交`7ded058b63e14b85b1afc6e63d8b31481c28ed77`（1.21.5）的全部三种时钟形态移植为现有Arduino / LVGL 8程序中的原生页面，并针对320×240横屏优化排版。本轮没有刷写设备。

## 可见功能与布局

时钟选择页采用两行六卡，保留原三款钟面，并增加“天气大字钟”“纯时钟”“紧凑看板”和共用“钟面设置”入口。

| 钟面 | 时间形态 | 排版与交互 |
|---|---|---|
| 天气大字钟 | 时分／时分秒 | 顶部日期与农历，中间大时间，底部天气与环境数据；点击时间切换秒 |
| 纯时钟 | 时分／时分秒 | 放大并居中显示时间；点击时间切换秒 |
| 紧凑看板 | 时分 | 左侧窄长数字，右侧日期、农历、节日和天气；按上游保持直立和固定字重 |

布局优化重新计算了数字间距与独立冒号间隔，消除数字分组挤压。普通钟面将年月、农历及干支生肖分行，底部保留可读的天气指标；紧凑看板将农历与节日分行，长节日滚动显示。六卡入口的紧凑缩略图缩小字号以完整显示时间。侧边返回与设置页滑块输入保护已接入现有导航。

共用设置包含六种预设色、RGB自定义颜色、100–900九档字重、透明度、数字间距、12／24小时制、秒显示、数字倾斜、农历、节日，以及雨／雪／雷电开关。紧凑看板按上游忽略秒、倾斜、字重与字距。设置先编辑草稿，保存后持久化，取消不保存，写入失败保留草稿与已提交配置。

## 动效与资源

- 变化的数字向上滚动，旧字移出、新字从下方进入；800ms，使用与上游相同的`cubic-bezier(0.18,0.18,0.43,1.34)`，包含回弹和逐位错峰。
- 普通／纯时钟支持±6°数字倾斜，600ms角度过渡；秒显隐时使用500ms尺寸与位置过渡。
- 首次进入、校时跳变、回拨与无效时间恢复直接同步；12小时制隐藏前导零，9:59→10:00时新出现的十位直接显示1。
- 原生绘制并动画化太阳、月亮、云、雾、雨、雪和雷暴图标；整屏雨雪最多20粒子，纯时钟不显示天气特效。
- 数字资源为110个可重着色的四位透明度RLE蒙版，共100,263字节。普通／纯时钟使用Nunito圆体，100档由200档光学细化；紧凑版采用上游ClockDigits形状。
- 1900–2099节日／节气数据由lunar-javascript 1.7.7生成，含11,077条日期事件和58个标签。按上游的农历节日、阳历节日、节气顺序取首项；干支在立春切年，生肖在春节切年。

天气复用设备现有数据。AQI显示接口原值，设备缺少降雨概率字段，因此不显示该指标。

| 运行布局 | PSRAM持有缓冲 |
|---|---:|
| 天气大字钟 | 273,296字节 |
| 纯时钟 | 405,536字节 |
| 紧凑看板 | 267,960字节 |

字形生成时的临时缓冲在生成后释放；页面退出时释放数字缓存、图像缓存和计时器。缓冲分配失败时仍显示静态时间。

## 主机验证

使用生产LVGL 8.4代码和真实LVGL指针输入离屏渲染，未用HTML重画替代界面。

- 63项界面及行为检查通过，生成111张帧图，执行36次页面生命周期循环。
- 覆盖五种时间形态、换秒／进位／午夜、12小时制十位显隐、时间跳变／回拨、无效时间恢复、六种配色及九档字重。
- 覆盖设置页指针操作、秒切换、退出、雨雪雷暴、内存分配失败，以及春节、七夕、冬至、立春和农历／节日开关。
- 8次独立进程检查验证保存与重启恢复，以及写入失败、部分写入和非法配置时保持已提交数据。
- 原三款钟面回归通过；原GIF的357帧及帧时长与Pillow解码一致。
- `run_host_regression.py`全套通过，包含新增配置默认值迁移、类型／范围检查和原子保存断言。

复现命令：

```powershell
python hardware/scripts/verify_dashboard_clock_ui.py --compiler 'D:/CLion 2026.2.3.1/bin/mingw/bin/g++.exe' --obj D:/Tian/codex-dashboard-host --out build/dashboard-clock-20261113
python hardware/scripts/run_host_regression.py
```

`D:/Tian/codex-dashboard-host`是指向项目内`build/host-lvgl`的ASCII目录联接，用于绕过Windows MinGW对中文绝对输出路径的限制。后续仅对新布局增量复测时可使用`--skip-legacy`，前提是本轮旧钟面回归已通过。

证据位于`build/dashboard-clock-20261113/`：

- `ui-verification.json`：测试结果和源码SHA-256；`host/runner-verification.json`：原生运行器结果。
- `dashboard-clock-faces.png`：六卡入口及五种时间形态。
- `dashboard-clock-review.png`：上述界面及设置页总览。
- `dashboard-clock-calendar.png`：节日／节气与紧凑看板排版。
- `dashboard-clock-weights.png`：九档字重。
- `dashboard-clock-motion.gif`：同生产代码采样的60帧动效预览，采样间隔40ms，不代表设备实际帧率。
- `../dashboard-clock-host-regression.log`：主机全套回归日志。

## ESP32构建与设备状态

目标为LCDWiki ES3C28P，ESP32-S3 N16R8，ILI9341横屏320×240，RGB565，40MHz SPI。使用Arduino ESP32 2.0.17-cn、LVGL 8.4和TFT_eSPI，桌面app0分区大小`0x880000`（8,912,896字节）。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File hardware/scripts/build.ps1 -OutputPath build/dashboard-clock-20261113/firmware -BuildPath build/dashboard-clock-20261113/arduino-build
```

最终构建于2026-11-13 13:00完成，命令退出码为0。最后的布局、节日表与12小时制首位显隐修复均包含在该镜像中。

| 项目 | 最终结果 |
|---|---:|
| 桌面镜像`yiyi_desk.ino.bin` | 8,144,096字节（约7.767MiB） |
| app0分区使用率 | 91.37% |
| app0剩余容量 | 768,800字节（约750.8KiB） |
| 编译器程序存储统计 | 8,143,733字节 |
| 链接期全局变量 | 113,852字节（34%） |
| 链接期剩余内部RAM | 213,828字节；此数不含运行时堆分配 |

使用esptool 4.11.0离线检查ESP32-S3镜像，5个段均可解析，校验和与验证哈希有效。镜像完整文件SHA-256：

```text
805d2e3c7ab485a5729a674070d8d801f67208334f76b298b2390def5b9e7cc3
```

额外逐字节核对了427个源码及配置文件的构建副本（仅剥除Arduino添加的首行`#line`映射），326个C/C++翻译单元的目标文件均晚于对应构建源文件，最终镜像时间晚于编译目标文件。界面报告中的全部源码与资源SHA-256仍与当前文件一致。

固件及证据：

- `build/dashboard-clock-20261113/firmware/yiyi_desk.ino.bin`：桌面app0镜像，烧写地址`0x10000`，不含整片Flash内容。
- 同目录的`.elf`、`.map`、bootloader和分区镜像保留用于检查。
- `build/dashboard-clock-20261113/firmware-verification.json`：源码一致性、目标文件和镜像验证结果，以及全部固件文件SHA-256。
- `build/dashboard-clock-20261113/firmware-image-info.log`：esptool镜像检查结果。
- `build/dashboard-clock-final-firmware-build.log`：最终构建日志。
- `build/dashboard-clock-20261113/collect_firmware_verification.py`：上述源码／镜像校验的可复现脚本。

首次构建开始后新增日历源文件，Arduino CLI因此在最终构建时清理了构建选项缓存并完整重编译；最终以该次完整构建及一致性检查作为交付依据。

本轮没有刷机、打开串口或测量设备实际帧率；物理屏幕的滚动流畅度和雨雪模式负载仍需设备验证。

## 源码与适配差异

主要实现为`src/pages/dashboard_clock_page.cpp`、`dashboard_clock_config.cpp`和`dashboard_clock_settings.cpp`；选择页与导航在`src/gift_ui.cpp`，配置验证和默认值迁移在`src/gift_data.cpp`，诊断状态在`desk.cpp`。资源与生成脚本均保存在工程中。

与上游网页的主要差异是：普通圆体使用允许嵌入的Nunito近似，100档字重为光学近似；所有元素按320×240重排；雨雪粒子有数量上限；天气值沿用设备数据，未移植网页专用网络服务。紧凑版原始字体形状已保留。来源、修改说明和CC BY-NC-SA／OFL／MIT许可见`arduino/yiyi_desk/src/pages/assets/dashboard_clock_sources.md`及相邻许可证文件。
