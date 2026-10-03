"""Check native pixel results against declared graphemes and measured font advances."""
import argparse
import json
from pathlib import Path
import re
from geometry_cases import GEOMETRY_CASES

EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'


def verify(path):
    records = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line]
    artifacts = [r for r in records if r.get('event') == 'artifact']
    if len(artifacts) != 1 or artifacts[0].get('exe_sha256') != EXE_HASH:
        raise ValueError('Missing exact executable fingerprint')
    if not re.fullmatch('[0-9a-f]{64}', artifacts[0].get('dll_sha256', '')):
        raise ValueError('Missing DLL fingerprint')
    if any(r.get('type') == 'error' for r in records):
        raise ValueError('Agent exception in geometry trace')
    payloads = [r['payload'] for r in records if r.get('type') == 'send']
    if any(p.get('event') == 'geometry-failed' for p in payloads):
        raise ValueError('Geometry probe failed')
    fonts = [p for p in payloads if p.get('event') == 'geometry-font']
    if len(fonts) != 1 or fonts[0].get('measure_rva') != '0x159b7c0' or fonts[0].get('font_margin') != 14 or fonts[0].get('flags') != 0:
        raise ValueError('Unexpected source GUI font measurement contract')
    results = [p for p in payloads if p.get('event') == 'geometry-case']
    if len(results) != len(GEOMETRY_CASES):
        raise ValueError('Missing or duplicate geometry cases')
    samples = 0
    for result, case in zip(results, GEOMETRY_CASES):
        edges = case['boundaries']
        if (result.get('name'), result.get('text'), result.get('boundaries')) != (case['name'], list(case['text'].encode('utf-8')), edges):
            raise ValueError('Incorrect geometry fixture')
        widths = result.get('widths', [])
        if len(widths) != len(edges) or not widths or widths[0] != 0 or any(type(w) is not int or w < 0 for w in widths) or widths != sorted(widths):
            raise ValueError('Invalid native font advances')
        if case['text'] and widths[-1] <= 0:
            raise ValueError('Nonempty font fixture has no measured width')
        hits = result.get('hits', [])
        expected_hits = []
        for x in range(-2, widths[-1] + 4):
            index = min(range(len(edges)), key=lambda i: abs(widths[i] - x))
            expected_hits.append([x, edges[-1] if x < 0 or not case['text'] else edges[index]])
        if hits != expected_hits:
            raise ValueError('Native point hit is not the closest complete grapheme edge: ' + case['name'])
        samples += len(hits)
        expected_fits = []
        for x in range(widths[-1] + 4):
            fitted = max(edge for edge, width in zip(edges, widths) if width <= x)
            if not fitted and case['text']:
                fitted = edges[1]
            expected_fits.append([x, fitted])
        if result.get('fits') != expected_fits:
            raise ValueError('Native fitting split a grapheme or lost progress: ' + case['name'])
        words = result.get('words', [])
        if [row[0] for row in words] != list(range(widths[-1] + 4)):
            raise ValueError('Incomplete native word-break observations')
        for x, last_byte in words:
            if type(last_byte) is not int or last_byte < -1 or last_byte + 1 not in edges:
                raise ValueError('Word wrap cuts a space-combining grapheme')
    complete = [p for p in payloads if p.get('event') == 'geometry-complete']
    if complete != [dict(event='geometry-complete', cases=len(GEOMETRY_CASES), samples=samples)]:
        raise ValueError('Missing complete geometry summary')
    return dict(artifact=artifacts[0], passed=True, cases=len(GEOMETRY_CASES), samples=samples,
                font=fonts[0], results=results,
                scope='Native point, pixel-fit and word-break primitives with a live GUI font and separate value fixtures; not physical mouse, GUI row-cache, multiline or complex-script shaping')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = verify(args.trace)
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'PASS: {report["cases"]} native geometry cases, {report["samples"]} pixel samples')


if __name__ == '__main__':
    main()
