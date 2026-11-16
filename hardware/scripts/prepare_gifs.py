"""Prepare the 罗小黑 GIF clips for the device.

Source clips are 260x568 portrait phone captures with the character in the upper
part and a near-black surround, so they cannot be shown as-is on a 320x240
landscape panel. This crops to the character, scales to the requested square size,
keys the near-black surround to GIF transparency and writes both a preview PNG and
an LVGL C array.

Run:
    python hardware/scripts/prepare_gifs.py "罗小黑gif和表情/lv_0_20261006202304.gif" \
        --size 120 --out-dir design/gifs
"""
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageSequence

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')

# A pixel counts as character content once it is brighter than this. The surround
# of these clips is near-black, so the threshold separates them cleanly.
CONTENT_THRESHOLD = 42
# Anything darker than this in the cropped result becomes transparent.
KEY_THRESHOLD = 30


def content_box(frames):
    """Union bounding box of non-background pixels across every frame."""
    box = None
    for frame in frames:
        grey = frame.convert('L')
        mask = grey.point(lambda v: 255 if v > CONTENT_THRESHOLD else 0)
        found = mask.getbbox()
        if not found:
            continue
        box = found if box is None else (min(box[0], found[0]), min(box[1], found[1]),
                                         max(box[2], found[2]), max(box[3], found[3]))
    return box


def square(box, frame_size, margin=6):
    """Expand a box to a padded square that fits inside the frame."""
    width, height = frame_size
    x0, y0, x1, y1 = box
    x0 -= margin; y0 -= margin; x1 += margin; y1 += margin
    side = max(x1 - x0, y1 - y0)
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    x0, y0 = cx - side // 2, cy - side // 2
    x1, y1 = x0 + side, y0 + side
    if x0 < 0: x1 -= x0; x0 = 0
    if y0 < 0: y1 -= y0; y0 = 0
    if x1 > width: x0 -= x1 - width; x1 = width
    if y1 > height: y0 -= y1 - height; y1 = height
    x0 = max(0, x0); y0 = max(0, y0)
    return (x0, y0, x1, y1)


def make_transparent(image, colours):
    """Key the near-black surround to transparent, then quantise the rest.

    Keying happens first so the surround does not consume palette entries; the last
    palette slot is reserved for full transparency.
    """
    rgba = image.convert('RGBA')
    width, height = rgba.size
    source = rgba.load()
    opaque = Image.new('RGB', (width, height), (0, 0, 0))
    target = opaque.load()
    for y in range(height):
        for x in range(width):
            r, g, b, a = source[x, y]
            if r <= KEY_THRESHOLD and g <= KEY_THRESHOLD and b <= KEY_THRESHOLD:
                source[x, y] = (0, 0, 0, 0)
            else:
                target[x, y] = (r, g, b)

    transparent_index = max(1, colours - 1)
    result = opaque.quantize(colors=transparent_index, method=Image.MEDIANCUT).convert('P')
    palette = result.getpalette() or []
    palette = palette[:transparent_index * 3]
    palette += [0, 0, 0]                       # the transparent slot
    palette += [0, 0, 0] * (colours - len(palette) // 3)
    result.putpalette(palette)

    pixels = result.load()
    for y in range(height):
        for x in range(width):
            if source[x, y][3] == 0:
                pixels[x, y] = transparent_index
    result.info['transparency'] = transparent_index
    return result


def c_array(name, blob):
    """Emit the byte array plus its length.

    The length is emitted as a symbol because the array is declared incomplete in
    the firmware (`extern const unsigned char name[]`), so sizeof() is unavailable
    there and lv_gif needs the byte count.
    """
    lines = [f'const unsigned char {name}[] = {{']
    for i in range(0, len(blob), 24):
        lines.append(','.join(str(v) for v in blob[i:i + 24]) + ',')
    lines.append('};')
    lines.append(f'const unsigned int {name}_len = {len(blob)};')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('--size', type=int, default=120, help='output square size in pixels')
    parser.add_argument('--colours', type=int, default=64, help='GIF palette size')
    parser.add_argument('--max-frames', type=int, default=0, help='0 keeps every frame')
    parser.add_argument('--out-dir', type=Path, default=Path('design/gifs'))
    parser.add_argument('--name', default='', help='C symbol prefix, defaults to the file stem')
    args = parser.parse_args()

    source = Image.open(args.source)
    frames = [f.convert('RGB') for f in ImageSequence.Iterator(source)]

    # The clips animate at roughly 15fps; thin them if asked so the C array shrinks.
    if args.max_frames and len(frames) > args.max_frames:
        step = len(frames) / args.max_frames
        frames = [frames[int(i * step)] for i in range(args.max_frames)]

    box = content_box(frames)
    if box is None:
        raise SystemExit('no character content found in ' + str(args.source))
    crop = square(box, source.size)
    duration = source.info.get('duration', 66)

    prepared = []
    for frame in frames:
        cut = frame.crop(crop).resize((args.size, args.size), Image.LANCZOS)
        prepared.append(make_transparent(cut, args.colours))

    stem = args.name or args.source.stem.replace('lv_0_', 'gif_')
    args.out_dir.mkdir(parents=True, exist_ok=True)
    gif_path = args.out_dir / f'{stem}.gif'
    prepared[0].save(gif_path, save_all=True, append_images=prepared[1:],
                     duration=duration, loop=0, transparency=prepared[0].info['transparency'],
                     disposal=2, optimize=True)

    # Preview strip at 3x so the result can be judged without the panel.
    scale = 3
    preview = Image.new('RGB', (args.size * scale * 3 + 16, args.size * scale), (250, 250, 245))
    for i in range(min(3, len(prepared))):
        shot = prepared[i].convert('RGBA').resize((args.size * scale, args.size * scale), Image.NEAREST)
        preview.paste(shot, (i * (args.size * scale + 8), 0), shot)
    preview.save(args.out_dir / f'{stem}-preview.png')

    blob = gif_path.read_bytes()
    header = (f'// {args.source.name} -> {args.size}x{args.size}, {len(prepared)} frames, '
              f'{duration}ms/frame, {len(blob)} bytes\n'
              f'// Raw GIF bytes for lv_gif with LV_IMG_CF_RAW. Generated by '
              f'hardware/scripts/prepare_gifs.py\n')
    (args.out_dir / f'{stem}.c').write_text(header + c_array(stem, blob), encoding='utf-8')

    print(f'{args.source.name}')
    print(f'  crop {crop} -> {args.size}x{args.size}, {len(prepared)} frames, {duration}ms')
    print(f'  {gif_path} : {len(blob):,} bytes')
    print(f'  {args.out_dir / (stem + ".c")} : C array')
    print(f'  {args.out_dir / (stem + "-preview.png")} : 3x preview')


if __name__ == '__main__':
    main()
