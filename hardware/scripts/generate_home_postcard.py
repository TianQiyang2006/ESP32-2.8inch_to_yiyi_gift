"""Generate the 194x116 RGB565 postcard image used on the device home page."""
from pathlib import Path

from PIL import Image, ImageOps


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "design/assets/home-postcard-source.jpg"
OUTPUT = ROOT / "hardware/arduino/yiyi_desk/src/pages/assets/home_postcard_image.c"
WIDTH, HEIGHT = 194, 116


def main():
    with Image.open(SOURCE) as source:
        image = ImageOps.fit(
            source.convert("RGB"),
            (WIDTH, HEIGHT),
            method=Image.Resampling.LANCZOS,
            centering=(0.5, 0.5),
        )

    pixels = bytearray()
    for red, green, blue in image.get_flattened_data():
        color = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        pixels.extend(color.to_bytes(2, "little"))

    rows = [", ".join(str(value) for value in pixels[i : i + 32])
            for i in range(0, len(pixels), 32)]
    OUTPUT.write_text(
        '#include "home_postcard_image.h"\n'
        "static const uint8_t home_postcard_image_pixels[] = {\n"
        + "\n".join(row + "," for row in rows)
        + "\n};\n"
        "const lv_img_dsc_t home_postcard_image = {\n"
        "    .header = {.cf = LV_IMG_CF_TRUE_COLOR, .always_zero = 0, .reserved = 0, "
        f".w = {WIDTH}, .h = {HEIGHT}" + "},\n"
        f"    .data_size = {len(pixels)}, .data = home_postcard_image_pixels\n"
        "};\n",
        encoding="ascii",
        newline="\n",
    )
    print(f"Wrote {OUTPUT.relative_to(ROOT)} ({len(pixels)} pixel bytes)")


if __name__ == "__main__":
    main()
