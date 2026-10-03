"""Audit pinned font cmap coverage against assigned Unicode 17 CJK characters."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request
import fontTools
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
DATA = {
    'UnicodeData': '2e1efc1dcb59c575eedf5ccae60f95229f706ee6d031835247d843c11d96470c',
    'Blocks': 'c0edefaf1a19771e830a82735472716af6bf3c3975f6c2a23ffbe2580fbbcb15',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=ROOT/'private/open-fonts')
    parser.add_argument('--output', type=Path, default=ROOT/'private/open-font-coverage.json')
    args = parser.parse_args()
    union, records = set(), []
    for item in json.loads((ROOT/'fixtures/open-fonts.json').read_text())['files']:
        path = args.directory/item['name']
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != item['sha256']:
            raise ValueError('Pinned font checksum mismatch')
        with TTFont(path) as font:
            reverse = font.getReverseGlyphMap()
            mapped = {scalar for scalar, name in font.getBestCmap().items() if reverse[name] != 0}
            union.update(mapped)
            records.append(dict(file=item['name'], sha256=digest, bytes=path.stat().st_size,
                family=font['name'].getDebugName(1), version=font['name'].getDebugName(5),
                copyright=font['name'].getDebugName(0), mapped_scalars=len(mapped)))
    sources = {}
    for name, digest in DATA.items():
        path = args.directory/(name+'-17.0.0.txt')
        url = 'https://www.unicode.org/Public/17.0.0/ucd/'+name+'.txt'
        if not path.exists():
            with urllib.request.urlopen(url) as response:
                path.write_bytes(response.read())
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Pinned Unicode data checksum mismatch')
        sources[name] = dict(url=url, sha256=digest)
    assigned, first = set(), None
    for line in (args.directory/'UnicodeData-17.0.0.txt').read_text().splitlines():
        scalar, name, *_ = line.split(';')
        value = int(scalar, 16)
        if name.endswith(', First>'):
            first = value
        elif name.endswith(', Last>'):
            if first is None:
                raise ValueError('Unpaired Unicode range')
            assigned.update(range(first, value+1))
            first = None
        else:
            assigned.add(value)
    blocks = []
    for line in (args.directory/'Blocks-17.0.0.txt').read_text().splitlines():
        line = line.split('#')[0].strip()
        if not line:
            continue
        extent, name = line.split('; ')
        if not name.startswith(('CJK Unified Ideographs', 'CJK Compatibility Ideographs')):
            continue
        start, end = [int(value, 16) for value in extent.split('..')]
        target = assigned.intersection(range(start, end+1))
        missing = sorted(target-union)
        blocks.append(dict(name=name, assigned=len(target), covered=len(target&union),
                           missing=['U+%X' % value for value in missing]))
    report = dict(unicode_version='17.0.0', tool='fontTools '+fontTools.__version__,
                  files=records, union_mapped_scalars=len(union), sources=sources, blocks=blocks,
                  all_assigned_cjk_covered=all(not block['missing'] for block in blocks),
                  scope='Font cmap coverage only; this does not prove EU4 rendering, shaping, variation selectors or atlas capacity')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=True))


if __name__ == '__main__':
    main()
