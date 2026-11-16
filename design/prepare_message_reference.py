"""Prepare the original message-board art without baked-in sample text."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent
source = ROOT.parent / 'Ui设计图片' / 'retouch_2026100810445006.jpg'
image = Image.open(source).convert('RGB')
width, height = image.size
pixels = image.load()
mask = Image.new('L', image.size)
marked = mask.load()
regions = [(0.37, 0.095, 0.625, 0.22), (0.12, 0.47, 0.34, 0.58),
           (0.395, 0.47, 0.625, 0.57), (0.7, 0.49, 0.88, 0.59)]
for left, top, right, bottom in regions:
    box = tuple(round(value * (width if index % 2 == 0 else height))
                for index, value in enumerate((left, top, right, bottom)))
    x1, y1, x2, y2 = box
    for y in range(y1, y2):
        for x in range(x1, x2):
            if max(pixels[x, y]) < 165:
                marked[x, y] = 255
    # Only replace the ink and its antialiased edge, preserving the paper lines.
    local = mask.crop(box).filter(ImageFilter.MaxFilter(7))
    local_pixels = local.load()
    for x in range(x1, x2):
        upper = pixels[x, max(0, y1 - 12)]
        lower = pixels[x, min(height - 1, y2 + 12)]
        for y in range(y1, y2):
            if local_pixels[x - x1, y - y1]:
                ratio = (y - y1) / max(1, y2 - y1)
                pixels[x, y] = tuple(round(a * (1-ratio) + b * ratio) for a, b in zip(upper, lower))
image = image.resize((960, 720), Image.Resampling.LANCZOS)
# Extend the existing paper to the edges, removing the drawn outer frame without
# scaling the cat or moving the three notes. Preserve the hearts above the paper.
paper = image.crop((220, 60, 550, 155)).resize(image.size, Image.Resampling.BICUBIC)
outside = Image.new('L', image.size, 255)
ImageDraw.Draw(outside).rounded_rectangle((50, 50, 908, 669), radius=48, fill=0)
outside = outside.filter(ImageFilter.GaussianBlur(3))
edge = outside.load(); artwork = image.load()
for y in range(110):
    for x in range(785, 890):
        r, g, b = artwork[x, y]
        if r > g + 18 and r > b + 8:
            edge[x, y] = 0
image = Image.composite(paper, image, outside)
output = ROOT / 'assets' / 'message-board.jpg'
image.save(output, quality=95)
print('Borderless message-board background prepared; original artwork unchanged.')
