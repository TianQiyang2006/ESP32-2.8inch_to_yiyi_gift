# AiDoGUI / SquareLine 导出界面：接入位置

这个目录就是**生成代码的落地点**。固件已经预留好接入缝，改动量很小。

## 一、为什么需要接入缝

内置界面和设计器导出的界面是**两套不同的屏幕管理模型**：

| | 内置界面 | AiDoGUI / SquareLine 导出 |
|---|---|---|
| 屏幕 | 只有一个 `lv_scr_act()`，每次导航清空子对象重建 | 自己创建多个 `lv_obj_t` 屏幕 |
| 切换 | 重建内容 | `lv_scr_load(ui_ScreenX)` |
| 代码 | `desk.cpp` 手写 | `ui_init()` + `ui_ScreenX_screen_init()` |

两套模型**不能同时当屏幕的主人**。接入缝的做法是：内置界面仍是默认，生成代码按需**接管其中若干个页面槽位**。

## 二、现有槽位

`src/screens.h` 里的 `Screens::Slot`：

```
SlotPageHome  SlotPageAlbum  SlotPageMessage  SlotPageCountdown  SlotPageApps
SlotAppAlbum  SlotAppMessages  SlotAppWeather  SlotAppCalendar  SlotAppCountdown
SlotAppSettings  SlotAppPhone  SlotAppAbout  SlotAppDiagnostics  SlotAppVoice
```

生成代码可以只接管一部分，其余继续用内置界面。**已经验证过的硬件驱动层（ILI9341V 的 MADCTL 方向、FT6336G 触摸）不受影响，不要动。**

## 三、接入步骤

### 1. 导出设置（在 AiDoGUI 里）

- 分辨率 **320 × 240**，横屏
- 色深 **16 位（RGB565）**
- LVGL 版本必须选 **8.4.0**（工程当前是 8.4.0；选 9.x 会因 API 全面变更而无法编译）

### 2. 放进本目录

把导出的 `ui.c` / `ui.h` / `ui_ScreenX.c` / 图片字体资源都放在 `src/ui_export/` 下。Arduino 会编译 sketch 的 `src/` 子目录，无需改构建脚本。

### 3. 写一个适配器

照抄 `ui_demo.cpp` 的骨架，实现 `Screens::Provider`：

```c
#include "../screens.h"

namespace MyUi {
bool provider(int slot) {
    switch (slot) {
        case Screens::SlotAppWeather:
            ui_ScreenWeather_screen_init();   // 或 ui_init()，只在首次
            lv_scr_load(ui_ScreenWeather);
            return true;                      // 已接管，内置渲染会被跳过
        default:
            return false;                     // 不接管，走内置界面
    }
}
}
```

然后在 `deskSetup()` 里注册一次：

```c
Screens::setProvider(MyUi::provider);
```

**返回 `true` 的前提是：你已经在函数里把屏幕切过去了。** 返回 `true` 但没切屏幕，会得到一片空白。

### 4. 绑定实时数据

生成代码里的控件是它自己命名的，例如 `ui_LabelTime`。固件侧在需要时更新：

```c
lv_label_set_text(ui_LabelTime, "10:08");
lv_img_set_src(ui_ImgWeather, &ui_img_sun_png);
```

建议在 AiDoGUI 里给需要动态更新的控件**起可读的名字**（`ui_LabelTime`、`ui_LabelWeather`、`ui_ContainerGif`），这样适配器里能直接引用，你换一次界面我只需改很少的绑定代码。

拍照/留言/天气这些**服务层不受影响**：WiFi、NTP、Open-Meteo、TF 卡、NVS 全部照常运行，只是把结果塞进你的控件。

## 四、Flash 预算（务必先看）

```
app 槽           7,340,032 B
当前已用        ~4,632,000 B  (63%)
剩余            ~2,700,000 B  ≈ 2.6 MiB
其中中文字库独占  3,073,464 B  ← 整个固件的 66%
```

- 生成的**代码**通常几百 KB，没问题
- 危险的是**图片**：一张全屏 320×240 RGB565 图 = **153,600 B**。2.6 MiB 大约还能放 17 张
- 如果图片多，应放到 TF 卡或 SPIFFS（分区表里 `spiffs` 有 1.94 MiB，目前完全没用），但那需要额外写一个 LVGL 文件系统驱动，**目前工程里还没有**

## 五、验证

接入后跑这些，全部应通过：

```powershell
python hardware/scripts/inspect_device.py selftest
python hardware/scripts/inspect_device.py capture --view 'app weather' --out build/check.png
python hardware/scripts/inspect_device.py stress --duration 30 --out build/device-stress.json
```

`selftest` 会同时检查内置界面、触摸、槽位映射和你注册的 provider。参考实现 `ui_demo.cpp` 可以用 `uiexport on` / `uiexport off` 在串口上开关，用来确认接入缝本身没坏。

## 六、GIF 动图（与设计器无关）

**AiDoGUI 不能做 GIF。** 它的图片转换器只产出静态单帧。GIF 播放要在固件侧解决：

1. `lv_conf.h` 打开 `LV_USE_GIF 1`
2. 在界面上放一个容器（比如命名 `ui_ContainerGif`），固件在容器里创建 `lv_gif` 控件
3. 需要一个能读 TF 卡的 LVGL 文件系统驱动（`lv_fs_drv_t` + `LV_USE_FS_*`），**目前没有**
4. GIF 素材放 TF 卡 `/gifs/`，因为 flash 放不下（现有素材共 7.5 MB）

`罗小黑gif和表情/` 里的 10 个 GIF 是 260×568 竖屏、深色背景，需要先裁剪到角色、缩放（建议 120×120 左右）；背景可保留黑底、抠成透明、或改用「月夜小黑」深色主题。
