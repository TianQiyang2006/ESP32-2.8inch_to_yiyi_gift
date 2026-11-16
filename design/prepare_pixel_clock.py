"""Extract the uploaded LED tile and build a live-clock thumbnail, not a fixed clock face."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent
BACKGROUND = (8, 15, 24)
TILE_BACKGROUND = (21, 40, 59)
PATTERNS = ['111101101101111', '001001001001001', '111001111100111',
            '111001111001111', '101101111001001', '111100111001111',
            '111100111101111', '111001001001001', '111101111101111', '111101111001111']
POSITIONS = [0, 65, 164, 231]


def main():
    reference = ROOT / 'assets/clock-pixel-reference.jpg'
    source = Image.open(reference).convert('RGB')
    # The first colon is an isolated square, so the tile contains its own halo.
    crop = source.crop((210, 37, 252, 79)).resize((26, 26), Image.Resampling.LANCZOS)
    tile = Image.new('RGBA', crop.size)
    values = []
    for index in range(26*26):
        rgb = crop.getpixel((index % 26, index // 26))
        alpha = max(0, *[(rgb[i] - TILE_BACKGROUND[i]) / (255 - TILE_BACKGROUND[i]) for i in range(3)])
        if alpha < .025:
            values.append((0, 0, 0, 0))
        else:
            color = tuple(min(255, max(0, round((rgb[i] - (1-alpha)*TILE_BACKGROUND[i]) / alpha))) for i in range(3))
            x, y = index % 26, index // 26
            fade = min(1, min(x+.5, y+.5, 25.5-x, 25.5-y) / 4)
            values.append((*color, round(alpha*fade*255)))
    tile.putdata(values)
    tile.save(ROOT / 'assets/clock-pixel-tile.png')
    frame = Image.new('RGBA', (320, 240), (*BACKGROUND, 255))
    for digit, x in zip('2023', POSITIONS):
        for cell, enabled in enumerate(PATTERNS[int(digit)]):
            if enabled == '1':
                frame.alpha_composite(tile, (20+x+cell%3*17-6, 49+cell//3*17-6))
    for row in [1, 3]:
        frame.alpha_composite(tile, (20+131-6, 49+row*17-6))
    small = tile.resize((13, 13), Image.Resampling.LANCZOS)
    for digit, x in zip('42', [0, 34]):
        for cell, enabled in enumerate(PATTERNS[int(digit)]):
            if enabled == '1':
                frame.alpha_composite(small, (130+x+cell%3*9-3, 160+cell//3*9-3))
    frame.convert('RGB').save(ROOT / 'assets/clock-matrix-reference.jpg', quality=96)
    (ROOT / 'mockups').mkdir(exist_ok=True)
    frame.convert('RGB').save(ROOT / 'mockups/clock-matrix-320x240.png')
    print('Blue pixel clock: uploaded tile, 3x5 live digit grid, 320x240 preview ready.')


if __name__ == '__main__':
    main()
