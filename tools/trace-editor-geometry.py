"""Observe native editor pixel primitives with a live font and separate value fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import frida
import psutil
from geometry_cases import GEOMETRY_CASES

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'private/runtime/eu4.exe'
EXPECTED_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'
SCRIPT = r'''
const base=Process.mainModule.base,options={exceptions:'propagate'};
if(new NativeFunction(Process.getModuleByName('eu4_unicode_probe.dll').getExportByName('Eu4UnicodeProbeEnabled'),'int',[])()!==1)
  throw new Error('Unicode patch is disabled');
const assign=new NativeFunction(base.add(0x95110),'pointer',['pointer','pointer','uint64'],options);
const destroy=new NativeFunction(base.add(0x95660),'void',['pointer'],options);
const point=new NativeFunction(base.add(0x15361f0),'void',['pointer','pointer','pointer'],options);
const fit=new NativeFunction(base.add(0x1537210),'int',['pointer','pointer'],options);
const word=new NativeFunction(base.add(0x1539820),'int',['pointer','pointer','int'],options);
const cases=__CASES__;
let view=null,ran=false;
function raw(value){const length=unescape(encodeURIComponent(value)).length;
  return Array.from(new Uint8Array(Memory.allocUtf8String(value).readByteArray(length)));}
function prefix(data,length){const p=Memory.alloc(length+1);p.writeByteArray(data.slice(0,length).concat([0]));return p;}
Interceptor.attach(base.add(0xefc090),{onEnter(args){if(args[0].add(0x9b8).readU32()===1)view=args[0];}});
Interceptor.attach(base.add(0x15988e0),{onEnter(){
  if(!view||ran)return;ran=true;
  let text=null;
  try {
    const parent=view.add(0xa0).readPointer();
    const lookup=new NativeFunction(parent.readPointer().add(0xb8).readPointer(),'pointer',['pointer','pointer'],options);
    const outer=lookup(parent,Memory.allocUtf8String('sort_search_edit'));
    if(outer.isNull())throw new Error('Search widget absent');
    const source=outer.add(0xc8),font=source.add(0x98).readPointer(),flags=source.add(0xe4).readU8();
    const measure=new NativeFunction(font.readPointer().add(0x60).readPointer(),'int',['pointer','pointer','int','uchar'],options);
    const margin=new NativeFunction(font.readPointer().add(0xa8).readPointer(),'int',['pointer','uchar'],options)(font,1);
    // These primitives access the value string, font, width and caret fields;
    // no GUI ownership, row cache, clipboard or source-widget state is borrowed.
    const widget=Memory.alloc(0x228);widget.writeByteArray(new Uint8Array(0x228));
    widget.add(0x98).writePointer(font);widget.add(0xe4).writeU8(flags);widget.add(0x60).writeU16(1);
    text=widget.add(0x30);text.add(24).writeU64(15);
    send({event:'geometry-font',font_margin:margin,flags,measure_rva:String(font.readPointer().add(0x60).readPointer().sub(base))});
    let samples=0;
    for(const c of cases) {
      const data=raw(c.text);assign(text,prefix(data,data.length),data.length);
      const widths=c.boundaries.map(n=>n===0?0:measure(font,prefix(data,n),n,flags));
      if(widths.some((n,i)=>n<0||(i&&n<widths[i-1])))throw new Error('Non-monotonic bitmap widths');
      const hits=[],fits=[],words=[];
      for(let x=-2;x<=widths[widths.length-1]+3;++x) {
        const xy=Memory.alloc(8);xy.writeS32(x);xy.add(4).writeS32(0);
        widget.add(0x54).writeU16(data.length);widget.add(0x56).writeU16(0);
        point(widget,text,xy);hits.push([x,widget.add(0x54).readU16()]);
        if(x>=__MIN_FIT_PIXEL__) {
          widget.add(0x68).writeU16(x+margin);
          const count=fit(widget,text);fits.push([x,count]);words.push([x,word(widget,text,count)]);
        }
        ++samples;
      }
      send({event:'geometry-case',name:c.name,text:data,boundaries:c.boundaries,widths,hits,fits,words});
    }
    destroy(text);text=null;send({event:'geometry-complete',cases:cases.length,samples});
  } catch(e){send({event:'geometry-failed',message:String(e)});}
  finally{if(text)destroy(text);}
}});
send({event:'ready',pid:Process.id});
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration', type=int, default=600)
    parser.add_argument('--output', type=Path, default=ROOT / 'private/native-editor-geometry.jsonl')
    parser.add_argument('--keep-open', action='store_true', help='Keep the observer attached until the owned game exits')
    parser.add_argument('--baseline', action='store_true', help='Skip zero available width in the older native fitter')
    args = parser.parse_args()
    if not 60 <= args.duration <= 1800:
        raise ValueError('Duration must be 60 to 1800 seconds')
    if hashlib.sha256(EXE.read_bytes()).hexdigest() != EXPECTED_HASH:
        raise ValueError('Unexpected isolated executable')
    if 'unicode_input=1' not in (EXE.parent / 'plugins/eu4_unicode_probe.ini').read_text():
        raise ValueError('Start the dedicated instance with -ExperimentalInput')
    processes = [p for p in psutil.process_iter(['exe']) if p.info['exe'] and Path(p.info['exe']) == EXE]
    if len(processes) != 1:
        raise RuntimeError('Start exactly one isolated instance')
    process = processes[0]
    session = frida.attach(process.pid)
    complete, errors = [], []
    try:
        script = session.create_script(SCRIPT.replace('__CASES__', json.dumps(GEOMETRY_CASES)).replace('__MIN_FIT_PIXEL__', '1' if args.baseline else '0'))
        with args.output.open('w', encoding='utf-8') as out:
            out.write(json.dumps(dict(event='artifact', exe_sha256=EXPECTED_HASH,
                dll_sha256=hashlib.sha256((EXE.parent / 'plugins/eu4_unicode_probe.dll').read_bytes()).hexdigest(),
                method='Native point, pixel-fit and word-break functions on UI thread; independent value fixtures with a live GUI font; no physical mouse, IME or GUI row-cache claim'))+'\n')
            def message(value, data):
                out.write(json.dumps(value)+'\n'); out.flush()
                event = value.get('payload', {}).get('event')
                print(json.dumps(value) if event != 'geometry-case' else json.dumps(dict(event=event, name=value['payload']['name'])), flush=True)
                if value.get('type') == 'error' or event == 'geometry-failed':
                    errors.append(value)
                if event == 'geometry-complete':
                    complete.append(value)
            script.on('message', message); script.load()
            deadline = time.monotonic() + args.duration
            while process.is_running() and time.monotonic() < deadline:
                if errors or (not args.keep_open and complete):
                    break
                time.sleep(.25)
        if errors or not complete:
            raise RuntimeError('Geometry probe did not complete')
    finally:
        if process.is_running():
            process.terminate(); process.wait(timeout=10)
        session.detach()


if __name__ == '__main__':
    main()
