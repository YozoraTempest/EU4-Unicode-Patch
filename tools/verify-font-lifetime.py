"""Independently validate native atlas lifetimes, metrics and bounded registry usage."""
import argparse
import json
from pathlib import Path
import re

FONT_NAMES = ['gfx/fonts/'+name for name in ['zh-hans-14', 'zh-hans-16', 'zh-hans-18', 'zh-hans-24', 'zh-hans-map']]
SCALARS = ['20013', '128512', '131072']
CYCLES = 64
EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'


def verify(path):
    records = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line]
    artifacts = [r for r in records if r.get('event') == 'artifact']
    if len(artifacts) != 1 or artifacts[0].get('exe_sha256') != EXE_HASH:
        raise ValueError('Missing exact native executable fingerprint')
    artifact = artifacts[0]
    if not re.fullmatch('[0-9a-f]{64}', artifact.get('dll_sha256', '')):
        raise ValueError('Missing DLL fingerprint')
    fixtures = artifact.get('fixtures', [])
    if [f.get('name') for f in fixtures] != FONT_NAMES:
        raise ValueError('Incomplete font fixtures')
    for fixture in fixtures:
        if not re.fullmatch('[0-9a-f]{64}', fixture.get('fnt_sha256', '')) or fixture.get('unicode_records', 0) <= 0:
            raise ValueError('Missing fixture hash or Unicode records')
        if sorted(fixture.get('metrics', {})) != sorted(SCALARS):
            raise ValueError('Missing Chinese, emoji or supplementary metrics')
        for metrics in fixture['metrics'].values():
            if not isinstance(metrics, list) or len(metrics) != 16 or any(type(v) is not int or not 0 <= v <= 255 for v in metrics):
                raise ValueError('Invalid native glyph metrics')
    if any(r.get('type') == 'error' for r in records):
        raise ValueError('Agent exception in native font trace')
    payloads = [r['payload'] for r in records if r.get('type') == 'send']
    if any(p.get('event') == 'font-lifetime-failed' for p in payloads):
        raise ValueError('Native font lifetime test failed')
    baselines = [p for p in payloads if p.get('event') == 'font-lifetime-baseline']
    if len(baselines) != 1:
        raise ValueError('Missing native registry baseline')
    baseline = baselines[0].get('usage', {})
    if baseline.get('fonts', 0) <= 0 or baseline.get('glyphs', 0) <= 0:
        raise ValueError('Source atlas registry is empty')
    if baselines[0].get('source_metrics') != fixtures[2]['metrics']['131072']:
        raise ValueError('Source native font does not match the fixture')
    cycles = [p for p in payloads if p.get('event') == 'native-font-lifecycle']
    if len(cycles) != CYCLES:
        raise ValueError('Missing or duplicated lifetime cycles')
    storage, anchors = set(), set()
    for index, cycle in enumerate(cycles):
        fixture = fixtures[index % len(fixtures)]
        expected_loaded = dict(fonts=baseline['fonts']+1, glyphs=baseline['glyphs']+fixture['unicode_records'])
        expected_presence = {scalar: True for scalar in SCALARS}
        if cycle.get('cycle') != index or cycle.get('name') != fixture['name']:
            raise ValueError('Incorrect lifetime cycle order')
        if cycle.get('metrics') != fixture['metrics'] or cycle.get('alias_same') != expected_presence:
            raise ValueError('Atlas metrics or alias pointers are incorrect')
        if cycle.get('after_destroy_missing') != expected_presence:
            raise ValueError('Destroyed atlas is still reachable through its old alias')
        if cycle.get('loaded_usage') != expected_loaded or cycle.get('after_usage') != baseline:
            raise ValueError('Font registry leaks records or loses a live font')
        if cycle.get('source_unchanged') is not True or cycle.get('texture', -1) < 0:
            raise ValueError('Independent source font was corrupted or texture load failed')
        for field in ('font', 'anchor'):
            if not re.fullmatch(r'0x[1-9a-f][0-9a-f]*', cycle.get(field, '')):
                raise ValueError('Missing native address observation')
        storage.add(cycle['font'])
        anchors.add(cycle['anchor'])
    if len(storage) != 1 or len(anchors) == CYCLES:
        raise ValueError('Native font storage or ASCII atlas identity was not reused')
    complete = [p for p in payloads if p.get('event') == 'font-lifetime-complete']
    if complete != [dict(event='font-lifetime-complete', cycles=CYCLES, final_usage=baseline)]:
        raise ValueError('Missing complete font lifetime summary')
    return dict(artifact=artifact, passed=True, cycles=CYCLES, baseline=baseline, unique_atlas_identities=len(anchors),
                results=cycles, scope='Five font sizes, native constructed font lifetimes and reused table/atlas identities; no live in-place hot reload, runtime raster cache, multi-page textures or GPU memory claim')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = verify(args.trace)
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'PASS: {CYCLES} native font lifetimes, five sizes, metrics, aliases, identity reuse and registry baseline')


if __name__ == '__main__':
    main()
