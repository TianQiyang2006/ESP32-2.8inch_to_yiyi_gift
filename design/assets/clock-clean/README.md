# 时钟背景素材

六张 PNG 来自 `Ui设计图片/时钟功能预设/1` 中用户提供的六张 JPG。原图保持不变；这里的 PNG 去掉了示例时间和日期，仅修补文字及其阴影覆盖的区域。

正常重新生成 320×240 背景只需要 Pillow：

```powershell
python design/prepare_clock_assets.py
python hardware/scripts/generate_seven_assets.py --clock-only
```

重建时钟字体可使用 `python hardware/scripts/generate_design_fonts.py --only gift_font_clock gift_font_clock_date`。两个生成器的局部更新选项会保留其他页面的资源和头文件声明。

固件使用 PNG 转换成 RGB565 的数组，不经过 JPG 再压缩，也不在设备上运行修补模型。屏幕背景采用等比例 4:3 裁切，保留中间及右侧的日月，具体源图、裁切范围和 SHA-256 见 `sources.json`。

如需从原始 JPG 重做修补：

```powershell
python -m pip install --target tools/clock-assets -r design/requirements-clock.txt
python design/prepare_clock_assets.py --clean --model tools/clock-assets/lama-small.onnx
```

修补工具使用 [LaMa 的 ONNX 量化版本](https://huggingface.co/othytenk/lama-int4-int8/blob/main/lama_int4_int8.onnx)，SHA-256：`d69db99d833ae6fb194fbbe43aa77de5369fff2d906053d693c16ae96ce9a312`。模型基于 [Carve/LaMa-ONNX](https://huggingface.co/Carve/LaMa-ONNX) 和 Suvorov 等人的 LaMa（WACV 2022，Places2 训练数据）；许可证为 Apache-2.0，副本见同目录 `LaMa-APACHE-2.0.txt`。模型只用于生成素材。
