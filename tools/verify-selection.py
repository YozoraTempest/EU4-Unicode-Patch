"""Reject partial, misaligned or incomplete native selection evidence."""
import argparse
import json
from pathlib import Path
import re

from selection_cases import SELECTION_CASES

EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'


def native_bytes(text):
    return list(text.encode('utf-8'))


def verify(path):
    records = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line]
    artifacts = [r for r in records if r.get('event') == 'artifact']
    if len(artifacts) != 1 or artifacts[0].get('exe_sha256') != EXE_HASH:
        raise ValueError('Missing exact executable fingerprint')
    if not re.fullmatch('[0-9a-f]{64}', artifacts[0].get('dll_sha256', '')):
        raise ValueError('Missing DLL fingerprint')
    if any(r.get('type') == 'error' for r in records):
        raise ValueError('Agent exception in selection trace')
    payloads = [r['payload'] for r in records if r.get('type') == 'send']
    if any(p.get('event') == 'selection-validation-failed' for p in payloads):
        raise ValueError('Selection probe failed')
    budget = [p for p in payloads if p.get('event') == 'selection-budget']
    if budget != [dict(event='selection-budget', original_byte_limit=250, test_byte_limit=80)]:
        raise ValueError('Unexpected native editor budget')
    fixtures = [p for p in payloads if p.get('event') == 'selection-fixture']
    steps = [p for p in payloads if p.get('event') == 'selection-step']
    expected_count = sum(len(c['actions']) for c in SELECTION_CASES)
    if len(fixtures) != len(SELECTION_CASES) or len(steps) != expected_count:
        raise ValueError('Missing or duplicate native selection results')
    index = 0
    for fixture, case in zip(fixtures, SELECTION_CASES):
        initial = dict(text=native_bytes(case['before']), caret=case['caret'], row=0,
                       anchor=0, anchor_row=0, selected=[], active=False)
        if fixture.get('name') != case['name'] or fixture.get('before') != initial:
            raise ValueError('Incorrect selection fixture: ' + case['name'])
        for step, (action, state) in enumerate(zip(case['actions'], case['states'])):
            actual = steps[index]
            index += 1
            expected = dict(text=native_bytes(state['text']), caret=state['caret'], row=0,
                            anchor=state['anchor'], anchor_row=0, selected=native_bytes(state['selected']), active=state['active'])
            if (actual.get('name'), actual.get('step'), actual.get('action'), actual.get('after')) != (case['name'], step, action, expected):
                raise ValueError(f'Incorrect native selection result: {case["name"]}, step {step}')
            if action['kind'] == 'key' and actual.get('handled') != 1:
                raise ValueError('Selection key was not handled')
            if action['kind'] != 'key' and actual.get('handled') is not None:
                raise ValueError('Unexpected key-handler result')
            notices = actual.get('notifications')
            if not isinstance(notices, list):
                raise ValueError('Missing notification observations')
            for notice in notices:
                try:
                    bytes(notice).decode('utf-8', errors='strict')
                except (UnicodeError, TypeError, ValueError) as error:
                    raise ValueError('Partial UTF-8 exposed in selection notification') from error
    complete = [p for p in payloads if p.get('event') == 'selection-validation-complete']
    if complete != [dict(event='selection-validation-complete', cases=len(SELECTION_CASES), steps=expected_count)]:
        raise ValueError('Missing complete selection summary')
    return dict(artifact=artifacts[0], passed=True, cases=len(SELECTION_CASES), steps=expected_count,
                results=steps, scope='Actual single-line native editor functions; not physical keyboard, mouse, IME, clipboard or multiline')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = verify(args.trace)
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'PASS: {report["cases"]} native selection cases, {report["steps"]} states')


if __name__ == '__main__':
    main()
