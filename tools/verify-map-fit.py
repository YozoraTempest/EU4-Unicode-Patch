"""Check the native map font measuring path during owned game startup.

Attach before fonts load. The probe calls native measurement with short test
strings, compares widths with loaded glyph advances and ASCII kerning, and
checks complete terminal UTF-8 scalars. It waits for game exit before detach.
"""
import hashlib
import argparse
import json
from pathlib import Path
import re
import time

import frida
import psutil

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--font-address', help='A current font address obtained from the native startup observer')
args = parser.parse_args()
RUNTIME = ROOT/'private/player-install/Europa Universalis IV'
EXE_HASH = '9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a'
assert hashlib.sha256((RUNTIME/'eu4.exe').read_bytes()).hexdigest() == EXE_HASH
processes = [p for p in psutil.process_iter(['exe']) if p.info['exe'] and
             Path(p.info['exe']).resolve() == (RUNTIME/'eu4.exe').resolve()]
assert len(processes) == 1, 'Start the owned game, then attach before fonts load'
process = processes[0]
mapping = (ROOT/'build/eu4_unicode_patch.map').read_text(encoding='utf-8')
symbols = {name:int(re.search(r'\s'+name+r'\s+([0-9a-fA-F]+) f',mapping)[1],16)-0x180000000
           for name in ['find_supplementary_glyph','map_last_scalar_offset','copy_last_map_scalar']}
values = ['A','AA','乌普兰','东约特兰','钦察','匈牙利','中文测试','中A文','中 文','𠮷野家','§Y中文§!']
cases = [{'text':v,'bytes':list(v.encode('utf-8')),'offset':len(v[:-1].encode('utf-8')),
          'last':v[-1] if v else ''} for v in values+['']]
session = frida.attach(process.pid)
records = []
ready = False
source = r'''
const base=Process.mainModule.base, patch=Process.getModuleByName('eu4_unicode_patch.dll');
const symbols=__SYMBOLS__,cases=__CASES__;
const sparse=new NativeFunction(patch.base.add(symbols.find_supplementary_glyph),'pointer',['pointer','uint']);
const lastOffset=new NativeFunction(patch.base.add(symbols.map_last_scalar_offset),'uint64',['pointer']);
const lastCopy=new NativeFunction(patch.base.add(symbols.copy_last_map_scalar),'uint64',['pointer','pointer']);
const kern=new NativeFunction(base.add(0x15943d0),'float',['pointer','uint','uint']);
let done=false;
function text(p){const n=p.add(16).readU64().toNumber();return (p.add(24).readU64().toNumber()<16?p:p.readPointer()).readUtf8String(n);}
function engineString(value){
 const p=Memory.alloc(32),data=Memory.alloc(value.bytes.length+1);
 data.writeByteArray([...value.bytes,0]);
 if(value.bytes.length<16){p.writeByteArray([...value.bytes,0]);p.add(24).writeU64(15);}
 else {p.writePointer(data);p.add(24).writeU64(value.bytes.length);}
 p.add(16).writeU64(value.bytes.length);return {p,data};
}
function glyph(table,scalar){
 let p=scalar<=255?table.add(scalar*8).readPointer():sparse(table,scalar);
 if(p.isNull()&&scalar>=32){p=sparse(table,0x2026);if(p.isNull())p=table.add(0x3f*8).readPointer();}
 return p;
}
function expectedWidth(f,value){
 const chars=Array.from(value.replace(/§[A-Za-z!]/g,'')),table=f.add(0x120),scale=f.add(0x968).readFloat();
 let width=0;
 for(let i=0;i<chars.length;i++){
  const scalar=chars[i].codePointAt(0),p=glyph(table,scalar);
  if(p.isNull())continue;
  width+=p.add(12).readS16()*scale;
  const next=i+1<chars.length?chars[i+1].codePointAt(0):null;
  if(p.add(14).readU8()&&scalar<128&&next!==null&&next<128)width+=kern(table,scalar,next);
 }
 return Math.trunc(width);
}
function verifyFont(f){
 if(done)return;
 try{
  const path=text(f.add(0xe0));if(!/map/i.test(path)||f.add(0x960).readS32()!==88)return;
  done=true;
  const measure=new NativeFunction(f.readPointer().add(0x78).readPointer(),'int',
     ['pointer','pointer','int','int','pointer','pointer','bool']);
  const results=[];
  for(const value of cases){
   const string=engineString(value),margin=Memory.alloc(8),size=Memory.alloc(8),last=Memory.alloc(8);
   margin.writeU64(0);size.writeU64(0);last.writeU64(0);
   const lines=measure(f,string.p,100000,100000,margin,size,1);
   const bytes=lastCopy(string.p,last).toNumber(),offset=lastOffset(string.p).toNumber();
   results.push({text:value.text,width:size.readS32(),expected_width:expectedWidth(f,value.text),height:size.add(4).readS32(),lines,
    last_offset:offset,expected_last_offset:value.offset,last:last.readUtf8String(bytes),expected_last:value.last});
  }
  send({event:'map-fit',font:path,measure_rva:f.readPointer().add(0x78).readPointer().sub(base).toString(),results});
 }catch(e){send({event:'failure',message:String(e),stack:e.stack});}
}
Interceptor.attach(base.add(0x15953c0),{onEnter(args){this.font=args[0];},onLeave(){verifyFont(this.font);}});
send({event:'ready'});
if(__FONT__!==null)verifyFont(ptr(__FONT__));
'''.replace('__SYMBOLS__',json.dumps(symbols)).replace('__CASES__',json.dumps(cases,ensure_ascii=True)).replace('__FONT__',json.dumps(args.font_address))
script = session.create_script(source)
output = ROOT/'private/map-fit-native.json'
def message(value,data):
    global ready
    records.append(value)
    if value.get('type') == 'send' and value.get('payload',{}).get('event') == 'map-fit':
        result = value['payload']
        valid = all(r['width']==r['expected_width'] and r['last_offset']==r['expected_last_offset'] and
                    r['last']==r['expected_last'] for r in result['results'])
        report = {'dll_sha256':hashlib.sha256((RUNTIME/'plugins/eu4_unicode_patch.dll').read_bytes()).hexdigest(),
                  'exe_sha256':EXE_HASH,'method':'Controlled native map measurement and terminal scalar copy calls',
                  'passed':valid,**result}
        output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        ready = True
        print(json.dumps(report,ensure_ascii=False),flush=True)
    elif value.get('type')!='send' or value.get('payload',{}).get('event') in {'ready','failure'}:
        print(json.dumps(value),flush=True)
script.on('message',message);script.load()
while process.is_running():time.sleep(.5)
session.detach()
assert ready, records
assert json.loads(output.read_text(encoding='utf-8'))['passed'], 'Native map fit regression failed'
