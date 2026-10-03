"""Verify native mod font selection and pixels in the owned player installation."""
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time
import zipfile

import frida
import psutil

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / 'private/player-install/Europa Universalis IV'
USERDIR = ROOT / 'private/player-userdir'
EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'


def read_font(text):
    records = {}
    for line in text.splitlines():
        if line.startswith('char '):
            p = {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', line)}
            records[p['id']] = p
    return records


def expected_glyphs(fnt, dds, scalars):
    records = read_font(fnt)
    kerned = {int(first) for first in re.findall(r'(?m)^kerning first=(\d+)\s', fnt)}
    width = struct.unpack_from('<I', dds, 16)[0]
    result = {}
    for scalar in scalars:
        p = records[scalar]
        pixels = b''.join(dds[128 + ((p['y']+y)*width+p['x'])*4:
                              128 + ((p['y']+y)*width+p['x']+p['width'])*4]
                          for y in range(p['height']))
        result[str(scalar)] = {
            'metrics': list(struct.pack('<7hBB', *(p[k] for k in
                ['x', 'y', 'width', 'height', 'xoffset', 'yoffset', 'xadvance']), int(scalar in kerned), 0)),
            'pixel_sha256': hashlib.sha256(pixels).hexdigest(),
        }
    return result


def make_fixtures():
    base = ROOT / 'private/mod-font-fixtures'
    generated = base / 'generated'
    generated.mkdir(parents=True, exist_ok=True)
    text = base / 'characters.txt'
    text.write_text('中文', encoding='utf-8')
    subprocess.run([str(ROOT/'build/fontpack.exe'), str(text), str(generated),
                    '--atlas-width', '512'], check=True)
    directory = base / 'directory'
    font_dir = directory / 'gfx/fonts'
    font_dir.mkdir(parents=True, exist_ok=True)
    (directory/'interface').mkdir(exist_ok=True)
    expectations, archive_files = {}, {}
    for name, size, advance, color, archive in [
        ('vic_18', 16, 37, (49, 67, 211), False),
        ('vic_22', 18, 43, (177, 51, 97), True),
        ('unicode-mod-test', 16, 53, (71, 193, 113), False),
    ]:
        fnt = (generated/f'zh-hans-{size}.fnt').read_text(encoding='utf-8')
        fnt = re.sub(r'(?m)^(char id=65 .*?xadvance=)\d+', rf'\g<1>{advance}', fnt)
        pixels = bytearray((generated/f'zh-hans-{size}.dds').read_bytes())
        p = read_font(fnt)[65]
        for y in range(p['height']):
            for x in range(p['width']):
                offset = 128 + ((p['y']+y)*512+p['x']+x)*4
                pixels[offset:offset+3] = bytes(color)
        files = {f'gfx/fonts/{name}.fnt': fnt.encode(), f'gfx/fonts/{name}.dds': bytes(pixels)}
        if archive:
            archive_files.update(files)
        else:
            for path, content in files.items():
                (directory/path).write_bytes(content)
        expectations[f'gfx/fonts/{name}'] = expected_glyphs(fnt, pixels, [65, 0x4e2d])
    # A texture-only override must also prevent alias redirection.
    fnt = (RUNTIME/'gfx/fonts/vic_18s.fnt').read_text(encoding='utf-8')
    dds = (RUNTIME/'gfx/fonts/vic_18s.dds').read_bytes()
    (font_dir/'vic_18s.dds').write_bytes(dds)
    expectations['gfx/fonts/vic_18s'] = expected_glyphs(fnt, dds, [65])
    chat = (RUNTIME/'interface/chatfonts.gfx').read_text(encoding='utf-8')
    (directory/'interface/chatfonts.gfx').write_text(
        chat.replace('gfx/fonts/standard', 'gfx/fonts/unicode-mod-test'), encoding='utf-8')
    archive = base/'archive.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as package:
        for path, content in archive_files.items():
            package.writestr(path, content)
    descriptors = USERDIR/'mod'
    descriptors.mkdir(exist_ok=True)
    (descriptors/'unicode-font-directory.mod').write_text(
        f'name="Unicode Font Directory Test"\npath="{directory.as_posix()}"\nsupported_version="1.37.*"\n', encoding='utf-8')
    (descriptors/'unicode-font-archive.mod').write_text(
        f'name="Unicode Font Archive Test"\narchive="{archive.as_posix()}"\nsupported_version="1.37.*"\n', encoding='utf-8')
    return expectations


SCRIPT = r'''
const base=Process.mainModule.base,patch=Process.getModuleByName('eu4_unicode_patch.dll');
const find=new NativeFunction(patch.base.add(__FIND__),'pointer',['pointer','uint32']);
const lookup=new NativeFunction(base.add(0x16c3f10),'pointer',['pointer','int']);
const resolve=new NativeFunction(base.add(0x19fad70),'pointer',['pointer']);
const targets=__TARGETS__,seen=new Set();let testedScale=false;
function string(s){const n=s.add(16).readU64().toNumber();return (s.add(24).readU64().compare(16)<0?s:s.readPointer()).readUtf8String(n);}
function method(p,index,args){return new NativeFunction(p.readPointer().add(index*8).readPointer(),'int',['pointer',...args],{exceptions:'propagate'});}
function checked(hr){if(hr<0)throw Error('D3D9 readback failed: '+hr);}
function release(p){if(p&&!p.isNull())method(p,2,[])(p);}
function glyphPixels(font,glyph){
 const context=font.add(0x48).readPointer(),manager=context.add(0x480).readPointer();
 const wrapper=lookup(manager,font.add(0x970).readS32());
 if(wrapper.isNull())throw Error('Missing native font texture wrapper');
 const texture=wrapper.readPointer();
 const values=[0,2,4,6].map(n=>glyph.add(n).readS16()),[x,y,w,h]=values;
 const output=Memory.alloc(8);let device=null,surface=null,target=null,system=null;
 try {
  checked(method(texture,3,['pointer'])(texture,output));device=output.readPointer();
  checked(method(texture,18,['uint','pointer'])(texture,0,output));surface=output.readPointer();
  checked(method(device,28,['uint','uint','int','int','uint','int','pointer','pointer'])(device,w,h,21,0,0,0,output,ptr(0)));target=output.readPointer();
  const rect=Memory.alloc(16);[x,y,x+w,y+h].forEach((v,i)=>rect.add(i*4).writeS32(v));
  checked(method(device,34,['pointer','pointer','pointer','pointer','int'])(device,surface,rect,target,ptr(0),0));
  checked(method(device,36,['uint','uint','int','int','pointer','pointer'])(device,w,h,21,2,output,ptr(0)));system=output.readPointer();
  checked(method(device,32,['pointer','pointer'])(device,target,system));
  const lock=Memory.alloc(16);checked(method(system,13,['pointer','pointer','uint'])(system,lock,ptr(0),16));
  let pixels=[];
  try {const pitch=lock.readS32(),data=lock.add(8).readPointer();for(let row=0;row<h;++row)pixels.push(...new Uint8Array(data.add(row*pitch).readByteArray(w*4)));}
  finally {checked(method(system,14,[])(system));}
  return pixels;
 } finally {release(system);release(target);release(surface);release(device);}
}
function record(font,path,event){
 const table=font.add(0x120),glyphs={};
 for(const scalar of targets[path]) {
  const glyph=find(table,scalar);if(glyph.isNull())throw Error('Mod glyph missing: '+scalar);
  glyphs[String(scalar)]={metrics:Array.from(new Uint8Array(glyph.readByteArray(16))),pixels:glyphPixels(font,glyph)};
 }
 const mounts={};
 for(const extension of ['.fnt','.tga','.dds']) {
  const mount=resolve(Memory.allocUtf8String(path+extension));
  mounts[extension]=mount.isNull()?null:mount.add(8).readPointer().readUtf8String();
 }
 send({event,path,dimensions:[font.add(0x978).readS32(),font.add(0x97c).readS32()],glyphs,mounts});
}
Interceptor.attach(base.add(0x15953c0),{onEnter(args){this.font=args[0];},onLeave(){
 try {
  const path=string(this.font.add(0xe0));
  if(targets[path]&&!seen.has(path)){seen.add(path);record(this.font,path,'mod-font');}
  if(!testedScale&&seen.size===Object.keys(targets).length){
   testedScale=true;
   const context=this.font.add(0x48).readPointer(),scale=context.add(0x32600),previous=scale.readFloat();
   const object=Memory.alloc(0x3d50);
   const ctor=new NativeFunction(base.add(0x1594490),'pointer',['pointer','pointer']);
   const assign=new NativeFunction(base.add(0x95110),'pointer',['pointer','pointer','uint64']);
   const load=new NativeFunction(base.add(0x15953c0),'void',['pointer']);
   const destroy=new NativeFunction(base.add(0x15947e0),'void',['pointer']);
   ctor(object,context);
   try {
    for(const [offset,value]of [[0xe0,'gfx/fonts/garamond_16'],[0x100,'gfx/fonts/unicode-mod-test']])assign(object.add(offset),Memory.allocUtf8String(value),value.length);
    scale.writeFloat(1.25);load(object);
    const high=string(object.add(0x100));
    if(high!=='gfx/fonts/unicode-mod-test')throw Error('Enlarged UI mod font was redirected');
    record(object,high,'mod-font-enlarged');
   } finally {scale.writeFloat(previous);destroy(object);}
   send({event:'complete'});
  }
 }catch(error){send({event:'failed',message:String(error)});}
}});
'''


def load_expectations():
    base = ROOT/'private/mod-font-fixtures'
    directory = base/'directory'
    expected = {}
    for name in ['vic_18', 'unicode-mod-test', 'vic_18s']:
        path = f'gfx/fonts/{name}'
        fnt = (RUNTIME if name == 'vic_18s' else directory)/f'{path}.fnt'
        expected[path] = expected_glyphs(fnt.read_text(encoding='utf-8'),
            (directory/f'{path}.dds').read_bytes(), [65] if name == 'vic_18s' else [65, 0x4e2d])
    with zipfile.ZipFile(base/'archive.zip') as archive:
        path = 'gfx/fonts/vic_22'
        expected[path] = expected_glyphs(archive.read(path+'.fnt').decode(),
            archive.read(path+'.dds'), [65, 0x4e2d])
    return expected


def verify_records(records, expected):
    verified = []
    cases = set()
    complete = False
    directory = ROOT/'private/mod-font-fixtures/directory'
    archive = ROOT/'private/mod-font-fixtures/archive.zip'
    for message in copy.deepcopy(records):
        if message.get('type') == 'error' or message.get('payload', {}).get('event') == 'failed':
            raise RuntimeError(json.dumps(message))
        record = message.get('payload', {})
        event, path = record.get('event'), record.get('path')
        if event == 'complete':
            complete = True
        if event not in {'mod-font', 'mod-font-enlarged'}:
            continue
        if path not in expected or set(record['glyphs']) != set(expected[path]):
            raise RuntimeError(f'Unexpected mod glyph capture: {path}')
        for scalar, actual in record['glyphs'].items():
            reference = expected[path][scalar]
            if actual['metrics'] != reference['metrics']:
                raise RuntimeError(f'Mod metrics changed: {path}, {scalar}')
            pixel_hash = hashlib.sha256(bytes(actual.pop('pixels'))).hexdigest()
            if pixel_hash != reference['pixel_sha256']:
                raise RuntimeError(f'Mod GPU pixels changed: {path}, {scalar}')
            actual['pixel_sha256'] = pixel_hash
        source = archive if path.endswith('/vic_22') else directory
        expected_mounts = {'.fnt': source, '.dds': source, '.tga': None}
        if path.endswith('/vic_18s'):
            expected_mounts['.fnt'] = RUNTIME
        elif path.endswith(('/vic_18', '/vic_22')):
            expected_mounts['.tga'] = RUNTIME
        for extension, mount in expected_mounts.items():
            actual = record['mounts'][extension]
            if (mount is None and actual is not None or
                mount is not None and (actual is None or Path(actual).resolve() != mount.resolve())):
                raise RuntimeError(f'Unexpected mounted font source: {path}{extension}')
        case = (event, path)
        if case in cases:
            raise RuntimeError(f'Duplicate mod font capture: {case}')
        cases.add(case)
        verified.append(record)
    required = {('mod-font', path) for path in expected}
    required.add(('mod-font-enlarged', 'gfx/fonts/unicode-mod-test'))
    if not complete or cases != required:
        raise RuntimeError(f'Incomplete mod font verification: {cases}')
    return verified


def write_report(records, expected, dll_hash):
    result = {
        'exe_sha256': EXE_HASH,
        'dll_sha256': dll_hash,
        'method': 'Native font loader, mounted resource selection, UTF-8 glyph lookup and real D3D9 pixel readback; controlled enlarged UI load; owned game closed before observer detach',
        'same_path_override': True, 'custom_path': True, 'archive_override': True,
        'texture_only_override': True, 'enlarged_ui': True, 'exact_metrics_and_pixels': True,
        'records': verify_records(records, expected),
    }
    (ROOT/'tests/evidence/player-runtime-mod-fonts.json').write_text(
        json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8', newline='\n')
    print(f'Verified {len(result["records"])} native mod font captures; exact metrics and GPU pixels retained.')


def main():
    exe = RUNTIME/'eu4.exe'
    if hashlib.sha256(exe.read_bytes()).hexdigest() != EXE_HASH:
        raise RuntimeError('Unexpected owned executable')
    if any(p.info['exe'] and Path(p.info['exe']) == exe for p in psutil.process_iter(['exe'])):
        raise RuntimeError('Close the owned player test first')
    expected = make_fixtures()
    dll_hash = hashlib.sha256((RUNTIME/'plugins/eu4_unicode_patch.dll').read_bytes()).hexdigest()
    mapping = (ROOT/'build/eu4_unicode_patch.map').read_text(encoding='utf-8')
    find = int(re.search(r'\sfind_loaded_glyph\s+([0-9A-Fa-f]+) f', mapping)[1], 16)-0x180000000
    source = SCRIPT.replace('__FIND__', str(find)).replace('__TARGETS__', json.dumps(
        {path: list(map(int, glyphs)) for path, glyphs in expected.items()}))
    settings = USERDIR/'dlc_load.json'
    previous = settings.read_bytes()
    settings.write_text(json.dumps({'disabled_dlcs': [], 'enabled_mods': [
        'mod/unicode-font-directory.mod', 'mod/unicode-font-archive.mod']}), encoding='utf-8')
    process, session, records = None, None, []
    try:
        process = subprocess.Popen([str(exe)], cwd=RUNTIME)
        time.sleep(1)
        session = frida.attach(process.pid)
        script = session.create_script(source)
        script.on('message', lambda message, data: records.append(message))
        script.load()
        deadline = time.monotonic()+100
        while process.poll() is None and time.monotonic() < deadline:
            events = [r.get('payload', {}).get('event') for r in records]
            if 'complete' in events or 'failed' in events or any(r.get('type') == 'error' for r in records):
                break
            time.sleep(.2)
    finally:
        if process and process.poll() is None:
            process.terminate();process.wait(15)
        if session:
            session.detach()
        settings.write_bytes(previous)
    (ROOT/'private/mod-font-observer.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    write_report(records, expected, dll_hash)


if __name__ == '__main__':
    main()
