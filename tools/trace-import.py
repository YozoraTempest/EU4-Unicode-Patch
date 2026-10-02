"""Observe UTF-8 import and rendering only in the isolated research executable."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import subprocess

import frida

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "private/runtime/eu4.exe"
EXPECTED_HASH = "9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a"

SCRIPT = r"""
const base=Process.mainModule.base;
function text(p) {
  const n=p.add(16).readU64().toNumber();
  const data=p.add(24).readU64().compare(16)<0?p:p.readPointer();
  return {length:n,bytes:Array.from(new Uint8Array(data.readByteArray(Math.min(n,512))))};
}
const seen=new Set();
Interceptor.attach(base.add(0x16fd650), {onEnter(args) {
  const key=text(args[0]);
  const name=String.fromCharCode(...key.bytes);
  const value=text(args[1]);
  const id='import-'+name+'-'+value.bytes.slice(0,8).join(',');
  if ((name.startsWith('EU4_UNICODE')||['FE_SINGLE_PLAYER','FE_MULTI_PLAYER','FE_TUTORIAL'].includes(name))&&!seen.has(id)) {
    seen.add(id);send({event:'import',key:name,value});
  }
}});
Interceptor.attach(base.add(0x16fa8d0), {onEnter(args) {
  const name=args[1].readCString();
  const bytes=[];
  for(let i=0;i<512;i++){const b=args[2].add(i).readU8();if(!b)break;bytes.push(b);}
  const id='register-'+name+'-'+bytes.slice(0,8).join(',');
  if ((name.startsWith('EU4_UNICODE')||['FE_SINGLE_PLAYER','FE_MULTI_PLAYER','FE_TUTORIAL'].includes(name))&&!seen.has(id)) {
    seen.add(id);
    send({event:'register',key:name,bytes});
  }
}});
let rendered=0;
const renderSeen=new Set();
let currentText=[];
Interceptor.attach(base.add(0x15988e0),{onEnter(args){
 const value=text(args[1]);
 this.previousText=currentText;currentText=value.bytes;
 const id=value.bytes.join(',');
 if(value.bytes.some(b=>b>=128)&&!renderSeen.has(id)&&rendered++<64) {
   renderSeen.add(id);send({event:'render',value});
 }
},onLeave(){currentText=this.previousText;}});
const glyphSeen=new Set();
const newlineSeen=new Set();
Interceptor.attach(base.add(0x159a7ac),{onEnter(){
 const slot=this.context.r8.toUInt32();
 if(slot===10&&!newlineSeen.has(currentText.join(','))) {
   newlineSeen.add(currentText.join(','));
   send({event:'draw-newline',source:currentText,present:!this.context.rdx.isNull()});
 }
 if(slot>255&&!glyphSeen.has(slot)) {
   glyphSeen.add(slot);
   send({event:'draw-glyph',slot,index:this.context.r15.toUInt32(),present:!this.context.rdx.isNull()});
 }
}});
send({event:'ready',pid:Process.id,path:Process.mainModule.path});
"""

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--duration",type=int,default=90)
    args=parser.parse_args()
    if hashlib.sha256(EXE.read_bytes()).hexdigest()!=EXPECTED_HASH:
        raise ValueError("Unexpected isolated executable")
    output=ROOT/"private/import-trace.jsonl"
    device=frida.get_local_device()
    process=subprocess.Popen([str(EXE),"-debug"],cwd=str(EXE.parent))
    pid=process.pid
    time.sleep(2)
    if process.poll() is not None:
        raise RuntimeError(f"Isolated game exited during startup: {process.returncode:#x}")
    session=device.attach(pid)
    script=session.create_script(SCRIPT)
    with output.open("w",encoding="utf-8") as file:
        def message(value,data):
            file.write(json.dumps(value,ensure_ascii=False)+"\n")
            file.flush()
            if value.get("type")=="send" and value["payload"].get("event")=="ready":
                print(json.dumps(value["payload"],ensure_ascii=False),flush=True)
            if value.get("type")=="error":
                print(value,flush=True)
        script.on("message",message)
        script.load()
        print(f"Started isolated EU4 pid={pid}",flush=True)
        stop=time.monotonic()+args.duration
        while time.monotonic()<stop and process.poll() is None:
            time.sleep(0.5)
    # On this machine Frida detach from hot text hooks caused an access
    # violation in frida-agent.dll_unloaded. End the dedicated traced process
    # before releasing the agent; use start-test.ps1 for interactive testing.
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=10)
    session.detach()
    print(f"Trace saved: {output}",flush=True)

if __name__=="__main__":
    main()
