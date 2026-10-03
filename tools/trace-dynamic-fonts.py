"""Observe dynamic glyph metrics and GPU readback after controlled SDL commits."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import time
import frida
import psutil

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT/'private/runtime/eu4.exe'
EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'
SAMPLES = ['中华人民共和国', '孔雀翡翠', '𠮷𰀀𲎰']

GPU_SCRIPT = r'''
const dynCases=__TEXTS__,dynSeen=new Set();
function dynMethod(p,index,types){return new NativeFunction(p.readPointer().add(index*8).readPointer(),'int',['pointer',...types]);}
function dynRelease(p){if(!p.isNull())dynMethod(p,2,[])(p);}
function dynString(p){return String.fromCharCode(...bytes(p));}
const dynFind=new NativeFunction(Process.getModuleByName('eu4_unicode_probe.dll').base.add(__FIND__),'pointer',['pointer','uint32']);
Interceptor.attach(base.add(0x16d4e90),{onEnter(args){
 if(!widget)return;
 const sample=dynCases.find(s=>JSON.stringify(s.payload)===JSON.stringify(bytes(widget.add(0x30))));
 if(!sample||dynSeen.has(sample.text))return;
 const active={sample,font:widget.add(0x98).readPointer()};
 const path=dynString(active.font.add(0xe0));if(path!=='gfx/fonts/zh-hans-16')return;
 try {
 const device=args[0].readPointer().add(8).readPointer(),textureOut=Memory.alloc(8);textureOut.writePointer(ptr(0));
 let texture=ptr(0),surface=ptr(0);const glyphs=[];
 const status=dynMethod(device,64,['uint','pointer'])(device,0,textureOut);if(status<0)throw Error('GetTexture '+status);
 texture=textureOut.readPointer();if(texture.isNull())throw Error('Font texture is null');
 try {
 const manager=active.font.add(0x48).readPointer().add(0x480).readPointer(),id=active.font.add(0x970).readS32();
 const expected=manager.add(8).readPointer().add(id*72).readPointer().readPointer();
 if(!texture.equals(expected))return;
 const desc=Memory.alloc(32);const hr=dynMethod(texture,17,['uint','pointer'])(texture,0,desc);if(hr<0)throw Error('GetLevelDesc '+hr);
 const surfaceOut=Memory.alloc(8);surfaceOut.writePointer(ptr(0));
 if(dynMethod(texture,18,['uint','pointer'])(texture,0,surfaceOut)<0)throw Error('GetSurfaceLevel');surface=surfaceOut.readPointer();
 const table=active.font.add(0x120);
 for(const scalar of active.sample.scalars){
   const glyph=dynFind(table,scalar);if(glyph.isNull())throw Error('Missing glyph '+scalar);
   const metrics=Array.from({length:7},(_,i)=>glyph.add(i*2).readS16());const [x,y,width,height]=metrics;
   let target=ptr(0),readback=ptr(0);const operations={};
   try {
    const targetOut=Memory.alloc(8);targetOut.writePointer(ptr(0));
    operations.create_target=dynMethod(device,28,['uint','uint','int','int','uint','int','pointer','pointer'])(device,width,height,21,0,0,0,targetOut,ptr(0));
    if(operations.create_target<0)throw Error('CreateRenderTarget');target=targetOut.readPointer();
    const sourceRect=Memory.alloc(16);[x,y,x+width,y+height].forEach((v,i)=>sourceRect.add(i*4).writeS32(v));
    operations.stretch=dynMethod(device,34,['pointer','pointer','pointer','pointer','int'])(device,surface,sourceRect,target,ptr(0),0);
    if(operations.stretch<0)throw Error('StretchRect');
    const readOut=Memory.alloc(8);readOut.writePointer(ptr(0));
    operations.create_readback=dynMethod(device,36,['uint','uint','int','int','pointer','pointer'])(device,width,height,21,2,readOut,ptr(0));
    if(operations.create_readback<0)throw Error('CreateOffscreenPlainSurface');readback=readOut.readPointer();
    operations.readback=dynMethod(device,32,['pointer','pointer'])(device,target,readback);if(operations.readback<0)throw Error('GetRenderTargetData');
    const pixels=Memory.alloc(16);operations.lock=dynMethod(readback,13,['pointer','pointer','uint'])(readback,pixels,ptr(0),16);if(operations.lock<0)throw Error('LockRect');
    let alpha=[];try{const pitch=pixels.readS32(),p=pixels.add(8).readPointer();for(let iy=0;iy<height;++iy)for(let ix=0;ix<width;++ix)alpha.push(p.add(iy*pitch+ix*4+3).readU8());}
    finally{operations.unlock=dynMethod(readback,14,[])(readback);}
    glyphs.push({scalar,metrics,alpha,operations,pointer:glyph.toString(),ellipsis_same:glyph.equals(dynFind(table,0x2026))});
   } finally {dynRelease(readback);dynRelease(target);}
 }
 send({event:'dynamic-font-gpu',text:active.sample.text,font:path,draw_return:this.returnAddress.sub(base).toString(),dimensions:[desc.add(24).readU32(),desc.add(28).readU32()],format:desc.readU32(),pool:desc.add(12).readU32(),glyphs});
 dynSeen.add(active.sample.text);
 } finally {dynRelease(surface);dynRelease(texture);}
 }catch(error){send({event:'dynamic-font-failed',message:String(error)});}
}});
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration', type=int, default=600)
    parser.add_argument('--readback-only', action='store_true', help='Read all uploaded sample regions from the currently focused font; no SDL injection')
    args = parser.parse_args()
    if not 60 <= args.duration <= 1800:
        raise ValueError('Duration must be 60 to 1800 seconds')
    if hashlib.sha256(EXE.read_bytes()).hexdigest() != EXE_HASH:
        raise ValueError('Unexpected isolated executable')
    if 'unicode_input=1' not in (EXE.parent/'plugins/eu4_unicode_probe.ini').read_text():
        raise ValueError('Enable the input experiment in the isolated game')
    dll_hash = hashlib.sha256((EXE.parent/'plugins/eu4_unicode_probe.dll').read_bytes()).hexdigest()
    if dll_hash != hashlib.sha256((ROOT/'build/eu4_unicode_probe.dll').read_bytes()).hexdigest():
        raise ValueError('Isolated DLL differs from the current build and symbol map')
    processes = [p for p in psutil.process_iter(['exe']) if p.info['exe'] and Path(p.info['exe']) == EXE]
    if len(processes) != 1:
        raise ValueError('Expected one isolated game')
    process = processes[0]
    spec = importlib.util.spec_from_file_location('sdl_trace', ROOT/'tools/trace-sdl-input.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    cases = [dict(name='dynamic-'+str(index), before='', caret=0, byte_limit=250,
                  payload=list(text.encode()), expected=text) for index, text in enumerate(SAMPLES)]
    mapping = (ROOT/'build/eu4_unicode_probe.map').read_text()
    match = re.search(r'\sfind_loaded_glyph\s+([0-9A-Fa-f]+) f', mapping)
    if not match:
        raise ValueError('Build map lacks glyph lookup')
    find = int(match[1], 16)-0x180000000
    gpu = GPU_SCRIPT.replace('__FIND__', str(find)).replace('__TEXTS__', json.dumps([
        dict(text=text, payload=list(text.encode()), scalars=sorted(set(map(ord, ''.join(SAMPLES) if args.readback_only else text)))) for text in SAMPLES]))
    focus_guard = """
    const dynOuter=widget.sub(0xc8),dynManager=base.add(0x23494f0).readPointer();
    if(dynManager.isNull()||dynOuter.add(0x260).readU8()!==1||
       !dynManager.add(0x210).readPointer().equals(dynOuter.add(0x1d0)))return;
    """
    script_source = module.SCRIPT.replace('__CASES__', json.dumps(cases)).replace('    if(waiting){', focus_guard+'    if(waiting){')+gpu
    if args.readback_only:
        script_source = r'''
const base=Process.mainModule.base;
if(new NativeFunction(Process.getModuleByName('eu4_unicode_probe.dll').getExportByName('Eu4UnicodeProbeEnabled'),'int',[])()!==1)throw Error('Patch disabled');
const manager=base.add(0x23494f0).readPointer(),callback=manager.add(0x210).readPointer();
if(callback.isNull())throw Error('No focused native editor');
const outer=callback.sub(0x1d0),widget=outer.add(0xc8);
if(outer.add(0x260).readU8()!==1)throw Error('Native editor is not focused');
function bytes(s){const n=s.add(16).readU64().toNumber();if(n>32000)throw Error('Invalid string');return Array.from(new Uint8Array((s.add(24).readU64().compare(16)<0?s:s.readPointer()).readByteArray(n)));}
send({event:'ready'});
''' + gpu
    arm, finish = ROOT/'private/dynamic-font-arm.txt', ROOT/'private/dynamic-font-finish.txt'
    if not args.readback_only:
        arm.unlink(missing_ok=True);finish.unlink(missing_ok=True)
    session = frida.attach(process.pid)
    errors, complete = [], []
    try:
        script = session.create_script(script_source)
        output='private/native-font-readback.jsonl' if args.readback_only else 'private/native-dynamic-fonts.jsonl'
        with (ROOT/output).open('w', encoding='utf-8') as out:
            out.write(json.dumps(dict(event='artifact', exe_sha256=EXE_HASH,
                dll_sha256=dll_hash, samples=SAMPLES,
                method=('Native GUI texture readback of already-uploaded glyphs; no SDL injection; not physical IME' if args.readback_only else
                        'Controlled SDL commits; actual native glyph lookup and native GUI draw; Direct3D texture region readback; not physical IME')))+'\n')
            def message(value, data):
                out.write(json.dumps(value)+'\n');out.flush()
                event = value.get('payload', {}).get('event')
                if value.get('type') == 'error' or event in ('sdl-validation-failed', 'dynamic-font-failed'):
                    errors.append(value)
                if event == 'sdl-validation-complete':
                    complete.append(value)
                if args.readback_only and event == 'dynamic-font-gpu':
                    complete.append(value)
                if event == 'native-sdl-result' and value['payload']['after'] != list(value['payload']['expected'].encode()):
                    errors.append(value)
                if event != 'dynamic-font-gpu':
                    print(json.dumps(value), flush=True)
                else:
                    print(json.dumps(dict(event=event, text=value['payload']['text'], glyphs=len(value['payload']['glyphs']))), flush=True)
            script.on('message', message);script.load()
            deadline = time.monotonic()+args.duration
            while not args.readback_only and process.is_running() and not arm.exists() and not errors and time.monotonic() < deadline:
                time.sleep(.25)
            if not args.readback_only and (not arm.exists() or errors):
                raise RuntimeError('Dynamic font probe was not armed')
            if not args.readback_only:
                script.exports_sync.arm()
            while process.is_running() and not finish.exists() and not errors and time.monotonic() < deadline:
                time.sleep(.25)
            if errors or not complete:
                raise RuntimeError('Dynamic font validation failed')
    finally:
        if process.is_running():
            process.terminate();process.wait(10)
        session.detach()


if __name__ == '__main__':
    main()
