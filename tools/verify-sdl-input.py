"""Check complete SDL queue payloads, single insertion and valid native notifications."""
import argparse
import json
from pathlib import Path
import re

from sdl_cases import SDL_CASES


def expected_commit(payload):
    if not payload or len(payload) >= 32:
        return None
    try:
        return bytes(payload).decode('utf-8', errors='strict')
    except UnicodeDecodeError:
        return None


def verify(path):
    records = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line]
    artifacts = [r for r in records if r.get('event') == 'artifact']
    if len(artifacts) != 1:
        raise ValueError('Expected one artifact fingerprint')
    artifact = artifacts[0]
    for key in ('exe_sha256', 'dll_sha256'):
        if not re.fullmatch('[0-9a-f]{64}', artifact.get(key, '')):
            raise ValueError('Missing artifact fingerprint')
    if artifact['exe_sha256'] != '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a':
        raise ValueError('Unexpected native executable')
    if any(r.get('type') == 'error' for r in records):
        raise ValueError('Agent exception in SDL trace')
    payloads = [r['payload'] for r in records if r.get('type') == 'send']
    if any(p.get('event') == 'sdl-validation-failed' for p in payloads):
        raise ValueError('SDL validation reported a failure')
    results = [p for p in payloads if p.get('event') == 'native-sdl-result']
    delivered = [p for p in payloads if p.get('event') == 'synthetic-sdl-textinput']
    if len(results) != len(SDL_CASES) or len(delivered) != len(SDL_CASES):
        raise ValueError('Missing or duplicated SDL cases')
    prohibited = set('"§¤£@{}/\\')
    for fixture, result, delivery in zip(SDL_CASES, results, delivered):
        if any(result.get(key) != value for key, value in fixture.items()):
            raise ValueError('SDL case does not match its fixture')
        if delivery.get('name') != fixture['name'] or delivery.get('payload') != fixture['payload'] or delivery.get('window_id', 0) <= 0:
            raise ValueError('SDL delivery was not observed')
        after = list(fixture['expected'].encode('utf-8'))
        if result.get('after') != after or result.get('after_caret') != fixture.get('expected_caret', len(after)):
            raise ValueError('Incorrect native SDL text or caret: ' + fixture['name'])
        commit = expected_commit(fixture['payload'])
        filtered = ''.join(c for c in commit if c not in prohibited) if commit is not None else ''
        expected_inserts = [list(filtered.encode('utf-8'))] if filtered else []
        if result.get('inserts') != expected_inserts:
            raise ValueError('Commit was not one complete native insertion: ' + fixture['name'])
        if 'selection' in fixture:
            selection = fixture['selection']
            before_selection = dict(anchor=selection['anchor'], caret=selection['caret'],
                                    selected=list(selection['selected'].encode('utf-8')), active=True)
            if result.get('selection_before') != before_selection:
                raise ValueError('Incorrect native selection before queued commit')
            after_selection = result.get('selection_after', {})
            if after_selection.get('caret') != fixture['expected_caret']:
                raise ValueError('Queued selection replacement left an incorrect caret')
            if filtered:
                if after_selection.get('active') is not False or after_selection.get('selected') != []:
                    raise ValueError('Accepted commit did not clear the replaced selection')
            elif after_selection != before_selection:
                raise ValueError('Blocked commit changed the existing selection')
        copies = result.get('queue_copies', [])
        if commit is None:
            if copies or result.get('notifications'):
                raise ValueError('Rejected SDL input reached the native queue or listener')
        else:
            expected_copy = fixture['payload'] + [0] * (32 - len(fixture['payload']))
            if not copies or any(c != expected_copy for c in copies):
                raise ValueError('Complete UTF-8 payload was not retained in queue copies')
            notifications = result.get('notifications')
            if notifications != ([after] if filtered else []):
                raise ValueError('Native listener received an incomplete or incorrect state')
            for value in notifications:
                bytes(value).decode('utf-8', errors='strict')
    if [p for p in payloads if p.get('event') == 'sdl-validation-complete'] != [dict(event='sdl-validation-complete', cases=len(SDL_CASES))]:
        raise ValueError('Missing complete SDL summary')
    budget = [p for p in payloads if p.get('event') == 'native-sdl-budget']
    if len(budget) != 1 or budget[0].get('original_byte_limit') != 250:
        raise ValueError('Unexpected native editor budget')
    return dict(artifact=artifact, passed=True, cases=results,
                scope='Synthetic SDL_TEXTINPUT in the actual polling loop, native queue copies, single insertion, native notifications and programmatically seeded single-line selections; not physical keyboard, IME composition or mouse selection')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = verify(args.trace)
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'PASS: {len(SDL_CASES)} actual SDL-loop cases, complete queue copies and native notifications')


if __name__ == '__main__':
    main()
