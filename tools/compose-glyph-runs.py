"""Compose exported shaped-run masks at their recorded baselines for visual QA."""
import argparse
import csv
import math
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path, help='TextLayout diagnostic PNG used only for canvas dimensions')
    parser.add_argument('runs', type=Path, help='Directory exported by unicode_layout_tests')
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    with Image.open(args.reference) as reference:
        canvas = Image.new('L', reference.size, 255)
    with (args.runs / 'runs.tsv').open(encoding='utf-8', newline='') as manifest:
        records = list(csv.DictReader(manifest, delimiter='\t'))
    for record in records:
        width, height = int(record['width']), int(record['height'])
        if width == 0 or height == 0:
            continue
        with Image.open(args.runs / f"run-{int(record['index'])}.pgm") as mask:
            if mask.mode != 'L' or mask.size != (width, height):
                raise ValueError('Glyph mask differs from the exported run bounds')
            x = math.floor(float(record['baseline_x'])) + int(record['left']) + 16
            y = math.floor(float(record['baseline_y'])) + int(record['top']) + 16
            canvas.paste(0, (x, y, x+width, y+height), mask)
    canvas.save(args.output)
    print(f'Composed {len(records)} shaped runs; independent diagnostic only')


if __name__ == '__main__':
    main()
