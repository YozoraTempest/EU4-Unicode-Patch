"""Verify synthetic SDL_TEXTINPUT through the actual game loop and native queue."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import frida
import psutil
from sdl_cases import SDL_CASES

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'private/runtime/eu4.exe'
EXPECTED_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'

SCRIPT = r'''
const base=Process.mainModule.base;
if(new NativeFunction(Process.getModuleByName('eu4_unicode_probe.dll').getExportByName('Eu4UnicodeProbeEnabled'),'int',[])()!==1)
  throw new Error('Unicode patch is disabled');
const cases=__CASES__;
const assign=new NativeFunction(base.add(0x95110),'pointer',['pointer','pointer','uint64']);
const destroy=new NativeFunction(base.add(0x95660),'void',['pointer']);
const select=new NativeFunction(base.add(0x153b170),'void',['pointer']);
function bytes(s) {
  const n=s.add(16).readU64().toNumber(),data=s.add(24).readU64().compare(16)<0?s:s.readPointer();
  if(n>32000)throw new Error('Unexpected native string size');
  return Array.from(new Uint8Array(data.readByteArray(n)));
}
let view=null,widget=null,index=0,windowId=0,originalLimit=null;
let armed=false,waiting=false,pending=false,deliveredAt=0,inserts=[],notifications=[],copies=[];
let selectionBefore=null;
function selectionState(){return {anchor:widget.add(0x92).readU16(),caret:widget.add(0x54).readU16(),
  selected:bytes(widget.add(0x70)),active:widget.add(0x90).readU8()!==0};}
rpc.exports.arm=function(){armed=true;};
function fail(error) {
  armed=false;
  if(widget&&originalLimit!==null)widget.add(0xe0).writeS32(originalLimit);
  send({event:'sdl-validation-failed',message:String(error)});
}
Interceptor.attach(base.add(0x1735400),{onEnter(args){this.event=args[0];},onLeave(result){
  if(this.event.isNull())return;
  if(result.toInt32()!==0){
    const type=this.event.readU32();
    if(type>=0x400&&type<=0x403)windowId=this.event.add(8).readU32();
    return;
  }
  if(!pending||!windowId)return;
  this.event.writeByteArray(new Uint8Array(56));
  this.event.writeU32(0x303);this.event.add(8).writeU32(windowId);
  if(cases[index].payload.length)this.event.add(12).writeByteArray(cases[index].payload);
  result.replace(1);pending=false;deliveredAt=Date.now();
  send({event:'synthetic-sdl-textinput',name:cases[index].name,
    payload:cases[index].payload,window_id:windowId});
}});
Interceptor.attach(base.add(0xefc090),{onEnter(args){
  if(args[0].add(0x9b8).readU32()===1)view=args[0];
}});
// Observe inside the original insert function. Intercepting the entry detour
// would replace the caller's return address, changing the patch's scoped ABI.
Interceptor.attach(base.add(0x1536ba3),{onEnter(args){
  if(waiting&&args[0].equals(widget))inserts.push(bytes(args[1]));
}});
Interceptor.attach(base.add(0xf160d0),{onEnter(args){
  if(waiting&&view&&args[0].equals(view))notifications.push(bytes(widget.add(0x30)));
}});
Interceptor.attach(base.add(0x836f10),{onEnter(args){
  if(waiting&&args[1].add(0x50).readU32()===2&&args[1].add(0x30).readU64().toString(16)==='3154463855503445')
    copies.push(Array.from(new Uint8Array(args[1].add(0x10).readByteArray(32))));
}});
Interceptor.attach(base.add(0x15988e0),{onEnter(){
  if(!armed||!view)return;
  try {
    if(!widget){
      const parent=view.add(0xa0).readPointer();
      const lookup=new NativeFunction(parent.readPointer().add(0xb8).readPointer(),'pointer',['pointer','pointer']);
      const outer=lookup(parent,Memory.allocUtf8String('sort_search_edit'));
      if(outer.isNull())throw new Error('Search widget was not found');
      widget=outer.add(0xc8);originalLimit=widget.add(0xe0).readS32();
      send({event:'native-sdl-budget',original_byte_limit:originalLimit});
    }
    if(waiting){
      if(!deliveredAt||Date.now()-deliveredAt<750)return;
      send({event:'native-sdl-result',...cases[index],after:bytes(widget.add(0x30)),
        after_caret:widget.add(0x54).readU16(),inserts,notifications,queue_copies:copies,
        ...(cases[index].selection?{selection_before:selectionBefore,selection_after:selectionState()}:{} )});
      waiting=false;++index;
    }
    if(index===cases.length){
      widget.add(0xe0).writeS32(originalLimit);armed=false;
      send({event:'sdl-validation-complete',cases:cases.length});return;
    }
    const c=cases[index];
    const storage=Memory.alloc(32);storage.writeByteArray(new Uint8Array(32));storage.add(24).writeU64(15);
    widget.add(0xe0).writeS32(80);
    assign(storage,Memory.allocUtf8String(c.before),unescape(encodeURIComponent(c.before)).length);
    const set=new NativeFunction(widget.readPointer().add(0x80).readPointer(),'void',['pointer','pointer']);
    try {set(widget,storage);} finally {destroy(storage);}
    if(JSON.stringify(bytes(widget.add(0x30)))!==JSON.stringify(Array.from(unescape(encodeURIComponent(c.before)),ch=>ch.charCodeAt(0))))
      throw new Error('Initial native fixture text differs');
    if(widget.add(0x56).readU16()!==0||widget.add(0x80).readU64().compare(0)!==0)
      throw new Error('Widget is not a single unselected line');
    widget.add(0x54).writeU16(c.caret);widget.add(0xe0).writeS32(c.byte_limit);
    widget.add(0x90).writeU8(0);
    if(c.selection){
      widget.add(0x92).writeU16(c.selection.anchor);widget.add(0x94).writeU16(0);
      widget.add(0x54).writeU16(c.selection.caret);select(widget);widget.add(0x90).writeU8(1);
      selectionBefore=selectionState();
    }
    inserts=[];notifications=[];copies=[];deliveredAt=0;waiting=true;pending=true;
  } catch(error){fail(error);}
}});
send({event:'ready',pid:Process.id});
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration', type=int, default=600)
    args = parser.parse_args()
    if not 60 <= args.duration <= 1800:
        raise ValueError('Duration must be 60 to 1800 seconds')
    if hashlib.sha256(EXE.read_bytes()).hexdigest() != EXPECTED_HASH:
        raise ValueError('Unexpected isolated executable')
    if 'unicode_input=1' not in (EXE.parent / 'plugins/eu4_unicode_probe.ini').read_text():
        raise ValueError('Start the isolated game with -ExperimentalInput first')
    processes = [p for p in psutil.process_iter(['exe']) if p.info['exe'] and Path(p.info['exe']) == EXE]
    if len(processes) != 1:
        raise RuntimeError('Start exactly one isolated instance first')
    process = processes[0]
    session = frida.attach(process.pid)
    complete, errors = [], []
    arm = ROOT / 'private/sdl-input-arm.txt'
    arm.unlink(missing_ok=True)
    try:
        script = session.create_script(SCRIPT.replace('__CASES__', json.dumps(SDL_CASES)))
        with (ROOT / 'private/native-sdl-input.jsonl').open('w', encoding='utf-8') as out:
            out.write(json.dumps(dict(event='artifact', exe_sha256=EXPECTED_HASH,
                dll_sha256=hashlib.sha256((EXE.parent / 'plugins/eu4_unicode_probe.dll').read_bytes()).hexdigest(),
                method='Synthetic SDL_PollEvent result on the video thread, native queue and focused edit widget; not physical keyboard or IME')) + '\n')

            def message(value, data):
                out.write(json.dumps(value) + '\n')
                out.flush()
                print(json.dumps(value), flush=True)
                event = value.get('payload', {}).get('event')
                if value.get('type') == 'error' or event == 'sdl-validation-failed':
                    errors.append(value)
                if event == 'sdl-validation-complete':
                    complete.append(value)

            script.on('message', message)
            script.load()
            print('Focus the diplomacy search box, then create private/sdl-input-arm.txt.', flush=True)
            deadline = time.monotonic() + args.duration
            while not arm.exists() and process.is_running() and not errors and time.monotonic() < deadline:
                time.sleep(.25)
            if not arm.exists() or errors:
                raise RuntimeError('SDL validation was not armed')
            script.exports_sync.arm()
            deadline = time.monotonic() + 60
            while process.is_running() and not complete and not errors and time.monotonic() < deadline:
                time.sleep(.25)
        if errors or not complete:
            raise RuntimeError('SDL validation did not complete')
    finally:
        if process.is_running():
            process.terminate()
            process.wait(timeout=10)
        session.detach()


if __name__ == '__main__':
    main()
