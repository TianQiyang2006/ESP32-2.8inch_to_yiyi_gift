"""Prepare text-free clock landscapes from the six supplied reference JPGs.

Cleaned, lossless sources are kept in assets/clock-clean so normal builds need
no ML runtime. Use --clean --model PATH to repeat the masked restoration with
a 512x512 LaMa ONNX model. Only masked pixels are replaced.
"""
from pathlib import Path
import argparse
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent / 'Ui设计图片/时钟功能预设/1'
CLEAN = ROOT / 'assets/clock-clean'
PERIODS = [
    ('morning', 'IMG_20261007_234908.jpg', (23, 14, 863, 414)),
    ('day', 'IMG_20261008_081659.jpg', (4, 1, 842, 403)),
    ('predawn', 'IMG_20261008_081716.jpg', (0, 5, 832, 406)),
    ('night', 'IMG_20261008_081731.jpg', (2, 3, 842, 402)),
    ('dusk', 'IMG_20261008_081746.jpg', (0, 3, 837, 404)),
    ('sunset', 'IMG_20261008_081812.jpg', (0, 9, 837, 404)),
]


def clean_sources(model):
    sys.path.insert(0, str(ROOT.parent / 'tools/clock-assets'))
    import numpy as np
    import onnxruntime as ort

    options = ort.SessionOptions()
    options.intra_op_num_threads = 4
    options.add_session_config_entry('session.disable_quant_qdq', '1')
    session = ort.InferenceSession(str(model), options,
                                   providers=['CPUExecutionProvider'])
    CLEAN.mkdir(parents=True, exist_ok=True)
    review = ROOT.parent / 'build/clock-clarity-review'
    review.mkdir(parents=True, exist_ok=True)
    for period, filename, bounds in PERIODS:
        original = Image.open(SOURCE / filename).convert('RGB').crop(bounds)
        height = round(original.height * 512 / original.width)
        small = np.array(original.resize((512, height), Image.Resampling.LANCZOS))
        region = np.zeros((height, 512), np.uint8)
        # Restrict detection to lettering to preserve clouds, stars and disks.
        if period == 'morning':
            region[28:94, 348:495] = 1
            region[97:138, 325:495] = 1
        else:
            region[35 if period == 'sunset' else 26:94, 326 if period == 'day' else 300:495] = 1
            region[96:139, 314:495] = 1
        # Remove entire text bounds, including colored shadows and counters.
        # A white-only glyph mask leaves shadow colors for the model to copy.
        mask = region * 255
        padded = np.pad(small, ((0, 512-height), (0, 0), (0, 0)), mode='reflect')
        # Reflected lettering must be masked too; otherwise the model can copy
        # those duplicate digits straight back into the restored sky.
        padded_mask = np.pad(mask, ((0, 512-height), (0, 0)), mode='reflect')
        output = session.run(None, {
            'image': padded.transpose(2, 0, 1)[None].astype(np.float32) / 255,
            'mask': (padded_mask[None, None] != 0).astype(np.float32),
        })[0]
        restored = Image.fromarray(np.clip(output[0].transpose(1, 2, 0)[:height],
                                           0, 255).astype(np.uint8))
        restored = restored.resize(original.size, Image.Resampling.LANCZOS)
        # Keep all source pixels outside the repair mask unchanged.
        full_mask = Image.fromarray(mask).resize(original.size, Image.Resampling.NEAREST)
        original.paste(restored, (0, 0), full_mask)
        original.save(CLEAN / f'{period}.png')
        Image.fromarray(mask).save(review / f'mask-{period}.png')
        print(f'Cleaned sample lettering: {period}', flush=True)


def frame(scene):
    width = round(scene.height * 320 / 240)
    # One uniform 4:3 cover crop retains the central/right celestial accents.
    # Avoid the previous left-only crop, distortion and JPEG recompression.
    left = round((scene.width - width) * .58)
    return scene.crop((left, 0, left + width, scene.height)).resize(
        (320, 240), Image.Resampling.LANCZOS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clean', action='store_true')
    parser.add_argument('--model', type=Path)
    args = parser.parse_args()
    if args.clean:
        if not args.model:
            parser.error('--clean requires --model PATH')
        clean_sources(args.model)
    for period, _, _ in PERIODS:
        with Image.open(CLEAN / f'{period}.png') as source:
            image = frame(source.convert('RGB'))
        image.save(ROOT / 'assets' / f'clock-{period}.png')
        # Existing exported design bundles also refer to the JPG names.
        image.save(ROOT / 'assets' / f'clock-{period}.jpg', quality=98, subsampling=0)
    print('Six lossless 320x240 clock backgrounds prepared; supplied originals unchanged.')


if __name__ == '__main__':
    main()
