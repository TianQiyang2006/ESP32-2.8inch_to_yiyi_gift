"""Extract the user's fifth-page artwork; interactive text stays in HTML."""
from pathlib import Path
from collections import deque
from PIL import Image

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent / 'Ui设计图片/retouch_2026100808385438.jpg'


def cut(image, box):
    scale = image.width / 1824
    return image.crop(tuple(round(v * scale) for v in box)).convert('RGBA')


def remove_edge_background(image, color, tolerance):
    pixels = image.load()
    seen = set()
    queue = deque([(x, y) for x in range(image.width) for y in (0, image.height - 1)] +
                  [(x, y) for y in range(image.height) for x in (0, image.width - 1)])
    while queue:
        x, y = queue.popleft()
        if (x, y) in seen or not 0 <= x < image.width or not 0 <= y < image.height:
            continue
        seen.add((x, y))
        r, g, b, a = pixels[x, y]
        if max(abs(v - c) for v, c in zip((r, g, b), color)) > tolerance:
            continue
        pixels[x, y] = (r, g, b, 0)
        queue.extend(((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)))
    return image


def main():
    reference = Image.open(SOURCE)
    mascot = cut(reference, (683, 797, 1200, 1363))
    remove_edge_background(mascot, (164, 204, 172), 43).resize((300, 328), Image.Resampling.LANCZOS).save(ROOT / 'assets/daily-mascot.png')
    for name, box in [('anxious', (938, 96, 1113, 281)), ('tired', (1161, 108, 1337, 281)), ('neutral', (1381, 108, 1557, 281))]:
        face = cut(reference, box)
        remove_edge_background(face, (255, 248, 220), 25).resize((120, 120), Image.Resampling.LANCZOS).save(ROOT / ('assets/daily-' + name + '.png'))
    # The lower artwork has no baked counter or labels. HTML supplies the
    # patterned header, mallet, live count and the separate More button.
    wood = cut(reference, (73, 1020, 650, 1367)).convert('RGB')
    wood.resize((420, 253), Image.Resampling.LANCZOS).save(ROOT / 'assets/daily-wood.jpg', quality=94)
    print('Prepared fifth-page mascot, three mood faces and wooden fish artwork.')


if __name__ == '__main__':
    main()
