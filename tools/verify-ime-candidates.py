"""Validate controlled native IME policy cases and Windows candidate forms."""
import argparse
import json
from pathlib import Path

EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'


def verify(records):
    if not records or records[0].get('event') != 'artifact':
        raise ValueError('Missing artifact record')
    artifact = records[0]
    if artifact.get('exe_sha256') != EXE_HASH or not artifact.get('contract'):
        raise ValueError('Unexpected executable or missing controlled contract mode')
    digest = artifact.get('dll_sha256', '')
    if len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
        raise ValueError('Missing DLL fingerprint')
    events = []
    for record in records[1:]:
        if record.get('type') != 'send':
            raise ValueError('Observation contains an error or unknown record')
        event = record.get('payload', {})
        if event.get('event') == 'ime-observer-failed':
            raise ValueError('Observer reported a failure')
        events.append(event)
    cases = [e for e in events if e.get('event') == 'ime-policy-case']
    expected = [('active-context', v, None) for v in (0, 1, 15, 0xc000000f)]
    expected += [('disabled-context', 0xc000000f, v) for v in (0x48, 0x4c, 0x50)]
    if len(cases) != 7:
        raise ValueError('Missing or duplicate native context cases')
    for case, (name, incoming, offset) in zip(cases, expected):
        if (case.get('name'), case.get('incoming'), case.get('offset')) != (name, incoming, offset):
            raise ValueError('Unexpected native context case')
        if case.get('outgoing') != incoming or case.get('trapped') != 0:
            raise ValueError('Native context UI flags were lost or trapped')
    complete = [e for e in events if e.get('event') == 'ime-policy-complete']
    if complete != [dict(event='ime-policy-complete', cases=7)] or events.index(complete[0]) < events.index(cases[-1]):
        raise ValueError('Incomplete native message validation')
    requests = []
    states = []
    for event in events:
        if event.get('event') == 'ime-window-message' and event.get('message') == 0x281 and event.get('enabled'):
            if event.get('incoming') != event.get('outgoing') or event.get('trapped') != 0:
                raise ValueError('Observed active context lost native UI flags')
        if event.get('event') == 'ime-candidate-request':
            if event.get('accepted') is not True:
                raise ValueError('Windows rejected candidate placement')
            requests.append(event)
        if event.get('event') == 'ime-candidate-state':
            if len(requests) != len(states)+1:
                raise ValueError('Missing or duplicate candidate API request')
            rect = event.get('rect', [])
            if len(rect) != 4 or any(type(v) is not int for v in rect):
                raise ValueError('Invalid candidate source rectangle')
            x, y, w, h = rect
            if not (0 <= x < 1280 and 0 <= y < 720 and w == 1 and h == 16 and y+h <= 720):
                raise ValueError('Unexpected search caret geometry')
            form = dict(index=0, style=128, position=[x, y], area=[x, y, x+w, y+h])
            if event.get('available') is not True or event.get('form') != form or requests[-1].get('form') != form:
                raise ValueError('Stored Windows candidate form differs from the painted caret')
            states.append(event)
    if not states or len(requests) != len(states):
        raise ValueError('Missing complete Windows candidate placement')
    return dict(exe_sha256=EXE_HASH, dll_sha256=digest, native_context_cases=7,
                windows_candidate_rectangles=len(states),
                physical_candidate_visibility='pending user verification')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    args = parser.parse_args()
    records = [json.loads(line) for line in args.evidence.read_text(encoding='utf-8').splitlines() if line.strip()]
    report = verify(records)
    target = args.evidence.with_suffix('.json')
    target.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
