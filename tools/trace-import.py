"""Observe UTF-8 import and rendering only in the isolated research executable."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import subprocess
import threading
import shutil

import frida
import psutil

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
const colorSeen=new Set();
const iconSeen=new Set();
Interceptor.attach(base.add(0x15366c0),{onEnter(args){
 this.widget=args[0];this.key=args[1].readU32();
 this.before=text(this.widget.add(0x30));
},onLeave(){
 const after=text(this.widget.add(0x30));
 if(this.before.bytes.some(b=>b>=128)||after.bytes.some(b=>b>=128))
   send({event:'editor-key',key:this.key,before:this.before,after,caret:this.widget.add(0x54).readU16(),row:this.widget.add(0x56).readU16()});
}});
Interceptor.attach(base.add(0x15a0390),{onEnter(args){
 const code=args[1].toUInt32()&255;
 if(currentText.length&&!colorSeen.has(code)) {
   colorSeen.add(code);send({event:'format-color',code,source:currentText});
 }
}});
for(const rva of [0x159a4c9,0x1598185]) Interceptor.attach(base.add(rva),{onEnter(){
 const name=this.context.r8.readCString();
 if(!iconSeen.has(name)) {iconSeen.add(name);send({event:'format-icon',name,source:currentText});}
}});
Interceptor.attach(base.add(0x159a7ac),{onEnter(){
 const slot=this.context.r8.toUInt32();
 if(slot===10&&!newlineSeen.has(currentText.join(','))) {
   newlineSeen.add(currentText.join(','));
   send({event:'draw-newline',source:currentText,present:!this.context.rdx.isNull()});
 }
 if(slot>255&&!glyphSeen.has(slot)) {
   glyphSeen.add(slot);
   const glyph=this.context.rdx;
   send({event:'draw-glyph',slot,index:this.context.r15.toUInt32(),present:!glyph.isNull(),
     metrics:glyph.isNull()?[]:Array.from(new Uint8Array(glyph.readByteArray(16)))});
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
    for candidate in psutil.process_iter(["exe"]):
        if candidate.info["exe"] and Path(candidate.info["exe"])==EXE:
            raise RuntimeError("Close the existing isolated instance before tracing")
    plugin=EXE.parent/"plugins/eu4_unicode_probe.dll"
    if (plugin.parent/"plugin64.dll").exists():
        raise RuntimeError("Legacy plugin is present in the isolated fixture")
    shutil.copy2(ROOT/"build/eu4_unicode_probe.dll",plugin)
    (plugin.parent/"eu4_unicode_probe.ini").write_text("[experimental]\nunicode_input=0\n",encoding="ascii")
    output=ROOT/"private/import-trace.jsonl"
    device=frida.get_local_device()
    process=subprocess.Popen([str(EXE),"-debug"],cwd=str(EXE.parent))
    pid=process.pid
    session=None
    ready=threading.Event()
    errors=[]
    try:
        time.sleep(2)
        if process.poll() is not None:
            raise RuntimeError(f"Isolated game exited during startup: {process.returncode:#x}")
        session=device.attach(pid)
        script=session.create_script(SCRIPT)
        with output.open("w",encoding="utf-8") as file:
            file.write(json.dumps({"type":"send","payload":{"event":"artifact",
                "exe_sha256":EXPECTED_HASH,"dll_sha256":hashlib.sha256(plugin.read_bytes()).hexdigest(),
                "font_backend":"system" if 'DirectWrite system fallback' in
                    (ROOT/'private/test-mod/gfx/fonts/zh-hans-18.fnt').read_text(encoding='utf-8') else "workshop"}})+"\n")
            def message(value,data):
                file.write(json.dumps(value,ensure_ascii=False)+"\n")
                file.flush()
                if value.get("type")=="send" and value["payload"].get("event")=="ready":
                    ready.set()
                    print(json.dumps(value["payload"],ensure_ascii=False),flush=True)
                if value.get("type")=="error":
                    errors.append(value)
                    print(value,flush=True)
            script.on("message",message)
            script.load()
            if not ready.wait(10):
                raise RuntimeError("Trace hooks did not finish installing")
            print(f"Started isolated EU4 pid={pid}",flush=True)
            stop=time.monotonic()+args.duration
            while time.monotonic()<stop and process.poll() is None and not errors:
                time.sleep(0.5)
        if errors:
            raise RuntimeError("Runtime trace reported script errors")
    finally:
        # End this dedicated process before releasing hot text hooks.
        # Interactive tests use start-test.ps1 and have no Frida agent.
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        if session:
            session.detach()
    print(f"Trace saved: {output}",flush=True)

if __name__=="__main__":
    main()
