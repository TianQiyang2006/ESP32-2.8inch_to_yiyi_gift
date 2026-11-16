"""Extract third-page artwork, keeping dates and words in live HTML layers."""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent / 'Ui设计图片/Screenshot_2026-10-08-08-09-28-40_7e617510ce8b5d.jpg'


def main():
    reference = Image.open(SOURCE).convert('RGB')
    # Coordinates refer to the 1426 x 913 original. Erase only baked lettering;
    # the calendar outline, rolled paper and stand remain original artwork.
    scale = reference.width / 1426
    box = lambda coords: tuple(round(v * scale) for v in coords)
    clean = reference.copy()
    draw = ImageDraw.Draw(clean)
    draw.rectangle(box((979, 239, 1225, 306)), fill='white')
    draw.rectangle(box((938, 330, 1220, 453)), fill='white')
    def save_art(image, name):
        # These three graphics sit on pure white. Undo that white matte so the
        # original antialiased strokes also work on a custom card background.
        image = image.convert('RGBA')
        pixels = []
        for r, g, b, _ in list(image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata()):
            alpha = 255 - min(r, g, b)
            pixels.append(tuple(round((c - (255 - alpha)) * 255 / alpha) for c in (r, g, b)) + (alpha,) if alpha else (0, 0, 0, 0))
        image.putdata(pixels)
        image.save(ROOT / 'assets' / name)

    save_art(clean.crop(box((871, 43, 1383, 474))), 'holiday-calendar.png')
    save_art(reference.crop(box((82, 549, 179, 641))), 'holiday-plane.png')
    save_art(reference.crop(box((83, 758, 174, 843))), 'holiday-dragon.png')
    print('Prepared third-page artwork from', SOURCE.name)


if __name__ == '__main__':
    main()
