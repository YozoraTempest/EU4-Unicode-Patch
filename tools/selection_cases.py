"""Independent expected states for the native single-line selection experiment."""
LEFT, RIGHT, HOME, END = 0x40000050, 0x4000004F, 0x4000004A, 0x4000004D


def key(code, modifiers=4):
    return dict(kind='key', code=code, modifiers=modifiers)


def state(text, caret, anchor, selected, active=True):
    return dict(text=text, caret=caret, anchor=anchor, selected=selected, active=active)


def case(name, before, caret, actions, states):
    assert len(actions) == len(states)
    return dict(name=name, before=before, caret=caret, actions=actions, states=states)


SELECTION_CASES = [
    case('shift-left-supplementary', 'A中𠀀Z', 8, [key(LEFT)], [state('A中𠀀Z', 4, 8, '𠀀')]),
    case('shift-left-cjk', 'A中𠀀Z', 4, [key(LEFT)], [state('A中𠀀Z', 1, 4, '中')]),
    case('shift-right-cjk', 'A中𠀀Z', 1, [key(RIGHT)], [state('A中𠀀Z', 4, 1, '中')]),
    case('expand-shrink', 'A中𠀀Z', 8, [key(LEFT), key(LEFT), key(RIGHT)],
         [state('A中𠀀Z', 4, 8, '𠀀'), state('A中𠀀Z', 1, 8, '中𠀀'), state('A中𠀀Z', 4, 8, '𠀀')]),
    case('cross-anchor', 'A中𠀀Z', 4, [key(LEFT), key(RIGHT), key(RIGHT)],
         [state('A中𠀀Z', 1, 4, '中'), state('A中𠀀Z', 4, 4, ''), state('A中𠀀Z', 8, 4, '𠀀')]),
    case('shift-home', 'A中𠀀Z', 8, [key(HOME)], [state('A中𠀀Z', 0, 8, 'A中𠀀')]),
    case('shift-end', 'A中𠀀Z', 1, [key(END)], [state('A中𠀀Z', 9, 1, '中𠀀Z')]),
    case('combining-selection', 'Ae\u0301Z', 4, [key(LEFT)], [state('Ae\u0301Z', 1, 4, 'e\u0301')]),
    case('flag-selection', 'A🇨🇳Z', 9, [key(LEFT)], [state('A🇨🇳Z', 1, 9, '🇨🇳')]),
    case('family-selection', 'A👩‍👩‍👧‍👦Z', 26, [key(LEFT)], [state('A👩‍👩‍👧‍👦Z', 1, 26, '👩‍👩‍👧‍👦')]),
    case('selected-backspace', 'A中𠀀Z', 8, [key(LEFT), key(8, 0)],
         [state('A中𠀀Z', 4, 8, '𠀀'), state('A中Z', 4, 8, '', False)]),
    case('selected-delete', 'A中𠀀Z', 1, [key(RIGHT), key(127, 0)],
         [state('A中𠀀Z', 4, 1, '中'), state('A𠀀Z', 1, 1, '', False)]),
    case('replace-selection', 'A中𠀀Z', 8, [key(LEFT), dict(kind='insert', text='中文')],
         [state('A中𠀀Z', 4, 8, '𠀀'), state('A中中文Z', 10, 4, '', False)]),
    case('reverse-interior-selection', 'A𠀀e\u0301Z', 6,
         [dict(kind='select', anchor=6, caret=2), key(127, 0)],
         [state('A𠀀e\u0301Z', 1, 8, '𠀀e\u0301'), state('AZ', 1, 8, '', False)]),
    case('collapsed-interior-selection', 'A中𠀀Z', 6,
         [dict(kind='select', anchor=6, caret=6)], [state('A中𠀀Z', 4, 4, '')]),
    case('clear-selection-with-left', 'A中𠀀Z', 8, [key(LEFT), key(LEFT, 0)],
         [state('A中𠀀Z', 4, 8, '𠀀'), state('A中𠀀Z', 1, 8, '', False)]),
    case('selection-at-start', '中A', 0, [key(LEFT)], [state('中A', 0, 0, '')]),
    case('native-selection-action', 'A中𠀀Z', 8, [dict(kind='native_select_left')], [state('A中𠀀Z', 4, 8, '𠀀')]),
]
