"""Observe real SDL input rectangles from native focused-editor painting."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import frida
import psutil

from ime_rect_cases import IME_RECT_CASES

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'private/runtime/eu4.exe'
EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'
SCRIPT = r'''
const base=Process.mainModule.base,patch=Process.getModuleByName('eu4_unicode_probe.dll');
if(new NativeFunction(patch.getExportByName('Eu4UnicodeProbeEnabled'),'int',[])()!==1)throw new Error('Patch disabled');
const options={exceptions:'propagate'};
const assign=new NativeFunction(base.add(0x95110),'pointer',['pointer','pointer','uint64'],options);
const destroy=new NativeFunction(base.add(0x95660),'void',['pointer'],options);
const blur=new NativeFunction(base.add(0x15353f0),'void',['pointer'],options);
const getWindow=new NativeFunction(base.add(0x17345f0),'pointer',[],options);
const getSize=new NativeFunction(base.add(0x17349f0),'void',['pointer','pointer','pointer'],options);
const version=Memory.alloc(3);new NativeFunction(Process.mainModule.getExportByName('SDL_GetVersion'),'void',['pointer'])(version);
send({event:'sdl-version',version:Array.from(new Uint8Array(version.readByteArray(3)))});
let view=null,outer=null,widget=null,armed=false,waiting=false,index=0,startedAt=0,afterBlur=false,awaitingRefocus=false,origin=null,lastRect=null,calls=[];
const cases=__CASES__;
function bytes(s){const n=s.add(16).readU64().toNumber(),p=s.add(24).readU64().compare(16)<0?s:s.readPointer();
 if(n>32000)throw new Error('Invalid native text length');return Array.from(new Uint8Array(p.readByteArray(n)));}
function focused(editor=outer){const manager=base.add(0x23494f0).readPointer();return !manager.isNull()&&editor.add(0x260).readU8()===1&&manager.add(0x210).readPointer().equals(editor.add(0x1d0));}
function source(editor=outer){
 const nativeWidget=editor.add(0xc8),sprite=editor.add(0x1f0).readPointer(),p=Memory.alloc(8);
 new NativeFunction(sprite.readPointer().add(0x178).readPointer(),'pointer',['pointer','pointer'],options)(sprite,p);
 const font=nativeWidget.add(0x98).readPointer(),height=new NativeFunction(font.readPointer().add(0x68).readPointer(),'int',['pointer'],options)(font);
 const prefix=bytes(nativeWidget.add(0x30)).slice(0,nativeWidget.add(0x54).readU16()),storage=Memory.alloc(prefix.length+1);storage.writeByteArray(prefix.concat([0]));
 const advance=new NativeFunction(font.readPointer().add(0x60).readPointer(),'int',['pointer','pointer','int','uint8'],options)(font,storage,prefix.length,nativeWidget.add(0xe4).readU8());
 const window=getWindow(),size=Memory.alloc(8);if(window.isNull())throw new Error('SDL window lost focus');getSize(window,size,size.add(4));
 return {sprite:[p.readFloat(),p.add(4).readFloat()],font_height:height,advance,window_size:[size.readS32(),size.add(4).readS32()],
  gui_scale:font.add(0x48).readPointer().add(0x32600).readFloat(),text:bytes(nativeWidget.add(0x30)),caret:nativeWidget.add(0x54).readU16(),row:nativeWidget.add(0x56).readU16(),focused:focused(editor)};
}
Interceptor.attach(base.add(0x1735940),{onEnter(args){
 const caller=Process.findModuleByAddress(this.returnAddress);
 if(!caller||caller.name!=='eu4_unicode_probe.dll')throw new Error('Unexpected SDL rectangle caller');
 this.rect=[0,4,8,12].map(n=>args[0].add(n).readS32());
 const callback=base.add(0x23494f0).readPointer().add(0x210).readPointer();
 if(callback.isNull())throw new Error('Input rectangle has no focused native owner');
 const owner=callback.sub(0x1d0);
 this.source=source(owner);this.owner=owner.toString();
},onLeave(){
 lastRect=this.rect;
 send({event:'sdl-ime-rect',rect:lastRect.slice(),caller:'eu4_unicode_probe.dll',thread:Process.getCurrentThreadId(),owner:this.owner,source:this.source});
 if(outer)calls.push({rect:lastRect.slice(),focused:focused()});
}});
Interceptor.attach(base.add(0xefc090),{onEnter(args){if(args[0].add(0x9b8).readU32()===1)view=args[0];}});
rpc.exports={arm(){armed=true;}};
Interceptor.attach(base.add(0x15988e0),{onEnter(){
 if(!armed||!view)return;
 try{
  if(!outer){const parent=view.add(0xa0).readPointer(),lookup=new NativeFunction(parent.readPointer().add(0xb8).readPointer(),'pointer',['pointer','pointer'],options);
   outer=lookup(parent,Memory.allocUtf8String('sort_search_edit'));if(outer.isNull())throw new Error('Search editor missing');widget=outer.add(0xc8);
   if(!outer.readPointer().equals(base.add(0x1d91358))||!focused())throw new Error('Unexpected editor or missing native focus');
   send({event:'ime-rect-contract',owner:outer.toString(),outer_vtable:'0x1d91358',byte_limit:widget.add(0xe0).readS32(),font_flags:widget.add(0xe4).readU8()});
  }
  if(awaitingRefocus){
   if(!focused()){startedAt=Date.now();return;}
   if(Date.now()-startedAt<750)return;
   send({event:'ime-rect-refocused',source:source(),rect:lastRect,calls});armed=false;
   send({event:'ime-rect-complete',cases:cases.length});return;
  }
  if(waiting){
   if(Date.now()-startedAt<750)return;
   const actual=source();
   if(afterBlur){send({event:'ime-rect-unfocused',source:actual,calls});calls=[];awaitingRefocus=true;startedAt=Date.now();send({event:'ime-rect-awaiting-refocus'});return;}
   if(index===0)origin=actual.sprite;
   send({event:'ime-rect-case',...cases[index],source:actual,origin,rect:lastRect,calls});
   ++index;waiting=false;
  }
  calls=[];startedAt=Date.now();
  if(index===cases.length){blur(outer);afterBlur=true;waiting=true;return;}
  const c=cases[index],value=Memory.alloc(32);value.writeByteArray(new Uint8Array(32));value.add(24).writeU64(15);
  assign(value,Memory.allocUtf8String(c.text),unescape(encodeURIComponent(c.text)).length);
  const set=new NativeFunction(widget.readPointer().add(0x80).readPointer(),'void',['pointer','pointer'],options);
  try{set(widget,value);}finally{destroy(value);}
  widget.add(0x54).writeU16(c.caret);widget.add(0x56).writeU16(0);widget.add(0x90).writeU8(0);widget.add(0xaa).writeU16(0xffff);
  waiting=true;
 }catch(e){armed=false;send({event:'ime-rect-failed',message:String(e)});}
}});
send({event:'ready'});
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration', type=int, default=600)
    args = parser.parse_args()
    if not 60 <= args.duration <= 1800:
        raise ValueError('Duration must be 60 to 1800 seconds')
    if hashlib.sha256(EXE.read_bytes()).hexdigest() != EXE_HASH:
        raise ValueError('Unexpected isolated executable')
    if 'unicode_input=1' not in (EXE.parent / 'plugins/eu4_unicode_probe.ini').read_text():
        raise ValueError('Start the isolated instance with -ExperimentalInput')
    processes = [p for p in psutil.process_iter(['exe']) if p.info['exe'] and Path(p.info['exe']) == EXE]
    if len(processes) != 1:
        raise RuntimeError('Start exactly one isolated instance')
    process = processes[0]
    session = frida.attach(process.pid)
    complete, errors = [], []
    arm = ROOT / 'private/ime-rect-arm.txt'
    arm.unlink(missing_ok=True)
    try:
        script = session.create_script(SCRIPT.replace('__CASES__', json.dumps(IME_RECT_CASES)))
        with (ROOT / 'private/native-ime-rect.jsonl').open('w', encoding='utf-8') as out:
            out.write(json.dumps(dict(event='artifact', exe_sha256=EXE_HASH,
                dll_sha256=hashlib.sha256((EXE.parent / 'plugins/eu4_unicode_probe.dll').read_bytes()).hexdigest(),
                method='Native editor fixtures, natural UI painting and unmodified SDL_SetTextInputRect; no synthetic IME or physical candidate-window claim'))+'\n')
            def message(value, data):
                out.write(json.dumps(value)+'\n');out.flush();print(json.dumps(value), flush=True)
                event = value.get('payload', {}).get('event')
                if value.get('type') == 'error' or event == 'ime-rect-failed':
                    errors.append(value)
                if event == 'ime-rect-complete':
                    complete.append(value)
            script.on('message', message);script.load()
            deadline = time.monotonic()+args.duration
            while not arm.exists() and process.is_running() and not errors and time.monotonic() < deadline:
                time.sleep(.25)
            if not arm.exists() or errors:
                raise RuntimeError('IME rectangle validation was not armed')
            script.exports_sync.arm()
            while process.is_running() and not complete and not errors and time.monotonic() < deadline:
                time.sleep(.25)
        if errors or not complete:
            raise RuntimeError('IME rectangle probe did not complete')
    finally:
        if process.is_running():
            process.terminate();process.wait(timeout=10)
        session.detach()


if __name__ == '__main__':
    main()
