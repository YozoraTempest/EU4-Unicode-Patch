"""Check native cursor painting, focus, and real SDL rectangle calls."""
import argparse
import json
from pathlib import Path
import re

from ime_rect_cases import IME_RECT_CASES

EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'


def verify(path):
    records = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line]
    artifacts = [r for r in records if r.get('event') == 'artifact']
    if len(artifacts) != 1 or artifacts[0].get('exe_sha256') != EXE_HASH:
        raise ValueError('Unexpected executable fingerprint')
    if not re.fullmatch('[0-9a-f]{64}', artifacts[0].get('dll_sha256', '')):
        raise ValueError('Missing DLL fingerprint')
    if any(r.get('type') == 'error' for r in records):
        raise ValueError('IME rectangle agent error')
    events = [r['payload'] for r in records if r.get('type') == 'send']
    if any(r.get('event') == 'ime-rect-failed' for r in events):
        raise ValueError('Native IME rectangle probe failed')
    if [r for r in events if r.get('event') == 'sdl-version'] != [dict(event='sdl-version', version=[2, 0, 4])]:
        raise ValueError('Unexpected embedded SDL version')
    contracts = [r for r in events if r.get('event') == 'ime-rect-contract']
    if len(contracts) != 1 or {k: v for k, v in contracts[0].items() if k != 'owner'} != \
            dict(event='ime-rect-contract', outer_vtable='0x1d91358', byte_limit=250, font_flags=0) \
            or not re.fullmatch('0x[0-9a-f]+', contracts[0].get('owner', '')):
        raise ValueError('Unexpected native editor contract')
    results = [r for r in events if r.get('event') == 'ime-rect-case']
    if len(results) != len(IME_RECT_CASES):
        raise ValueError('Missing or duplicated rectangle cases')
    previous = [328, 485, 1, 16]
    expected_calls = []
    for case, result in zip(IME_RECT_CASES, results):
        if any(result.get(key) != value for key, value in case.items()):
            raise ValueError('IME rectangle fixture differs')
        source = result.get('source', {})
        required = dict(text=list(case['text'].encode('utf-8')), caret=case['caret'], row=0,
                        font_height=16, gui_scale=1, window_size=[1280, 720], focused=True)
        if any(source.get(key) != value for key, value in required.items()):
            raise ValueError('Incorrect native geometry source or focus')
        if result.get('origin') != [328, 485] or not isinstance(source.get('advance'), int) or source['advance'] < 0:
            raise ValueError('Missing native origin or font measurement')
        expected = [328 + source['advance'], 485, 1, 16]
        if source.get('sprite') != expected[:2] or result.get('rect') != expected:
            raise ValueError('SDL rectangle differs from rendered caret: ' + case['name'])
        calls = result.get('calls')
        if calls != ([dict(rect=expected, focused=True)] if expected != previous else []):
            raise ValueError('Missing, duplicated or unfocused SDL rectangle call')
        if expected != previous:
            expected_calls.append(expected)
        previous = expected
    completed_calls = [r for r in events if r.get('event') == 'sdl-ime-rect']
    initial_calls = [r for r in events[:events.index(contracts[0])] if r.get('event') == 'sdl-ime-rect']
    if len(initial_calls) != 1 or initial_calls[0].get('rect') != [328, 485, 1, 16] \
            or initial_calls[0].get('owner') != contracts[0]['owner']:
        raise ValueError('Missing initial focused-editor rectangle')
    if [r.get('rect') for r in completed_calls[len(initial_calls):]] != expected_calls + [previous]:
        raise ValueError('Completed SDL calls differ from native painting')
    if any(r.get('caller') != 'eu4_unicode_probe.dll' or not isinstance(r.get('thread'), int)
           for r in completed_calls) or len({r['thread'] for r in completed_calls}) != 1:
        raise ValueError('SDL calls have incorrect caller or thread')
    for call in completed_calls:
        source = call.get('source', {})
        sprite, size, line = source.get('sprite', []), source.get('window_size', []), source.get('font_height')
        if source.get('focused') is not True or not re.fullmatch('0x[0-9a-f]+', call.get('owner', '')) \
                or len(sprite) != 2 or len(size) != 2 or any(n <= 0 for n in size) \
                or not isinstance(line, int) or line <= 0:
            raise ValueError('SDL call lacks focused native geometry')
        x, y = [int(max(0, min(value, limit-1))) for value, limit in zip(sprite, size)]
        if call.get('rect') != [x, y, 1, min(line, size[1]-y)]:
            raise ValueError('Completed SDL call differs from its native cursor source')
    if any(r['owner'] != contracts[0]['owner'] for r in completed_calls[len(initial_calls):]):
        raise ValueError('Fixture rectangle belongs to another editor')
    unfocused = [r for r in events if r.get('event') == 'ime-rect-unfocused']
    if len(unfocused) != 1 or unfocused[0].get('calls') != [] or unfocused[0].get('source', {}).get('focused') is not False:
        raise ValueError('Unfocused editor changed SDL input rectangle')
    refocused = [r for r in events if r.get('event') == 'ime-rect-refocused']
    if len(refocused) != 1 or refocused[0].get('source') != results[-1]['source'] \
            or refocused[0].get('rect') != previous \
            or refocused[0].get('calls') != [dict(rect=previous, focused=True)] \
            or len([r for r in events if r.get('event') == 'ime-rect-awaiting-refocus']) != 1:
        raise ValueError('Refocusing did not refresh the same native caret rectangle')
    if [r for r in events if r.get('event') == 'ime-rect-complete'] != [dict(event='ime-rect-complete', cases=len(IME_RECT_CASES))]:
        raise ValueError('Missing IME rectangle completion')
    ordered = [r['event'] for r in events if r.get('event') in
               {'ime-rect-contract', 'ime-rect-case', 'ime-rect-unfocused',
                'ime-rect-awaiting-refocus', 'ime-rect-refocused', 'ime-rect-complete'}]
    if ordered != ['ime-rect-contract'] + ['ime-rect-case']*len(IME_RECT_CASES) + \
            ['ime-rect-unfocused', 'ime-rect-awaiting-refocus', 'ime-rect-refocused', 'ime-rect-complete']:
        raise ValueError('IME rectangle evidence is out of order')
    return dict(artifact=artifacts[0], passed=True, cases=results, completed_calls=completed_calls,
                initial_calls=initial_calls, unfocused=unfocused[0], refocused=refocused[0],
                scope='Real SDL 2.0.4 input rectangle follows native painted caret of this focused single-line editor at 1280x720 and GUI scale 1; not physical IME, candidate-window visibility, preedit rendering, other scales or other controls')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = verify(args.trace)
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f"PASS: {len(report['cases'])} native caret-to-SDL rectangles and unfocused suppression")


if __name__ == '__main__':
    main()
