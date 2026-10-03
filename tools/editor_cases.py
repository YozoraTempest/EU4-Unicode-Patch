"""Native edit-widget fixtures and expected byte offsets."""
KEY_CASES = [
    ('supplementary-backspace', 'A中𠀀Z', 8, 8, 'A中Z', 4),
    ('supplementary-left', 'A中𠀀Z', 8, 0x40000050, 'A中𠀀Z', 4),
    ('supplementary-right', 'A中𠀀Z', 4, 0x4000004F, 'A中𠀀Z', 8),
    ('interior-backspace', 'A中𠀀Z', 6, 8, 'A中Z', 4),
    ('interior-delete', 'A中𠀀Z', 2, 127, 'A𠀀Z', 1),
    ('combining-backspace', 'Ae\u0301Z', 4, 8, 'AZ', 1),
    ('flag-backspace', 'A🇨🇳Z', 9, 8, 'AZ', 1),
    ('zwj-backspace', 'A👩‍👩‍👧‍👦Z', 26, 8, 'AZ', 1),
    ('empty-after-delete', '中', 3, 8, '', 0),
    ('left-at-start', '中A', 0, 0x40000050, '中A', 0),
    ('right-from-start', '中A', 0, 0x4000004F, '中A', 3),
]
LIMIT_CASES = [
    ('scalar-budget', 'A中Z', 2, 'A'),
    ('combining-budget', 'Ae\u0301Z', 2, 'A'),
    ('zwj-budget', 'A👩‍👩‍👧‍👦Z', 16, 'A'),
    ('supplementary-budget', 'A𠀀Z', 5, 'A𠀀'),
]
INSERT_CASES = [
    ('prepend-budget', '中Z', 0, '𠀀', 6, '𠀀'),
    ('middle-budget', 'A中Z', 1, '𠀀', 6, 'A𠀀'),
]
FILTER_CASES = [
    ('native-blacklist-collision', 'A代俣俤俧👧§£¤@{}/\\"Z', 'A代俣俤俧👧Z'),
]
