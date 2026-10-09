"""Run EU4 1.37.5 layout routines with the built player hooks in this process.

Requires 64-bit Windows, a local supported eu4.exe, and a CI-built player DLL
with its linker map. The executable is mapped without running its entry point.
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
parser.add_argument('--assets-directory', type=Path, help='Game resources when the EXE is stored separately')
parser.add_argument('--report', type=Path)
args = parser.parse_args()
if os.name != 'nt' or C.sizeof(C.c_void_p) != 8:
    parser.error('64-bit Windows Python is required')
if not __debug__:
    parser.error('Assertions must be enabled; do not use python -O')
build = (root / args.build_directory).resolve()
game = args.executable.resolve()
game_hash = hashlib.sha256(game.read_bytes()).hexdigest()
import subprocess
subprocess.run([str(build / 'executable_check.exe'), str(game)], check=True)
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
patch_log = None
if args.report:
    import msvcrt
    patch_log = args.report.with_suffix('.patch.log').open('w+b')
    pointer('log_file', msvcrt.get_osfhandle(patch_log.fileno()))
source = (root / 'src/plugin.cpp').read_text(encoding='utf-8')
# Verify native site bytes before any test hook changes the mapped image.
profile = (root / 'src/eu4_1375_profile.cpp').read_text(encoding='utf-8')
guards = re.findall(r'\{(0x[0-9a-f]+),"([0-9a-f]+)"', profile)
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
hook(0x159b7c0,'measure_paragraph_text','original_text_width')
hook(0x159b470,'measure_paragraph_height','original_text_height')

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
                  (0x15968e1, 'button_format_hook'), (0x1596e83, 'button_icon_copy_hook'),
                  (0x15966ec, 'button_geometry_entry_hook'), (0x1598841, 'button_geometry_end_hook')]:
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
# The UI uses two backend entry points: cached uploads and buffers created
# with initial data. Verify the supported executable routes them to our hooks.
for slot, target in [(0x1fd1190, 0x16d6640), (0x1fd11a8, 0x16d5f20), (0x1fd1188, 0x16d65d0)]:
    assert C.c_void_p.from_address(base+slot).value == base+target, hex(slot)

# Execute the production geometry-emission hooks with the native frame layout.
# Replace only their marking callback: GPU tests exercise actual glyph-to-page
# lookup and drawing, while this checks the MASM frame/index and register ABI.
ui_vertices=C.create_string_buffer(30*28)
ui_frame=C.create_string_buffer(0x2400)
ui_frame_base=C.addressof(ui_frame)+0x20
ui_output=C.create_string_buffer(128)
ui_glyph=C.addressof(glyphs[65])
marker_calls=[]
@C.CFUNCTYPE(None,C.c_void_p,C.c_void_p)
def mark_ui_vertices(glyph,vertices):
    marker_calls.append((glyph,vertices))
    for vertex in range(6):
        C.c_float.from_address(vertices+vertex*28+12).value += 2
trampoline=C.c_void_p()
assert create(symbol('mark_popup_font_glyph'),C.cast(mark_ui_vertices,C.c_void_p).value,C.byref(trampoline))==0
assert enable(symbol('mark_popup_font_glyph'))==0
pointer('g_ui_vertices',C.addressof(ui_vertices))
ui_page_results=[]
for routine,hook_name,return_name,glyph_offset in [
        ('main','main_page_hook','g_main_page_return',0x38),
        ('button','button_page_hook','g_button_page_return',0x58)]:
    result_register=b'\x89\x18' if routine=='main' else b'\x89\x10'
    continuation=executable_code(b'\x48\xb8'+struct.pack('<Q',C.addressof(ui_output))+result_register+
        b'\x4c\x89\x40\x08\x4c\x89\x50\x10\x48\x81\xc4\x00\x01\x00\x00\x41\x5e\x5b\x5d\xc3')
    pointer(return_name,continuation)
    for first_vertex in (0,6,18):
        for vertex in range(30):
            C.memmove(C.addressof(ui_vertices)+vertex*28,struct.pack('<5f2I',1,2,3,.25,.5,0x12345678,0x87654321),28)
        C.c_void_p.from_address(ui_frame_base+glyph_offset).value=ui_glyph
        C.c_void_p.from_address(ui_frame_base-0x18).value=C.addressof(ui_vertices)
        C.c_uint.from_address(ui_frame_base+0x21c8).value=37
        marker_calls.clear()
        sentinel8=0x123456789abcdef0;sentinel10=0x1020304050607080
        code=(b'\x55\x53\x41\x56\x48\x81\xec\x00\x01\x00\x00'
              b'\x48\xbd'+struct.pack('<Q',ui_frame_base)+
              b'\x41\xbe'+struct.pack('<I',first_vertex)+
              b'\xc7\x44\x24\x48'+struct.pack('<I',first_vertex)+
              b'\x49\xb8'+struct.pack('<Q',sentinel8)+b'\x49\xba'+struct.pack('<Q',sentinel10)+
              b'\xff\x25\x00\x00\x00\x00'+struct.pack('<Q',symbol(hook_name)))
        C.CFUNCTYPE(None)(executable_code(code))()
        assert marker_calls==[(ui_glyph,C.addressof(ui_vertices)+first_vertex*28)],(routine,marker_calls)
        assert C.c_uint.from_buffer(ui_output).value==(first_vertex+6 if routine=='main' else 37)
        assert C.c_uint64.from_buffer(ui_output,8).value==sentinel8
        assert C.c_uint64.from_buffer(ui_output,16).value==sentinel10
        for vertex in range(30):
            expected=2.25 if first_vertex<=vertex<first_vertex+6 else .25
            assert C.c_float.from_buffer(ui_vertices,vertex*28+12).value==expected,(routine,first_vertex,vertex)
            assert C.string_at(C.addressof(ui_vertices)+vertex*28,12)==struct.pack('<3f',1,2,3)
            assert C.string_at(C.addressof(ui_vertices)+vertex*28+16,12)==struct.pack('<f2I',.5,0x12345678,0x87654321)
        ui_page_results.append({'routine':routine,'first_vertex':first_vertex,'tagged_vertices':6})
ui_scope_results=[]
scope_input=engine_string(b'Native scope input')
scope_output=engine_string(b'Native scope replacement')
scope_calls=[]
@C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p,C.c_void_p,C.c_int,C.c_bool)
def capture_main_scope(font,source,box,inset,formatted):
    scope_calls.append(('main',font,source,box,inset,formatted))
    return C.addressof(scope_output)
@C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p,C.c_int,C.c_void_p,C.c_bool)
def capture_button_scope(font,source,width,margin,formatted):
    scope_calls.append(('button',font,source,width,margin,formatted))
    return C.addressof(scope_output)
@C.CFUNCTYPE(C.c_void_p,C.c_void_p,C.c_void_p,C.c_int)
def capture_popup_scope(font,source,width):
    scope_calls.append(('popup',font,source,width))
    return C.addressof(scope_output)
for name,callback in [('begin_native_paragraph',capture_main_scope),
                      ('begin_native_button_paragraph',capture_button_scope),
                      ('begin_native_popup_paragraph',capture_popup_scope)]:
    scope_trampoline=C.c_void_p()
    assert create(symbol(name),C.cast(callback,C.c_void_p).value,C.byref(scope_trampoline))==0
    assert enable(symbol(name))==0
pointer('g_popup_data',executable_code(b'\x48\x89\xc8\xc3'))
scope_box=(C.c_int*4)(0,0,220,55)
C.c_void_p.from_address(ui_frame_base+0x2380).value=C.addressof(scope_box)
C.c_int.from_address(ui_frame_base+0x2388).value=7
C.c_ubyte.from_address(ui_frame_base+0x23a0).value=1
C.c_void_p.from_address(ui_frame_base+0x21c8).value=C.addressof(scope_input)
C.c_int.from_address(ui_frame_base+0x21d8).value=220
C.c_int.from_address(ui_frame_base+0x21e0).value=55
C.c_void_p.from_address(ui_frame_base+0x21e8).value=C.addressof(margin)
C.c_ubyte.from_address(ui_frame_base+0x21f8).value=0
C.c_ubyte.from_address(ui_frame_base+0x2210).value=1
C.c_int.from_address(ui_frame_base+0x398).value=-1
xmm_values=C.create_string_buffer(bytes(range(96)))
load_xmm=b'\x48\xb8'+struct.pack('<Q',C.addressof(xmm_values))+b''.join(
    b'\xf3\x0f\x6f'+bytes([0x80+index*8])+struct.pack('<I',index*16) for index in range(6))
def store_xmm(base_register):
    return b''.join(b'\xf3\x0f\x7f'+bytes([0x80+index*8+base_register])+
                    struct.pack('<I',32+index*16) for index in range(6))
for routine,frame_size in [('main',0x2408),('button',0x2260),('popup',0x438)]:
    for phase in ('entry','end'):
        if phase=='entry':
            save_registers={'main':b'\x4c\x89\x20\x4c\x89\x70\x08',
                            'button':b'\x48\x89\x18\x48\x89\x78\x08',
                            'popup':b'\x48\x89\x30\x4c\x89\x78\x08'}[routine]
            capture=(b'\x48\xb8'+struct.pack('<Q',C.addressof(ui_output))+save_registers+
                     b'\x4c\x89\x40\x10\x4c\x89\x50\x18'+store_xmm(0))
        else:
            capture=(b'\x48\xb9'+struct.pack('<Q',C.addressof(ui_output))+b'\x48\x89\x01'
                     b'\x4c\x89\xd8\x48\x29\xe0\x48\x89\x41\x08\x4c\x89\x41\x10\x4c\x89\x51\x18'+store_xmm(1))
        continuation=executable_code(capture+b'\x48\x81\xc4\x00\x01\x00\x00\x41\x5f\x5e\x5f\x41\x5e\x41\x5c\x5b\x5d\xc3')
        return_name=f'g_{routine}_{phase}_return' if routine=='popup' else f'g_{routine}_geometry_{phase}_return'
        hook_name=f'popup_{phase}_hook' if routine=='popup' else f'{routine}_geometry_{phase}_hook'
        pointer(return_name,continuation)
        code=(b'\x55\x53\x41\x54\x41\x56\x57\x56\x41\x57\x48\x81\xec\x00\x01\x00\x00'
              b'\x48\xbd'+struct.pack('<Q',ui_frame_base)+
              b'\x48\xb9'+struct.pack('<Q',font_base)+b'\x48\xba'+struct.pack('<Q',C.addressof(scope_input))+
              b'\x48\xbe'+struct.pack('<Q',C.addressof(scope_input))+b'\x49\xbf'+struct.pack('<Q',font_base)+
              b'\x49\xb8'+struct.pack('<Q',sentinel8)+b'\x49\xba'+struct.pack('<Q',sentinel10)+
              load_xmm+
              b'\x48\xb8'+struct.pack('<Q',0xabcdef)+
              b'\xff\x25\x00\x00\x00\x00'+struct.pack('<Q',symbol(hook_name)))
        C.CFUNCTYPE(None)(executable_code(code))()
        expected=(sentinel8,font_base) if routine=='button' else (C.addressof(scope_output),font_base)
        if phase=='end': expected=(0xabcdef,frame_size)
        assert tuple(C.c_uint64.from_buffer(ui_output,offset).value for offset in (0,8))==expected,(routine,phase)
        assert C.c_uint64.from_buffer(ui_output,16).value==sentinel8
        assert C.c_uint64.from_buffer(ui_output,24).value==sentinel10
        assert ui_output.raw[32:128]==xmm_values.raw[:96],(routine,phase,'volatile SIMD registers')
        if routine=='button' and phase=='entry':
            assert C.c_void_p.from_address(ui_frame_base+0x21c8).value==C.addressof(scope_output)
        ui_scope_results.append({'routine':routine,'phase':phase,'native_registers_preserved':True,
                                 'volatile_simd_preserved':True})
assert scope_calls==[
    ('main',font_base,C.addressof(scope_input),C.addressof(scope_box),7,True),
    ('button',font_base,C.addressof(scope_input),220,C.addressof(margin),True),
    ('popup',font_base,C.addressof(scope_input),-1)],scope_calls
button_format_results=[]
begin_button=fn('begin_button_paragraph',C.c_void_p,C.c_void_p,C.c_void_p,C.c_void_p)
for width,height,truncate,formatted in [(220,0,False,True),(220,55,False,False),
                                      (220,0,True,False),(220,55,True,True)]:
    C.c_int.from_address(ui_frame_base+0x21d8).value=width
    C.c_int.from_address(ui_frame_base+0x21e0).value=height
    C.c_ubyte.from_address(ui_frame_base+0x21f8).value=truncate
    C.c_ubyte.from_address(ui_frame_base+0x2210).value=formatted
    result=begin_button(font_base,C.addressof(scope_input),ui_frame_base+0x21d8)
    assert result==C.addressof(scope_output)
    assert scope_calls[-1]==('button',font_base,C.addressof(scope_input),width,C.addressof(margin),formatted),scope_calls[-1]
    button_format_results.append({'width':width,'height':height,'truncate':truncate,'formatted':formatted})
selection_check = __import__('runpy').run_path(str(root / 'tools/native-selection-check.py'))
selection_results = selection_check['verify'](base, symbols, address_hook, callbacks, crt)
map_check = __import__('runpy').run_path(str(root / 'tools/native-map-check.py'))
assets = args.assets_directory.resolve() if args.assets_directory else game.parent
map_results = map_check['verify'](base, fn, hook, engine_string, assets, pointer, symbol, executable_code)
hook(0x159e5c4,'map_fit_begin_hook')
hook(0x159e7e1,'map_fit_wrap_hook')
# Events and map labels share this native size-fitting routine. Use fixed
# glyph metrics so a width regression cannot hide behind font substitution.
for cp in map(ord,'中文，。'):
    record=fn('find_unicode_glyph',C.c_void_p,C.c_void_p,C.c_uint)(table,cp) or allocate(table,cp)
    assert record,cp
    C.memmove(record,struct.pack('<7h2B',0,0,10,10,0,0,10,0,0),16)
fit=C.CFUNCTYPE(C.c_int,C.c_void_p,C.c_void_p,C.c_int,C.c_int,C.c_void_p,C.c_void_p,C.c_bool)(base+0x159e510)
fit_results=[]
for text in ['中文中文中文中文','§Y中文§!中文中文中文','中文，中文。中文',
             '中e§R\u0301§!文','ABC DEF GHI JKL']:
    source_string=engine_string(text.encode())
    for width in (20,30,40,1000):
        output_size=(C.c_int*2)()
        lines=fit(font_base,C.byref(source_string),width,10000,C.byref(margin),C.byref(output_size),True)
        if text.startswith('中文中文') or text.startswith('§Y中文'):
            expected=max(1,(8+(width//10)-1)//(width//10))
            assert lines==expected and output_size[0]<=width,(text,width,lines,list(output_size),expected)
        fit_results.append({'text':text,'width':width,'lines':lines,'size':list(output_size)})
editor_check = __import__('runpy').run_path(str(root / 'tools/native-editor-check.py'))
editor_results = editor_check['verify'](base, fn, hook, engine_string, font_base, callbacks, crt,
                                       address_hook, executable_code)
patch_messages = ''
if patch_log:
    pointer('log_file', C.c_void_p(-1).value)
    patch_log.seek(0)
    patch_messages = patch_log.read().decode('utf-8')
    patch_log.close()
    assert 'Unicode commit caret alignment failed' not in patch_messages, patch_messages
report = {'source_commit': build_info['source_commit'], 'patch_dll_sha256': dll_hash,
          'game_exe_sha256': game_hash, 'site_guards': len(guards),
          'native_width': results, 'native_layout': layout_results,
          'substring_callers': [hex(x) for x in sorted(substring_callers)],
          'ui_page_emission':ui_page_results,'ui_geometry_scopes':ui_scope_results,
          'button_format_arguments':button_format_results,
          'native_selection_sprites':selection_results,'native_map_fit':map_results,
          'native_multiline_editor':editor_results,'native_event_fit':fit_results,
          'native_patch_messages':patch_messages}
if args.report:
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(f"PASS: {len(results)} native width cases, {len(layout_results)} native layouts, "
      f"all {len(expected_callers)} truncation exits; player DLL {dll_hash}.", flush=True)
print(f"PASS: {len(ui_page_results)} native UI page-emission cases preserve positions, colors and registers.",flush=True)
print(f"PASS: {len(ui_scope_results)} native UI geometry scope cases preserve entry and exit registers.",flush=True)
print(f"PASS: {len(button_format_results)} native button format arguments remain independent of height and truncation.",flush=True)
print("PASS: native selection factory, expired render parents, relinking and manager release.",flush=True)
print(f"PASS: {len(map_results)} shaped map labels retain native fitting dimensions and scoped glyphs.",flush=True)
print(f"PASS: {len(editor_results)} native multiline editors preserve rows, complete deletion and undo/redo.",flush=True)
