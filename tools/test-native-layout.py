"""Run EU4 1.37.5 layout routines with the built player hooks in this process.

Requires 64-bit Windows, a local supported eu4.exe, and the DLL/linker map from
tools/build.ps1. The executable is mapped without running its entry point.
Engine allocation and graphics dependencies are replaced inside this process;
no running game is accessed. Native copy/measure/wrap/substring routines remain
in use. The independent native_font_pages_tests covers GPU submission.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import re
import struct

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--executable', required=True, type=Path)
parser.add_argument('--build-directory', default='build', type=Path)
parser.add_argument('--report', type=Path)
args = parser.parse_args()
if os.name != 'nt' or C.sizeof(C.c_void_p) != 8:
    parser.error('64-bit Windows Python is required')
if not __debug__:
    parser.error('Assertions must be enabled; do not use python -O')
build = (root / args.build_directory).resolve()
game = args.executable.resolve()
game_hash = hashlib.sha256(game.read_bytes()).hexdigest()
if game_hash != '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a':
    parser.error('Unsupported game executable SHA-256')
dll_path = build / 'eu4_unicode_patch.dll'
dll_hash = hashlib.sha256(dll_path.read_bytes()).hexdigest()
build_info = json.loads((build / 'build-info.json').read_text(encoding='utf-8-sig'))
if dll_hash != build_info['patch_dll_sha256']:
    parser.error('DLL does not match build-info.json')
k32 = C.WinDLL('kernel32', use_last_error=True)
k32.LoadLibraryExW.argtypes = [C.c_wchar_p, C.c_void_p, C.c_uint]
k32.LoadLibraryExW.restype = C.c_void_p
base = k32.LoadLibraryExW(str(game), None, 1)
if not base:
    raise C.WinError(C.get_last_error())
dll = C.WinDLL(str(dll_path))
symbols = {}
for line in (build / 'eu4_unicode_patch.map').read_text().splitlines():
    m = re.match(r'\s+\w{4}:\w{8}\s+(\S+)\s+(\w{16})', line)
    if m:
        symbols[m[1]] = dll._handle + int(m[2], 16) - 0x180000000

def symbol(fragment):
    if fragment in symbols:
        return symbols[fragment]
    found = [value for name, value in symbols.items() if name == fragment or name.startswith('?' + fragment + '@')]
    if len(found) != 1:
        raise ValueError((fragment, len(found)))
    return found[0]

def pointer(name, value):
    C.c_void_p.from_address(symbol(name)).value = value

def fn(name, result, *args):
    return C.CFUNCTYPE(result, *args)(symbol(name))

pointer('image', base)
source = (root / 'src/plugin.cpp').read_text(encoding='utf-8')
# Verify native site bytes before any test hook changes the mapped image.
guards = re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)"\}', source)
assert guards, 'No native site guards found'
for rva, pattern in guards:
    expected = bytes.fromhex(pattern)
    assert C.string_at(base + int(rva, 16), len(expected)) == expected, rva
for name, rva in re.findall(r'(g_\w+)=address\((0x\w+)\)', source):
    pointer(name, base + int(rva, 16))
initialize = fn('MH_Initialize', C.c_int)
create = fn('MH_CreateHook', C.c_int, C.c_void_p, C.c_void_p, C.POINTER(C.c_void_p))
enable = fn('MH_EnableHook', C.c_int, C.c_void_p)
assert initialize() in (0, 1)

def hook(rva, name, original=None):
    trampoline = C.c_void_p()
    status = create(base + rva, symbol(name), C.byref(trampoline))
    assert status == 0, (hex(rva), name, status)
    if original:
        pointer(original, trampoline.value)
    assert enable(base + rva) == 0

def address_hook(rva, destination):
    trampoline = C.c_void_p()
    assert create(base+rva, destination, C.byref(trampoline)) == 0, hex(rva)
    assert enable(base+rva) == 0

k32.VirtualAlloc.argtypes=[C.c_void_p,C.c_size_t,C.c_ulong,C.c_ulong]
k32.VirtualAlloc.restype=C.c_void_p
k32.GetCurrentProcess.restype = C.c_void_p
k32.FlushInstructionCache.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
k32.FlushInstructionCache.restype = C.c_bool
def executable_code(code):
    entry=k32.VirtualAlloc(None,len(code),0x3000,0x40)
    assert entry
    C.memmove(entry,code,len(code))
    assert k32.FlushInstructionCache(k32.GetCurrentProcess(), entry, len(code))
    return entry

crt = C.CDLL('ucrtbase')
crt.malloc.argtypes = [C.c_size_t]
crt.malloc.restype = C.c_void_p
crt.free.argtypes = [C.c_void_p]
malloc = C.CFUNCTYPE(C.c_void_p, C.c_size_t)(crt.malloc)
free = C.CFUNCTYPE(None, C.c_void_p)(crt.free)
address_hook(0x1a332d4, C.cast(malloc, C.c_void_p).value)
address_hook(0x17394f0, C.cast(free, C.c_void_p).value)

for rva, name in [(0x159b91a, 'alternate_measure_hook'), (0x159b85c, 'alternate_format_hook'),
                  (0x159b999, 'alternate_advance_hook'), (0x159b95a, 'alternate_kern_hook')]:
    hook(rva, name)

font = C.create_string_buffer(0x4000)
font_base = C.addressof(font)
table = font_base + 0x120
glyphs = []
glyph_scalars={}
for cp in range(256):
    glyph = C.create_string_buffer(struct.pack('<7h2B', 0, 0, 7, 10 if cp > 32 else 0, 0, 0, 7, 0, 0))
    glyphs.append(glyph)
    C.c_void_p.from_address(table + cp * 8).value = C.addressof(glyph)
    glyph_scalars[C.addressof(glyph)]=cp
for cp in (10, 13):
    C.c_void_p.from_address(table + cp * 8).value = None
for offset in (0x848, 0x968):
    C.c_float.from_address(font_base + offset).value = 1.0
C.c_int.from_address(font_base + 0x960).value = 16
vtable = (C.c_void_p * 64)()
callbacks = []
for offset, width in [(0xe8, 11), (0xf0, 16), (0xf8, 13)]:
    callback = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)(lambda a, b, w=width: w)
    callbacks.append(callback)
    vtable[offset // 8] = C.cast(callback, C.c_void_p).value
C.c_void_p.from_address(font_base).value = C.addressof(vtable)
allocate = fn('allocate_unicode_glyph', C.c_void_p, C.c_void_p, C.c_uint)
for cp in set(map(ord, '万帕诺亚格施泰莫阿克中文𠮷…，。\u0301')):
    record = allocate(table, cp)
    assert record
    glyph_scalars[record]=cp
    C.memmove(record, struct.pack('<7h2B', 0, 0, 10, 10, 0, 0, 10, 0, 0), 16)
measure = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_char_p, C.c_int, C.c_bool)(base + 0x159b7c0)
results = []
for text, expected in [('ABC', 21), ('万帕诺亚格', 50), ('é中𠮷A', 34), ('§Y中文§!', 20),
                       ('A£adm£中文', 38), ('中文\n中', 20), ('£yes 中文', 31)]:
    value = text.encode()
    width = measure(font_base, value, len(value), True)
    assert width == expected, (text, width, expected)
    results.append({'text': text, 'bytes': len(value), 'width': width})
for text in ('é', '中', '𠮷'):
    value = text.encode()
    for length in range(len(value)):
        width = measure(font_base, value, length, False)
        assert width == 0, (text, length, width)
        results.append({'text': text, 'bytes': length, 'width': width})
class EngineString(C.Structure):
    _fields_ = [('data', C.c_ubyte*16), ('size', C.c_uint64), ('capacity', C.c_uint64)]
    def value(self):
        address = C.addressof(self) if self.capacity < 16 else C.c_void_p.from_address(C.addressof(self)).value
        return C.string_at(address, self.size)

assign = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_char_p, C.c_size_t)(base+0x95110)
def engine_string(value=b''):
    result = EngineString()
    result.capacity = 15
    assign(C.byref(result), value, len(value))
    return result

pointer('repeat_text', base+0x90320)
pointer('append_text', base+0x932f0)
for rva, name in [(0x159ef48, 'bitmap_split_hook'), (0x159f1db, 'bitmap_advance_hook'),
                  (0x159eddd, 'split_format_hook'), (0x159f06d, 'split_kern_hook'),
                  (0x159f87d, 'list_measure_hook'), (0x159fde5, 'list_advance_hook'),
                  (0x159f6ef, 'list_format_hook'), (0x159f9a9, 'list_kern_hook'),
                  (0x1596858, 'button_copy_hook'), (0x1597071, 'button_measure_hook'),
                  (0x15974cb, 'button_advance_hook'), (0x15970df, 'button_wrap_hook'),
                  (0x15968e1, 'button_format_hook'), (0x1596e83, 'button_icon_copy_hook')]:
    hook(rva, name)
substring_callers=set()
@C.CFUNCTYPE(None,C.c_void_p)
def record_substring(caller):
    substring_callers.add(caller-base)
# Preserve the native return address through the tracing bridge. The production
# substring adapter restricts its UTF-8 rounding by precisely these callers.
trace_code=(b'\x48\x83\xec\x58\x48\x89\x4c\x24\x20\x48\x89\x54\x24\x28'
            b'\x4c\x89\x44\x24\x30\x4c\x89\x4c\x24\x38\x48\x8b\x4c\x24\x58\x48\xb8'+
            struct.pack('<Q',C.cast(record_substring,C.c_void_p).value)+b'\xff\xd0'
            b'\x48\x8b\x4c\x24\x20\x48\x8b\x54\x24\x28\x4c\x8b\x44\x24\x30\x4c\x8b\x4c\x24\x38'
            b'\x48\x83\xc4\x58\x48\xb8'+struct.pack('<Q',symbol('layout_substring'))+b'\xff\xe0')
trampoline=C.c_void_p()
assert create(base+0x1704af0,executable_code(trace_code),C.byref(trampoline))==0
pointer('original_layout_substring',trampoline.value)
assert enable(base+0x1704af0)==0
margin = (C.c_int*4)(0,0,0,0)
split = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.c_int, C.c_int, C.c_void_p, C.c_bool)(base+0x159ec30)
list_layout = C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p,C.c_void_p,C.c_int,C.c_int,C.c_void_p,C.c_bool,C.c_bool,C.c_bool,C.c_void_p,C.c_void_p)(base+0x159f510)
layout_results=[]
layout_texts = ['万帕诺亚格', '施泰亚莫阿克', '§Y万帕诺亚格§!', 'A£adm£中文',
                'é中𠮷A', '万帕诺亚格施泰亚莫阿克中文',
                '万 帕 诺 亚 格 施 泰 亚 莫 阿 克 中 文',
                '万帕诺亚格\n施泰亚莫阿克\n中文',
                '万帕诺亚格 施泰亚莫阿克 中文',
                'A 中 A 文 A 中 A 文 A 中 A 文',
                'ABC DEF GHI JKL MNO PQR STU']
for text in layout_texts:
    source_string=engine_string(text.encode())
    for width in [1,10,20,30,40,50,100]:
        for height in [1,16,32,1000]:
            output=engine_string()
            split(font_base,C.byref(source_string),C.byref(output),width,height,margin,True)
            value=output.value()
            try:
                value.decode('utf-8')
            except UnicodeDecodeError:
                raise AssertionError(('bitmap',text,width,height,value))
            layout_results.append({'routine':'bitmap','text':text,'width':width,'height':height,'output':value.decode()})
    for width in [1,10,20,30,40,50,100]:
        for truncate in [False,True]:
            for wrap in [False,True]:
                for height in [1,16,32,1000]:
                    output=engine_string()
                    list_layout(font_base,C.byref(source_string),C.byref(output),width,height,margin,truncate,wrap,True,None,None)
                    value=output.value()
                    try:
                        value.decode('utf-8')
                    except UnicodeDecodeError:
                        raise AssertionError(('list',text,width,height,truncate,wrap,value))
                    if width==100 and height==1000:
                        assert b''.join(value.split())==b''.join(text.encode().split()), (text,value)
                    layout_results.append({'routine':'list','text':text,'width':width,'height':height,'truncate':truncate,'wrap':wrap,'output':value.decode()})
captured=[]
@C.CFUNCTYPE(None,C.c_void_p)
def capture_text(source):
    captured.append(EngineString.from_address(source).value())
capture_code=(b'\x48\x83\xec\x20\x48\x8d\x4d\xa0\x48\xb8'+
              struct.pack('<Q',C.cast(capture_text,C.c_void_p).value)+b'\xff\xd0\x48\x83\xc4\x20\x48\xb8'+
              struct.pack('<Q',base+0x1598835)+b'\xff\xe0')
# Capture the native button output after layout and branch to native cleanup,
# before any graphics context is needed.
address_hook(0x1597634,executable_code(capture_code))
button=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_void_p,C.c_void_p,C.c_void_p,C.c_int,C.c_void_p,C.c_void_p,C.c_int,C.c_int,C.c_void_p,C.c_int,C.c_bool,C.c_int,C.c_bool,C.c_bool)(base+0x1596660)
for text in ['施泰亚莫阿克中文', '§Y施泰亚莫阿克§!中文', '中文，中文。中文', '中e§R\u0301§!文', 'A£adm£中文']:
    source_string=engine_string(text.encode())
    for width in [10,20,30,40,100]:
        captured.clear()
        button(font_base,None,None,None,0,C.byref(source_string),None,width,0,margin,0,False,1,False,True)
        assert len(captured)==1, captured
        value=captured[0]
        assert value.replace(b'\n',b'')==text.encode(), (text,width,value)
        value.decode('utf-8')
        layout_results.append({'routine':'button','text':text,'width':width,'output':value.decode()})
for rva,name in re.findall(r'\{(0x[0-9a-f]+),reinterpret_cast<void\*>\((popup_\w+_hook)\)\}',source):
    hook(int(rva,16),name)
popup_layout=[]
popup_glyphs=[]
@C.CFUNCTYPE(None,C.c_void_p,C.c_int)
def capture_popup(text,width):
    popup_layout.append((EngineString.from_address(text).value(),width))
@C.CFUNCTYPE(None,C.c_void_p,C.c_uint)
def capture_popup_glyph(glyph,offset):
    popup_glyphs.append((glyph_scalars[glyph],offset))
# Capture popup layout, bypass graphics setup, then execute its actual format,
# glyph lookup and cursor loop. Capture each glyph before geometry emission.
setup_code=(b'\x48\x83\xec\x20\x48\x8d\x8c\x24\x80\x00\x00\x00\x44\x89\xea\x48\xb8'+
            struct.pack('<Q',C.cast(capture_popup,C.c_void_p).value)+b'\xff\xd0\x48\x83\xc4\x20\x33\xf6'
            b'\x45\x0f\x57\xc0\x45\x0f\x57\xed\x45\x0f\x57\xff\x48\xb8'+struct.pack('<Q',base+0x159cba1)+b'\xff\xe0')
address_hook(0x159c9d6,executable_code(setup_code))
glyph_code=(b'\x48\x83\xec\x20\x4c\x89\xe9\x89\xf2\x48\xb8'+
            struct.pack('<Q',C.cast(capture_popup_glyph,C.c_void_p).value)+b'\xff\xd0\x48\x83\xc4\x20\x33\xc9\x48\xb8'+
            struct.pack('<Q',base+0x159da37)+b'\xff\xe0')
address_hook(0x159cf2a,executable_code(glyph_code))
address_hook(0x159cd51,executable_code(b'\x48\xb8'+struct.pack('<Q',base+0x159cd5e)+b'\xff\xe0'))
@C.CFUNCTYPE(None,C.c_void_p,C.c_uint,C.c_void_p)
def parse_color(font,code,target):
    C.c_uint.from_address(target).value=0
address_hook(0x15a0390,C.cast(parse_color,C.c_void_p).value)
@C.CFUNCTYPE(None,C.c_void_p,C.c_void_p,C.c_char_p)
def draw_icon(font,context,name):
    pass
vtable[0xe0//8]=C.cast(draw_icon,C.c_void_p).value
popup=C.CFUNCTYPE(None,C.c_void_p,C.c_void_p,C.c_int,C.c_int,C.c_void_p,C.c_void_p,C.c_void_p,C.c_float,C.c_float,C.c_void_p,C.c_void_p,C.c_int)(base+0x159c600)
for text in ['万帕诺亚格','é中𠮷A','§Y中文§!','A£adm£中文','中文，中文。中文','施泰亚莫阿克']:
    source_string=engine_string(text.encode())
    for width in [10,20,30,100]:
        popup_layout.clear();popup_glyphs.clear()
        popup(font_base,C.byref(source_string),0,width,None,None,None,1.,1.,None,None,0)
        assert len(popup_layout)==1,popup_layout
        value,measured=popup_layout[0]
        assert value.replace(b'\n',b'')==text.encode(),(text,value)
        expected=re.sub(r'§.','',re.sub(r'£[A-Za-z0-9_]+£','',value.decode())).replace('\n','')
        actual=''.join(chr(cp) for cp,offset in popup_glyphs)
        assert actual==expected,(text,width,expected,actual)
        layout_results.append({'routine':'popup','text':text,'width':width,'output':value.decode(),'glyphs':actual,'measured':measured})
expected_callers = {0x159f148, 0x159f230, 0x159f373, 0x159fa18, 0x159faec,
                    0x159fc46, 0x159fd48, 0x159fe60, 0x159feba, 0x159ff80, 0x15a00a0}
assert expected_callers <= substring_callers, expected_callers - substring_callers
report = {'source_commit': build_info['source_commit'], 'patch_dll_sha256': dll_hash,
          'game_exe_sha256': game_hash, 'site_guards': len(guards),
          'native_width': results, 'native_layout': layout_results,
          'substring_callers': [hex(x) for x in sorted(substring_callers)]}
if args.report:
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(f"PASS: {len(results)} native width cases, {len(layout_results)} native layouts, "
      f"all {len(expected_callers)} truncation exits; player DLL {dll_hash}.", flush=True)
