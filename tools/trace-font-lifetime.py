"""Verify native font construction, load, destruction and atlas identity reuse."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time

import frida
import psutil

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'private/runtime/eu4.exe'
EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'
FONT_NAMES = ['zh-hans-14', 'zh-hans-16', 'zh-hans-18', 'zh-hans-24', 'zh-hans-map']
CYCLES = 64


def font_fixtures():
    result = []
    for name in FONT_NAMES:
        path = ROOT / 'private/test-mod/gfx/fonts' / (name+'.fnt')
        characters = {}
        for line in path.read_text(encoding='utf-8').splitlines():
            if line.startswith('char '):
                values = {key: int(value) for key, value in re.findall(r'(\w+)=(-?\d+)', line)}
                if values['id'] in characters:
                    raise ValueError('Duplicate fixture glyph')
                characters[values['id']] = values
        metrics = {}
        for scalar in [0x4e2d, 0x1f600, 0x20000]:
            c = characters[scalar]
            metrics[str(scalar)] = list(struct.pack('<7hBB', *(c[key] for key in
                ['x', 'y', 'width', 'height', 'xoffset', 'yoffset', 'xadvance']), 0, 0))
        result.append(dict(name='gfx/fonts/'+name, fnt_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                           unicode_records=sum(scalar > 255 for scalar in characters), metrics=metrics))
    return result


SCRIPT = r'''
const base=Process.mainModule.base,patch=Process.getModuleByName('eu4_unicode_probe.dll');
if(new NativeFunction(patch.getExportByName('Eu4UnicodeProbeEnabled'),'int',[])()!==1)throw new Error('Patch disabled');
const options={exceptions:'propagate'};
const ctor=new NativeFunction(base.add(0x1594490),'pointer',['pointer','pointer'],options);
const load=new NativeFunction(base.add(0x15953c0),'void',['pointer'],options);
const destroy=new NativeFunction(base.add(0x15947e0),'void',['pointer'],options);
const assign=new NativeFunction(base.add(0x95110),'pointer',['pointer','pointer','uint64'],options);
const find=new NativeFunction(patch.base.add(__FIND__),'pointer',['pointer','uint32'],options);
const fontCount=new NativeFunction(patch.getExportByName('Eu4UnicodeProbeGlyphFonts'),'uint64',[]);
const glyphCount=new NativeFunction(patch.getExportByName('Eu4UnicodeProbeGlyphRecords'),'uint64',[]);
const fixtures=__FIXTURES__,cycles=__CYCLES__;
function usage(){return {fonts:fontCount().toNumber(),glyphs:glyphCount().toNumber()};}
function string(s){const n=s.add(16).readU64().toNumber(),d=s.add(24).readU64().compare(16)<0?s:s.readPointer();return d.readUtf8String(n);}
function metrics(g){return g.isNull()?[]:Array.from(new Uint8Array(g.readByteArray(16)));}
let source=null,ran=false;
Interceptor.attach(base.add(0x1595570),{onEnter(args){this.font=args[0];},onLeave(){
 if(!ran&&string(this.font.add(0xe0))==='gfx/fonts/zh-hans-18')source=this.font;
}});
Interceptor.attach(base.add(0x15988e0),{onEnter(){
 if(!source||ran)return;ran=true;
 try {
   const sourceTable=source.add(0x120),sourceGlyph=find(sourceTable,0x20000),sourceMetrics=metrics(sourceGlyph);
   if(sourceGlyph.isNull())throw new Error('Source font lacks U+20000');
   const context=source.add(0x48).readPointer(),font=Memory.alloc(0x3d50),alias=Memory.alloc(0x800),baseline=usage();
   send({event:'font-lifetime-baseline',usage:baseline,source_metrics:sourceMetrics});
   for(let cycle=0;cycle<cycles;++cycle){
     const fixture=fixtures[cycle%fixtures.length];
     ctor(font,context);
     assign(font.add(0xe0),Memory.allocUtf8String(fixture.name),fixture.name.length);
     load(font);
     const table=font.add(0x120),anchor=table.add(0x41*8).readPointer(),loadedUsage=usage(),actualMetrics={},aliasSame={};
     if(anchor.isNull())throw new Error('Native font did not load ASCII A');
     alias.writeByteArray(table.readByteArray(0x800));
     for(const scalar of Object.keys(fixture.metrics)) {
       const glyph=find(table,Number(scalar));actualMetrics[scalar]=metrics(glyph);
       aliasSame[scalar]=!glyph.isNull()&&find(alias,Number(scalar)).equals(glyph);
     }
     const texture=font.add(0x970).readS32();
     destroy(font);
     const cleared={};for(const scalar of Object.keys(fixture.metrics))cleared[scalar]=find(alias,Number(scalar)).isNull();
     send({event:'native-font-lifecycle',cycle,name:fixture.name,font:font.toString(),anchor:anchor.toString(),texture,
       metrics:actualMetrics,alias_same:aliasSame,after_destroy_missing:cleared,loaded_usage:loadedUsage,after_usage:usage(),
       source_unchanged:find(sourceTable,0x20000).equals(sourceGlyph)&&JSON.stringify(metrics(sourceGlyph))===JSON.stringify(sourceMetrics)});
   }
   send({event:'font-lifetime-complete',cycles,final_usage:usage()});
 } catch(e){send({event:'font-lifetime-failed',message:String(e)});}
}});
send({event:'ready'});
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration', type=int, default=240)
    args = parser.parse_args()
    if not 60 <= args.duration <= 600:
        raise ValueError('Duration must be 60 to 600 seconds')
    if hashlib.sha256(EXE.read_bytes()).hexdigest() != EXE_HASH:
        raise ValueError('Unexpected isolated executable')
    if any(p.info['exe'] and Path(p.info['exe']) == EXE for p in psutil.process_iter(['exe'])):
        raise RuntimeError('Close the existing isolated instance')
    plugin = EXE.parent / 'plugins/eu4_unicode_probe.dll'
    if (plugin.parent / 'plugin64.dll').exists():
        raise RuntimeError('Legacy plugin is present')
    shutil.copy2(ROOT / 'build/eu4_unicode_probe.dll', plugin)
    (plugin.parent / 'eu4_unicode_probe.ini').write_text('[experimental]\nunicode_input=0\n', encoding='ascii')
    mapping = (ROOT / 'build/eu4_unicode_probe.map').read_text()
    match = re.search(r'\sfind_loaded_glyph\s+([0-9A-Fa-f]+) f', mapping)
    if match is None:
        raise ValueError('Current build map lacks the glyph lookup symbol')
    find_rva = int(match[1], 16)-0x180000000
    if not 0 < find_rva < plugin.stat().st_size:
        raise ValueError('Invalid mapped glyph lookup address')
    fixtures = font_fixtures()
    script_source = SCRIPT.replace('__FIND__', str(find_rva)).replace('__FIXTURES__', json.dumps(fixtures)).replace('__CYCLES__', str(CYCLES))
    process = subprocess.Popen([str(EXE), '-debug'], cwd=str(EXE.parent))
    session = None
    complete, errors = [], []
    try:
        time.sleep(2)
        if process.poll() is not None:
            raise RuntimeError('Game exited during startup')
        session = frida.attach(process.pid)
        script = session.create_script(script_source)
        with (ROOT / 'private/native-font-lifetime.jsonl').open('w', encoding='utf-8') as out:
            out.write(json.dumps(dict(event='artifact', exe_sha256=EXE_HASH,
                dll_sha256=hashlib.sha256(plugin.read_bytes()).hexdigest(), fixtures=fixtures,
                method='Native font constructor, resource/font loader and destructor on UI thread; same font storage reused; copied ASCII table identifies borrowed atlas alias'))+'\n')

            def message(value, data):
                out.write(json.dumps(value)+'\n')
                out.flush()
                event = value.get('payload', {}).get('event')
                if value.get('type') == 'error' or event == 'font-lifetime-failed':
                    errors.append(value)
                if event == 'font-lifetime-complete':
                    complete.append(value)
                if event != 'native-font-lifecycle':
                    print(json.dumps(value), flush=True)

            script.on('message', message)
            script.load()
            deadline = time.monotonic()+args.duration
            while process.poll() is None and not errors and not complete and time.monotonic() < deadline:
                time.sleep(.25)
        if errors or not complete:
            raise RuntimeError('Native font lifetime probe did not complete')
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        if session:
            session.detach()


if __name__ == '__main__':
    main()
