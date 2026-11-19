#!/usr/bin/env python3
"""Link Stefan BASIC into resident/MMU code plus guarded cold-code overlays.

All images share one logical link layout and one resident runtime/state area.
Typed permanent wrappers restore their caller's code before returning. Cross-
group arguments refer only to resident data or the C stack, never cold constants.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'scripts'))
from cpu6_gc import read_units,select,clean,INTERNAL
from cpu6_object import attach,symbols

# C names, return type, parameter types. Static helpers and shared globals
# remain permanent; a group can use them without switching another overlay.
GROUPS=[
 ('lexer',{'lex_input':('void',[]),'storeline':('void',[]),'storetoken':('void',[]),
           'parsenumber':('uint16_t',['char *','float *']),
           'parsenumber2':('uint16_t',['char *','float *']),
           'parsenumbern':('uint16_t',['char *','float *']),
           'xpeek':('void',[])}),
 ('circular',{'__c6_sin':('float',['float']),'__c6_cos':('float',['float']),
          '__c6_tan':('float',['float']),'__c6_atan':('float',['float']),
          '__c6_reduce':('unsigned',['uint32_t','float *'])}),
 ('listing',{'listlines':('void',['uint16_t','uint16_t']),'outputtoken':('void',[])}),
 ('powers',{'__c6_exp':('float',['float']),'__c6_log':('float',['float']),
          '__c6_fscale':('uint32_t',['uint32_t','int']),'sb_pow':('float',['float','float'])}),
 ('state',{'sb_mod':('float',['float','float']),'__c6_fsqrt':('uint32_t',['uint32_t']),
           'xrnd':('void',[]),'bas_random':('float',['float']),
           'array':('void',['void *','char','float *']),
           'xnew':('void',[]),'resetbasicstate':('void',[]),'displaybanner':('void',[])})]
# Audited immutable table byte images from lib/float32/{atan,exp,log,reduce,
# trig}. Match the data bytes as well as the library: future changed tables
# stay resident until re-audited. Mutable initialized data is never inferred.
FLOAT_CONSTANTS={
 '8ca291cf7fbbc462879c94b92044028771cea1b71abd5b9b9aa3b41aa57678cb',
 'b3f8167c71ceb4cd2df7575037755ab6c59f1bf6ede96e58b47d23b13e7f8edb',
 '07bff22bd0eb3e12cefe1d243cc0f70425f3ab4c4684c3486c12ec84cf7eb005',
 '421762bc1edc94f0bca53e8f6644a7effab9deb1dfc341166f3d40db808c3519',
 'ea66726b9b83609fd84180f7c0e85f612317ca1fa614dbc4f29ae08f730ade2c',
 '932c61a618f867c3e5a79419a6ab2cd337ee9942daa236d5ddb80f2322d9e7e3'}
TOKENS=re.compile(r'"(?:[^"\\]|\\.)*"|\b[A-Za-z_]\w*\b')
FUNCTION=re.compile(r'^; CPU6 function (\w+) begin$',re.M)
SEGMENT=re.compile(r'^\s*\.(code|data|bss|literal)\s*$')

def maps(path):
 return {f[2]:int(f[0],16) for line in path.read_text().splitlines() if len(f:=line.split())==3}

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('-o',required=True,type=Path)
 parser.add_argument('--commands',action='store_true',help='build the CENTOS REPL and bytecode VM with float32 and strings')
 parser.add_argument('--vm-math',action='store_true',help='keep common mathematical functions available to the running VM')
 parser.add_argument('--file-service',type=Path,default=HERE/'files.c')
 parser.add_argument('--script',action='store_true',help='build the deterministic core qualification input')
 parser.add_argument('--stack-size',type=int,default=3072)
 parser.add_argument('--memory',type=int,default=4096)
 parser.add_argument('--mmu-bytes',type=int,choices=[20480,22528,24576],default=20480,help='larger values are capacity experiments until system-qualified')
 parser.add_argument('--save-temps',type=Path)
 parser.add_argument('--profile',choices=['speed','size','auto'],default='speed')
 parser.add_argument('--no-auto-inline',action='store_true',help='explicit measured size fallback only')
 parser.add_argument('--no-lto',action='store_true',help='build a comparison without inter-file inlining')
 parser.add_argument('--vm-c',action='store_true',help='use portable C bytecode word accesses and integer arithmetic')
 parser.add_argument('--lib-root',type=Path,default=ROOT/'build/out/toolchain/lib')
 args=parser.parse_args()
 if args.vm_math and not args.commands:parser.error('--vm-math requires --commands')
 if args.profile=='auto':
  runpy.run_path(str(HERE/'select-profile.py'))['build'](sys.argv[1:],args);return
 if not 256<=args.stack_size<=8192 or not 4096<=args.memory<=16384:parser.error('invalid stack or program reservation')
 output=args.o.resolve();name=output.stem.upper()
 if output.suffix!='.exf' or not re.fullmatch('[A-Z][A-Z0-9]{0,9}',name):parser.error('-o needs a CENTOS name with .exf suffix')
 output.parent.mkdir(parents=True,exist_ok=True)
 sources=[ROOT/'swext/stefan-basic/src/basic.c',HERE/'runtime.c',HERE/'main.c',HERE/'power.c',HERE/'decimal.c']
 if args.commands:
  sources += [HERE/n for n in ['commands.c','program.c','editor.c','fileio.c','text.c','bytecode.c','bc-compile.c','bc-expr.c','bc-run.c','bc-values.c','bc-strings.c','bc-data.c']]+[args.file_service.resolve()]
  # Read and tokenize every text record in the same slot, including the
  # exact numeric parser, from the first open to the second close.
  GROUPS[0][1].update({'bas_stage':('int',['char *']),'bas_copy':('void',['unsigned','unsigned']),
   'bas_read_source':('int',['const char *','unsigned']), 'bas_io_read':('int',[]),
   'bas_io_open':('int',['const char *','unsigned']),
   'bas_atof':('unsigned',['const char *','float *']),
   'bas_stage_immediate':('int',['char *'])})
  GROUPS[0][1]['bas_ed_token']=('void',['unsigned','unsigned'])
  GROUPS[2][1].update({'loop':('void',[]),'bas_command':('int',['char *']),
   'bas_editor':('void',['unsigned']),
   'bas_ed_note':('void',['unsigned','unsigned','unsigned'])})
  GROUPS[4][1].update({n:('void',[]) for n in ('esave','eload')})
  GROUPS.append(('commands',{'bas_parse':('unsigned',['const char *']),'bas_renum':('unsigned',['unsigned']),
   'xinput':('void',[]),'innumber':('int',['float *','char *','uint16_t'])}))
  # Close/unassign is permanent. Finalizing a write shares the writer's
  # overlay; closing a read takes no trip through it.
  GROUPS.append(('files',{'bas_save_file':('void',[]),'bas_io_write':('int',[]),
   'bas_finish_text':('int',[])}))
  if args.file_service.resolve()!=HERE/'files.c':
   GROUPS[-1][1]['bas_files']=('void',['unsigned'])
  GROUPS.append(('text',{'bas_render':('unsigned',['unsigned']),
   'bas_ftoa':('unsigned',['char *','float'])}))
  GROUPS.append(('directory',{'bas_query':('unsigned',['unsigned']),'bas_publish':('void',[]),
   'bas_dir_rename':('int',['const char *','const char *']),
   'bas_dir_open':('int',[]),'bas_dir_next':('unsigned char *',[]),'bas_dir_close':('int',[])}))
  # Input and bytecode emission use the same memory at different times.
  # Place the expression parser with the statements that call it.
  GROUPS.append(('bytecode-compile',{'ib_compile':('int',['void *','unsigned']),
   'ib_declare_int':('void',['void *']),
   # Keep less common statement bodies in the small compiler overlay.
   # DATA validation stays with the source walk.
   'ib_read_stmt':('void',['void *']), 'ib_restore_stmt':('void',['void *']),
   'ib_while_stmt':('void',['void *']), 'ib_wend_stmt':('void',['void *']),
   'ib_compile_immediate':('int',['void *','unsigned','unsigned'])}))
  GROUPS.append(('bytecode-stmt',{n:('void',['void *']) for n in
   ('ib_lines','ib_stmt','ib_assign','ib_for_stmt','ib_next_stmt','ib_print_stmt','ib_dimension')}))
  GROUPS[-1][1].update({'ib_expr':('unsigned',['void *']), 'ib_expr_at':('unsigned',['void *','unsigned']),
   'ib_primary':('unsigned',['void *']), 'ib_coerce':('void',['void *','unsigned','unsigned'])})
  # The VM and its numeric operations share an overlay. A floating-point
  # opcode can call its implementation without replacing the running VM.
  vm_functions={'ib_execute':('int',['void *']), 'ib_for_real':('int',['void *','void *','unsigned']),
                'ib_float':('int',['unsigned','void *','void *']),
                'ib_input':('int',['unsigned','void *'])}
  if not args.vm_c:
   vm_functions.update({n:('int',['void *','int']) for n in ('ib_add16','ib_mul16')})
   vm_functions['ib_dispatch']=('int',['void *'])
  if args.vm_math:
   # Number input shares its parser with the lexer. Keep the range reducer
   # and logarithm in mapped code so the trigonometric functions and LOG
   # can execute without replacing the VM.
   vm_functions.pop('ib_input')
   GROUPS[0][1]['ib_input']=('int',['unsigned','void *'])
   GROUPS[:]=[entry for entry in GROUPS if entry[0] not in ('circular','powers')]
   vm_functions.update({name:('float',['float']) for name in ('__c6_sin','__c6_cos','__c6_tan')})
   cold_math=next(functions for label,functions in GROUPS if label=='state')
   cold_math.update({'__c6_atan':('float',['float']),'__c6_exp':('float',['float']),
                     '__c6_fscale':('uint32_t',['uint32_t','int']),
                     'sb_pow':('float',['float','float'])})
   for function_name in ('__c6_fsqrt','bas_random'):
    vm_functions[function_name]=cold_math.pop(function_name)
  GROUPS.append(('bytecode-run',vm_functions))
  GROUPS.append(('display',{'writenumber2':('uint16_t',['char *','float']),
                           'tinydtostrf':('uint16_t',['float','int','char *']),
                           'writenumber':('uint16_t',['char *','int32_t']),
                           'outnumber':('void',['float']), 'ib_print':('void',['unsigned','void *']),
                           'debugtoken':('void',[]), 'xlist':('void',[])}))
 tools=ROOT/'build/out/toolchain';libs=args.lib_root.resolve();sdk=HERE/'mmu-runtime'
 libraries=[libs/'os6/libprintf-sdcc.a',libs/'os6/liboverlay-sdcc.a',libs/'os6/libos6.a',
            libs/'cpu6/libfloat32-sdcc.a',libs/'cpu6/libcompiler-sdcc.a']
 inputs=[*sources,*sorted(HERE.glob('*.h')),*sorted((ROOT/'os6/include').glob('*.h')),*sorted((ROOT/'swext/stefan-basic/src').glob('*.h')),*[sdk/n for n in ['runtime.c','gate.s','module-start.s']],
         ROOT/'os6/crt0.s',ROOT/'os6/file.c',ROOT/'os6/include/os6file.h',HERE/'clock.s',HERE/'console.s',Path(__file__),ROOT/'scripts/cpu6_lto.py',tools/'sdcc-cpu6/bin/sdcc',*libraries]
 if args.commands:inputs.append(HERE/'bc-ops.def')
 if args.commands and not args.vm_c:inputs.extend([HERE/'bc-integer.s',HERE/'bc-dispatch.S'])
 digest=hashlib.sha256()
 input_hashes={}
 for path in inputs:
  content=path.read_bytes();digest.update(content);input_hashes[str(path)]=hashlib.sha256(content).hexdigest()
 digest.update(str((args.stack_size,args.memory,args.script,args.commands,args.mmu_bytes,args.profile,args.no_auto_inline,args.no_lto,args.vm_c,args.vm_math)).encode())
 identity=digest.digest()[:4];idtext=identity.hex().upper();module_name='M'+idtext
 if len(GROUPS)>16:raise ValueError('overlay filename scheme supports sixteen groups')
 group_names=['O'+format(i,'X')+idtext for i in range(len(GROUPS))]
 flags=['-mcpu6','--cpu6-float32','-S','--std=c99','--opt-code-'+args.profile,
        '--disable-warning','283','-D__cpu6__=1','-D__ee200__=1','-D__CPU6_OS6__=1',
        '-DSTEFAN_TINY','-DSTEFAN_CENTOS','-DSTEFAN_FLOAT','-DMEMSIZE='+str(args.memory),
        '-I'+str(ROOT/'os6/include'),'-I'+str(ROOT/'baremetal/include'),'-I'+str(ROOT/'swext/stefan-basic/src')]
 if args.script:flags.append('-DSTBASIC_SCRIPT')
 if args.commands:flags.append('-DSTBASIC_COMMANDS')
 if args.no_auto_inline:flags.append('--cpu6-no-auto-inline')
 if args.commands and not args.vm_c:flags.append('-DSTBASIC_VM_ASM')
 work=Path(tempfile.mkdtemp(prefix='basic-images-',dir=os.environ.get('CENTURION_IMAGE_WORK', ROOT/'build/out')))
 commands=[]
 print('Results:',work,flush=True)
 def run(command,logname=None):
  commands.append(list(map(str,command)))
  with (work/(logname or 'tools.log')).open('a') as log:
   subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT)
 def assemble(text,stem):
  asm=work/(stem+'.s');obj=asm.with_suffix('.o')
  (work/(stem+'.ir.s')).write_text(text)
  asm.write_text('\n'.join(line for line in text.splitlines() if not line.lstrip().startswith(';'))+'\n')
  run([tools/'bin/asee200','-o',obj,asm]);attach(obj,text);return obj
 def compile(source,stem,extra=()):
  asm=work/(stem+'.compiler.s')
  run([tools/'sdcc-cpu6/bin/sdcc',*flags,*extra,'-I'+str(source.parent),'-o',asm,source],stem+'-compile.log')
  return assemble(asm.read_text(),stem)
 def absolute(values,stem):
  # EQU must precede EXPORT: the assembler clears public flags on definition.
  lines=['.setcpu 6']
  for key,value in sorted(values.items()):
   if key in INTERNAL:continue
   lines += [key+' equ '+str(value),'.export '+key]
  return assemble('\n'.join(lines)+'\n',stem)
 try:
  # Run the same inter-file optimizer as the ordinary C driver before assigning
  # bodies to MMU/overlay groups. Whole-program reachability is still applied.
  assemblies=[]
  # The statement compiler is cold during program execution and occupies
  # the largest overlay. Keep it compact and share its parser helpers.
  unit_options={}
  if args.commands:
   unit_options[HERE/'bc-compile.c']=['--opt-code-size']
   if not args.no_auto_inline:
    unit_options[HERE/'bc-compile.c'].append('--cpu6-no-auto-inline')
    unit_options[HERE/'decimal.c']=['--cpu6-no-auto-inline']
  for i,source in enumerate(sources):
   asm=work/('user%d.compiler.s'%i)
   run([tools/'sdcc-cpu6/bin/sdcc',*flags,*unit_options.get(source,()),'-I'+str(source.parent),'-o',asm,source],'user%d-compile.log'%i)
   assemblies.append(asm)
  if not args.no_lto:
   # Share the hot primitives and the expression entry across call sites so
   # input and bytecode emission fit the shared overlay slot.
   runpy.run_path(str(ROOT/'scripts/cpu6_lto.py'))['optimize'](assemblies,work/'lto-report.json',no_inline={'_ib_emit','_ib_expr','_outsc'})
  user=[assemble(asm.read_text(),'user%d'%i) for i,asm in enumerate(assemblies)]
  user.append(assemble((HERE/'clock.s').read_text(),'clock'))
  user.append(assemble((HERE/'console.s').read_text(),'console'))
  if args.commands and not args.vm_c:
   user.append(assemble((HERE/'bc-integer.s').read_text(),'bc-integer'))
   # The dispatcher checks that the byte is inside the handler table.
   count=len(re.findall(r'^IB_OP\(', (HERE/'bc-ops.def').read_text(), re.M))
   if not 0<count<=256:raise ValueError('bytecode opcode count exceeds byte encoding')
   dispatch=work/'bc-dispatch.pre.s'
   run([tools/'sdcc-cpu6/bin/sdcpp','-P','-I'+str(HERE),'-I'+str(work),HERE/'bc-dispatch.S',dispatch])
   user.append(assemble(dispatch.read_text(),'bc-dispatch'))
  runtime=compile(sdk/'runtime.c','mmu-runtime',['-DMMU_ID1='+str(int.from_bytes(identity[:2],'big'))+'U',
                                               '-DMMU_ID2='+str(int.from_bytes(identity[2:],'big'))+'U'])
  gate=assemble((sdk/'gate.s').read_text(),'mmu-gate')
  prefix_text=(sdk/'module-start.s').read_text();prefix=assemble(prefix_text,'mmu-prefix')
  crt_text=(ROOT/'os6/crt0.s').read_text().replace('jsr _main','jsr ___mp_launch')
  crt_text=crt_text.replace('__osstack:\n\t.ds 1024','__osstack:\n\t.ds '+str(args.stack_size))
  crt_text+='\n.export __osstack\n.export __osstack_end\n'
  crt=assemble(crt_text,'crt0')
  initial=assemble('.setcpu 6\n.literal\n.export ___mp_initial\n___mp_initial:\n.word 0\n','initial')
  paths=[crt,*user,runtime,gate,prefix,initial,libs/'os6/putchar.o',libs/'os6/puts.o',*libraries]
  units=read_units(paths);live=select(units,['start','__mmu_start'])
  definitions={}
  for u in units:
   for symbol in u.exports:definitions.setdefault(symbol,[]).append(u.local[symbol])
  def resolve(u,symbol):
   if symbol in u.local:return u.local[symbol]
   choices=definitions.get(symbol,[])
   return sorted(choices,key=lambda a:a.unit.library)[0] if choices else None
  entries={a:FUNCTION.search('\n'.join(a.lines))[1][:16] for u in units for a in u.atoms
           if a in live and a.kind=='function' and FUNCTION.search('\n'.join(a.lines))}
  cold={};prototypes={};real={}
  for group,(_,functions) in enumerate(GROUPS):
   for cname,signature in functions.items():
    symbol=('_'+cname)[:16]
    choices=definitions.get(symbol,[])
    atom=sorted(choices,key=lambda a:a.unit.library)[0] if choices else None
    if atom is not None and atom in live:
     if atom.kind!='function' or atom.unit.opaque:raise ValueError('cold function has no usable IR: '+symbol)
     cold[atom]=group;prototypes[atom]=(cname,*signature);real[atom]='_ovf%02d_%03d'%(group,len(real))
  # Private helpers and compiler string constants referenced exclusively by
  # one cold group can move with that group. Mutable data always stays fixed.
  consumers={a:set() for a in live}
  for u in units:
   for a in u.atoms:
    if a in live:
     for ref in a.refs:
      target=resolve(u,ref)
      if target in consumers and target is not a:consumers[target].add(a)
  def float_library(a):return a.unit.name.startswith(str(libs/'cpu6/libfloat32-sdcc.a')+'(')
  def float_constant(a):
   if not float_library(a) or a.kind!='data' or a.segment!='data':return False
   data=bytearray()
   for line in a.lines:
    statement=clean(line)
    if not statement or statement.endswith(':'):continue
    if not statement.startswith('.byte '):return False
    try:data.extend(int(value,0)&255 for value in statement[6:].split(','))
    except ValueError:return False
   return hashlib.sha256(data).hexdigest() in FLOAT_CONSTANTS
  changed=True
  while changed:
   changed=False
   for a in live:
    if a in cold or a.unit.opaque:continue
    private=a.kind=='function' and a in entries and (not (a.defined & a.unit.exports) or (float_library(a) and entries[a].startswith('__r_')))
    string=a.kind=='data' and a.segment=='data' and all(n.startswith('___str_') for n in a.defined)
    users=consumers[a]
    if (private or string or float_constant(a)) and users and all(u in cold for u in users):
     owners={cold[u] for u in users}
     if len(owners)==1:
      cold[a]=owners.pop()
      if private:real[a]='_ovf%02d_%03d'%(cold[a],len(real))
      changed=True
  # Calls within a group are already rebound directly to its real entries.
  # Keep permanent ABI wrappers only for entries used outside that group.
  prototypes={a:signature for a,signature in prototypes.items()
              if any(u not in cold or cold[u]!=cold[a] for u in consumers[a])}
  missing=[i for i in range(len(GROUPS)) if i not in cold.values()]
  if missing:raise ValueError('empty cold group '+str(missing))
  naming={u:{s:('Q%03x%08x'%(i,j)) for j,s in enumerate(sorted(u.local)) if s not in u.exports} for i,u in enumerate(units)}
  def normal(u,s):return naming[u].get(s,s)
  def symbol_for(u,s,group=None):
   a=resolve(u,s[:16])
   if group is not None and a in cold and cold[a]==group and entries.get(a)==s[:16]:return real[a]
   return normal(u,s[:16]) if s[:16] in u.local else s
  def lines(a,segment=None,group=None):
   result=[]
   for line in a.lines:
    if SEGMENT.match(line):result.append('.'+(segment or SEGMENT.match(line)[1]));continue
    result.append(TOKENS.sub(lambda m:m[0] if m[0].startswith('"') else symbol_for(a.unit,m[0],group),line))
   return result
  def exports(a,group=None):
   return ['.export '+symbol_for(a.unit,s,group) for s in sorted(a.defined)]
  # Measure every cold body without resolving absolute imports yet.
  groups=[]
  for i,(label,_) in enumerate(GROUPS):
   header=[0x42,0x4f,1,i,*identity]
   objects=[assemble('.setcpu 6\n.code\n.byte '+','.join(map(str,header))+'\n','overlay%d-header'%i)]
   for a in cold:
    if cold[a]==i:
     text=['.setcpu 6','.code',*exports(a,i),*lines(a,'code',i)]
     objects.append(assemble('\n'.join(text)+'\n','overlay%d-function%03d'%(i,len(objects))))
   size=sum(symbols(obj.read_bytes())[2][1] for obj in objects)
   groups.append({'group':i,'label':label,'name':group_names[i],'bytes':size,'objects':objects})
  slot_size=max(g['bytes'] for g in groups)
  # One code slot with a bounded resident caller stack. The audited nested
  # calls pass only stack or permanent data pointers; wrappers restore code
  # before returning. The SDK load-only API validates each image identity.
  loader=work/'loader.c'
  descriptors=','.join('{"%s",1,%d,0}'%(g['name'],g['bytes']) for g in groups)
  loader.write_text('''#include <stdint.h>
#include <os6overlay.h>
#include "hardware.h"
#include "language.h"
#include "basic.h"
extern mem_t st;extern token_t token;
unsigned char bas_ov_area['''+str(slot_size)+'''];
static struct os6_ov_slot slot;
static struct os6_ov_handle handle;
static const struct os6_ov_desc modules[]={'''+descriptors+'''};
static unsigned ready,active,loaded=65535U;
static unsigned char callers[8];
void bas_ov_abort(void);
static const unsigned char identity[]={'''+','.join(map(str,identity))+'''};
/* Report without a numeric line prefix: the display overlay itself might be
   the failed image. error() still clears all interpreter stacks. */
static unsigned failed(void){st=SINT;error(EFILE);token=EOL;return 0;}
static unsigned load(unsigned group){unsigned i;
 if(loaded==group)return 1;
 loaded=65535U;if(os6_ov_load(&slot,&modules[group],&handle))return 0;
 if(bas_ov_area[0]!=0x42||bas_ov_area[1]!=0x4f||bas_ov_area[2]!=1||bas_ov_area[3]!=group)return 0;
 for(i=0;i<4;i++)if(bas_ov_area[i+4]!=identity[i])return 0;loaded=group;return 1;
}
static void restore(void){
 if(active&&!load(callers[active-1])){puts("Overlay restore failed");bas_ov_abort();}
}
unsigned bas_ov_begin(unsigned group){
 if(group>=sizeof(modules)/sizeof(modules[0])||active==sizeof(callers))return failed();
 if(!ready){if(os6_ov_init(&slot,bas_ov_area,sizeof(bas_ov_area)))return failed();ready=1;}
 if(!load(group)){restore();return failed();}
 callers[active++]=(unsigned char)group;return 1;
}
void bas_ov_end(void){if(active){active--;restore();}}
''')
  loader_obj=compile(loader,'loader')
  wrapper=work/'wrappers.c';text=['#include <stdint.h>','extern unsigned bas_ov_begin(unsigned);','extern void bas_ov_end(void);']
  for a,(cname,ret,types) in prototypes.items():
   params=','.join(t+' a'+str(i) for i,t in enumerate(types)) or 'void'
   arguments=','.join('a'+str(i) for i in range(len(types)))
   actual=real[a][1:]
   text.append('extern '+ret+' '+actual+'('+params+');')
   text.append(ret+' '+cname+'('+params+'){')
   if ret!='void':text.append(ret+' result;')
   text.append('if(!bas_ov_begin(%dU))return%s;'%(cold[a],'' if ret=='void' else ' 0'))
   text.append(('' if ret=='void' else 'result=')+actual+'('+arguments+');')
   text.append('bas_ov_end();'+('' if ret=='void' else 'return result;')+'}')
  wrapper.write_text('\n'.join(text)+'\n')
  wrapper_obj=compile(wrapper,'wrappers')
  # Bring in the loader's library closure. Original cold definitions stay in
  # this graph only to retain their shared data/helper dependencies.
  full_paths=[*paths[:8],loader_obj,*paths[8:]]
  expanded=read_units(full_paths);extra_live=select(expanded,['start','__mmu_start','_bas_ov_begin','_bas_ov_end'])
  # Reuse original units/atoms by input name, preserving their private names.
  originals={u.name:u for u in units};additional=[]
  for u in expanded:
   if u.name in originals:
    old=originals[u.name]
    for a in u.atoms:
     if a in extra_live:
      match=next((b for b in old.atoms if b.defined==a.defined),None)
      if match is not None:live.add(match)
   else:
    additional.append(u)
    for a in u.atoms:
     if a in extra_live:live.add(a)
  wrapper_unit=read_units([wrapper_obj])[0];additional.append(wrapper_unit);live.update(wrapper_unit.atoms)
  units.extend(additional)
  for index,u in enumerate(units):
   if u not in naming:naming[u]={s:'Q%03x%08x'%(index,j) for j,s in enumerate(sorted(u.local)) if s not in u.exports}
  # Pick permanent function bodies for the lower window. Never place launch
  # runtime or address-taken callbacks there; all persistent data stays upper.
  taken=set()
  all_entries={name for name in entries.values()}
  for u in units:
   for a in u.atoms:
    if a not in live:continue
    for line in a.lines:
     stmt=clean(line);op=stmt.split(None,1)[0] if stmt else ''
     if op in ['jsr','jmp','bra','bz','bnz','bl','bnl','bm','bp','bv','bnv','bf','bnf'] or ':' in stmt:continue
     taken.update(set(re.findall(r'\b_\w+',stmt))&all_entries)
  sizes={}
  for index,(a,entry) in enumerate(entries.items()):
   if a in cold:continue
   if a.unit.name==str(runtime) and entry!='___mp_dispatch':continue
   if entry in taken:continue
   obj=assemble('\n'.join(['.setcpu 6','.code',*exports(a),*lines(a,'code')])+'\n','measure%03d'%index)
   sizes[a]=symbols(obj.read_bytes())[2][1]
  dispatch=next(a for a,entry in entries.items() if entry=='___mp_dispatch')
  bank_atoms=[dispatch];occupied=symbols(prefix.read_bytes())[2][5]+sizes[dispatch]
  for a in sorted(sizes,key=lambda a:(-sizes[a],entries[a])):
   if a is dispatch:continue
   if occupied+sizes[a]<=args.mmu_bytes:bank_atoms.append(a);occupied+=sizes[a]
  # Assemble each function separately. Combining many bodies changes the
  # assembler's finite-pass jump relaxation and can invalidate a short branch
  # that was valid in the measured function. Linking does not relax its bytes.
  bank=[prefix]
  for index,a in enumerate(bank_atoms):
   bank.append(assemble('\n'.join(['.setcpu 6','.discard',*exports(a),*lines(a,'discard')])+'\n','bank%03d'%index))
  fixed=[]
  for index,u in enumerate(units):
   if u.name in [str(prefix),str(initial)]:continue
   kept=[a for a in u.atoms if a in live and a not in cold and a not in bank_atoms]
   if not kept:continue
   if u.opaque:
    obj=work/('fixed%03d.o'%index);obj.write_bytes(u.raw)
   else:
    text=['.setcpu 6']
    for a in kept:text+=['.'+a.segment,*exports(a),*lines(a)]
    obj=assemble('\n'.join(text)+'\n','fixed%03d'%index)
   fixed.append(obj)
  def link_fixed(stem,real_values,literal_base):
   imports=absolute(real_values,stem+'-imports');binary=work/(stem+'.bin');mapping=work/(stem+'.map')
   run([tools/'bin/ldee200','-b','-C','33280','-S','32903','-X','4096','-f','CDBX','-L',str(literal_base),
        '-m',mapping,'-o',binary,*fixed,*bank,initial,imports])
   return binary.read_bytes(),maps(mapping)
  preliminary,sym=link_fixed('preliminary',{s:0x8000 for s in real.values()},0x6000)
  if sym['__end']<0x8200:raise ValueError('resident image wrapped; reduce code before program reservation')
  slot_base=sym['_bas_ov_area'];real_values={};pack=runpy.run_path(str(ROOT/'run/mkos6bin.py'))['make_cbin_sections']
  for group in groups:
   own={real[a] for a in real if cold[a]==group['group']}
   imports=absolute({k:v for k,v in sym.items() if k not in own and k not in real.values()},'group%d-imports'%group['group'])
   binary=work/('group%d.bin'%group['group']);mapping=work/('group%d.map'%group['group'])
   run([tools/'bin/ldee200','-b','-C',str(slot_base),'-m',mapping,'-o',binary,*group['objects'],imports])
   gsym=maps(mapping);raw=binary.read_bytes()[slot_base:]
   if len(raw)!=group['bytes']:raise ValueError('overlay size changed at final address')
   for symbol in own:real_values[symbol]=gsym[symbol]
   group['cbin']=pack([(0,raw)],0)
   group['addresses']={k:gsym[k] for k in sorted(own)}
  image,final=link_fixed('program',real_values,sym['__end'])
  end=final['__literal']+final['__literal_size']
  if end>65536 or final['_bas_ov_area']!=slot_base or final['__discard_size']!=occupied:
   raise ValueError('resident/MMU budget or fixed overlay address changed')
  sections=[(0x8c,image[0x808c:0x8097]),(0xe7,image[0x80e7:0x8107]),(0x152,image[0x8152:0x815f]),
            (0x200,image[0x8200:final['__bss']]),(final['__literal']-0x8000,image[final['__literal']:end])]
  if any(hashlib.sha256(p.read_bytes()).hexdigest()!=input_hashes[str(p)] for p in inputs):
   raise ValueError('a build input changed while compiling; rerun with stable sources')
  output.write_bytes(pack(sections,final['start']-0x8000))
  output.with_suffix('.mmu.exf').write_bytes(pack([(0,image[0x1000:0x1000+occupied])],0))
  for group in groups:
   path=output.parent/(group['name']+'.exf');path.write_bytes(group.pop('cbin'));group['path']=str(path);group.pop('objects')
  result={'format':'cpu6-stefan-mmu-overlays','program':name,'moduleName':module_name,'identity':idtext,
          'residentEnd':end-0x8000,'moduleBytes':occupied,'modulePages':(occupied+2047)//2048,
          'programMemory':args.memory,'stackBytes':args.stack_size,'overlaySlot':slot_base,'overlayBytes':slot_size,
          'overlays':groups,'wrapperFunctions':[p[0] for p in prototypes.values()],'mappedFunctions':{entries[a]:sizes[a] for a in bank_atoms},
          'sourceHashes':input_hashes}
  result.update(compiler={'path':str(tools/'sdcc-cpu6/bin/sdcc'),'sha256':input_hashes[str(tools/'sdcc-cpu6/bin/sdcc')],
                 'version':subprocess.check_output([tools/'sdcc-cpu6/bin/sdcc','--version'],text=True).strip()},
                profile=args.profile,flags=flags,unitOptions={p.name:f for p,f in unit_options.items()},
                lto=not args.no_lto,gc=True,autoInline=not args.no_auto_inline,
                libRoot=str(libs),componentHashes={name:hashlib.sha256(output.read_bytes()).hexdigest(),
                 module_name:hashlib.sha256(output.with_suffix('.mmu.exf').read_bytes()).hexdigest(),
                 **{g['name']:hashlib.sha256(Path(g['path']).read_bytes()).hexdigest() for g in groups}})
  output.with_suffix('.json').write_text(json.dumps(result,indent=2)+'\n');shutil.copy2(work/'program.map',output.with_suffix('.map'))
  output.with_suffix('.job').write_text('.LOAD BASIC '+module_name+'\n.RUN '+name+'\n')
  print('PASS build: resident',result['residentEnd'],'MMU',occupied,'overlay slot',slot_size,'program memory',args.memory,flush=True)
 finally:
  (work/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  if args.save_temps:
   args.save_temps.mkdir(parents=True,exist_ok=True)
   for p in work.iterdir():
    if p.is_file():shutil.copy2(p,args.save_temps/p.name)

if __name__=='__main__':
 try:main()
 except subprocess.CalledProcessError as error:
  print('basic-build:',Path(error.cmd[0]).name,'failed; see tools/compile logs in the reported results directory',file=sys.stderr);sys.exit(1)
 except (OSError,ValueError) as error:
  print('basic-build:',error,file=sys.stderr);sys.exit(1)
