"""Verify native single-line grapheme keys in the explicitly enabled input experiment."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import frida
import psutil
from editor_cases import KEY_CASES, LIMIT_CASES, INSERT_CASES, FILTER_CASES

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "private/runtime/eu4.exe"
EXPECTED_HASH = "9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a"

SCRIPT = r"""
const base=Process.mainModule.base;
if(new NativeFunction(Process.getModuleByName('eu4_unicode_probe.dll').getExportByName('Eu4UnicodeProbeEnabled'),'int',[])()!==1)
  throw new Error('Unicode patch is disabled');
const options={exceptions:'propagate'};
const assign=new NativeFunction(base.add(0x95110),'pointer',['pointer','pointer','uint64'],options);
const destroy=new NativeFunction(base.add(0x95660),'void',['pointer'],options);
const key=new NativeFunction(base.add(0x15366c0),'bool',['pointer','pointer'],options);
function text(p) {
  const n=p.add(16).readU64().toNumber(),data=p.add(24).readU64().compare(16)<0?p:p.readPointer();
  if(n>32000)throw new Error('Unexpected native text size');
  return data.readUtf8String(n);
}
const cases=__KEY_CASES__;
let view=null,refreshes=0,ran=false;
Interceptor.attach(base.add(0xefc090),{onEnter(args) {
  if(args[0].add(0x9b8).readU32()===1){view=args[0];++refreshes;}
}});
Interceptor.attach(base.add(0x15988e0),{onEnter() {
  if(!view||refreshes<2||ran)return;ran=true;
  try {
    const parent=view.add(0xa0).readPointer();
    const lookup=new NativeFunction(parent.readPointer().add(0xb8).readPointer(),'pointer',['pointer','pointer'],options);
    const widget=lookup(parent,Memory.allocUtf8String('sort_search_edit')).add(0xc8);
    const set=new NativeFunction(widget.readPointer().add(0x80).readPointer(),'void',['pointer','pointer'],options);
    const insert=new NativeFunction(widget.readPointer().add(0xa0).readPointer(),'void',['pointer','pointer'],options);
    function apply(handler,value) {
      const storage=Memory.alloc(32);storage.writeByteArray(new Uint8Array(32));storage.add(24).writeU64(15);
      assign(storage,Memory.allocUtf8String(value),unescape(encodeURIComponent(value)).length);
      try { handler(widget,storage); } finally { destroy(storage); }
    }
    const originalLimit=widget.add(0xe0).readS32();
    const blacklist=widget.add(0xb8),blacklistSize=blacklist.add(16).readU64().toNumber();
    const blacklistData=blacklist.add(24).readU64().compare(16)<0?blacklist:blacklist.readPointer();
    send({event:'native-editor-budget',original_byte_limit:originalLimit,test_byte_limit:80,
      blacklist_bytes:Array.from(new Uint8Array(blacklistData.readByteArray(blacklistSize)))});
    widget.add(0xe0).writeS32(80);
    try {
    for(const [name,value,caret,code,expected,expectedCaret] of cases) {
      apply(set,value);
      const nativeText=widget.add(0x30),nativeLength=nativeText.add(16).readU64().toNumber();
      const nativeData=nativeText.add(24).readU64().compare(16)<0?nativeText:nativeText.readPointer();
      send({event:'native-editor-fixture',name,native_bytes:Array.from(new Uint8Array(nativeData.readByteArray(nativeLength))),
        size:nativeLength,width:widget.add(0x6a).readU16(),rows:widget.add(0x60).readU16(),
        backspace_rva:widget.readPointer().add(0x138).readPointer().sub(base).toString(),
        delete_rva:widget.readPointer().add(0x140).readPointer().sub(base).toString()});
      if(text(widget.add(0x30))!==value||widget.add(0x56).readU16()!==0||widget.add(0x80).readU64().compare(0)!==0)
        throw new Error('Widget did not accept a complete unselected single-line fixture');
      widget.add(0x54).writeU16(caret);
      const event=Memory.alloc(12);event.writeByteArray(new Uint8Array(12));event.writeU32(code);
      key(widget,event);
      const after=text(widget.add(0x30)),afterCaret=widget.add(0x54).readU16();
      const passed=after===expected&&afterCaret===expectedCaret;
      send({event:'native-editor-key',name,before:value,caret,key:code,after,after_caret:afterCaret,
        expected,expected_caret:expectedCaret,passed});
      if(!passed)throw new Error('Native grapheme-key result differs from its expected complete text');
    }
    const budgets=__LIMIT_CASES__;
    for(const [name,value,budget,expected] of budgets) {
      widget.add(0xe0).writeS32(budget);
      apply(set,value);
      const after=text(widget.add(0x30)),passed=after===expected;
      send({event:'native-editor-limit',name,before:value,byte_limit:budget,after,expected,passed});
      if(!passed)throw new Error('Native editor retained a partial grapheme');
    }
    const inserts=__INSERT_CASES__;
    for(const [name,value,caret,insertion,budget,expected] of inserts) {
      widget.add(0xe0).writeS32(80);apply(set,value);
      widget.add(0xe0).writeS32(budget);widget.add(0x54).writeU16(caret);
      apply(insert,insertion);
      const after=text(widget.add(0x30)),passed=after===expected;
      send({event:'native-editor-insert',name,before:value,caret,insertion,byte_limit:budget,after,expected,passed});
      if(!passed)throw new Error('Native bounded insertion retained a partial grapheme');
    }
    const filters=__FILTER_CASES__;
    for(const [name,value,expected] of filters) {
      widget.add(0xe0).writeS32(80);apply(set,value);
      const after=text(widget.add(0x30)),passed=after===expected;
      send({event:'native-editor-filter',name,before:value,after,expected,passed});
      if(!passed)throw new Error('Native blacklist corrupted UTF-8 or accepted a prohibited scalar');
    }
    send({event:'editor-validation-complete',cases:cases.length,budget_cases:budgets.length,
      insertion_cases:inserts.length,filter_cases:filters.length,passed:true});
    } finally { widget.add(0xe0).writeS32(originalLimit); }
  } catch(error) { send({event:'editor-validation-failed',message:String(error)}); }
}});
send({event:'ready',pid:Process.id});
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=int, default=600)
    args = parser.parse_args()
    if not 60 <= args.duration <= 1800:
        raise ValueError("Duration must be 60 to 1800 seconds")
    if hashlib.sha256(EXE.read_bytes()).hexdigest() != EXPECTED_HASH:
        raise ValueError("Unexpected isolated executable")
    if "unicode_input=1" not in (EXE.parent / "plugins/eu4_unicode_probe.ini").read_text():
        raise ValueError("Start the dedicated game with -ExperimentalInput first")
    processes = [p for p in psutil.process_iter(["exe"]) if p.info["exe"] and Path(p.info["exe"]) == EXE]
    if len(processes) != 1:
        raise RuntimeError("Start exactly one isolated instance first")
    process = processes[0]
    session = frida.attach(process.pid)
    complete = []
    errors = []
    try:
        code = SCRIPT
        for name, fixtures in [('KEY', KEY_CASES), ('LIMIT', LIMIT_CASES), ('INSERT', INSERT_CASES), ('FILTER', FILTER_CASES)]:
            code = code.replace('__' + name + '_CASES__', json.dumps(fixtures, ensure_ascii=True))
        script = session.create_script(code)
        output = ROOT / "private/native-editor-trace.jsonl"
        with output.open("w", encoding="utf-8") as file:
            file.write(json.dumps({"event": "artifact", "exe_sha256": EXPECTED_HASH,
                "dll_sha256": hashlib.sha256((EXE.parent / "plugins/eu4_unicode_probe.dll").read_bytes()).hexdigest(),
                "method": "programmatic native SetText and key handler on UI thread; not physical input or IME"}) + "\n")
            def message(value, data):
                file.write(json.dumps(value, ensure_ascii=True) + "\n")
                file.flush()
                payload = value.get("payload", {})
                if value.get("type") == "error" or payload.get("event") == "editor-validation-failed":
                    errors.append(value)
                if payload.get("event") == "editor-validation-complete":
                    complete.append(payload)
                print(json.dumps(value, ensure_ascii=True), flush=True)
            script.on("message", message)
            script.load()
            print("Open diplomacy, then click Name sort to exercise the native edit widget.", flush=True)
            deadline = time.monotonic() + args.duration
            while time.monotonic() < deadline and process.is_running() and not errors and not complete:
                time.sleep(.25)
        if errors or not complete:
            raise RuntimeError("Native editor validation did not pass")
        print(f"PASS: {complete[-1]['cases']} native grapheme-key cases", flush=True)
    finally:
        if process.is_running():
            process.terminate()
            process.wait(timeout=10)
        session.detach()


if __name__ == "__main__":
    main()
