"""Replay the stock and patched color-strip entry in an isolated mapped EU4 PE.

Use 64-bit Windows Python and a built DLL with its matching linker map. The
game entry point is not executed, and no installed game/mod files are changed.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--build-directory', type=Path, required=True)
parser.add_argument('--dll', choices=['eu4_unicode_patch', 'eu4_unicode_probe'], default='eu4_unicode_patch')
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
assert os.name == 'nt' and C.sizeof(C.c_void_p) == 8 and __debug__
root = Path(__file__).resolve().parents[1]
build = (root / args.build_directory).resolve()
subprocess.run([str(build / 'executable_check.exe'), str(args.executable)], check=True)
dll_path = build / (args.dll + '.dll')
map_text = (build / (args.dll + '.map')).read_text()
preferred = int(re.search(r'Preferred load address is\s+([0-9a-fA-F]+)', map_text)[1], 16)
k32 = C.WinDLL('kernel32', use_last_error=True)
k32.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k32.LoadLibraryExW.restype = C.c_void_p
k32.FreeLibrary.argtypes = [C.c_void_p]
k32.FreeLibrary.restype = C.c_bool
base = k32.LoadLibraryExW(str(args.executable.resolve()), None, 1)
assert base, C.get_last_error()
dll = C.WinDLL(str(dll_path))
assert dll.Eu4UnicodeProbeEnabled() == 0, 'The unsupported Python host must remain unpatched'
symbols = {}
for line in map_text.splitlines():
    match = re.match(r'\s+\w{4}:\w{8}\s+(\S+)\s+(\w{16})', line)
    if match:
        symbols[match[1]] = dll._handle + int(match[2], 16) - preferred


def symbol(fragment):
    found = [address for name, address in symbols.items()
             if name == fragment or name.startswith('?' + fragment + '@')]
    assert len(found) == 1, (fragment, len(found))
    return found[0]


def fn(name, result, *parameters):
    return C.CFUNCTYPE(result, *parameters)(symbol(name))


initialize = fn('MH_Initialize', C.c_int)
uninitialize = fn('MH_Uninitialize', C.c_int)
create = fn('MH_CreateHook', C.c_int, C.c_void_p, C.c_void_p, C.POINTER(C.c_void_p))
enable = fn('MH_EnableHook', C.c_int, C.c_void_p)
disable = fn('MH_DisableHook', C.c_int, C.c_void_p)
remove = fn('MH_RemoveHook', C.c_int, C.c_void_p)
hooks = []
callbacks = []


def hook(rva, callback):
    callbacks.append(callback)
    address = C.cast(callback, C.c_void_p).value if not isinstance(callback, int) else callback
    assert create(base + rva, address, None) == 0, hex(rva)
    hooks.append(base + rva)
    assert enable(base + rva) == 0, hex(rva)


class EngineString(C.Structure):
    _fields_ = [('storage', C.c_ubyte * 16), ('size', C.c_uint64), ('capacity', C.c_uint64)]


assert C.sizeof(EngineString) == 32
strip = C.CFUNCTYPE(C.c_void_p, C.POINTER(EngineString), C.POINTER(EngineString))(base + 0x17046a0)
destroy = C.CFUNCTYPE(None, C.POINTER(EngineString))(base + 0x95660)
crt = C.CDLL('ucrtbase')
crt.malloc.argtypes = [C.c_size_t]
crt.malloc.restype = C.c_void_p
crt.free.argtypes = [C.c_void_p]
crt.free.restype = None
allocations = set()
allocation_errors = []
allocation_count = 0


@C.CFUNCTYPE(C.c_void_p, C.c_size_t)
def allocate(size):
    global allocation_count
    address = crt.malloc(size)
    if address:
        allocations.add(address)
        allocation_count += 1
    return address


@C.CFUNCTYPE(None, C.c_void_p)
def release(address):
    if address and address not in allocations:
        allocation_errors.append(hex(address))
        return
    allocations.discard(address)
    crt.free(address)


def replay(name, raw, expected=None):
    owner = C.create_string_buffer(raw)
    source = EngineString()
    source.size = len(raw)
    source.capacity = max(16, len(raw))
    C.c_void_p.from_address(C.addressof(source)).value = C.addressof(owner)
    original_abi = bytes(source)
    output = EngineString()
    C.memset(C.addressof(output), 0xcc, C.sizeof(output))
    try:
        assert strip(C.byref(source), C.byref(output)) == C.addressof(output), name
        address = (C.addressof(output) if output.capacity < 16
                   else C.c_void_p.from_address(C.addressof(output)).value)
        value = C.string_at(address, output.size)
        assert C.string_at(address + output.size, 1) == b'\0', name
        assert bytes(source) == original_abi and C.string_at(owner, len(raw)) == raw, name
        if expected is not None:
            assert value == expected, (name, value.hex(), expected.hex())
        C.memset(owner, 0x5a, len(raw))
        assert C.string_at(address, output.size) == value, name
        return {'case': name, 'input_hex': raw.hex(), 'output_hex': value.hex(),
                'matches_expected': expected is not None and value == expected,
                'source_preserved': True, 'independent_output': True}
    finally:
        destroy(C.byref(output))
        assert not allocations and not allocation_errors, (name, allocations, allocation_errors)


word = '瓦兰吉卫队'.encode()
cases = [('utf8_color', '§J瓦兰吉卫队§!'.encode(), word),
         ('single_byte_color', b'\xa7J' + word + b'\xa7!', word),
         ('cjk_continuation_collision', '俧X'.encode(), '俧X'.encode()),
         ('question_and_circumflex', '¿Cómo estás? Âdel Shâh'.encode(), '¿Cómo estás? Âdel Shâh'.encode()),
         ('name_marker_preserved', ' ¿李'.encode(), ' ¿李'.encode()),
         ('mixed_color', '§Y红'.encode() + b'\xa7R' + '蓝§!'.encode() + b'\xa7!', '红蓝'.encode()),
         ('format_tokens', '§Y£adm£ @FRA ¤1 {12 $NAME$ [Root.GetName]§!'.encode(),
          '£adm£ @FRA ¤1 {12 $NAME$ [Root.GetName]'.encode()),
         ('single_byte_icon', b'\xa7Y\xa3' + b'adm\xa3\xa7!', b'\xa3adm\xa3'),
         ('literal_section', '§中文§'.encode(), '§中文§'.encode()),
         ('truncated_utf8', b'\xe4\xbf', b'\xe4\xbf'),
         ('single_byte_section', b'\xa7', b'\xa7'),
         ('embedded_nul', '§Y'.encode() + b'A\0B' + '§!'.encode(), b'A\0B')]
for length in [0, 1, 15, 16, 255, 4096]:
    raw = b'x' * length
    cases.append((f'length_{length}', raw, raw))
    cases.append((f'colored_length_{length}', '§Y'.encode() + raw + '§!'.encode(), raw))
call_sites = [0x3a8b39, 0x411d0c, 0x844ce0, 0x852f9c, 0x853482, 0xe7c8eb, 0xe7d3d0, 0x151c624]
assert initialize() == 0
try:
    for rva in call_sites:
        instruction = C.string_at(base + rva, 5)
        assert instruction[0] == 0xe8 and rva + 5 + struct.unpack('<i', instruction[1:])[0] == 0x17046a0
    for rva in [0x1a332d4, 0x1a33310]:
        hook(rva, allocate)
    hook(0x17394f0, release)
    before = [replay(name, raw) for name, raw, _ in cases[:5]]
    C.c_void_p.from_address(symbol('native_color_assign')).value = base + 0x95110
    hook(0x17046a0, symbol('construct_colorless_text'))
    after = [replay(name, raw, expected) for name, raw, expected in cases]
    for _ in range(20):
        for name, raw, expected in cases:
            replay(name, raw, expected)
    report = {'passed': True, 'game_sha256': hashlib.sha256(args.executable.read_bytes()).hexdigest(),
              'dll_sha256': hashlib.sha256(dll_path.read_bytes()).hexdigest(), 'dll': args.dll,
              'mode': 'isolated mapped PE, game entrypoint not executed', 'routine_rva': '0x17046a0',
              'call_sites': [hex(rva) for rva in call_sites], 'installed_files_changed': False,
              'native_allocations_released': allocation_count, 'outstanding_allocations': len(allocations),
              'stock': before, 'patched': after, 'replay_count': len(before) + len(cases) * 21}
finally:
    for address in reversed(hooks):
        assert disable(address) == 0
        assert remove(address) == 0
    assert uninitialize() == 0
    assert k32.FreeLibrary(base)
args.report.parent.mkdir(parents=True, exist_ok=True)
args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(f"PASS: {report['replay_count']} native calls, {len(call_sites)} callers share the repaired entry, "
      f"{allocation_count} allocations released ({args.dll}).")
