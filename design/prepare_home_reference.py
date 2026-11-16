"""Extract illustration layers from the user's Ui设计图片 homepage reference.

Only artwork is rasterized. Time, weather values and calendar dates are drawn by
the HTML UI. The original reference and source GIFs are never rewritten.
"""
from pathlib import Path
from PIL import Image, ImageOps, ImageFilter, ImageDraw
from collections import deque

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent / 'Ui设计图片/retouch_2026100722571065.jpg'


def extract(reference, box):
    scale = reference.width / 1824
    return reference.crop(tuple(round(value * scale) for value in box)).convert('RGB')


def main():
    reference = Image.open(SOURCE)
    art = extract(reference, (133, 68, 1206, 680))
    ImageOps.contain(art, (800, 600)).save(ROOT / 'assets/postcard-art.jpg', quality=94)

    weather = ImageOps.contain(extract(reference, (103, 883, 527, 1298)), (600, 600))
    w, h = weather.size
    # Remove the screenshot's baked cloud and 36°C using nearby flower texture.
    # A feathered edge keeps the illustration continuous; the widget draws its
    # own icon and configurable temperature on top of the resulting artwork.
    box = (round(w*.04), round(h*.075), round(w*.30), round(h*.38))
    floral = weather.crop((round(w*.32), 0, round(w*.59), round(h*.27)))
    patch = floral.resize((box[2]-box[0], box[3]-box[1]), Image.Resampling.LANCZOS)
    mask = Image.new('L', patch.size, 0)
    ImageDraw.Draw(mask).rectangle((5,5,patch.width-5,patch.height-5), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(3))
    weather.paste(patch, box[:2], mask)
    weather.save(ROOT / 'assets/postcard-weather.jpg', quality=94)

    cat = ImageOps.contain(extract(reference, (680, 879, 1120, 1150)), (440, 300)).convert('RGBA')
    pixels = cat.load()
    eligible = Image.new('L', cat.size)
    data = eligible.load()
    for y in range(cat.height):
        for x in range(cat.width):
            r, g, b, _ = pixels[x,y]
            data[x,y] = 255 if min(r,g,b)>185 and max(r,g,b)-min(r,g,b)<22 else 0
    # Remove only light background connected to the boundary. The white eyes,
    # enclosed by the outline, are preserved together with all dark body pixels.
    queue = deque()
    for x in range(cat.width):
        for y in [0,cat.height-1]:
            if data[x,y]==255: data[x,y]=128;queue.append((x,y))
    for y in range(cat.height):
        for x in [0,cat.width-1]:
            if data[x,y]==255: data[x,y]=128;queue.append((x,y))
    while queue:
        x,y=queue.popleft()
        for a,b in [(x-1,y),(x+1,y),(x,y-1),(x,y+1)]:
            if 0<=a<cat.width and 0<=b<cat.height and data[a,b]==255:
                data[a,b]=128;queue.append((a,b))
    for y in range(cat.height):
        for x in range(cat.width):
            if data[x,y]==128:
                r,g,b,_=pixels[x,y];pixels[x,y]=(r,g,b,0)
    cat.save(ROOT / 'assets/postcard-cat.png')

    wallpaper = ImageOps.fit(art, (320,240)).filter(ImageFilter.GaussianBlur(13))
    wallpaper = Image.blend(wallpaper, Image.new('RGB', (320,240), '#d4dfbb'), .62)
    wallpaper.save(ROOT / 'assets/postcard-background.jpg', quality=91)
    print('Prepared four homepage artwork layers from', SOURCE.name)


if __name__ == '__main__':
    main()
