"""Observe native Windows IME UI policy and candidate placement in the isolated game."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import frida
import psutil

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'private/runtime/eu4.exe'
EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'
SCRIPT = r'''
const base=Process.mainModule.base,patch=Process.getModuleByName('eu4_unicode_probe.dll');
if(new NativeFunction(patch.getExportByName('Eu4UnicodeProbeEnabled'),'int',[])()!==1)throw new Error('Patch disabled');
const imm=Process.getModuleByName('imm32.dll');
const getContext=new NativeFunction(imm.getExportByName('ImmGetContext'),'pointer',['pointer']);
const releaseContext=new NativeFunction(imm.getExportByName('ImmReleaseContext'),'int',['pointer','pointer']);
const getCandidate=new NativeFunction(imm.getExportByName('ImmGetCandidateWindow'),'int',['pointer','uint','pointer']);
const handle=new NativeFunction(base.add(0x1764940),'int',['pointer','uint','uint64','pointer','pointer']);
let video=null,armed=__CONTRACT__,checked=false;
function enabled(data){return !data.isNull()&&[0x48,0x4c,0x50].every(n=>data.add(n).readS32()!==0);}
function form(p){return {index:p.readU32(),style:p.add(4).readU32(),position:[p.add(8).readS32(),p.add(12).readS32()],
 area:[16,20,24,28].map(n=>p.add(n).readS32())};}
Interceptor.attach(base.add(0x1764940),{onEnter(args){
 this.message=args[1].toUInt32();this.parameter=args[2].toString();this.flags=args[3];this.incoming=this.flags.readU64().toString();this.active=enabled(args[4]);
},onLeave(result){if(this.message===0x281||this.message===0x282)send({event:'ime-window-message',message:this.message,parameter:this.parameter,
 incoming:this.incoming,outgoing:this.flags.readU64().toString(),trapped:result.toInt32(),enabled:this.active});}});
Interceptor.attach(base.add(0x1763a30),{onEnter(args){this.id=args[1].toUInt32();this.show=args[2];},onLeave(result){
 send({event:'ime-tsf-ui-element',id:this.id,hresult:result.toInt32(),show:this.show.readS32()});}});
Interceptor.attach(imm.getExportByName('ImmSetCandidateWindow'),{onEnter(args){this.nativeContext=args[0].toString();this.candidateForm=form(args[1]);},onLeave(result){
 send({event:'ime-candidate-request',context:this.nativeContext,form:this.candidateForm,accepted:result.toInt32()!==0});}});
Interceptor.attach(base.add(0x17657c0),{onEnter(args){
 video=args[0].add(0x390).readPointer();this.data=video;this.rect=args[1].isNull()?null:[0,4,8,12].map(n=>args[1].add(n).readS32());
},onLeave(){
 if(!this.rect||!enabled(this.data))return;
 const window=this.data.add(0x60).readPointer(),context=getContext(window);if(context.isNull())throw new Error('Missing native IME context');
 const candidate=Memory.alloc(32);candidate.writeByteArray(new Uint8Array(32));
 try { const accepted=getCandidate(context,0,candidate);send({event:'ime-candidate-state',rect:this.rect,available:accepted!==0,form:form(candidate)}); }
 finally { if(!releaseContext(window,context))throw new Error('IME context release failed'); }
}});
Interceptor.attach(base.add(0x15988e0),{onEnter(){
 if(!armed||checked||!video||!enabled(video))return;checked=true;
 try {
  const window=video.add(0x60).readPointer(),flags=Memory.alloc(8);
  for(const incoming of [0,1,15,0xc000000f]){
   flags.writeU64(incoming);const trapped=handle(window,0x281,1,flags,video),outgoing=flags.readU64().toNumber();
   if(trapped!==0||outgoing!==incoming)throw new Error('Native context UI flags lost');
   send({event:'ime-policy-case',name:'active-context',incoming,outgoing,trapped});
  }
  const copy=Memory.alloc(0x1590);Memory.copy(copy,video,0x1590);
  for(const offset of [0x48,0x4c,0x50]){
   copy.add(offset).writeS32(0);flags.writeU64(0xc000000f);const trapped=handle(window,0x281,0,flags,copy),outgoing=flags.readU64().toNumber();
   if(trapped!==0||outgoing!==0xc000000f)throw new Error('Disabled native context changed');
   send({event:'ime-policy-case',name:'disabled-context',offset,incoming:0xc000000f,outgoing,trapped});
   copy.add(offset).writeS32(video.add(offset).readS32());
  }
  send({event:'ime-policy-complete',cases:7});
 } catch(error){send({event:'ime-observer-failed',message:String(error)});}
}});
send({event:'ready',contract:armed});
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration', type=int, default=1800)
    parser.add_argument('--contract', action='store_true', help='Also check seven native context-message contracts on the UI thread')
    parser.add_argument('--output', default='private/native-ime-candidates.jsonl')
    args = parser.parse_args()
    if not 60 <= args.duration <= 1800:
        raise ValueError('Duration must be 60 to 1800 seconds')
    output = (ROOT / args.output).resolve()
    if not output.is_relative_to((ROOT / 'private').resolve()):
        raise ValueError('Keep input observations inside the private directory')
    if hashlib.sha256(EXE.read_bytes()).hexdigest() != EXE_HASH:
        raise ValueError('Unexpected isolated executable')
    if 'unicode_input=1' not in (EXE.parent / 'plugins/eu4_unicode_probe.ini').read_text():
        raise ValueError('Start the isolated instance with -ExperimentalInput')
    processes = [p for p in psutil.process_iter(['exe']) if p.info['exe'] and Path(p.info['exe']) == EXE]
    if len(processes) != 1:
        raise RuntimeError('Start exactly one isolated instance')
    process = processes[0]
    session = frida.attach(process.pid)
    errors = []
    finish = ROOT / 'private/ime-candidates-finish.txt'
    finish.unlink(missing_ok=True)
    try:
        script = session.create_script(SCRIPT.replace('__CONTRACT__', 'true' if args.contract else 'false'))
        with output.open('w', encoding='utf-8') as out:
            out.write(json.dumps(dict(event='artifact', exe_sha256=EXE_HASH,
                dll_sha256=hashlib.sha256((EXE.parent / 'plugins/eu4_unicode_probe.dll').read_bytes()).hexdigest(),
                method='Native Windows IME requests and stored forms; physical candidate visibility requires user verification',
                contract=args.contract))+'\n')

            def message(value, data):
                out.write(json.dumps(value)+'\n')
                out.flush()
                print(json.dumps(value), flush=True)
                if value.get('type') == 'error' or value.get('payload', {}).get('event') == 'ime-observer-failed':
                    errors.append(value)

            script.on('message', message)
            script.load()
            deadline = time.monotonic()+args.duration
            while process.is_running() and not finish.exists() and not errors and time.monotonic() < deadline:
                time.sleep(.25)
        if errors:
            raise RuntimeError('IME observation failed')
    finally:
        if process.is_running():
            process.terminate()
            process.wait(timeout=10)
        session.detach()


if __name__ == '__main__':
    main()
