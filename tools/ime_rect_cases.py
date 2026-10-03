"""Fixtures for native caret-to-SDL input rectangle evidence."""

IME_RECT_CASES = [
    dict(name='empty', text='', caret=0),
    dict(name='ascii-start', text='abc', caret=0),
    dict(name='ascii-end', text='abc', caret=3),
    dict(name='chinese', text='A中Z', caret=4),
    dict(name='supplementary', text='A𠀀Z', caret=5),
    dict(name='combining-accent', text='Ae\u0301Z', caret=4),
    dict(name='flag-grapheme', text='A🇨🇳Z', caret=9),
    dict(name='zwj-family', text='A👩‍👩‍👧‍👦Z', caret=26),
    dict(name='continuation-collisions', text='代俣俤俧', caret=12),
]
