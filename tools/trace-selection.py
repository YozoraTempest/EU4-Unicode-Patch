"""Exercise native UTF-8 selection actions and read each complete editor state."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import frida
import psutil
from selection_cases import SELECTION_CASES

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'private/runtime/eu4.exe'
EXPECTED_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'

SCRIPT = r'''
const base=Process.mainModule.base;
if(new NativeFunction(Process.getModuleByName('eu4_unicode_probe.dll').getExportByName('Eu4UnicodeProbeEnabled'),'int',[])()!==1)
  throw new Error('Unicode patch is disabled');
const options={exceptions:'propagate'};
const assign=new NativeFunction(base.add(0x95110),'pointer',['pointer','pointer','uint64'],options);
const destroy=new NativeFunction(base.add(0x95660),'void',['pointer'],options);
const key=new NativeFunction(base.add(0x15366c0),'bool',['pointer','pointer'],options);
const select=new NativeFunction(base.add(0x153b170),'void',['pointer'],options);
const cases=__CASES__;
let view=null,widget=null,refreshes=0,ran=false,notices=[],observing=false;
function bytes(s) {
  const n=s.add(16).readU64().toNumber(),d=s.add(24).readU64().compare(16)<0?s:s.readPointer();
  if(n>32000)throw new Error('Unexpected native string size');
  return Array.from(new Uint8Array(d.readByteArray(n)));
}
function state() {
  return {text:bytes(widget.add(0x30)),caret:widget.add(0x54).readU16(),row:widget.add(0x56).readU16(),
    anchor:widget.add(0x92).readU16(),anchor_row:widget.add(0x94).readU16(),
    selected:bytes(widget.add(0x70)),active:widget.add(0x90).readU8()!==0};
}
function apply(handler,value) {
  const s=Memory.alloc(32);s.writeByteArray(new Uint8Array(32));s.add(24).writeU64(15);
  assign(s,Memory.allocUtf8String(value),unescape(encodeURIComponent(value)).length);
  try {handler(widget,s);} finally {destroy(s);}
}
Interceptor.attach(base.add(0xefc090),{onEnter(args){
  if(args[0].add(0x9b8).readU32()===1){view=args[0];++refreshes;}
}});
Interceptor.attach(base.add(0xf160d0),{onEnter(args){
  if(observing&&args[0].equals(view))notices.push(bytes(widget.add(0x30)));
}});
Interceptor.attach(base.add(0x15988e0),{onEnter(){
  if(!view||refreshes<2||ran)return;ran=true;
  let originalLimit=null;
  try {
    const parent=view.add(0xa0).readPointer();
    const lookup=new NativeFunction(parent.readPointer().add(0xb8).readPointer(),'pointer',['pointer','pointer'],options);
    const outer=lookup(parent,Memory.allocUtf8String('sort_search_edit'));
    if(outer.isNull())throw new Error('Native search widget is absent');
    widget=outer.add(0xc8);originalLimit=widget.add(0xe0).readS32();widget.add(0xe0).writeS32(80);
    const vtable=widget.readPointer();
    const set=new NativeFunction(vtable.add(0x80).readPointer(),'void',['pointer','pointer'],options);
    const insert=new NativeFunction(vtable.add(0xa0).readPointer(),'void',['pointer','pointer'],options);
    const left=new NativeFunction(vtable.add(0xd0).readPointer(),'void',['pointer'],options);
    send({event:'selection-budget',original_byte_limit:originalLimit,test_byte_limit:80});
    for(const c of cases) {
      observing=false;apply(set,c.before);
      if(widget.add(0x56).readU16()!==0||widget.add(0x60).readU16()>1||bytes(widget.add(0x70)).length)
        throw new Error('Fixture is not an unselected single line');
      widget.add(0x54).writeU16(c.caret);widget.add(0x90).writeU8(0);
      widget.add(0x92).writeU16(0);widget.add(0x94).writeU16(0);
      send({event:'selection-fixture',name:c.name,before:state()});
      for(let step=0;step<c.actions.length;++step) {
        const action=c.actions[step];notices=[];observing=true;
        let handled=null;
        if(action.kind==='key') {
          const e=Memory.alloc(12);e.writeByteArray(new Uint8Array(12));e.writeU32(action.code);e.add(8).writeU32(action.modifiers);
          handled=key(widget,e);
        } else if(action.kind==='insert') apply(insert,action.text);
        else if(action.kind==='select') {
          widget.add(0x92).writeU16(action.anchor);widget.add(0x54).writeU16(action.caret);
          select(widget);widget.add(0x90).writeU8(1);
        } else if(action.kind==='native_select_left') left(widget);
        else throw new Error('Unknown selection action');
        observing=false;
        send({event:'selection-step',name:c.name,step,action,after:state(),handled,notifications:notices});
      }
    }
    widget.add(0xe0).writeS32(originalLimit);
    send({event:'selection-validation-complete',cases:cases.length,steps:cases.reduce((n,c)=>n+c.actions.length,0)});
  } catch(e){observing=false;send({event:'selection-validation-failed',message:String(e)});}
  finally {if(widget&&originalLimit!==null)widget.add(0xe0).writeS32(originalLimit);}
}});
send({event:'ready',pid:Process.id});
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration', type=int, default=600)
    parser.add_argument('--keep-open', action='store_true', help='Keep the observer until the owned game exits')
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
        script = session.create_script(SCRIPT.replace('__CASES__', json.dumps(SELECTION_CASES)))
        with (ROOT / 'private/native-selection.jsonl').open('w', encoding='utf-8') as out:
            out.write(json.dumps(dict(event='artifact', exe_sha256=EXPECTED_HASH,
                dll_sha256=hashlib.sha256((EXE.parent / 'plugins/eu4_unicode_probe.dll').read_bytes()).hexdigest(),
                method='Native key, selection and insertion functions on UI thread; not physical keyboard, mouse or IME'))+'\n')

            def message(value, data):
                out.write(json.dumps(value)+'\n')
                out.flush()
                print(json.dumps(value), flush=True)
                event = value.get('payload', {}).get('event')
                if value.get('type') == 'error' or event == 'selection-validation-failed':
                    errors.append(value)
                if event == 'selection-validation-complete':
                    complete.append(value)

            script.on('message', message)
            script.load()
            deadline = time.monotonic() + args.duration
            while process.is_running() and time.monotonic() < deadline:
                if errors or (not args.keep_open and complete):
                    break
                time.sleep(.25)
        if errors or not complete:
            raise RuntimeError('Selection probe did not complete')
    finally:
        if process.is_running():
            process.terminate()
            process.wait(timeout=10)
        session.detach()


if __name__ == '__main__':
    main()
