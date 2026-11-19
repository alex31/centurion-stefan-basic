#!/usr/bin/env python3
"""Import numbered ASCII BASIC using the real interpreter in a NEW disk copy."""
import argparse,hashlib,json,os,re,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('source',type=Path);p.add_argument('--image',required=True,type=Path)
p.add_argument('--name',required=True);p.add_argument('-o',required=True,type=Path)
a=p.parse_args();source=a.source.resolve(strict=True);seed=a.image.resolve(strict=True);output=a.o.resolve();name=a.name.upper()
if not re.fullmatch('[A-Z0-9_]{1,10}',name) or name.startswith('BFREE'):p.error('invalid or reserved native filename')
if output.exists() or output==seed:p.error('output must be a new distinct image')
try:text=source.read_text(encoding='ascii')
except UnicodeError:p.error('source must be ASCII')
if any(ord(c)<32 and c not in '\r\n\t' or ord(c)>126 for c in text):p.error('unsupported source control character')
lines=[]
for number,line in enumerate(text.splitlines(),1):
 if not line.strip():continue
 m=re.match(r'^\s*(\d+)',line)
 if not m or not 1<=int(m[1])<=65535:p.error('source line %d needs a BASIC line number1..65535'%number)
 if len(line)>125:p.error('source line %d exceeds125 characters'%number)
 lines.append(line)
if not lines:p.error('source has no numbered lines')
work=Path(tempfile.mkdtemp(prefix='basic-import-',dir=os.environ.get('CENTURION_IMAGE_WORK', ROOT/'build/out')));print('Results:',work,flush=True)
(work/'lines.json').write_text(json.dumps(lines));original=hashlib.sha256(seed.read_bytes()).hexdigest()
qa=work/'import.js'
qa.write_text(r'''const fs=require('node:fs'),assert=require('node:assert/strict');
const {OS6Session}=require(process.env.BASIC_SESSION);const sleep=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{const s=new OS6Session(process.env.BASIC_IMAGE);try{
 await s.boot();
 async function cmd(line,expected){const from=s.output.length;await s.send(line+'\r');const end=Date.now()+120000;
  while(!(s.output.length>from&&s.output.endsWith('\n> '))&&Date.now()<end)await sleep(25);
  assert.ok(s.output.length>from&&s.output.endsWith('\n> '),s.output.slice(from));
  const out=s.output.slice(from).replace(line,'');
  assert.ok(!/ABORT\s+\d|JX\s*\d|\bError\b|failed|refused/.test(out),out);
  if(expected)assert.ok(out.includes(expected),out);return out;
 }
 await cmd('STARTBASIC');await cmd('.NEW');
 for(const line of JSON.parse(fs.readFileSync(process.env.BASIC_LINES)))await cmd(line);
 await cmd('.SAVE "'+process.env.BASIC_NAME+'"','Saved');
 await cmd('.NEW');await cmd('.LOAD "'+process.env.BASIC_NAME+'"','Loaded');
 const from=s.output.length;await s.send('.BYE\r');await s.waitFor('FOR SYSRDR',from,120000);await sleep(400);
 await s.command('.STA','STATUS DISPLAY');s.saveNew(process.env.BASIC_OUTPUT);
 }finally{fs.writeFileSync(process.env.BASIC_CONSOLE,s.output);s.stop();}
})().then(()=>process.exit(0),e=>{console.error(e);process.exit(1)});''')
output.parent.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,BASIC_SESSION=str(ROOT/'scripts/os6-session.js'),BASIC_IMAGE=str(seed),BASIC_OUTPUT=str(output),
 BASIC_LINES=str(work/'lines.json'),BASIC_NAME=name,BASIC_CONSOLE=str(work/'console.log'))
with (work/'test.log').open('w') as log:subprocess.run(['node',qa],check=True,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=900)
assert hashlib.sha256(seed.read_bytes()).hexdigest()==original
result={'result':'PASS','nativeTokenization':True,'programFileType':'A','program':name,'source':str(source),'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),
 'inputImage':str(seed),'inputImageUnchanged':True,'image':str(output),'imageSha256':hashlib.sha256(output.read_bytes()).hexdigest(),'lines':len(lines)}
output.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS imported:',name,'into',output)
