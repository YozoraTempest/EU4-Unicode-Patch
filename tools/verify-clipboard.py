"""Independently check native clipboard insertion, selection and SDL ownership."""
import argparse
import json
from pathlib import Path
import re

from clipboard_cases import CLIPBOARD_CASES, ROUNDTRIP_CASES


def filtered_source(case):
    if len(case['source']) > 32000:
        return ''
    try:
        value = bytes(case['source']).decode('utf-8', errors='strict')
    except UnicodeDecodeError:
        return ''
    return ''.join(c for c in value if c not in set('"§¤£@{}/\\'))


def verify(path, *, roundtrip=False):
    records = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines() if line]
    artifacts = [r for r in records if r.get('event') == 'artifact']
    if len(artifacts) != 1:
        raise ValueError('Expected one artifact fingerprint')
    artifact = artifacts[0]
    if roundtrip and artifact.get('mode') != 'roundtrip':
        raise ValueError('Expected copy/cut roundtrip evidence')
    if artifact.get('exe_sha256') != '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a':
        raise ValueError('Unexpected native executable')
    if not re.fullmatch('[0-9a-f]{64}', artifact.get('dll_sha256', '')):
        raise ValueError('Missing DLL fingerprint')
    if not re.fullmatch('[0-9a-f]{64}', artifact.get('map_sha256', '')) or not re.fullmatch(
            '0x[0-9a-f]+', artifact.get('insert_observer_rva', '')):
        raise ValueError('Missing matching map and insertion observer metadata')
    if any(r.get('type') == 'error' for r in records):
        raise ValueError('Agent exception in clipboard trace')
    events = [r['payload'] for r in records if r.get('type') == 'send']
    if any(r.get('event') == 'clipboard-failed' for r in events):
        raise ValueError('Native clipboard probe failed')
    contracts = [r for r in events if r.get('event') == 'clipboard-contract']
    if contracts != [dict(event='clipboard-contract', original_byte_limit=250, original_font_flags=0,
                          paste_rva='0x1539240', font_transform_rva='0x15a04f0')]:
        raise ValueError('Unexpected native clipboard ABI')
    results = [r for r in events if r.get('event') == 'clipboard-case']
    cases = ROUNDTRIP_CASES if roundtrip else CLIPBOARD_CASES
    if len(results) != len(cases):
        raise ValueError('Missing or duplicated clipboard cases')
    for case, result in zip(cases, results):
        for key, value in dict(name=case['name'], source_length=len(case['source']),
                               byte_limit=case['byte_limit'], font_flags=case['font_flags']).items():
            if result.get(key) != value:
                raise ValueError('Clipboard case differs from its fixture')
        before, after = result.get('before', {}), result.get('after', {})
        before_text = list(case['before'].encode('utf-8'))
        if before.get('text') != before_text or before.get('row') != 0:
            raise ValueError('Incorrect original native clipboard state')
        selection = case.get('selection')
        if selection:
            expected = dict(anchor=selection['anchor'], caret=selection['caret'], active=True,
                            selected=list(selection['selected'].encode('utf-8')))
        else:
            expected = dict(caret=case['caret'], active=False, selected=[])
        if any(before.get(key) != value for key, value in expected.items()):
            raise ValueError('Incorrect original native clipboard selection')
        if roundtrip:
            action = result.get('action_result', {})
            if result.get('action') != case['action'] or action.get('handled') is not True:
                raise ValueError('Native copy/cut action was not handled')
            if action.get('writes') != [case['source']] or action.get('reads') != 0 or action.get('releases') != 0:
                raise ValueError('Copy/cut changed UTF-8 or clipboard ownership')
            if action.get('inserts') != [] or action.get('notifications') != []:
                raise ValueError('Copy/cut unexpectedly inserted or notified during direct dispatch')
            action_after = action.get('after', {})
            if result.get('paste_before') != action_after:
                raise ValueError('Paste did not consume the actual post-copy/cut state')
            if case['action'] == 'copy':
                if action_after != before:
                    raise ValueError('Copy changed native text or selection')
            else:
                required = dict(text=list(b'AZ'), caret=1, row=0, anchor=1, selected=[], active=False)
                if action_after != required:
                    raise ValueError('Cut did not remove exactly the selected graphemes')
        accepted = filtered_source(case)
        inserted = list(accepted.encode('utf-8'))
        final = list(case['expected'].encode('utf-8'))
        if roundtrip and case['action'] == 'cut':
            caret = 1 + len(inserted)
        elif selection and accepted:
            caret = min(selection['anchor'], selection['caret']) + len(inserted)
        elif accepted:
            caret = min(case['caret'] + len(inserted), len(final))
        else:
            caret = before['caret']
        if after.get('text') != final or after.get('caret') != caret or after.get('row') != 0:
            raise ValueError('Incorrect native clipboard text or caret: ' + case['name'])
        if result.get('handled') is not True or result.get('reads') != 1 or result.get('releases') != 1:
            raise ValueError('Native Ctrl-V or SDL clipboard ownership failed')
        if result.get('inserts') != ([inserted] if accepted else []):
            raise ValueError('Clipboard insertion was incomplete or duplicated: ' + case['name'])
        if result.get('notifications') != ([final] if accepted else []):
            raise ValueError('Clipboard notification was incomplete or duplicated: ' + case['name'])
        if accepted:
            if after.get('active') is not False or after.get('selected') != []:
                raise ValueError('Accepted clipboard input retained its old selection')
        elif after != before:
            raise ValueError('Rejected clipboard input changed text or selection')
    count = len(cases)
    if [r for r in events if r.get('event') == 'clipboard-complete'] != [
            dict(event='clipboard-complete', cases=count, allocations=count, releases=count)]:
        raise ValueError('Missing complete ownership summary')
    return dict(artifact=artifact, passed=True, cases=results,
                scope=('Native copy/cut-to-paste roundtrip on live single-line editor with a private SDL text provider; copy/cut state, source bytes and complete paste notification verified, direct copy/cut does not notify; not physical shortcuts, OS clipboard conversion, IME or other controls'
                       if roundtrip else 'Native Ctrl-V dispatcher and live single-line editor, controlled SDL UTF-8 source and real allocation/free; not physical Ctrl-V, OS clipboard conversion, copy/cut, IME or other controls'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--roundtrip', action='store_true')
    args = parser.parse_args()
    report = verify(args.trace, roundtrip=args.roundtrip)
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f"PASS: {len(report['cases'])} native clipboard {'copy/cut roundtrip' if args.roundtrip else 'paste'} cases, complete insertion, selection and balanced SDL buffers")


if __name__ == '__main__':
    main()
