"""Verify the recorded user-operated Chinese IME editing sequence."""
import argparse
import json
from pathlib import Path

EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'


def verify(evidence):
    if evidence.get('exe_sha256') != EXE_HASH or len(evidence.get('dll_sha256', '')) != 64:
        raise ValueError('Missing game and DLL fingerprints')
    if evidence.get('user_confirmation') != '全部正常，最后是“中字”':
        raise ValueError('Missing user confirmation')
    records = evidence.get('records', [])
    indexes = [e['source_index'] for e in records]
    if not indexes or indexes != sorted(set(indexes)):
        raise ValueError('Missing or unordered observed events')
    states = [e['state'] for e in records if e['event'] == 'physical-editor-state']
    expected_text = ['中文测试', '中文测试阿', '中文测试', '中文测', '中文', '中文', '中中', '中中', '中字']
    expected_carets = [12, 15, 12, 9, 6, 3, 6, 3, 6]
    if len(states) != 9:
        raise ValueError('Missing complete native editing sequence')
    for i, (state, text, caret) in enumerate(zip(states, expected_text, expected_carets)):
        if bytes(state['text']).decode('utf-8') != text or state['caret'] != caret or state['row'] != 0:
            raise ValueError('Native text or caret differs from the physical operation')
        selected = '文' if i == 5 else '中' if i == 7 else ''
        if bytes(state['selected']).decode('utf-8') != selected or state['active'] != bool(selected):
            raise ValueError('Native selection was lost or retained after replacement')
        if selected and state['anchor'] != 6:
            raise ValueError('Selection anchor split a Chinese character')
    commits = [e for e in records if e['event'] == 'physical-sdl-text']
    inserts = [e for e in records if e['event'] == 'physical-native-insert']
    expected_commits = ['中文测试', '阿', '中', '字']
    if [bytes(e['utf8']).decode('utf-8') for e in commits] != expected_commits:
        raise ValueError('Unexpected physical SDL commit sequence')
    if [bytes(e['payload']).decode('utf-8') for e in inserts] != expected_commits:
        raise ValueError('Missing, split or duplicate native insertions')
    if any(c['source_index'] >= i['source_index'] for c, i in zip(commits, inserts)):
        raise ValueError('Native insertion preceded the SDL commit')
    notifications = [e for e in records if e['event'] == 'physical-native-notify']
    expected_notifications = ['中文测试', '中文测试阿', '中文测试', '中文测', '中文', '中中', '中字']
    if [bytes(e['state']['text']).decode('utf-8') for e in notifications] != expected_notifications:
        raise ValueError('Missing or duplicate native notifications')
    keys = [e for e in records if e['event'] == 'physical-sdl-key']
    if sum(e['key'] == 8 for e in keys) != 3:
        raise ValueError('Missing physical Backspace events')
    left = [e for e in keys if e['key'] == 0x40000050]
    if len(left) != 2 or any(not e['modifiers'] & 3 for e in left):
        raise ValueError('Missing physical Shift+Left selections')
    return dict(native_states=9, ime_commits=4, native_insertions=4,
                native_notifications=7, backspaces=3, shift_left_selections=2,
                final_text='中字', scope=evidence['scope'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('evidence', type=Path)
    args = parser.parse_args()
    report = verify(json.loads(args.evidence.read_text(encoding='utf-8')))
    print(json.dumps(report, ensure_ascii=True))


if __name__ == '__main__':
    main()
