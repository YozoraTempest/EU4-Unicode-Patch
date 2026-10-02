"""Trace native file opens and exercise path conversion in the isolated EU4 only."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import frida
import psutil

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "private/runtime/eu4.exe"
EXPECTED_HASH = "9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a"

SCRIPT = r"""
const base=Process.mainModule.base;
const plugin=Process.getModuleByName('eu4_unicode_probe.dll');
if(new NativeFunction(plugin.getExportByName('Eu4UnicodeProbeEnabled'),'int',[])()!==1)
  throw new Error('The isolated Unicode patch is disabled');
const encode=new NativeFunction(base.add(0x19fc030),'void',['pointer','pointer','uint64']);
const decode=new NativeFunction(base.add(0x19fc590),'void',['pointer','pointer','uint64']);
// Fresh scratch buffers only: no game objects or files are modified.
for(const value of ['ASCII','法兰西','𐀀','𠀀','😀','\u{10ffff}']) {
  const wide=Memory.allocUtf16String(value),bytes=Memory.alloc(128),again=Memory.alloc(128);
  encode(wide,bytes,128);decode(bytes,again,128);
  const encoded=bytes.readUtf8String(),roundtrip=again.readUtf16String();
  send({event:'path-conversion',value,encoded,roundtrip,passed:encoded===value&&roundtrip===value});
  if(encoded!==value||roundtrip!==value) throw new Error('Native Unicode path conversion failed');
}
const narrow=Memory.alloc(5);
encode(Memory.allocUtf16String('𠀀A'),narrow,5);
if(narrow.readUtf8String()!=='𠀀') throw new Error('Native encoder split a scalar at its capacity');
send({event:'path-capacity',passed:true,capacity:5,value:narrow.readUtf8String()});
for(const name of ['CreateFileW','CreateFileA']) {
  Interceptor.attach(Process.getModuleByName('KernelBase.dll').getExportByName(name),{
    onEnter(args) {
      const path=name.endsWith('W')?args[0].readUtf16String():args[0].readCString();
      if(path&&(path.toLowerCase().includes('save games')||path.toLowerCase().endsWith('.eu4'))) {
        this.path=path;this.access=args[1].toUInt32();this.caller=this.returnAddress.sub(base).toString();
      }
    },onLeave(result) {
      if(this.path)send({event:'open-save-file',api:name,path:this.path,access:this.access,
        result:result.toString(),caller:this.caller});
    }
  });
}
send({event:'ready',pid:Process.id});
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=int, default=300)
    args = parser.parse_args()
    if not 1 <= args.duration <= 1800:
        raise ValueError("Trace duration must be between 1 and 1800 seconds")
    if hashlib.sha256(EXE.read_bytes()).hexdigest() != EXPECTED_HASH:
        raise ValueError("Unexpected isolated executable hash")
    processes = [p for p in psutil.process_iter(["exe"])
                 if p.info["exe"] and Path(p.info["exe"]) == EXE]
    if len(processes) != 1:
        raise RuntimeError("Start exactly one isolated instance with start-test.ps1 first")
    process = processes[0]
    session = frida.attach(process.pid)
    errors = []
    try:
        script = session.create_script(SCRIPT)
        output = ROOT / "private/path-trace.jsonl"
        with output.open("w", encoding="utf-8") as file:
            file.write(json.dumps({"event": "artifact", "exe_sha256": EXPECTED_HASH,
                "dll_sha256": hashlib.sha256((EXE.parent / "plugins/eu4_unicode_probe.dll").read_bytes()).hexdigest()}) + "\n")
            def message(value, data):
                file.write(json.dumps(value, ensure_ascii=True) + "\n")
                file.flush()
                if value.get("type") == "error":
                    errors.append(value)
                    print(value, flush=True)
                if value.get("payload", {}).get("event") in {"ready", "path-conversion", "path-capacity"}:
                    print(json.dumps(value["payload"], ensure_ascii=True), flush=True)
            script.on("message", message)
            script.load()
            stop = time.monotonic() + args.duration
            while time.monotonic() < stop and process.is_running() and not errors:
                time.sleep(.25)
        if errors:
            raise RuntimeError("Native path verification failed")
        if not process.is_running():
            raise RuntimeError("Isolated game exited before tracing completed")
        print(f"Trace saved: {output}", flush=True)
    finally:
        # End our dedicated test process before removing live trace hooks.
        if process.is_running():
            process.terminate()
            process.wait(timeout=10)
        session.detach()


if __name__ == "__main__":
    main()
