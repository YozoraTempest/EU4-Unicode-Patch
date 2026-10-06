"""Check legacy import hooks against mapped EU4 code, without starting a game.

Use 64-bit Windows Python and a CI-built DLL with its linker map. Native lexer
code runs in this process with a memory input and mocked allocation/lookup.
The game's entry point and any running game are never accessed.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--executable', type=Path, required=True)
parser.add_argument('--build-directory', type=Path, required=True)
parser.add_argument('--script-values', type=Path, help='Optional JSON list with raw text in hex')
args = parser.parse_args()
assert os.name == 'nt' and C.sizeof(C.c_void_p) == 8 and __debug__
build = (root / args.build_directory).resolve()
subprocess.run([str(build / 'executable_check.exe'), str(args.executable)], check=True)
info = json.loads((build / 'build-info.json').read_text(encoding='utf-8-sig'))
assert hashlib.sha256((build / 'eu4_unicode_patch.dll').read_bytes()).hexdigest() == info['patch_dll_sha256']
k32 = C.WinDLL('kernel32', use_last_error=True)
k32.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k32.LoadLibraryExW.restype = C.c_void_p
base = k32.LoadLibraryExW(str(args.executable.resolve()), None, 1)
assert base
dll = C.WinDLL(str(build / 'eu4_unicode_patch.dll'))
symbols = {}
for line in (build / 'eu4_unicode_patch.map').read_text().splitlines():
    match = re.match(r'\s+\w{4}:\w{8}\s+(\S+)\s+(\w{16})', line)
    if match:
        symbols[match[1]] = dll._handle + int(match[2],16) - 0x180000000
def symbol(fragment):
    found = [address for name, address in symbols.items() if name == fragment or name.startswith('?' + fragment + '@')]
    assert len(found) == 1, (fragment, len(found))
    return found[0]
def pointer(name, value):
    C.c_void_p.from_address(symbol(name)).value = value
def fn(name, result, *parameters):
    return C.CFUNCTYPE(result, *parameters)(symbol(name))
initialize = fn('MH_Initialize', C.c_int)
create = fn('MH_CreateHook', C.c_int, C.c_void_p, C.c_void_p, C.POINTER(C.c_void_p))
enable = fn('MH_EnableHook', C.c_int, C.c_void_p)
assert initialize() in (0,1)
callbacks = []
def hook(rva, callback, original=None):
    callbacks.append(callback)
    destination = C.cast(callback, C.c_void_p).value if not isinstance(callback,int) else callback
    trampoline = C.c_void_p()
    assert create(base+rva, destination, C.byref(trampoline)) == 0, hex(rva)
    if original:
        pointer(original, trampoline.value)
    assert enable(base+rva) == 0

class Storage(C.Union):
    _fields_ = [('inline',C.c_char*16),('pointer',C.c_void_p)]
class EngineString(C.Structure):
    _fields_ = [('storage',Storage),('size',C.c_uint64),('capacity',C.c_uint64)]
class Input(C.Structure):
    _fields_ = [('vtable',C.c_void_p),('line',C.c_int),('last',C.c_ubyte),
                ('pushback',C.c_ubyte),('padding',C.c_byte*10),('filename',EngineString)]
class Lexer(C.Structure):
    _fields_ = [('vtable',C.c_void_p),('input',C.c_void_p),('previous',C.c_uint),
                ('token_type',C.c_uint),('text',C.c_char*512),('flags',C.c_ubyte*8)]
assert Input.filename.offset == 0x18 and Lexer.text.offset == 0x18 and Lexer.flags.offset == 0x218
streams = {}
@C.CFUNCTYPE(C.c_ubyte,C.c_void_p)
def read_byte(address):
    stream = Input.from_address(address)
    data, pos = streams[address]
    if stream.pushback:
        stream.pushback = 0
        value = stream.last
    else:
        value = data[pos] if pos < len(data) else 255
        streams[address] = (data,min(pos+1,len(data)))
        stream.last = value
    if value == 10:
        stream.line += 1
    return value
@C.CFUNCTYPE(C.c_bool,C.c_void_p)
def available(address):
    data, pos = streams[address]
    return pos < len(data) or Input.from_address(address).pushback != 0
@C.CFUNCTYPE(C.c_bool,C.c_void_p,C.c_void_p,C.c_int)
def read(address,target,size):
    data, pos = streams[address]
    if size < 0 or size > len(data)-pos:
        return False
    C.memmove(target,data[pos:pos+size],size)
    streams[address] = (data,pos+size)
    return True
@C.CFUNCTYPE(C.c_bool,C.c_void_p,C.c_int)
def seek(address,pos):
    data, _ = streams[address]
    if pos < 0 or pos > len(data):
        return False
    streams[address] = (data,pos)
    return True
@C.CFUNCTYPE(C.c_int64,C.c_void_p)
def position(address):
    return streams[address][1]
@C.CFUNCTYPE(C.c_int,C.c_int)
def space(value):
    return int(value in (9,10,11,12,13,32))
@C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p)
def lookup(_map,_key):
    return None
@C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p,C.c_size_t)
def copy(target,source,size):
    C.memmove(target,source,size)
    return target
crt = C.CDLL('ucrtbase')
crt.malloc.argtypes=[C.c_size_t];crt.malloc.restype=C.c_void_p
crt.free.argtypes=[C.c_void_p]
malloc = C.CFUNCTYPE(C.c_void_p,C.c_size_t)(crt.malloc)
free = C.CFUNCTYPE(None,C.c_void_p)(crt.free)
for rva, callback in [(0x1a5b23c,space),(0x6e4880,lookup),(0x1a35750,copy),
                      (0x1a33310,malloc),(0x1a332d4,malloc),(0x17394f0,free)]:
    hook(rva,callback)
hook(0x170e700,symbol('read_legacy_script_token'),'original_script_token')
methods = (C.c_void_p*17)()
for offset, callback in [(8,read_byte),(0x20,read),(0x58,available),(0x60,seek),(0x80,position)]:
    methods[offset//8] = C.cast(callback,C.c_void_p).value
lexer_methods = (C.c_void_p*3)()
lexer_methods[1] = base+0x170e700
parse = C.CFUNCTYPE(C.c_int,C.POINTER(Lexer))(base+0x170e700)
decode_exe = build / 'legacy_decode.exe'
references = {}
def prepare_references(values):
    values = list(dict.fromkeys(values))
    output = subprocess.check_output([str(decode_exe)],input=''.join(f'r {raw.hex()}\n' for raw in values),text=True)
    lines = output.splitlines()
    assert len(lines) == len(values)
    for raw, line in zip(values,lines):
        parts = line.split(' ')
        assert parts[0] == 'OK'
        references[raw] = bytes.fromhex(parts[3]) if len(parts)==4 else b''
def reference(raw):
    if raw not in references:
        prepare_references([raw])
    return references[raw]
def verify(raw, quoted=True, bom=False):
    # Keep two trailing bytes: the native input's availability test follows
    # each byte read, including the closing quote and consumed delimiter.
    text = (b'\xef\xbb\xbf' if bom else b'') + b'name = ' + (b'"'+raw.replace(b'\\',b'\\\\').replace(b'"',b'\\"')+b'"' if quoted else raw) + b'\n\n'
    stream = Input(C.addressof(methods),1,0,0)
    address = C.addressof(stream)
    streams[address] = (text,0)
    lexer = Lexer(C.addressof(lexer_methods),address)
    if bom:
        assert fn('consume_script_bom',C.c_bool,C.c_void_p)(address)
    actual = []
    for _ in range(8):
        result = parse(C.byref(lexer))
        if not result:
            break
        actual.append((lexer.token_type,bytes(lexer.text)))
    del streams[address]
    assert actual == [(15,b'name'),(1,b'='),(15,reference(raw))], actual
    assert not any(x in actual[-1][1] for x in range(16,20))
for raw in [b'\x10-N',b'\x11\x0eN\x12\xf9R\x13it',
            '中文 العربية हिन्दी'.encode(),b'\xa7Y\x10-N\xa7! \xa3adm\xa3 @FRA $COUNTRY$ \\n',
            b'Ariq-B\xf6kid \x10-N']:
    verify(raw)
verify(b'\x10-N',quoted=False)
verify(b'\x10-N',bom=True)
verify('中文𠀀'.encode(),bom=True)
count = 8
if args.script_values:
    records = json.loads(args.script_values.read_text(encoding='utf-8'))
    prepare_references(bytes.fromhex(record['hex']) for record in records)
    for record in records:
        verify(bytes.fromhex(record['hex']))
    count += len(records)
print(f'PASS: {count} script values parsed by native EU4 lexer and normalized by the CI import hook.')
