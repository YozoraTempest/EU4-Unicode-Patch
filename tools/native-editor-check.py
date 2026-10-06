"""Exercise multiline edit hooks against EU4's mapped row and caret routines."""
import ctypes as C
import json
import struct


def verify(base, fn, hook, engine_string, font_source, callbacks, crt):
    fn('configure_native_editor_text', None, C.c_void_p)(base)
    fn('configure_editor_presentation', None, C.c_void_p)(base)
    for rva, name, original in (
        (0x1537210, 'editor_width_fit', 'original_editor_width_fit'),
        (0x1539820, 'editor_word_break', 'original_editor_word_break'),
        (0x1536060, 'editor_caret_position', 'original_editor_caret'),
        (0x1535eb0, 'editor_anchor_position', 'original_editor_anchor'),
        (0x153b170, 'editor_selection', 'original_editor_selection'),
        (0x15384d0, 'editor_left', 'original_editor_left'),
        (0x15385a0, 'editor_right', 'original_editor_right'),
        (0x15366c0, 'editor_key', 'original_editor_key'),
        (0x95110, 'assign_editor_prefix', 'original_assign_text'),
        (0xb19590, 'filter_editor_text', 'original_filter_text'),
        (0x1536b80, 'insert_editor_commit', 'original_editor_insert'),
        (0x1536e51, 'editor_fit_hook', None),
    ):
        hook(rva, name, original)

    def callback(result, arguments, function):
        value = C.CFUNCTYPE(result, *arguments)(function)
        callbacks.append(value)
        return C.cast(value, C.c_void_p).value

    font = C.create_string_buffer(0x4000)
    C.memmove(font, font_source, 0x1000)
    font_address = C.addressof(font)
    context = C.create_string_buffer(0x500)
    methods = (C.c_void_p * 64)()
    C.memmove(methods, C.c_void_p.from_buffer(font).value, C.sizeof(methods))
    methods[0x60 // 8] = base + 0x159b7c0
    methods[0x68 // 8] = callback(C.c_int, [C.c_void_p], lambda _: 16)
    methods[0xa8 // 8] = callback(C.c_int, [C.c_void_p, C.c_ubyte], lambda *_: 0)
    C.c_void_p.from_buffer(font).value = C.addressof(methods)
    C.c_void_p.from_buffer(font, 0x48).value = C.addressof(context)
    C.c_void_p.from_buffer(context, 0x480).value = C.addressof(context)
    for offset, value in ((0x960, 16), (0x970, 93), (0x978, 2048), (0x97c, 4096)):
        C.c_int.from_buffer(font, offset).value = value
    C.c_float.from_buffer(font, 0x968).value = 1
    name_buffer = C.create_string_buffer(b'gfx/fonts/mod-bitmap-16')
    name = (C.c_void_p * 2)(C.addressof(name_buffer), len(name_buffer.value))
    fn('register_font_atlas', None, C.c_void_p, C.c_void_p)(font_address, C.addressof(name))
    assert fn('dynamic_font', C.c_bool, C.c_void_p)(font_address)
    editor_methods = (C.c_void_p * 96)()
    C.memmove(editor_methods, base + 0x1d915c0, C.sizeof(editor_methods))
    notifications = []
    editor_methods[0x208 // 8] = callback(None, [C.c_void_p], lambda widget: notifications.append(widget))
    assign = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_char_p, C.c_size_t)(base + 0x95110)
    destroy = C.CFUNCTYPE(None, C.c_void_p)(base + 0x95660)
    get_rows = C.CFUNCTYPE(C.c_void_p, C.c_void_p)(base + 0x15373a0)
    absolute = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint, C.c_uint)(base + 0x153aa90)
    caret = fn('native_edit_caret', None, C.c_void_p, C.c_size_t, C.c_bool)
    key = fn('editor_key', C.c_bool, C.c_void_p, C.c_void_p)
    insert = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p)(base + 0x1536b80)

    def dispatch(widget, code, modifiers=0):
        event = (C.c_uint32 * 3)(code, 0, modifiers)
        assert key(widget, C.addressof(event))

    def current(widget):
        return engine_type.from_address(widget + 0x30).value()

    def offset(widget):
        return absolute(widget, C.c_uint16.from_address(widget + 0x56).value,
                        C.c_uint16.from_address(widget + 0x54).value)

    engine_type = type(engine_string())
    owners = []
    results = []

    def create_editor(sample, width, multiline=True, byte_limit=32000):
        owner = C.create_string_buffer(0x400)
        owners.append(owner)
        widget = C.addressof(owner) + 0xc8
        C.c_void_p.from_address(widget).value = C.addressof(editor_methods)
        C.c_void_p.from_address(widget + 0x98).value = font_address
        C.c_uint16.from_address(widget + 0x68).value = width
        C.c_uint16.from_address(widget + 0x6a).value = 200
        C.c_uint16.from_address(widget + 0x6e).value = 32000
        C.c_uint32.from_address(widget + 0xdc).value = 32000
        C.c_uint32.from_address(widget + 0xe0).value = byte_limit
        C.c_uint8.from_address(widget + 0xd9).value = multiline
        # Native editor insertion mode and unrestricted input validation.
        C.c_uint8.from_address(widget + 0x6c).value = 1
        C.c_uint8.from_address(widget + 0xe5).value = 1
        for field in (0x30, 0x70, 0xb8):
            C.c_uint64.from_address(widget + field + 24).value = 15
        original = sample.encode()
        assign(widget + 0x30, original, len(original))
        C.c_uint8.from_address(widget + 0x100).value = 1
        C.c_uint8.from_address(widget + 0x101).value = 1
        return widget

    def release_editor(widget):
        fn('forget_editor_history', None, C.c_void_p)(widget)
        start, finish, _capacity = (C.c_void_p * 3).from_address(get_rows(widget))
        for entry in range(start or 0, finish or 0, 40):
            destroy(entry)
        if start:
            crt.free(start)
        for field in (0x30, 0x70, 0xb8):
            destroy(widget + field)

    for sample, width in (('中文 English\nالعربية 123\nहिन्दी e\u0301𠮷', 110),
                          ('A中\r\nالعربية\r\nहिन्दी', 90),
                          ('中文 English العربية हिन्दी 中文 English', 55),
                          ('中文\n\nالعربية\n', 110), ('', 55)):
        widget = create_editor(sample, width)
        original = sample.encode()
        row_vector = get_rows(widget)
        start, finish = (C.c_void_p * 2).from_address(row_vector)
        rows = [engine_type.from_address(entry).value()
                for entry in range(start or 0, finish or 0, 40)]
        consumed = 0
        for index, row in enumerate(rows):
            inserted = C.c_uint8.from_address(start + index * 40 + 32).value
            length = len(row) - bool(inserted)
            part = original[consumed:consumed + length]
            assert part == row[:length] or (not inserted and row.endswith(b'\n') and
                   part.endswith(b' ') and part[:-1] == row[:-1]), (sample, rows, index, part)
            consumed += length
        assert consumed == len(original), (sample, rows)
        for target in (0, len(original)):
            caret(widget, target, True)
            assert offset(widget) == target, (sample, target, offset(widget), rows)
        if len(rows) > 1:
            caret(widget, 0, True)
            dispatch(widget, 0x40000051)
            assert C.c_uint16.from_address(widget + 0x56).value == 1, sample
            point = (C.c_uint16 * 2)()
            fn('editor_caret_position', C.c_void_p, C.c_void_p, C.c_void_p)(widget, C.addressof(point))
            expected_y = max(0, 16 + C.c_int.from_buffer(font, 0x3c).value)
            # DirectWrite may put a fallback script's text top above its
            # uniform line box. The caret must stay on the selected row.
            assert abs(point[1] - expected_y) < 8, (sample, list(point), expected_y)
            dispatch(widget, 0x40000052)
            assert C.c_uint16.from_address(widget + 0x56).value == 0, sample
            caret(widget, 0, True)
            dispatch(widget, 0x40000051, 4)
            selected = engine_type.from_address(widget + 0x70).value()
            assert selected and selected.decode() and C.c_uint8.from_address(widget + 0x90).value, sample
            caret(widget, len(original), True)
        if original:
            dispatch(widget, 8)
            deleted = current(widget)
            assert len(deleted) < len(original) and deleted.decode(), (sample, deleted)
            dispatch(widget, ord('z'), 1)
            assert current(widget) == original and offset(widget) == len(original), sample
            dispatch(widget, ord('y'), 1)
            assert current(widget) == deleted, sample
            dispatch(widget, ord('z'), 1)
            assert current(widget) == original, sample
            # Select all using actual native row/column conversion, then delete
            # across hard and soft breaks as a single history operation.
            C.c_uint32.from_address(widget + 0x92).value = 0
            C.c_uint8.from_address(widget + 0x90).value = 1
            fn('editor_selection', None, C.c_void_p)(widget)
            assert engine_type.from_address(widget + 0x70).value() == original, sample
            dispatch(widget, 127)
            assert current(widget) == b'' and offset(widget) == 0, sample
            dispatch(widget, ord('z'), 1)
            assert current(widget) == original, sample
        results.append({'text': sample, 'native_rows': len(rows), 'wrap_width': width,
                        'undo_redo': bool(original), 'cross_row_delete': bool(original)})
        release_editor(widget)
    commits = []
    for sample, target, payload, width, limit, expected, expected_caret in (
        ('', 0, '中文', 110, 32000, '中文', 6),
        ('Z', 0, '中文 English العربية हिन्दी ', 55, 32000,
         '中文 English العربية हिन्दी Z', len('中文 English العربية हिन्दी '.encode())),
        ('Z', 0, '中文\nالعربية\r\nहिन्दी\n', 110, 32000,
         '中文\nالعربية\r\nहिन्दी\nZ', len('中文\nالعربية\r\nहिन्दी\n'.encode())),
        ('AZ', 1, '中文', 110, 32000, 'A中文Z', 7),
        ('AZ', 1, '中文', 110, 6, 'A中', 4),
        ('Z', 0, '中文', 110, 5, '中', 3),
        ('A', 1, '中文', 110, 5, 'A中', 4),
        ('eZ', 1, '\u0301', 110, 32000, 'e\u0301Z', 3),
        ('\nZ', 0, '\r', 110, 32000, '\r\nZ', 2),
        ('Z', 0, '𠮷e\u0301👩‍👩‍👧‍👦', 55, 32000,
         '𠮷e\u0301👩‍👩‍👧‍👦Z', len('𠮷e\u0301👩‍👩‍👧‍👦'.encode())),
        ('AZ', 1, '中文\nالعربية\n', 55, 32000,
         'A中文\nالعربية\nZ', len('A中文\nالعربية\n'.encode())),
        ('中文', 6, '\nالعربية\r\nहिन्दी\n', 110, 32000,
         '中文\nالعربية\r\nहिन्दी\n', len('中文\nالعربية\r\nहिन्दी\n'.encode())),
        ('Z', 0, '𠮷', 110, 4, '𠮷', 4),
        ('Z', 0, 'e\u0301', 110, 2, '', 0),
        ('AZ', 1, '中文', 110, 2, 'AZ', 1),
        ('Z', 0, '', 110, 32000, 'Z', 0),
    ):
        widget = create_editor(sample, width, byte_limit=limit)
        caret(widget, target, True)
        incoming = engine_string(payload.encode())
        insert(widget, C.addressof(incoming))
        destroy(C.addressof(incoming))
        value = current(widget)
        actual_caret = offset(widget)
        row_vector = get_rows(widget)
        start, finish = (C.c_void_p * 2).from_address(row_vector)
        rows = [engine_type.from_address(entry).value().decode()
                for entry in range(start or 0, finish or 0, 40)]
        positions = []
        consumed = 0
        for index, row in enumerate(rows):
            synthetic = C.c_uint8.from_address(start + index * 40 + 32).value
            encoded = row.encode()
            if synthetic:
                encoded = encoded[:-1]
            visible = encoded
            if visible.endswith(b'\n'):
                visible = visible[:-1]
                if visible.endswith(b'\r'):
                    visible = visible[:-1]
            length = len(visible)
            positions.append((consumed, length))
            consumed += len(encoded)
        if not positions or value.endswith(b'\n'):
            positions.append((len(value), 0))
        expected_row = max(index for index, (begin, _) in enumerate(positions)
                           if begin <= expected_caret)
        expected_column = min(expected_caret - positions[expected_row][0], positions[expected_row][1])
        actual_row = C.c_uint16.from_address(widget + 0x56).value
        actual_column = C.c_uint16.from_address(widget + 0x54).value
        commits.append({'before': sample, 'insert': payload, 'width': width,
                        'limit': limit, 'after': value.decode(), 'caret': actual_caret,
                        'row': actual_row, 'column': actual_column,
                        'rows': rows, 'expected_caret': expected_caret,
                        'expected_row': expected_row, 'expected_column': expected_column,
                        'passed': value == expected.encode() and actual_caret == expected_caret
                                  and (actual_row, actual_column) == (expected_row, expected_column)})
        if commits[-1]['passed'] and value != sample.encode():
            dispatch(widget, ord('z'), 1)
            assert current(widget) == sample.encode() and offset(widget) == target, commits[-1]
            dispatch(widget, ord('y'), 1)
            assert current(widget) == value and offset(widget) == expected_caret, commits[-1]
            if expected_caret:
                dispatch(widget, 8)
                assert len(current(widget)) < len(value), commits[-1]
                current(widget).decode()
        release_editor(widget)
    if not all(case['passed'] for case in commits):
        print('Native insert results: ' + json.dumps(commits, ensure_ascii=True), flush=True)
    assert all(case['passed'] for case in commits), commits
    results.append({'native_commits': commits})
    assert notifications
    fn('release_font_atlas', None, C.c_void_p)(font_address + 0x120)
    fn('release_unicode_font', None, C.c_void_p)(font_address + 0x120)
    return results
