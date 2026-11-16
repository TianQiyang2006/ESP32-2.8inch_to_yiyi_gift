"""Convert a folder of images to baseline JPEGs fitted within 320x240.

Originals are never modified. Numbered output names preserve natural ordering.
Usage: python prepare_photos.py INPUT_DIR OUTPUT_DIR
"""
import argparse
import re
from pathlib import Path
from PIL import Image, ImageOps

def natural_key(path):
    return [int(s) if s.isdigit() else s.lower() for s in re.split(r'(\d+)',path.name)]

def main():
    p=argparse.ArgumentParser()
    p.add_argument('input',type=Path); p.add_argument('output',type=Path)
    args=p.parse_args()
    source=args.input.resolve(strict=True); destination=args.output.resolve()
    if not source.is_dir() or source==destination:
        raise ValueError('Input and output must be different directories')
    files=sorted([f for f in source.iterdir() if f.is_file() and f.suffix.lower() in {'.jpg','.jpeg','.png','.webp','.bmp'}],key=natural_key)
    if len(files)>256:
        raise ValueError('The first firmware supports at most 256 photos')
    destination.mkdir(parents=True,exist_ok=True)
    outputs=[destination/f'{i:03d}.jpg' for i in range(1,len(files)+1)]
    if any(f.exists() for f in outputs):
        raise FileExistsError('Numbered output files already exist; use an empty output folder')
    for source_file,out in zip(files,outputs):
        with Image.open(source_file) as original:
            image=ImageOps.exif_transpose(original).convert('RGB')
            image.thumbnail((320,240),Image.Resampling.LANCZOS)
            image.save(out,quality=90,progressive=False,optimize=True,subsampling=0)
        print(out)

if __name__=='__main__': main()
