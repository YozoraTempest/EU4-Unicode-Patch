"""Replay EU4's save selection and confirmation with the built filename patch.

Requires 64-bit Windows Python, a supported EU4 1.37.5 EXE, and a built DLL
with its matching linker map. The EXE is mapped without executing its entry
point. UI objects, directory lookup, allocation and serialization dependencies
are fixtures. Native selection, confirmation, string ownership, path building,
trimming and UTF-8 to UTF-16 conversion remain in use. Only temporary fixture
files are created; no installed game, translation or player save is modified.
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
import tempfile


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--build-directory', type=Path, default=Path('build'))
parser.add_argument('--dll', choices=['eu4_unicode_patch', 'eu4_unicode_probe'], default='eu4_unicode_patch')
parser.add_argument('--report', type=Path, required=True)
args = parser.parse_args()
if os.name != 'nt' or C.sizeof(C.c_void_p) != 8 or not __debug__:
    parser.error('64-bit Windows Python with assertions enabled is required')
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
k32.VirtualProtect.argtypes = [C.c_void_p, C.c_size_t, C.c_ulong, C.POINTER(C.c_ulong)]
k32.VirtualProtect.restype = C.c_bool
k32.GetCurrentProcess.restype = C.c_void_p
k32.FlushInstructionCache.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
k32.FlushInstructionCache.restype = C.c_bool
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
    matches = [address for name, address in symbols.items()
               if name == fragment or name.startswith('?' + fragment + '@')]
    assert len(matches) == 1, (fragment, len(matches))
    return matches[0]


def fn(name, result, *parameters):
    return C.CFUNCTYPE(result, *parameters)(symbol(name))


def native(rva, result, *parameters):
    return C.CFUNCTYPE(result, *parameters)(base + rva)


initialize = fn('MH_Initialize', C.c_int)
uninitialize = fn('MH_Uninitialize', C.c_int)
create = fn('MH_CreateHook', C.c_int, C.c_void_p, C.c_void_p, C.POINTER(C.c_void_p))
enable = fn('MH_EnableHook', C.c_int, C.c_void_p)
disable = fn('MH_DisableHook', C.c_int, C.c_void_p)
remove = fn('MH_RemoveHook', C.c_int, C.c_void_p)
hooks = []
callbacks = []
restores = []
errors = []


def hook(rva, callback, original=None):
    callbacks.append(callback)
    address = callback if isinstance(callback, int) else C.cast(callback, C.c_void_p).value
    trampoline = C.c_void_p()
    assert create(base + rva, address, C.byref(trampoline)) == 0, hex(rva)
    if original:
        C.c_void_p.from_address(symbol(original)).value = trampoline.value
    hooks.append(base + rva)
    assert enable(base + rva) == 0, hex(rva)


def write(rva, value):
    address = base + rva
    old = C.c_ulong()
    assert k32.VirtualProtect(address, len(value), 0x40, C.byref(old))
    C.memmove(address, value, len(value))
    assert k32.FlushInstructionCache(k32.GetCurrentProcess(), address, len(value))
    assert k32.VirtualProtect(address, len(value), old.value, C.byref(old))


def fixture_data(rva, value):
    restores.append((rva, C.string_at(base + rva, len(value))))
    write(rva, value)


class EngineString(C.Structure):
    _fields_ = [('storage', C.c_ubyte * 16), ('size', C.c_uint64), ('capacity', C.c_uint64)]

    def value(self):
        address = (C.addressof(self) if self.capacity < 16
                   else C.c_void_p.from_address(C.addressof(self)).value)
        assert C.string_at(address + self.size, 1) == b'\0'
        return C.string_at(address, self.size)


assign = native(0x95110, C.c_void_p, C.c_void_p, C.c_char_p, C.c_size_t)
destroy = native(0x95660, None, C.c_void_p)
select_save = native(0x1174a00, None, C.c_void_p, C.c_void_p)
confirm_save = native(0x1175110, None, C.c_void_p)
frontend_commit = native(0x13b8d40, None, C.c_void_p)
frontend_confirm = native(0x13b8e90, None, C.c_void_p)
decode_path = native(0x19fc590, None, C.c_char_p, C.c_void_p, C.c_uint64)
transliterate = native(0x1705180, None, C.c_void_p)
crt = C.CDLL('ucrtbase')
crt.malloc.argtypes = [C.c_size_t]
crt.malloc.restype = C.c_void_p
crt.free.argtypes = [C.c_void_p]
crt.free.restype = None
allocations = set()
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
        errors.append(('unknown_allocation', hex(address)))
        return
    allocations.discard(address)
    crt.free(address)


def get_string(address):
    return EngineString.from_address(address).value()


def set_string(address, raw, initialize=False):
    if initialize:
        C.memset(address, 0, C.sizeof(EngineString))
        EngineString.from_address(address).capacity = 15
    assign(address, raw, len(raw))


menu = C.create_string_buffer(0x300)
frontend = C.create_string_buffer(0x300)
row = C.create_string_buffer(0x300)
dialog = C.create_string_buffer(0x200)
widget = C.create_string_buffer(0x200)
checkbox = C.create_string_buffer(0x200)
game_state = C.create_string_buffer(0x7500)
settings = C.create_string_buffer(0x300)
editor = EngineString()
editor.capacity = 15
saved_name = C.addressof(row) + 0x240
frontend_name = C.addressof(game_state) + 0x2430
EngineString.from_address(saved_name).capacity = 15
EngineString.from_address(frontend_name).capacity = 15
for buffer, offset, value in [(menu, 0x30, C.addressof(dialog)),
                              (frontend, 0x38, C.addressof(dialog))]:
    C.c_void_p.from_address(C.addressof(buffer) + offset).value = value
C.c_int.from_address(C.addressof(frontend) + 0x298).value = 1  # Local, not cloud.
state = {}


@C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_char_p)
def find_editor(this, key):
    if key != b'game_name':
        errors.append(('editor_key', repr(key)))
    return C.addressof(widget)


@C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p)
def find_checkbox(this, key):
    if get_string(key) != b'checkbox_compressed':
        errors.append(('checkbox_key', get_string(key).hex()))
    return C.addressof(checkbox)


@C.CFUNCTYPE(None, C.c_void_p, C.c_void_p)
def set_editor(this, text):
    set_string(C.addressof(editor), get_string(text))


@C.CFUNCTYPE(C.c_void_p, C.c_void_p)
def get_editor(this):
    return C.addressof(editor)


@C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.POINTER(C.c_ubyte))
def select_row(this, selected, changed):
    C.c_void_p.from_address(this + 0x40).value = selected
    changed[0] = 0


@C.CFUNCTYPE(None, C.c_void_p)
def no_ui(this):
    pass


@C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_void_p)
def copy_empty_vector(this, begin, end):
    if begin or end:
        errors.append(('nonempty_fixture_vector', begin, end))


@C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p)
def directory_prefix(output, vector):
    prefix = b'save games/' if vector == C.addressof(frontend) + 0x280 else b'save games'
    set_string(output, prefix, initialize=True)
    return output


@C.CFUNCTYPE(C.c_int, C.c_char_p)
def exists(path):
    raw = bytes(path)
    wide = C.create_unicode_buffer(len(raw) + 1)
    decode_path(raw, wide, C.sizeof(wide))
    if wide.value != raw.decode('utf-8'):
        errors.append(('utf16_path', raw.hex(), wide.value))
    state['lookups'].append(wide.value)
    # Frontend confirmation is exercised on its no-conflict branch, which
    # commits the native name. In-game save uses an existing temporary file.
    return int(state['mode'] == 'save' and (state['directory'] / wide.value).is_file())


@C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p,
             C.c_bool, C.c_bool, C.c_bool, C.c_void_p)
def overwrite_prompt(this, parent, name, unused, ironman, compressed, vector):
    state['prompts'].append({'name': get_string(name).decode('utf-8'), 'compressed': compressed})
    return this


@C.CFUNCTYPE(None, C.c_void_p, C.c_void_p)
def attach_prompt(parent, prompt):
    release(prompt)


@C.CFUNCTYPE(C.c_bool, C.c_void_p)
def no_ironman(this):
    return False


@C.CFUNCTYPE(None, C.c_void_p, C.c_bool, C.c_bool, C.c_bool, C.c_bool, C.c_void_p)
def serialize_fixture(name, compressed, ironman, manual, unused, vector):
    state['direct_saves'].append(get_string(name).decode('utf-8'))
    # The native callee owns this by-value string argument. No serializer runs.
    destroy(name)


dialog_vtable = (C.c_void_p * 64)()
editor_vtable = (C.c_void_p * 64)()
for table, offset, callback in [(dialog_vtable, 0xb8, find_editor),
                                (dialog_vtable, 0x120, find_checkbox),
                                (editor_vtable, 0x80, set_editor),
                                (editor_vtable, 0x98, get_editor)]:
    callbacks.append(callback)
    table[offset // 8] = C.cast(callback, C.c_void_p).value
C.c_void_p.from_address(C.addressof(dialog)).value = C.addressof(dialog_vtable)
C.c_void_p.from_address(C.addressof(widget) + 0xc8).value = C.addressof(editor_vtable)

# Read the production constants, rather than silently testing a second patch.
source = (root / 'src/plugin.cpp').read_text(encoding='utf-8')
save_sites = [0x1174a99, 0x1174e95, 0x117519e, 0x1175c2c, 0x13b8de1, 0x13b8f6c, 0x13b9567]
new_sites = {0x1174a99, 0x13b8de1, 0x13b8f6c}
constants = {int(rva, 16): (bytes.fromhex(before), bytes.fromhex(after))
             for rva, before, after in re.findall(
                 r'\{(0x[0-9a-f]+),bytes\("([0-9a-f]+)"\),bytes\("([0-9a-f]+)"\)\}', source)}
for rva in save_sites:
    before, after = constants[rva]
    assert C.string_at(base + rva, len(before)) == before, hex(rva)
    assert before[0] == 0xe8 and rva + 5 + struct.unpack('<i', before[1:])[0] == 0x1705180
    assert after == b'\x90' * 5


def replay(name, compressed, directory, expected):
    state.clear()
    state.update(mode='save', directory=directory, lookups=[], prompts=[], direct_saves=[])
    set_string(saved_name, name.encode())
    original_abi = C.string_at(saved_name, 32)
    C.c_int.from_address(C.addressof(checkbox) + 0x100).value = int(compressed)
    C.c_ubyte.from_address(C.addressof(settings) + 0x28b).value = int(compressed)
    select_save(C.addressof(menu), C.addressof(row))
    displayed = editor.value().decode('utf-8')
    assert C.string_at(saved_name, 32) == original_abi and get_string(saved_name) == name.encode()
    confirm_save(C.addressof(menu))
    result = {'name': name, 'compressed': compressed, 'editor': displayed,
              'lookups': list(state['lookups']), 'prompts': list(state['prompts']),
              'direct_saves': list(state['direct_saves'])}
    if expected:
        assert displayed == name, result
        assert state['lookups'] == ['save games/' + name + '.eu4'], result
        assert state['prompts'] == [{'name': name, 'compressed': compressed}], result
        assert not state['direct_saves'], result
    assert not errors, errors
    return result


def replay_frontend(name, confirm, directory, expected):
    state.clear()
    state.update(mode='frontend', directory=directory, lookups=[], prompts=[], direct_saves=[])
    set_string(C.addressof(editor), name.encode())
    (frontend_confirm if confirm else frontend_commit)(C.addressof(frontend))
    stored = get_string(frontend_name).decode('utf-8')
    if expected:
        assert stored == name, (name, stored)
        if confirm:
            assert state['lookups'] == ['save games/' + name + '.eu4'], state
    assert not errors, errors
    return {'entry': 'confirm' if confirm else 'commit', 'input': name,
            'stored': stored, 'lookups': list(state['lookups'])}


assert initialize() == 0
try:
    for rva in [0x1a332d4, 0x1a33310]:
        hook(rva, allocate)
    hook(0x17394f0, release)
    for rva, callback in [(0x1174300, select_row), (0x1534bb0, no_ui),
                          (0x5c9c00, directory_prefix), (0x19f6ea0, exists),
                          (0x11bd6e0, overwrite_prompt), (0x6ecae0, attach_prompt),
                          (0x6eaa10, no_ironman), (0x5cbee0, serialize_fixture),
                          (0x824b00, no_ui), (0x1eebc0, copy_empty_vector),
                          (0x13b79e0, no_ui), (0x111fe10, no_ui)]:
        hook(rva, callback)
    C.c_void_p.from_address(symbol('image')).value = base
    hook(0x1705180, symbol('transliterate_save_path'), 'original_transliterate')
    fixture_data(0x233fe78, struct.pack('<Q', C.addressof(game_state)))
    fixture_data(0x242bde8, struct.pack('<Q', C.addressof(settings)))
    extension = EngineString()
    extension.capacity = 15
    C.memmove(C.addressof(extension), b'.eu4', 4)
    extension.size = 4
    fixture_data(0x1fd2f68, bytes(extension))
    for rva in save_sites:
        before, after = constants[rva]
        restores.append((rva, before))
        if rva not in new_sites:
            write(rva, after)
    with tempfile.TemporaryDirectory(prefix='eu4-save-name-') as temporary:
        directory = Path(temporary)
        (directory / 'save games').mkdir()
        names = ['联合王冠1575_03_02', '明1585_02_03', '中', '𠮷与中文_1575',
                 '¿España Âdel Shâh', 'Chinese English 1575', '测试' * 60 + '_1575_03_02']
        original_files = {}
        for name in names:
            path = directory / 'save games' / (name + '.eu4')
            payload = ('fixture:' + name).encode()
            path.write_bytes(payload)
            original_files[path.name] = hashlib.sha256(payload).hexdigest()
        before = replay(names[0], True, directory, expected=False)
        # The stock byte transliterator emits '?' for UTF-8 continuation
        # bytes. This fixture records the raw setter input; it does not
        # emulate the real edit widget's character filtering.
        assert before['editor'] == 'e??a??c??a??1575_03_02', before
        assert not before['prompts'] and before['direct_saves'] == [before['editor']], before
        before_frontend = [replay_frontend(names[0], confirm, directory, expected=False)
                           for confirm in [False, True]]
        assert all(item['stored'] == before['editor'] for item in before_frontend), before_frontend
        for rva in new_sites:
            write(rva, constants[rva][1])
        after = [replay(name, compressed, directory, expected=True)
                 for name in names for compressed in [False, True]]
        frontends = [replay_frontend(name, confirm, directory, expected=True)
                     for name in names for confirm in [False, True]]
        # Repeated selection must not retain a previous filename or leak strings.
        for _ in range(10):
            for name in names:
                replay(name, True, directory, expected=True)
        actual_files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in (directory / 'save games').iterdir()}
        assert actual_files == original_files
    # The shared transliterator still performs its ordinary Latin-1 behavior
    # for callers outside the seven filename sites and scoped path hook.
    set_string(C.addressof(editor), b'\xc9cole')
    transliterate(C.byref(editor))
    assert editor.value() == b'Ecole'
finally:
    for address in [C.addressof(editor), saved_name, frontend_name]:
        destroy(address)
    for rva, original in reversed(restores):
        write(rva, original)
    for address in reversed(hooks):
        assert disable(address) == 0
        assert remove(address) == 0
    assert uninitialize() == 0
    assert not allocations and not errors, (allocations, errors)
    assert k32.FreeLibrary(base)
report = {'passed': True, 'game_sha256': hashlib.sha256(args.executable.read_bytes()).hexdigest(),
          'dll_sha256': hashlib.sha256(dll_path.read_bytes()).hexdigest(), 'dll': args.dll,
          'source_commit': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
          'source_tree_dirty': bool(subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain'])),
          'production_source_sha256': hashlib.sha256(source.encode()).hexdigest(),
          'replay_script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
          'mode': 'production source constants and compiled path hook in isolated mapped PE; '
                  'game entrypoint and serializer not executed',
          'native_entries': [hex(rva) for rva in [0x1174a00, 0x1175110, 0x13b8d40, 0x13b8e90,
                                               0x5ca070, 0x19fc590]],
          'guarded_save_call_sites': [hex(rva) for rva in save_sites],
          'installed_files_changed': False, 'temporary_files_unchanged': True,
          'native_allocations_released': allocation_count, 'outstanding_allocations': len(allocations),
          'previous_patch': before, 'previous_frontend': before_frontend,
          'patched': after, 'frontend': frontends,
          'repeated_selection_cycles': 10, 'unrelated_transliteration_preserved': True}
args.report.parent.mkdir(parents=True, exist_ok=True)
args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(f'PASS: screenshot regression, {len(after)} overwrite checks, {len(frontends)} frontend checks, '
      f'{allocation_count} allocations released ({args.dll}).')
