"""Fit the supplied artwork to 320x240 without stretching or clipping the cats."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent
source = ROOT / 'assets' / 'message-board-yellow-source.png'
image = Image.open(source).convert('RGB')
# The supplied screenshot has a black outer edge. Only remove that frame.
pixels = image.load()
mid_x, mid_y = image.width // 2, image.height // 2
left, top, right, bottom = 0, 0, image.width, image.height
while max(pixels[left, mid_y]) < 50:
    left += 1
while max(pixels[right - 1, mid_y]) < 50:
    right -= 1
while max(pixels[mid_x, top]) < 50:
    top += 1
while max(pixels[mid_x, bottom - 1]) < 50:
    bottom -= 1
# Discard the one antialiased pixel between the black frame and the paper, too.
image = image.crop((left + 1, top + 1, right - 1, bottom - 1))
background = image.getpixel((20, 20))
image.thumbnail((960, 720), Image.Resampling.LANCZOS)
canvas = Image.new('RGB', (960, 720), background)
# Extend the plain paper above, keeping the original proportions and bottom margin.
canvas.paste(image, ((960 - image.width) // 2, 720 - image.height))
canvas.save(ROOT / 'assets' / 'message-board-yellow.png', optimize=True)
print('Yellow message background ready: 960 x 720, original illustration preserved.')
