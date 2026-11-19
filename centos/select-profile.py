"""Try speed first; fall back only after a measured CENTOS memory overflow."""
import hashlib,json,re,shutil,subprocess,sys
from pathlib import Path

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]

def build(argv,args):
    output=args.o.resolve();output.parent.mkdir(parents=True,exist_ok=True)
    base=[];i=0
    while i<len(argv):
        if argv[i] in ('--profile','--lib-root'):i+=2;continue
        if argv[i].startswith(('--profile=','--lib-root=')):i+=1;continue
        base.append(argv[i]);i+=1
    report={'requested':'speed first','residentBudget':32768,'programBytes':args.memory,'attempts':[]}
    def attempt(profile,libs,label):
        log=output.parent/('profile-'+label+'.log')
        command=[sys.executable,HERE/'build.py',*base,'--profile',profile,'--lib-root',libs]
        with log.open('w') as stream:result=subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT)
        row={'label':label,'profile':profile,'libraries':str(libs),'command':list(map(str,command)),'log':str(log)}
        if result.returncode==0:
            manifest=json.loads(output.with_suffix('.json').read_text())
            row.update(result='PASS',residentBytes=manifest['residentEnd'],moduleBytes=manifest['moduleBytes'])
        else:
            paths=re.findall(r'^Results: (.+)$',log.read_text(),re.M)
            mapping=Path(paths[-1])/'preliminary.map' if paths else Path('/nonexistent')
            symbols={f[2]:int(f[0],16) for line in mapping.read_text().splitlines() if len(f:=line.split())==3} if mapping.exists() else {}
            extent=512+sum(symbols.get(k,0) for k in ('__code_size','__data_size','__bss_size','__literal_size'))
            # Do not hide compiler, ABI or symbol failures behind a size retry.
            diagnostics=(mapping.parent/'tools.log').read_text() if mapping.exists() else ''
            if extent<=32768 or re.search('undefined|multiply defined',diagnostics,re.I):
                raise RuntimeError('BASIC build failed; see '+str(log))
            row.update(result='memory budget exceeded',residentBytes=extent,moduleBytes=symbols.get('__discard_size'))
        report['attempts'].append(row)
        print('BASIC '+label+': '+row['result']+', resident '+str(row['residentBytes'])+' bytes',flush=True)
        return result.returncode==0
    libraries=args.lib_root.resolve()
    if attempt('speed',libraries,'speed'):
        report['selected']='speed'
    elif attempt('size',libraries,'size'):
        report['selected']='size'
    else:
        # The float library dominates the remaining permanent math helpers.
        # Keep console, file and loader support at their speed profile.
        private=output.parent/'runtime-size/lib'
        names=['os6/libos6.a','os6/libprintf-sdcc.a','os6/liboverlay-sdcc.a',
               'os6/putchar.o','os6/puts.o','cpu6/libcompiler-sdcc.a']
        for name in names:
            source=libraries/name;target=private/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
            provenance=source.with_suffix(source.suffix+'.build.json')
            if provenance.exists():
                data=json.loads(provenance.read_text());data.update(artifact=str(target),copiedFrom=str(source))
                target.with_suffix(target.suffix+'.build.json').write_text(json.dumps(data,indent=2)+'\n')
        with (output.parent/'profile-float-runtime.log').open('w') as log:
            subprocess.run([sys.executable,ROOT/'scripts/build-float32.py','--profile','size','--destination',private/'cpu6'],check=True,stdout=log,stderr=subprocess.STDOUT)
        if not attempt('size',private,'size-float-runtime'):
            raise RuntimeError('BASIC still exceeds its memory budget; see the profile logs')
        report['selected']='size-float-runtime'
    report['compilerSha256']=hashlib.sha256((ROOT/'build/out/toolchain/sdcc-cpu6/bin/sdcc').read_bytes()).hexdigest()
    output.with_suffix('.profiles.json').write_text(json.dumps(report,indent=2)+'\n')
