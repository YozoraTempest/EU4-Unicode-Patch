"""Exercise the observed native diplomacy search using its widget text and callback."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import frida
import psutil
from search_cases import CASES

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "private/runtime/eu4.exe"
EXPECTED_HASH = "9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a"

SCRIPT = r"""
const base=Process.mainModule.base;
if(new NativeFunction(Process.getModuleByName('eu4_unicode_probe.dll').getExportByName('Eu4UnicodeProbeEnabled'),'int',[])()!==1)
  throw new Error('Unicode patch is disabled');
function text(p) {
  const length=p.add(16).readU64().toNumber(),data=p.add(24).readU64().compare(16)<0?p:p.readPointer();
  if(length>32000)throw new Error('Unexpected native string size');
  return data.readUtf8String(length);
}
const callOptions={exceptions:'propagate',traps:'all'};
const assign=new NativeFunction(base.add(0x95110),'pointer',['pointer','pointer','uint64'],callOptions);
const destroy=new NativeFunction(base.add(0x95660),'void',['pointer'],callOptions);
const changed=new NativeFunction(base.add(0xf160d0),'void',['pointer'],callOptions);
let view=null,pending=null,applying=false,activeQuery=null,activeWidget=null,completedFilters=0;
const records=new Map();
const candidates=new Map();
Interceptor.attach(base.add(0xefc090),{onEnter(args) {
  if(args[0].add(0x9b8).readU32()!==1)return;
  view=args[0];
  this.current={id:activeQuery,query:text(view.add(0xa78)),examined:0,matched:[]};
  this.thread=Process.getCurrentThreadId();
  const stack=records.get(this.thread)||[];stack.push(this.current);records.set(this.thread,stack);
},onLeave() {
  if(this.current) {
    ++completedFilters;
    send({event:'country-filter',...this.current,displayed:activeWidget?text(activeWidget.add(0x30)):null});
    const stack=records.get(this.thread);stack.pop();if(!stack.length)records.delete(this.thread);
  }
}});
// Observe the earlier argument-store instruction. Leave the CALL and its
// callee untouched: interception can substitute _ReturnAddress and disable
// the patch's deliberately scoped adapter.
Interceptor.attach(base.add(0xefc383),{onEnter() {
  const stack=records.get(Process.getCurrentThreadId()),record=stack?.[stack.length-1];
  if(!record)return;
  const length=this.context.rbp.sub(0x59).readU64().toNumber();
  candidates.set(Process.getCurrentThreadId(),{name:this.context.rcx.readUtf8String(length),record});
}});
// A callee onLeave also substitutes its return address. Observe the native
// instruction after the call separately, preserving the adapter's caller.
Interceptor.attach(base.add(0xefc394),{onEnter() {
  const id=Process.getCurrentThreadId(),candidate=candidates.get(id);candidates.delete(id);
  if(!candidate)return;
  ++candidate.record.examined;
  if(this.context.rax.toInt32()!==-1)candidate.record.matched.push(candidate.name);
}});
// Queue the widget setter on the game/UI thread at the next text draw. This
// tests native search integration, not keyboard dispatch or physical IME.
Interceptor.attach(base.add(0x15988e0),{onEnter() {
  if(!pending||!view||applying)return;
  const query=pending;pending=null;applying=true;
  activeQuery=query.id;
  try {
    if(view.add(0x9b8).readU32()!==1)throw new Error('Diplomacy country list is no longer active');
    const parent=view.add(0xa0).readPointer();
    const lookup=new NativeFunction(parent.readPointer().add(0xb8).readPointer(),'pointer',['pointer','pointer'],callOptions);
    const widget=lookup(parent,Memory.allocUtf8String('sort_search_edit')).add(0xc8);
    activeWidget=widget;
    const get=new NativeFunction(widget.readPointer().add(0x98).readPointer(),'pointer',['pointer'],callOptions);
    const set=new NativeFunction(widget.readPointer().add(0x80).readPointer(),'void',['pointer','pointer'],callOptions);
    const storage=Memory.alloc(32),bytes=Memory.allocUtf8String(query.value);
    storage.writeByteArray(new Uint8Array(32));storage.add(24).writeU64(15);
    const length=unescape(encodeURIComponent(query.value)).length;
    assign(storage,bytes,length);
    try { set(widget,storage); } finally { destroy(storage); }
    changed(view);
    const stored=text(view.add(0xa78)),displayed=text(get(widget));
    send({event:'query-applied',id:query.id,requested:query.value,stored,displayed,
      original_preserved:stored===query.value&&displayed===query.value});
  } catch(error) { send({event:'query-failed',id:query.id,message:String(error)}); }
  applying=false;
}});
rpc.exports={ready(){return view!==null&&completedFilters>=2;},query(id,value){
  if(!view||pending)throw new Error('Search widget is not ready');
  pending={id,value};return true;
}};
send({event:'ready',pid:Process.id});
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=int, default=600)
    parser.add_argument("--hold", type=int, default=30, help="Seconds to leave the final result visible after validation")
    args = parser.parse_args()
    if not 60 <= args.duration <= 1800:
        raise ValueError("Duration must be 60 to 1800 seconds")
    if not 0 <= args.hold <= 300:
        raise ValueError("Final-result hold must be 0 to 300 seconds")
    if hashlib.sha256(EXE.read_bytes()).hexdigest() != EXPECTED_HASH:
        raise ValueError("Unexpected isolated executable")
    localisation = ROOT / "private/test-mod/localisation/replace/eu4_unicode_probe_l_english.yml"
    if 'ENG:0 "École Straße Ａ 英格兰𠀀"' not in localisation.read_text(encoding="utf-8-sig"):
        raise ValueError("Run prepare-test.ps1 -SystemFonts -SearchProbe first")
    processes = [p for p in psutil.process_iter(["exe"])
                 if p.info["exe"] and Path(p.info["exe"]) == EXE]
    if len(processes) != 1:
        raise RuntimeError("Start exactly one isolated instance first")
    process = processes[0]
    session = frida.attach(process.pid)
    messages = []
    errors = []
    try:
        script = session.create_script(SCRIPT)
        output = ROOT / "private/search-trace.jsonl"
        with output.open("w", encoding="utf-8") as file:
            file.write(json.dumps({"event": "artifact", "exe_sha256": EXPECTED_HASH,
                "dll_sha256": hashlib.sha256((EXE.parent / "plugins/eu4_unicode_probe.dll").read_bytes()).hexdigest(),
                "query_method": "native widget setter and observed change callback; not IME or keyboard"}) + "\n")
            def message(value, data):
                file.write(json.dumps(value, ensure_ascii=True) + "\n")
                file.flush()
                if value.get("type") == "error" or value.get("payload", {}).get("event") == "query-failed":
                    errors.append(value)
                    print(value, flush=True)
                if value.get("type") == "send":
                    messages.append(value["payload"])
                    if value["payload"]["event"] in {"ready", "query-applied"}:
                        print(json.dumps(value["payload"], ensure_ascii=True), flush=True)
            script.on("message", message)
            script.load()
            deadline = time.monotonic() + args.duration
            print("Open diplomacy, then click the country list Name sort. Queries begin after a second filter refresh; empty queries bypass substring matching.", flush=True)
            while not script.exports_sync.ready() and time.monotonic() < deadline and process.is_running() and not errors:
                time.sleep(.25)
            if not script.exports_sync.ready():
                raise RuntimeError("Diplomacy country list was not observed")
            for index, (query, expected, exact) in enumerate(CASES):
                script.exports_sync.query(index, query)
                # A nested NativeFunction refresh may be invisible to the
                # outer Frida callbacks. Observe a natural UI sort refresh.
                print(f"Click Name sort to verify query {index}: {ascii(query)}",flush=True)
                query_deadline = min(deadline, time.monotonic() + 60)
                while not any(e.get("event") == "country-filter" and e.get("id") == index and e.get("examined",0)>0 for e in messages) and time.monotonic() < query_deadline and not errors:
                    time.sleep(.1)
                applied = [e for e in messages if e.get("event") == "query-applied" and e.get("id") == index]
                if not applied or not applied[-1]["original_preserved"]:
                    raise RuntimeError(f"Native query was not preserved: {query!r}")
                filtered=[e for e in messages if e.get("event")=="country-filter" and e.get("id")==index and e.get("examined",0)>0]
                if not filtered or filtered[-1]["query"]!=query:
                    raise RuntimeError(f"No actual native candidates were observed for {query!r}")
                if filtered[-1]["displayed"]!=query:
                    raise RuntimeError(f"The search widget no longer contains {query!r}")
                matches=filtered[-1]["matched"]
                expected_set=[] if expected is None else [expected]
                if (exact and matches!=expected_set) or (not exact and expected not in matches):
                    raise RuntimeError(f"Incorrect native matches for {query!r}: {matches!r}")
                print(f"PASS: query {index}, {filtered[-1]['examined']} candidates, {len(matches)} matches; original preserved", flush=True)
                time.sleep(.5)
            print("Controlled queries finished; final query is U+20000 for UI inspection.", flush=True)
            hold_deadline = min(deadline, time.monotonic() + args.hold)
            while time.monotonic() < hold_deadline and process.is_running() and not errors:
                time.sleep(.25)
        if errors:
            raise RuntimeError("Native search trace failed")
        if not process.is_running():
            raise RuntimeError("Game exited during tracing")
        print(f"Trace saved: {output}", flush=True)
    finally:
        if process.is_running():
            process.terminate()
            process.wait(timeout=10)
        session.detach()


if __name__ == "__main__":
    main()
