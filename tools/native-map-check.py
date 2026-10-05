"""Check shaped map transport against EU4's mapped native font fitting routine."""
import ctypes as C
import struct


def verify(base, fn, hook, engine_string, game_directory, pointer, symbol, executable_code):
    keep = []

    def path_object(text):
        backing = C.create_unicode_buffer(str(text))
        keep.append(backing)
        return C.create_string_buffer(struct.pack('<QQQQ', C.addressof(backing), 0,
                                                 len(str(text)), len(str(text)) + 1))

    def view(text):
        backing = C.create_string_buffer(text)
        keep.append(backing)
        return C.create_string_buffer(struct.pack('<QQ', C.addressof(backing), len(text)))

    directory = path_object(game_directory)
    optional = path_object(game_directory / 'plugins/eu4_unicode_patch/fonts')
    prefix = view(b'gfx/fonts/eu4-unicode/cache/')
    fn('configure_font_atlases', None, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.c_bool)(
        C.addressof(directory), C.addressof(optional), None, C.addressof(prefix), True)
    font = C.create_string_buffer(0x4000)
    context = C.create_string_buffer(0x500)
    methods = (C.c_void_p * 64)()
    anchor = C.create_string_buffer(16)
    address = C.addressof(font)
    C.c_void_p.from_buffer(font).value = C.addressof(methods)
    C.c_void_p.from_buffer(font, 0x48).value = C.addressof(context)
    C.c_void_p.from_buffer(font, 0x120 + 0x41 * 8).value = C.addressof(anchor)
    C.c_void_p.from_buffer(context, 0x480).value = C.addressof(context)
    for offset, value in [(0x960, 88), (0x970, 25), (0x978, 2048), (0x97c, 4096)]:
        C.c_int.from_buffer(font, offset).value = value
    C.c_float.from_buffer(font, 0x968).value = 1
    name = view(b'gfx/fonts/eu4-unicode/cache/zh-hans-map')
    fn('register_font_atlas', None, C.c_void_p, C.c_void_p)(address, C.addressof(name))
    assert fn('dynamic_map_font', C.c_bool, C.c_void_p)(address)
    for rva, name in [(0x159e60d, 'map_fit_format_hook'), (0x159e75d, 'map_fit_measure_hook'),
                      (0x159e7c5, 'map_fit_kern_hook'), (0x159e6f1, 'map_fit_icon_end_hook')]:
        hook(rva, name)
    begin = fn('begin_native_map_paragraph', None)
    end = fn('end_native_paragraph', None)
    prepare = fn('prepare_native_map_paragraph', C.c_void_p, C.c_void_p, C.c_void_p)
    find = fn('find_paragraph_glyph', C.c_void_p, C.c_void_p, C.c_uint32)
    fit = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_int, C.c_int,
                     C.c_void_p, C.c_void_p, C.c_bool)(base + 0x159e510)
    margin = (C.c_int * 4)()
    results = []
    for sample in ('العربية', 'हिन्दी', 'English العربية 123', 'a\u0301 e\u0308'):
        original = sample.encode()
        source = engine_string(original)
        begin()
        try:
            draw = prepare(address, C.addressof(source))
            assert draw != C.addressof(source)
            size = C.c_size_t.from_address(draw + 16).value
            text = C.string_at(C.c_void_p.from_address(draw).value, size).decode()
            records = [find(address + 0x120, ord(token)) for token in text]
            assert all(records)
            expected = sum(C.c_int16.from_address(record + 12).value for record in records)
            output = (C.c_int * 2)()
            lines = fit(address, draw, 100000, 100000, C.addressof(margin), C.addressof(output), True)
            assert lines == 1 and output[0] == expected and output[1] == 88, (sample, lines, list(output), expected)
            assert source.value() == original
            results.append({'text': sample, 'clusters': len(records), 'native_width': output[0], 'native_height': output[1]})
        finally:
            end()
        assert not find(address + 0x120, ord(text[0]))
    frame = C.create_string_buffer(0x300)
    owner = C.create_string_buffer(0x40)
    C.c_void_p.from_buffer(owner, 0x30).value = address
    C.c_void_p.from_buffer(frame, 0x240).value = C.addressof(owner)
    output = C.create_string_buffer(128)
    xmm = C.create_string_buffer(bytes(range(96)))
    load_xmm = b'\x48\xb8' + struct.pack('<Q', C.addressof(xmm)) + b''.join(
        b'\xf3\x0f\x6f' + bytes([0x80 + index * 8]) + struct.pack('<I', index * 16) for index in range(6))
    # Seven callee-saved pushes align the stack at the production hook boundary.
    prologue = b'\x55\x53\x41\x54\x41\x55\x41\x57\x57\x56\x48\x83\xec\x40'
    epilogue = b'\x48\x83\xc4\x40\x5e\x5f\x41\x5f\x41\x5d\x41\x5c\x5b\x5d\xc3'
    capture = b'\x48\xb8' + struct.pack('<Q', C.addressof(output)) + (
        b'\x48\x89\x58\x00\x4c\x89\x60\x08\x4c\x89\x68\x10\x4c\x89\x78\x18') + b''.join(
        b'\xf3\x0f\x7f' + bytes([0x80 + index * 8]) + struct.pack('<I', 32 + index * 16) for index in range(6))
    continuation = executable_code(capture + epilogue)
    assign = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_char_p, C.c_size_t)(base + 0x95110)
    original = 'العربية'.encode()
    C.c_uint64.from_buffer(frame, 0xc8).value = 15
    assign(C.addressof(frame) + 0xb0, original, len(original))
    begin()
    try:
        pointer('g_country_shape_return', continuation)
        code = (prologue + b'\x48\xbd' + struct.pack('<Q', C.addressof(frame)) +
                b'\x48\xbb' + struct.pack('<Q', 0x12345678) +
                b'\x49\xbc' + struct.pack('<Q', address) + b'\x41\xbd\x11\x00\x00\x00' +
                b'\x41\xbf\x21\x00\x00\x00' + load_xmm +
                b'\xff\x25\x00\x00\x00\x00' + struct.pack('<Q', symbol('country_shape_hook')))
        C.CFUNCTYPE(None)(executable_code(code))()
        assert C.c_uint64.from_buffer(output).value == 0x12345678
        assert output.raw[32:] == xmm.raw[:96]
        assert not C.c_uint64.from_buffer(frame, 0xf8).value and not C.c_uint64.from_buffer(frame, 0x20).value
        size = C.c_size_t.from_buffer(frame, 0xc0).value
        changed = C.string_at(C.c_void_p.from_buffer(frame, 0xb0).value, size).decode()
        assert changed and all(find(address + 0x120, ord(token)) for token in changed)
    finally:
        end()
    # The province hook changes only its text-block pointer and the two registers
    # written by the overwritten native instructions. The scoped copy survives
    # counting, fitting and vertex creation, but never replaces the source block.
    label = C.create_string_buffer(0x30)
    source = engine_string('हिन्दी'.encode())
    C.memmove(C.addressof(label) + 0x10, C.addressof(source), C.sizeof(source))
    begin()
    try:
        pointer('g_province_shape_return', continuation)
        code = (prologue + b'\x48\xbd' + struct.pack('<Q', C.addressof(frame)) +
                b'\x48\xbb' + struct.pack('<Q', C.addressof(label)) +
                b'\x49\xbc' + struct.pack('<Q', address) + b'\x41\xbd\x11\x00\x00\x00' +
                b'\x41\xbf\x21\x00\x00\x00' + load_xmm +
                b'\xff\x25\x00\x00\x00\x00' + struct.pack('<Q', symbol('province_shape_hook')))
        C.CFUNCTYPE(None)(executable_code(code))()
        cloned = C.c_void_p.from_buffer(output).value
        assert cloned != C.addressof(label) and output.raw[32:] == xmm.raw[:96]
        assert C.c_void_p.from_buffer(output, 8).value == address
        assert C.c_uint64.from_buffer(output, 16).value == 17
        assert source.value() == 'हिन्दी'.encode()
        output_size = (C.c_int * 2)()
        assert fit(address, cloned + 0x10, 100000, 100000, C.addressof(margin), C.addressof(output_size), True) == 1
    finally:
        end()
    for active, extent, skipped in [(False, 1, True), (False, 3, False), (True, 3, True)]:
        begin()
        try:
            if active:
                sample = engine_string(original)
                assert prepare(address, C.addressof(sample)) != C.addressof(sample)
            values = (C.c_float * 2)(extent, 2)
            restore_xmm = b'\xf3\x44\x0f\x6f\x64\x24\x30'
            for target, selected in [('g_country_gap_return', 0), ('g_country_gap_skip', 1)]:
                continuation = executable_code(b'\x48\xb8' + struct.pack('<Q', C.addressof(output)) +
                    b'\xc7\x00' + struct.pack('<I', selected) + restore_xmm + epilogue)
                pointer(target, continuation)
            code = (prologue + b'\xf3\x44\x0f\x7f\x64\x24\x30' +
                    b'\x48\xb8' + struct.pack('<Q', C.addressof(values)) +
                    b'\xf3\x44\x0f\x10\x20\xf3\x0f\x10\x48\x04' +
                    b'\xff\x25\x00\x00\x00\x00' + struct.pack('<Q', symbol('country_shape_gap_hook')))
            C.CFUNCTYPE(None)(executable_code(code))()
            assert bool(C.c_uint.from_buffer(output).value) == skipped
        finally:
            end()
    fn('release_font_atlas', None, C.c_void_p)(address + 0x120)
    return results
