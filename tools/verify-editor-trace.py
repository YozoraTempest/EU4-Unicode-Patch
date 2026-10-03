"""Independently check all expected native editor results, rejecting incomplete traces."""
import argparse
import json
from pathlib import Path
import re

from editor_cases import KEY_CASES, LIMIT_CASES, INSERT_CASES, FILTER_CASES


def verify(path):
    records = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line]
    artifacts = [r for r in records if r.get('event') == 'artifact']
    if len(artifacts) != 1:
        raise ValueError('Expected one artifact fingerprint')
    artifact = artifacts[0]
    for key in ('exe_sha256', 'dll_sha256'):
        if not re.fullmatch('[0-9a-f]{64}', artifact.get(key, '')):
            raise ValueError('Missing artifact fingerprint')
    payloads = []
    for record in records:
        if record.get('type') == 'error':
            raise ValueError('Agent exception in native editor trace')
        if record.get('type') == 'send':
            payloads.append(record['payload'])
    if any(p.get('event') == 'editor-validation-failed' for p in payloads):
        raise ValueError('Native editor validation reported a failure')
    groups = {
        'native-editor-key': (KEY_CASES, ['name', 'before', 'caret', 'key', 'after', 'after_caret']),
        'native-editor-limit': (LIMIT_CASES, ['name', 'before', 'byte_limit', 'after']),
        'native-editor-insert': (INSERT_CASES, ['name', 'before', 'caret', 'insertion', 'byte_limit', 'after']),
        'native-editor-filter': (FILTER_CASES, ['name', 'before', 'after']),
    }
    checked = {}
    for event, (fixtures, fields) in groups.items():
        actual = [p for p in payloads if p.get('event') == event]
        if len(actual) != len(fixtures):
            raise ValueError('Incomplete or duplicated native editor group: ' + event)
        for record, expected in zip(actual, fixtures):
            if tuple(record.get(field) for field in fields) != expected or record.get('passed') is not True:
                raise ValueError('Incorrect native editor result: ' + record.get('name', event))
        checked[event] = actual
    complete = [p for p in payloads if p.get('event') == 'editor-validation-complete']
    if complete != [{'event': 'editor-validation-complete', 'cases': len(KEY_CASES),
                     'budget_cases': len(LIMIT_CASES), 'insertion_cases': len(INSERT_CASES),
                     'filter_cases': len(FILTER_CASES), 'passed': True}]:
        raise ValueError('Missing complete native editor summary')
    return {'artifact': artifact, 'passed': True, 'groups': checked,
            'scope': 'Programmatic native SetText, insertion and key handler on an actual single-line unselected widget; not physical keyboard, IME, selection, clipboard or multiplayer'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = verify(args.trace)
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('PASS: 11 native keys, 4 bounded assignments, 2 bounded insertions and scalar filtering')


if __name__ == '__main__':
    main()
