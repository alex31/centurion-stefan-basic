#!/usr/bin/env python3
"""Install and boot-test BASIC in a NEW native CENTOS disk image."""
import argparse,hashlib,json,os,re,subprocess,sys,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from centos_text import encode
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('directory',type=Path,help='directory containing the built BASIC.json and components')
p.add_argument('-o',required=True,type=Path,help='new output image; never overwrite an existing disk')
p.add_argument('--seed',type=Path,default=ROOT/'build/out/image/CENTOS_13.IMG')
p.add_argument('--slots',type=int,default=16,help='preallocated native program files (including BDEMO)')
p.add_argument('--example',action='append',default=[],metavar='NAME=SOURCE',help='install an ASCII BASIC source in SBASIC')
p.add_argument('--check-games',action='store_true',help='also verify XCHESS and XCONNECT4 from the seed image')
a=p.parse_args();directory=a.directory.resolve();seed=a.seed.resolve(strict=True);output=a.o.resolve()
if not 1<=a.slots<=128:p.error('--slots must be 1..128')
if output.exists() or output==seed:p.error('output must be a new distinct image')
d=json.loads((directory/'BASIC.json').read_text())
if d.get('format')!='cpu6-stefan-mmu-overlays' or not {'files','directory','commands'}<={g['label'] for g in d['overlays']}:
 p.error('build the interactive application with --commands first')
work=Path(tempfile.mkdtemp(prefix='basic-install-',dir=os.environ.get('CENTURION_IMAGE_WORK', ROOT/'build/out')));print('Results:',work,flush=True)
original=hashlib.sha256(seed.read_bytes()).hexdigest()
job=work/'SBASIC.BASIC.txt';job.write_bytes(encode((directory/'BASIC.job').read_text(encoding='ascii')))
entry=work/'BASIC.txt';entry.write_bytes(encode('SBASIC.BASIC ON 1\n'))
probe=work/'BASPROBE.exf'
subprocess.run([ROOT/'run/cpu6-os6-sdcc','--gc-sections',HERE/'mmu-runtime/probe.c',
 ROOT/'os6/mmu/gate.s','-o',probe],check=True)
files=[f'SBASIC.BASICEXE={directory / "BASIC.exf"}',f'SBASIC.{d["moduleName"]}={directory / "BASIC.mmu.exf"}',
 f'SBASIC.BASPROBE={probe}']
files += ['SBASIC.'+g['name']+'='+str(directory/(g['name']+'.exf')) for g in d['overlays']]
# Keywords expand by at most 8x in this profile; native record padding can
# nearly double that. Allow also the extra CR markers and a final sector.
sectors=(17*d['programMemory']+400+2+399)//400
allocate=[sys.executable,ROOT/'scripts/install-os6-bundle.py',seed,*files,'--library','SBASIC',
 '--job',f'BASIC={entry}','--job',f'SBASIC.BASIC={job}']
for spec in a.example:
 name,separator,source=spec.partition('=')
 if not separator or not re.fullmatch('[A-Z][A-Z0-9]{0,9}',name) or name.startswith('BFREE') or name in {'BASIC','BDEMO'}:
  p.error('invalid example: '+spec)
 encoded=work/(name+'.txt');encoded.write_bytes(encode(Path(source).read_text(encoding='ascii')))
 allocate+=['--job',f'SBASIC.{name}={encoded}']
for i in range(a.slots):allocate += ['--data-file','SBASIC.BFREE%04d:%d:A'%(i,sectors)]
with (work/'allocate.log').open('w') as log:
 subprocess.run([*allocate,'-o',work/'installed.img'],check=True,stdout=log,stderr=subprocess.STDOUT)
qa=HERE/'verify-image.js'
env=dict(os.environ,BASIC_SESSION=str(ROOT/'scripts/os6-session.js'),BASIC_IMAGE=str(work/'installed.img'),
 BASIC_OUTPUT=str(output),BASIC_SAMPLE=str(HERE/'demo.bas'),
 BASIC_GAMES='1' if a.check_games else '0',BASIC_RESULT=str(work/'validation.json'),BASIC_CONSOLE=str(work/'console.log'))
output.parent.mkdir(parents=True,exist_ok=True)
with (work/'verify.log').open('w') as log:subprocess.run(['node',qa],env=env,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=420)
assert hashlib.sha256(seed.read_bytes()).hexdigest()==original
result=json.loads((work/'validation.json').read_text())
result.update(image=str(output),imageSha256=hashlib.sha256(output.read_bytes()).hexdigest(),sourceImage=str(seed),sourceSha256=original,
 sourceUnchanged=True,library='SBASIC',programSlots=a.slots,freeSlots=a.slots-1,programFileType='A',programFileSectors=sectors,programMemory=d['programMemory'],stackBytes=d['stackBytes'],
 residentEnd=d['residentEnd'],moduleBytes=d['moduleBytes'],components={item.split('=',1)[0]:hashlib.sha256(Path(item.split('=',1)[1]).read_bytes()).hexdigest() for item in files})
output.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS installed and boot-tested:',output,flush=True)
