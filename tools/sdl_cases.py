"""Expected isolated SDL text-input results (byte offsets and native limits)."""


def case(name, commit, expected=None, before='', caret=0, budget=80, raw=None, selection=None, expected_caret=None):
    encoded = list(commit.encode('utf-8')) if raw is None else raw
    result = dict(name=name, payload=encoded, before=before, caret=caret,
                  byte_limit=budget, expected=commit if expected is None else expected)
    if selection is not None:
        result['selection'] = selection
    if expected_caret is not None:
        result['expected_caret'] = expected_caret
    return result


SDL_CASES = [
    case('ascii', 'abc'),
    case('chinese', '中文'),
    case('supplementary', '𠀀'),
    case('continuation-blacklist-collision', '👧'),
    case('combining-accent', 'Ae\u0301Z'),
    case('flag-grapheme', 'A🇨🇳Z'),
    case('zwj-family', 'A👩‍👩‍👧‍👦Z'),
    case('cjk-collisions', '代俣俤俧👧'),
    case('real-prohibited-prefix', '§£¤@{}/\\"中文', expected='中文'),
    case('ascii-prohibited-prefix', '@中文', expected='中文'),
    case('fully-prohibited-commit', '@{}/\\"§£¤', expected=''),
    case('31-byte-sdl-limit', '𠀀' * 7 + '中'),
    case('native-height-fitting', 'A' * 27 + '𠀀', expected='A' * 13),
    case('append-byte-limit', '𠀀', expected='A中', before='A中', caret=4, budget=6),
    case('middle-byte-limit', '𠀀', expected='A𠀀', before='A中Z', caret=1, budget=6),
    case('family-byte-limit', '👩‍👩‍👧‍👦', expected='A', before='A', caret=1, budget=16),
    case('invalid-overlong', '', expected='', raw=[0xc0, 0xaf]),
    case('invalid-truncated', '', expected='', raw=[0xf0, 0x9f]),
    case('invalid-surrogate', '', expected='', raw=[0xed, 0xa0, 0x80]),
    case('missing-terminator', '', expected='', raw=[0x41] * 32),
    case('empty-input', '', expected=''),
    case('selected-supplementary-replacement', '中文', expected='A中中文Z', before='A中𠀀Z', caret=8,
         selection=dict(anchor=8, caret=4, selected='𠀀'), expected_caret=10),
    case('selected-family-replacement', '𠀀', expected='A𠀀Z', before='A👩‍👩‍👧‍👦Z', caret=26,
         selection=dict(anchor=26, caret=1, selected='👩‍👩‍👧‍👦'), expected_caret=5),
    case('selected-forward-cjk-replacement', '👧', expected='A👧𠀀Z', before='A中𠀀Z', caret=1,
         selection=dict(anchor=1, caret=4, selected='中'), expected_caret=5),
    case('selected-combining-replacement', '中', expected='A中Z', before='Ae\u0301Z', caret=4,
         selection=dict(anchor=4, caret=1, selected='e\u0301'), expected_caret=4),
    case('blocked-commit-preserves-selection', '@§', expected='A中𠀀Z', before='A中𠀀Z', caret=8,
         selection=dict(anchor=8, caret=4, selected='𠀀'), expected_caret=4),
]
