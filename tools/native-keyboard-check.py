"""Exercise the player's speed alias at EU4's mapped campaign dispatch."""
import ctypes as C
import struct


def verify(base, fn, hook, pointer, address_hook, executable_code, callbacks):
    game = C.create_string_buffer(0x2000)
    session = C.create_string_buffer(0x400)
    permissions = C.create_string_buffer(0x100)
    C.c_void_p.from_buffer(game, 0x330).value = C.addressof(session)
    C.c_void_p.from_buffer(session, 0x370).value = C.addressof(permissions)
    actions = []
    held = set()
    permitted = False

    def callback(result, arguments, function):
        value = C.CFUNCTYPE(result, *arguments)(function)
        callbacks.append(value)
        return C.cast(value, C.c_void_p).value

    user32 = C.WinDLL('user32')
    target = C.cast(user32.GetKeyState, C.c_void_p).value
    trampoline = C.c_void_p()
    replacement = callback(C.c_short, [C.c_int], lambda key: -32768 if key in held else 0)
    assert fn('MH_CreateHook', C.c_int, C.c_void_p, C.c_void_p, C.POINTER(C.c_void_p))(
        target, replacement, C.byref(trampoline)) == 0
    assert fn('MH_EnableHook', C.c_int, C.c_void_p)(target) == 0
    try:
        address_hook(0x81f6d0, callback(None, [C.c_void_p], lambda _: actions.append('increase')))
        address_hook(0x81f840, callback(None, [C.c_void_p], lambda _: actions.append('decrease')))
        address_hook(0x76ecd0, callback(C.c_bool, [C.c_void_p], lambda _: permitted))
        # Enter with the original dispatcher stack alignment and RSI owner.
        # Both native branch exits restore this bridge's saved RSI and return.
        finish = executable_code(b'\x48\x8b\x74\x24\x20\x48\x83\xc4\x28\xc3')
        address_hook(0x816e4d, finish)
        address_hook(0x816e54, finish)
        hook(0x816dd5, 'speed_increase_hook')
        bridge = executable_code(b'\x48\x83\xec\x28\x48\x89\x74\x24\x20\x48\xbe' +
                                 struct.pack('<Q', C.addressof(game)) + b'\x0f\xb6\xc1\x49\xba' +
                                 struct.pack('<Q', base + 0x816dd5) + b'\x41\xff\xe2')
        dispatch = C.CFUNCTYPE(None, C.c_ubyte)(bridge)
        results = []

        def check(character, expected, label):
            actions.clear()
            dispatch(character)
            assert actions == expected, (label, actions, expected)
            results.append({'case': label, 'actions': list(actions)})

        check(ord('='), ['increase'], 'plain equals')
        check(ord('+'), ['increase'], 'native plus, once')
        check(ord('-'), ['decrease'], 'native minus')
        check(ord('a'), [], 'other ASCII')
        check(0xe4, [], 'UTF-8 leading byte')
        for key, label in ((0x11, 'Ctrl'), (0x12, 'Alt'), (0x5b, 'left Win'), (0x5c, 'right Win')):
            held.add(key)
            check(ord('='), [], label + ' equals')
            held.clear()
        pointer('start_native_text_input', callback(None, [], lambda: None))
        pointer('stop_native_text_input', callback(None, [], lambda: None))
        pointer('native_text_event_state', callback(C.c_ubyte, [C.c_uint, C.c_int], lambda *_: 1))
        focus = fn('focus_native_editor', None, C.c_void_p)
        blur = fn('blur_native_editor', None, C.c_void_p)
        editor = C.c_int()
        focus(C.addressof(editor))
        check(ord('='), [], 'editor owns literal equals')
        blur(C.addressof(editor))
        check(ord('='), ['increase'], 'after editor blur')
        C.c_ubyte.from_buffer(game, 0x19f1).value = 1
        for character in (ord('='), ord('+')):
            check(character, [], 'multiplayer permission denied ' + chr(character))
            permitted = True
            check(character, ['increase'], 'multiplayer permission granted ' + chr(character))
            permitted = False
        return results
    finally:
        assert fn('MH_DisableHook', C.c_int, C.c_void_p)(target) == 0
        assert fn('MH_RemoveHook', C.c_int, C.c_void_p)(target) == 0
