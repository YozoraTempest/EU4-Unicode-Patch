"""Exercise native Ctrl-V with a private SDL text source and the real SDL allocator."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import time

import frida
import psutil
import pefile
from clipboard_cases import CLIPBOARD_CASES, ROUNDTRIP_CASES

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'private/runtime/eu4.exe'
EXPECTED_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'
SCRIPT = r'''
const base=Process.mainModule.base,patch=Process.getModuleByName('eu4_unicode_probe.dll'),options={exceptions:'propagate'};
if(new NativeFunction(patch.getExportByName('Eu4UnicodeProbeEnabled'),'int',[])()!==1)
  throw new Error('Unicode patch is disabled');
const cases=__CASES__;
const assign=new NativeFunction(base.add(0x95110),'pointer',['pointer','pointer','uint64'],options);
const destroy=new NativeFunction(base.add(0x95660),'void',['pointer'],options);
const select=new NativeFunction(base.add(0x153b170),'void',['pointer'],options);
const key=new NativeFunction(base.add(0x15366c0),'bool',['pointer','pointer'],{...options,traps:'all'});
const allocate=new NativeFunction(base.add(0x1735ec0),'pointer',['uint64'],options);
let view=null,widget=null,ran=false,active=null,source=null,provided=null,writes=[],reads=0,releases=0,inserts=[],notifications=[];
function bytes(s){const n=s.add(16).readU64().toNumber(),data=s.add(24).readU64().compare(16)<0?s:s.readPointer();
  if(n>32000)throw new Error('Unexpected native string size');return Array.from(new Uint8Array(data.readByteArray(n)));}
function state(){return {text:bytes(widget.add(0x30)),caret:widget.add(0x54).readU16(),row:widget.add(0x56).readU16(),
  anchor:widget.add(0x92).readU16(),selected:bytes(widget.add(0x70)),active:widget.add(0x90).readU8()!==0};}
// This source supplies test text only. It never reads or writes the OS clipboard.
// SDL owns each allocation until the patched paste action calls SDL_free.
const sourceCallback=new NativeCallback(function(){
  if(!active)throw new Error('Unexpected clipboard read outside a fixture');
  if(reads++)throw new Error('Clipboard source read more than once');
  if(provided===null)throw new Error('Clipboard source has no text');
  source=allocate(provided.length+1);
  if(source.isNull())throw new Error('SDL allocation failed');
  source.writeByteArray(provided.concat([0]));return source;
},'pointer',[]);
// The exported SDL entry points are seven-byte jumps through SDL's own dynamic
// API table. Replace the source/table entries directly, including nested calls;
// the private provider cannot fall through to the OS clipboard implementation.
function apiSlot(rva,expected){
  const entry=base.add(rva);
  if(JSON.stringify(Array.from(new Uint8Array(entry.readByteArray(3))))!==JSON.stringify([0x48,0xff,0x25]))
    throw new Error('Unexpected SDL dynamic API entry');
  const slot=entry.add(7).add(entry.add(3).readS32());
  if(!slot.equals(base.add(expected)))throw new Error('Unexpected SDL dynamic API slot');
  return slot;
}
const getSlot=apiSlot(0x1734490,0x1fb01f0),freeSlot=apiSlot(0x1735e00,0x1fb0aa0);
const release=new NativeFunction(freeSlot.readPointer(),'void',['pointer'],{...options,traps:'none'});
const releaseCallback=new NativeCallback(function(value){
  if(active&&source&&value.equals(source)){
    if(releases++)throw new Error('Clipboard source released more than once');
    if(JSON.stringify(Array.from(new Uint8Array(source.readByteArray(provided.length+1))))!==JSON.stringify(provided.concat([0])))
      throw new Error('Clipboard source buffer was modified');
  }
  release(value);
},'void',['pointer']);
getSlot.writePointer(sourceCallback);freeSlot.writePointer(releaseCallback);
// Native copy/cut writes only to this private provider. The subsequent paste
// reads these captured bytes, rather than reconstructing the expected fixture.
const setCallback=new NativeCallback(function(text){
  if(!active||!active.action)throw new Error('Unexpected clipboard write');
  const value=text.readUtf8String();
  provided=Array.from(unescape(encodeURIComponent(value)),ch=>ch.charCodeAt(0));
  writes.push(provided.slice());return 0;
},'int',['pointer']);
if(cases[0].action)apiSlot(0x17357c0,0x1fb01e8).writePointer(setCallback);
// These forwarding callbacks also observe nested native invocations made from
// the UI-frame callback. Ordinary Interceptor listeners are suppressed there.
// The insertion callback calls the existing MinHook trampoline without changing
// call sites inside its body. Neither observer implements insertion or notify.
const insertSlot=patch.base.add(__INSERT_SLOT__);
const originalInsert=new NativeFunction(insertSlot.readPointer(),'void',['pointer','pointer'],{...options,traps:'none'});
const insertCallback=new NativeCallback(function(target,text){
  if(active&&target.equals(widget))inserts.push(bytes(text));
  originalInsert(target,text);
},'void',['pointer','pointer']);
insertSlot.writePointer(insertCallback);
let originalNotify=null;
const notifyCallback=new NativeCallback(function(target){
  if(active&&view&&target.equals(view))notifications.push(bytes(widget.add(0x30)));
  originalNotify(target);
},'void',['pointer']);
originalNotify=new NativeFunction(Interceptor.replaceFast(base.add(0xf160d0),notifyCallback),'void',['pointer'],{...options,traps:'none'});
Interceptor.attach(base.add(0xefc090),{onEnter(args){if(args[0].add(0x9b8).readU32()===1)view=args[0];}});
Interceptor.attach(base.add(0x15988e0),{onEnter(){
  if(!view||ran)return;ran=true;
  let originalLimit=null,originalFlags=null;
  try{
    const parent=view.add(0xa0).readPointer();
    const lookup=new NativeFunction(parent.readPointer().add(0xb8).readPointer(),'pointer',['pointer','pointer'],options);
    const outer=lookup(parent,Memory.allocUtf8String('sort_search_edit'));
    if(outer.isNull())throw new Error('Search widget not found');
    widget=outer.add(0xc8);originalLimit=widget.add(0xe0).readS32();originalFlags=widget.add(0xe4).readU8();
    const set=new NativeFunction(widget.readPointer().add(0x80).readPointer(),'void',['pointer','pointer'],options);
    send({event:'clipboard-contract',original_byte_limit:originalLimit,original_font_flags:originalFlags,
      paste_rva:String(widget.readPointer().add(0x180).readPointer().sub(base)),
      font_transform_rva:String(widget.add(0x98).readPointer().readPointer().add(0xc0).readPointer().sub(base))});
    for(const c of cases){
      const value=Memory.alloc(32);value.writeByteArray(new Uint8Array(32));value.add(24).writeU64(15);
      widget.add(0xe0).writeS32(80);widget.add(0xe4).writeU8(originalFlags);
      assign(value,Memory.allocUtf8String(c.before),unescape(encodeURIComponent(c.before)).length);
      try{set(widget,value);}finally{destroy(value);}
      widget.add(0x90).writeU8(0);widget.add(0x54).writeU16(c.caret);widget.add(0x56).writeU16(0);
      if(c.selection){widget.add(0x92).writeU16(c.selection.anchor);widget.add(0x94).writeU16(0);
        widget.add(0x54).writeU16(c.selection.caret);select(widget);widget.add(0x90).writeU8(1);}
      const before=state();widget.add(0xe0).writeS32(c.byte_limit);widget.add(0xe4).writeU8(c.font_flags);
      reads=0;releases=0;source=null;provided=c.action?null:c.source;writes=[];inserts=[];notifications=[];active=c;
      const event=Memory.alloc(12);event.add(4).writeU32(0);event.add(8).writeU32(1);
      let actionResult=null;
      if(c.action){
        event.writeU32(c.action==='copy'?0x63:0x78);
        const actionHandled=key(widget,event)!==0;
        actionResult={handled:actionHandled,after:state(),writes:writes.slice(),reads,releases,
          inserts:inserts.slice(),notifications:notifications.slice()};
        if(writes.length!==1||provided===null)throw new Error('Native copy/cut did not supply clipboard text');
        inserts=[];notifications=[];
      }
      const pasteBefore=state();event.writeU32(0x76);
      const handled=key(widget,event)!==0;const after=state();
      if(reads!==1||releases!==1)throw new Error('Clipboard ownership: '+JSON.stringify({name:c.name,reads,releases,handled,before,after,inserts}));
      active=null;
      send({event:'clipboard-case',name:c.name,source_length:c.source.length,before,after,handled,
        reads,releases,inserts,notifications,byte_limit:c.byte_limit,font_flags:c.font_flags,
        ...(c.action?{action:c.action,action_result:actionResult,paste_before:pasteBefore}:{} )});
    }
    send({event:'clipboard-complete',cases:cases.length,allocations:cases.length,releases:cases.length});
  }catch(e){send({event:'clipboard-failed',message:String(e)});}
  finally{active=null;if(widget&&originalLimit!==null){widget.add(0xe0).writeS32(originalLimit);widget.add(0xe4).writeU8(originalFlags);}}
}});
send({event:'ready',pid:Process.id});
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration', type=int, default=600)
    parser.add_argument('--roundtrip', action='store_true', help='Exercise native copy/cut followed by paste')
    args = parser.parse_args()
    if not 60 <= args.duration <= 1800:
        raise ValueError('Duration must be 60 to 1800 seconds')
    if hashlib.sha256(EXE.read_bytes()).hexdigest() != EXPECTED_HASH:
        raise ValueError('Unexpected isolated executable')
    if 'unicode_input=1' not in (EXE.parent / 'plugins/eu4_unicode_probe.ini').read_text():
        raise ValueError('Start the isolated instance with -ExperimentalInput')
    plugin = EXE.parent / 'plugins/eu4_unicode_probe.dll'
    dll_hash = hashlib.sha256(plugin.read_bytes()).hexdigest()
    if dll_hash != hashlib.sha256((ROOT / 'build/eu4_unicode_probe.dll').read_bytes()).hexdigest():
        raise ValueError('Runtime DLL differs from the mapped build')
    mapping_path = ROOT / 'build/eu4_unicode_probe.map'
    mapping = mapping_path.read_text()
    match = re.search(r'\s\?original_editor_insert\S*\s+([0-9A-Fa-f]+)\s', mapping)
    if not match:
        raise ValueError('Build map lacks original_editor_insert')
    image = pefile.PE(str(plugin), fast_load=True)
    insert_slot = int(match[1], 16) - image.OPTIONAL_HEADER.ImageBase
    section = image.get_section_by_rva(insert_slot)
    if not section or not section.Characteristics & 0x80000000 or not (
            section.VirtualAddress <= insert_slot <= section.VirtualAddress + section.Misc_VirtualSize - 8):
        raise ValueError('Invalid mapped insertion pointer')
    image.close()
    processes = [p for p in psutil.process_iter(['exe']) if p.info['exe'] and Path(p.info['exe']) == EXE]
    if len(processes) != 1:
        raise RuntimeError('Start exactly one isolated instance')
    process = processes[0]
    session = frida.attach(process.pid)
    complete, errors = [], []
    try:
        cases = ROUNDTRIP_CASES if args.roundtrip else CLIPBOARD_CASES
        script = session.create_script(SCRIPT.replace('__CASES__', json.dumps(cases))
                                       .replace('__INSERT_SLOT__', str(insert_slot)))
        output = 'native-clipboard-roundtrip.jsonl' if args.roundtrip else 'native-clipboard.jsonl'
        with (ROOT / 'private' / output).open('w', encoding='utf-8') as out:
            out.write(json.dumps(dict(event='artifact', exe_sha256=EXPECTED_HASH,
                dll_sha256=dll_hash, map_sha256=hashlib.sha256(mapping_path.read_bytes()).hexdigest(),
                insert_observer_rva=hex(insert_slot),
                mode='roundtrip' if args.roundtrip else 'paste',
                method='Native Ctrl-V dispatcher, isolated SDL clipboard source, real SDL_malloc/SDL_free, forwarding insertion/notification observers, live single-line edit widget; no OS clipboard writes or physical shortcut claim'))+'\n')
            def message(value, data):
                out.write(json.dumps(value)+'\n'); out.flush()
                event = value.get('payload', {}).get('event')
                print(json.dumps(dict(event=event, name=value['payload']['name'])) if event == 'clipboard-case' else json.dumps(value), flush=True)
                if value.get('type') == 'error' or event == 'clipboard-failed':
                    errors.append(value)
                if event == 'clipboard-complete':
                    complete.append(value)
            script.on('message', message); script.load()
            deadline = time.monotonic()+args.duration
            while process.is_running() and not complete and not errors and time.monotonic() < deadline:
                time.sleep(.25)
        if errors or not complete:
            raise RuntimeError('Clipboard probe did not complete')
    finally:
        if process.is_running():
            process.terminate(); process.wait(timeout=10)
        session.detach()


if __name__ == '__main__':
    main()
