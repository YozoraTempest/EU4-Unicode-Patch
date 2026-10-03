"""Native Ctrl-V fixtures with an isolated SDL clipboard source."""


def case(name, text='', expected=None, before='', caret=0, budget=80, raw=None, selection=None, flags=0):
    result = dict(name=name, source=list(text.encode('utf-8')) if raw is None else raw,
                  before=before, caret=caret, byte_limit=budget, font_flags=flags,
                  expected=text if expected is None else expected)
    if selection is not None:
        result['selection'] = selection
    return result


CLIPBOARD_CASES = [
    case('ascii', 'abc'),
    case('chinese', '中文'),
    case('supplementary', '𠀀'),
    case('combining-accent', 'Ae\u0301Z'),
    case('flag-grapheme', 'A🇨🇳Z'),
    case('zwj-family', 'A👩‍👩‍👧‍👦Z'),
    case('more-than-sdl-event', '𠀀' * 8),
    case('font-filter-continuation', '俤¤中👧', expected='俤中👧', flags=1),
    case('native-filter', '@§俤👧', expected='俤👧'),
    case('middle-byte-limit', '𠀀', expected='A𠀀', before='A中Z', caret=1, budget=6),
    case('selected-supplementary', '中文', expected='A中中文Z', before='A中𠀀Z', caret=8,
         selection=dict(anchor=4, caret=8, selected='𠀀')),
    case('selected-family', '𠀀', expected='A𠀀Z', before='A👩‍👩‍👧‍👦Z', caret=26,
         selection=dict(anchor=26, caret=1, selected='👩‍👩‍👧‍👦')),
    case('blocked-preserves-selection', '@§', expected='A中𠀀Z', before='A中𠀀Z', caret=8,
         selection=dict(anchor=4, caret=8, selected='𠀀')),
    case('empty-preserves-selection', '', expected='A中𠀀Z', before='A中𠀀Z', caret=8,
         selection=dict(anchor=4, caret=8, selected='𠀀')),
    case('invalid-preserves-selection', expected='A中𠀀Z', before='A中𠀀Z', caret=8, raw=[0xed, 0xa0, 0x80],
         selection=dict(anchor=4, caret=8, selected='𠀀')),
    case('oversized-preserves-selection', expected='A中𠀀Z', before='A中𠀀Z', caret=8, raw=[65] * 32001,
         selection=dict(anchor=4, caret=8, selected='𠀀')),
]
