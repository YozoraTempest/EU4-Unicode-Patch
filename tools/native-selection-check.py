"""Exercise selection sprites against the mapped EU4 factory and parent lists."""
import ctypes as C
import struct


def verify(base, symbols, address_hook, callbacks, crt):
    def method(prefix, result, *arguments):
        addresses = [address for name, address in symbols.items() if name.startswith(prefix)]
        assert len(addresses) == 1, prefix
        return C.CFUNCTYPE(result, *arguments)(addresses[0])

    manager = C.create_string_buffer(0x500)
    manager_address = C.addressof(manager)
    registry = (C.c_void_p * 16)()
    registry_address = C.addressof(registry)
    for offset, value in [(0xf8, registry_address), (0x100, registry_address),
                          (0x108, registry_address + C.sizeof(registry))]:
        C.c_void_p.from_buffer(manager, offset).value = value
    template_methods = (C.c_void_p * 32)()
    sprite_methods = (C.c_void_p * 64)()
    resource = C.c_void_p(C.addressof(template_methods))
    sprites = []
    parents = [C.create_string_buffer(0x120), C.create_string_buffer(0x120)]

    def callback(result, arguments, function):
        value = C.CFUNCTYPE(result, *arguments)(function)
        callbacks.append(value)
        return C.cast(value, C.c_void_p).value

    def clone(_resource, _viewport):
        sprite = C.create_string_buffer(0x300)
        C.c_void_p.from_buffer(sprite).value = C.addressof(sprite_methods)
        sprites.append(sprite)
        return C.addressof(sprite)

    template_methods[0xa8 // 8] = callback(C.c_void_p, [C.c_void_p, C.c_uint32], clone)
    address_hook(0x8e9f0, callback(C.c_void_p, [C.c_void_p, C.c_void_p],
                                 lambda _map, _name: C.addressof(resource)))
    address_hook(0x1aa090, callback(None, [C.c_void_p, C.c_void_p], lambda *_: None))
    for offset in (0x60, 0x68):
        sprite_methods[offset // 8] = callback(None, [C.c_void_p], lambda *_: None)
    sprite_methods[0x1b0 // 8] = callback(None, [C.c_void_p] * 4, lambda *_: None)
    sprite_methods[0x168 // 8] = base + 0x162f0b0
    sprite_methods[0x178 // 8] = base + 0x162f1b0
    sprite_methods[0x1d0 // 8] = base + 0x162f070
    # These two operations run the game's actual intrusive render-list code.
    sprite_methods[0xe0 // 8] = base + 0x1562390
    sprite_methods[0xc8 // 8] = base + 0x1562420
    factory = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p,
                         C.c_ubyte, C.c_void_p)(base + 0x14db940)
    name_buffer = C.create_string_buffer(b'gfx_transparency_white')
    name = C.create_string_buffer(struct.pack('<QQQQ', C.addressof(name_buffer), 0, 22, 23))
    output = C.create_string_buffer(struct.pack('<QQQQ', 0, 0, 0, 15))
    prototype = factory(manager_address, C.addressof(name), C.addressof(parents[0]), 0, C.addressof(output))
    assert prototype and C.c_void_p.from_address(prototype + 0x110).value == C.addressof(parents[0])
    destroy = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p)(base + 0x14db6a0)
    detach = C.CFUNCTYPE(None, C.c_void_p)(base + 0x1562390)
    attach = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p)(base + 0x1562420)
    owner = C.create_string_buffer(8)
    state = C.create_string_buffer(8)
    constructor = method('??0NativeEditorSelections@eu4unicode@@', C.c_void_p,
                         C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p)
    constructor(C.addressof(state), base + 0x14db940, base + 0x14db6a0, base + 0x95660)
    capture = method('?capture@NativeEditorSelections@eu4unicode@@', None,
                     C.c_void_p, C.c_void_p, C.c_void_p, C.c_ubyte)
    update = method('?update@NativeEditorSelections@eu4unicode@@', None,
                    C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p)
    setup = method('?setup@NativeEditorSelections@eu4unicode@@', None,
                   C.c_void_p, C.c_void_p, C.c_void_p)
    release = method('?release@NativeEditorSelections@eu4unicode@@', None,
                     C.c_void_p, C.c_void_p, C.c_void_p)
    capture(C.addressof(state), prototype, manager_address, 0)
    boxes = C.create_string_buffer(struct.pack('<12i', 10, 20, 15, 11, 35, 20, 13, 11, 60, 20, 9, 11))
    vector = (C.c_void_p * 3)(C.addressof(boxes), C.addressof(boxes) + 48, C.addressof(boxes) + 48)
    # Expire the original parent, as happens when a temporary frame ends.
    detach(prototype)
    C.memset(C.addressof(parents[0]), 0xdd, C.sizeof(parents[0]))
    attach(prototype, C.addressof(parents[1]))
    update(C.addressof(state), C.addressof(owner), prototype, C.addressof(parents[1]), C.addressof(vector))
    assert len(sprites) == 3
    position = method('?native_sprite_input_position@eu4unicode@@', C.c_uint64, C.c_void_p)
    screen_position = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p)(base + 0x162f1b0)
    for index, sprite in enumerate(sprites):
        assert C.c_void_p.from_buffer(sprite, 0x110).value == C.addressof(parents[1])
        expected = struct.unpack_from('<4i', boxes.raw, index * 16)
        assert struct.unpack('<ff', struct.pack('<Q', position(C.addressof(sprite)))) == expected[:2]
        assert struct.unpack_from('<2i', sprite.raw, 0x1b4) == expected[2:]
        # The screen getter uses the computed render matrix, not the position
        # that was just supplied. Deliberately retain a different transform.
        struct.pack_into('<ff', sprite, 0xdc, expected[0], -224)
        screen = C.create_string_buffer(8)
        screen_position(C.addressof(sprite), C.addressof(screen))
        assert struct.unpack('<ff', screen.raw)[1] == 224
        assert struct.unpack('<ff', struct.pack('<Q', position(C.addressof(sprite)))) == expected[:2]
    # Relink all extras while the previous list is still valid, then expire it.
    fresh_parent = C.create_string_buffer(0x120)
    setup(C.addressof(state), C.addressof(owner), C.addressof(fresh_parent))
    attach(prototype, C.addressof(fresh_parent))
    C.memset(C.addressof(parents[1]), 0xdd, C.sizeof(parents[1]))
    release(C.addressof(state), C.addressof(owner), prototype)
    destroy(manager_address, prototype)
    assert C.c_void_p.from_buffer(manager, 0x100).value == registry_address
    assert not C.c_void_p.from_buffer(fresh_parent, 0x100).value
    assert not C.c_void_p.from_buffer(fresh_parent, 0x108).value
    for sprite in sprites:
        assert not C.c_void_p.from_buffer(sprite, 0x110).value
    method('??1NativeEditorSelections@eu4unicode@@', None, C.c_void_p)(C.addressof(state))
    node = C.c_void_p.from_buffer(manager, 0x18).value
    while node:
        previous = C.c_void_p.from_address(node + 0x10).value
        crt.free(node)
        node = previous
    return {'native_factory': True, 'expired_parent_replaced': True,
            'native_parent_relink': True, 'native_manager_release': True,
            'native_position_and_size': True, 'render_transform_independent': True,
            'rectangles': len(sprites)}
