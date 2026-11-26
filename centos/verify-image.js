'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const {OS6Session}=require(process.env.BASIC_SESSION);
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
(async()=>{
 const s=new OS6Session(process.env.BASIC_IMAGE),checks=[];
 try{
  await s.boot();
  async function command(text,marker='> '){
   const from=s.output.length;await s.send(text+'\r');
   if(marker==='> '){
    const end=Date.now()+120000;
    while(!(s.output.length>from&&s.output.endsWith('\n> '))&&Date.now()<end)await sleep(25);
    assert.ok(s.output.length>from&&s.output.endsWith('\n> '),s.output.slice(from));
   }else await s.waitFor(marker,from,120000);
   await sleep(100);
   const output=s.output.slice(from);
   assert.ok(!/ABORT\s+\d|JX\s*\d|Error|failed|refused/.test(output),output);
   return output;
  }
  const cold=await command('BASIC');
  assert.match(cold,/\.LOAD BASIC SBASIC\.M[0-9A-F]{8}\b/,cold);
  assert.match(cold,/\.RUN SBASIC\.BASICEXE\b/,cold);
  checks.push('the BASIC command loads the module at cold boot and starts the library executable');
  let output=await command('PRINT "CHECK ";SIN(0);" ";COS(0);" ";LOG(EXP(2));" ";2^10');
  assert.ok(output.includes('CHECK 0 1 2 1024'),output);
  output=await command('PRINT "REAL DIV ";7/2');
  assert.match(output,/\nREAL DIV 3\.5 *\r?\n/,output);
  output=await command('PRINT "REAL ";300*300;" ";ABS(EXP(LOG(3.5))-3.5)<.0001;" ";2^3^2');
  assert.ok(output.includes('REAL 90000 -1 512'),output);
  output=await command('PRINT "SQUARE ";(-3)^2;",";0^2');
  assert.match(output,/\nSQUARE 9,0 *\r?\n/,output);
  output=await command('PRINT "TRIG ";ABS(COS(1)-0.5403023)<.00001;",";ABS(TAN(1)-1.5574077)<.00001');
  assert.match(output,/\nTRIG -1,-1 *\r?\n/,output);
  output=await command('K=RND(1):PRINT "RANDOM ";K>=0;",";K<=1;",";RND(0)');
  assert.match(output,/\nRANDOM -1,-1,0 *\r?\n/,output);
  await command('A$="CPU"');
  await command('A$=A$+CHR$(54)');
  output=await command('PRINT "ALPHA ";LEFT$(A$,3);RIGHT$(A$,1);" ";LEN(A$);" ";VAL(STR$(3.5))');
  assert.ok(output.includes('ALPHA CPU6 4 3.5'),output);
  output=await command('PRINT MID$(A$,2,2);" ";ASC(A$)');
  assert.ok(output.includes('PU 67'),output);
  await command('B$="TAIL"');
  await command('A$=A$+"!"');
  output=await command('PRINT "PACK ";A$;" ";B$;" ";A$="CPU6!"');
  assert.ok(output.includes('PACK CPU6! TAIL -1'),output);
  const inputStart=s.output.length;
  await s.send('INPUT C$\r');
  await s.waitFor('? ',inputStart);
  await command('LINE INPUT');
  output=await command('PRINT C$');
  assert.ok(output.includes('LINE INPUT'),output);
  const numberStart=s.output.length;
  await s.send('INPUT K\r');
  await s.waitFor('? ',numberStart);
  await command('1.25');
  output=await command('PRINT "NUMBER ";K');
  assert.match(output,/\nNUMBER 1\.25 *\r?\n/,output);
  checks.push('the immediate VM preserves real and string variables between commands');
  const wide='X'.repeat(78)+'  ';
  await command('D$="'+wide+'"');
  output=await command('PRINT D$;"Y"');
  assert.ok(output.includes('\n'+wide+'Y'),output);
  output=await command('PRINT LEFT$(D$,3)+CHR$(10)+"TAIL"');
  assert.match(output,/\nXXX *\r?\nTAIL/,output);
  checks.push('terminal batching preserves spaces at column 80 and embedded newlines');
  await command('.NEW');
  await command('Z$=Z$+"X"');
  await command('Z$=Z$+"YZ"');
  output=await command('PRINT Z$');
  assert.match(output,/\nXYZ *\r?\n/,output);
  await command('Z$="'+'X'.repeat(84)+'"');
  await command('Z$=Z$+Z$+Z$+"ab"');
  await command('Z$=Z$+"!"');
  output=await command('Z$=Z$+"?"');
  assert.match(output,/string length out of range/,output);
  output=await command('PRINT LEN(Z$);RIGHT$(Z$,3)');
  assert.match(output,/\n255 *ab!/,output);
  for(const slot of 'ABCDEFGHIJKLMNO')await command(slot+'$=Z$');
  await command('P$="1234567890123456"');
  output=await command('P$=P$+"!"');
  assert.match(output,/BASIC memory exhausted/,output);
  output=await command('PRINT LEN(P$);",";RIGHT$(P$,1)');
  assert.match(output,/\n16,6 *\r?\n/,output);
  await command('A$=""');
  await command('P$=P$+"!"');
  output=await command('PRINT LEN(P$);",";RIGHT$(P$,1);",";LEN(Z$)');
  assert.match(output,/\n17,!,255 *\r?\n/,output);
  checks.push('literal append preserves strings at the length and heap limits and after compaction');
  await command('.NEW');
  await command('10PRINT "'+'X'.repeat(115)+'"');
  await command('.EDIT 10','Ctrl-D cancels');
  output=await command('X');assert.ok(output.includes('\x07'),output);
  await command('.NEW');
  checks.push('maximum-length displayed line refuses unsafe insertion');
  const listHelp='Q/Enter: exit';
  async function browse(keys,marker=listHelp){
   const from=s.output.length;
   await s.send(keys);
   await s.waitFor(marker,from,20000);
   return s.output.slice(from);
  }
  const listStart=(text,line)=>assert.ok(text.includes('\x1b[2J\x1b[H'+line+' REM'),text);
  for(let line=10;line<=260;line+=10){
   await command(line+' REM '+(line===20?'X'.repeat(92)+'TAIL':'LINE'));
  }
  output=await browse('.LIST\r');listStart(output,10);
  assert.ok(output.split('\r\n').find(row=>row.startsWith('20 REM X')).length<80,output);
  assert.match(output,/\nX+TAIL *\r?\n/,output);
  assert.doesNotMatch(output,/230 REM/);
  output=await browse('\x1bOA');assert.ok(!output.includes('\x1b[H'),output);
  output=await browse('\x1bOB');listStart(output,20);
  output=await browse('\x1b[A');listStart(output,10);
  output=await browse('\x1b[6~');listStart(output,230);
  output=await browse('\x1b[6~');assert.ok(!output.includes('\x1b[H'),output);
  output=await browse('\x1b[5~');listStart(output,10);
  output=await browse('\x1bOC');listStart(output,230);
  output=await browse('\x1bOD');listStart(output,10);
  output=await browse('q','\x1b[?1l');assert.ok(output.includes('\x1b[?1l'));
  await s.waitFor('> ',s.output.lastIndexOf('\x1b[?1l'),20000);
  output=await command('.LIST 10');assert.match(output,/\n10 REM.*LINE/,output);
  assert.doesNotMatch(output,/Q\/Enter: exit|20 REM/);
  output=await browse('LIST\r');listStart(output,10);
  output=await browse('\r','\x1b[?1l');assert.ok(output.includes('\x1b[?1l'));
  await s.waitFor('> ',s.output.lastIndexOf('\x1b[?1l'),20000);
  await command('.NEW');
  await command('10 PRINT '+'.1;'.repeat(35)+'"TAIL"');
  output=await browse('.LIST\r');
  assert.match(output,/10 PRINT 0\.100000001/);
  assert.match(output,/"TAIL"/);
  await browse('Q','\x1b[?1l');
  await s.waitFor('> ',s.output.lastIndexOf('\x1b[?1l'),20000);
  await command('.NEW');
  checks.push('LIST wraps long lines, scrolls by line and page, and exits with Q or Enter');
  for(const line of [
   '5 DEFINT I,J,S',
   '10 I=7:J=2:R=2',
   '20 PRINT "DIV ";I/J;",";-I/J;",";I/-J;",";-I/-J',
   '21 PRINT "MUL ";(-I)*J;",";I*(-J);",";(-I)*(-J)',
   '22 PRINT "BITS ";NOT I;",";I AND J;",";I OR J',
   '30 X=I/J:PRINT "MIXED ";X;",";I/R;",";R/(J*2)',
   '31 PRINT "LITERALS ";I/10;",";ABS(I/10.5-0.666667)<.000001',
   '40 S=0:FOR I=1 TO 10:S=S+I/30:NEXT I:PRINT "SUM ";S',
   '50 I=-32768:PRINT "LIMITS ";I/1;",";I/I;",";1/I;",";I MOD -1',
  ]) await command(line);
  output=await command('.RUN');
  assert.match(output,/\nDIV 3,-3,-3,3 *\r?\n/,output);
  assert.match(output,/\nMUL -14,-14,14 *\r?\n/,output);
  assert.match(output,/\nBITS -8,2,7 *\r?\n/,output);
  assert.match(output,/\nMIXED 3,3\.5,0\.5 *\r?\n/,output);
  assert.match(output,/\nLITERALS 0,-1 *\r?\n/,output);
  assert.match(output,/\nSUM 0 *\r?\n/,output);
  assert.match(output,/\nLIMITS -32768,1,0,0 *\r?\n/,output);
  output=await command('PRINT I/0');assert.match(output,/Division by zero/,output);
  output=await command('PRINT I/-1');assert.match(output,/out of range/,output);
  output=await command('PRINT -I');assert.match(output,/out of range/,output);
  output=await command('PRINT I*(-1)');assert.match(output,/out of range/,output);
  output=await command('PRINT -7 MOD 3;",";7 MOD -3');assert.match(output,/\n-1 *,1 *\r?\n/,output);
  await command('.NEW');
  checks.push('signed integer division truncates, checks bounds and promotes mixed operands to real');
  for(const line of ['5 DEFINT A,I','10 DIM A(1):I=5','20 PRINT A(I)','30 PRINT 99'])
   await command(line);
  output=await command('.RUN');
  assert.match(output,/Bytecode error at 20: .*out of range/,output);
  await command('.NEW');
  output=await command('.DIR "BDEMO"');assert.ok(output.includes('0 files'),'BDEMO already exists; refusing to replace it');
  for(const line of fs.readFileSync(process.env.BASIC_SAMPLE,'ascii').trim().split(/\r?\n/))await command(line);
  output=await command('.RUN');assert.ok(output.includes('SUM 55')&&output.includes('SQRT2 1.41421'),output);
  assert.match(output,/\nDATA -7,CPU6:VT100,OK,2\.5,-6\.25/,output);
  assert.match(output,/\nRESTORE CPU6:VT100,OK,2\.5/,output);
  assert.match(output,/\nWHILE 2,\*{6}!/,output);
  assert.doesNotMatch(output,/FAIL SKIPPED/);
  output=await command('N=0:FOR I=1 TO 3:WHILE N<I:N=N+1:WEND:NEXT I:PRINT "NEST ";N');
  assert.match(output,/\nNEST 3 /,output);
  output=await command('I=0:WHILE I<3:I=I+1:WEND:PRINT "INTEGER ";I');
  assert.match(output,/\nINTEGER 3 /,output);
  for(const bad of ['WEND','WHILE 1','WHILE 1:FOR I=1 TO 1:WEND:NEXT I',
                    'FOR I=1 TO 1:WHILE 1:NEXT I:WEND',
                    'WHILE 0:'.repeat(9)+'WEND:'.repeat(8)+'WEND']){
   output=await command(bad);assert.match(output,/Unmatched FOR\/NEXT, WHILE\/WEND/,output);
  }
  checks.push('WHILE and WEND test real and string expressions and enforce loop nesting');
  for(const [loop,final] of [
   ['FOR K=127 TO 129:NEXT K',130],
   ['FOR K=16777214 TO 16777215:NEXT K',16777216],
   ['FOR K=1 TO 3:K=2.5:NEXT K',3.5],
   ['FOR K=1 TO 2 STEP 0.5:NEXT K',2.5],
   ['FOR K=3 TO 1 STEP -1:NEXT K',0],
  ]) {
   output=await command(loop+':PRINT "LIMIT ";K='+final);
   assert.match(output,/\nLIMIT -1 *\r?\n/,output);
  }
  for(const loop of ['FOR K=1 TO 3 STEP 0:NEXT K','FOR K=16777216 TO 16777218:NEXT K']){
   output=await command(loop);assert.match(output,/out of range/,output);
  }
  checks.push('real FOR preserves exponent transitions, fractional and negative steps and range errors');
  for(const [value,converted] of [['-0.75',0],['127.75',127],['128.75',128],
                                ['32767.75',32767],['-32768.75',-32768]]) {
   output=await command('I='+value+':PRINT "CONVERT ";I');
   assert.match(output,new RegExp('\\nCONVERT '+converted+' *\\r?\\n'),output);
  }
  for(const value of ['32768','-32769']){
   output=await command('I='+value);assert.match(output,/out of range/,output);
  }
  checks.push('real to integer conversion truncates toward zero with signed 16-bit bounds');
  const integerStart=s.output.length;
  await s.send('INPUT I\r');
  await s.waitFor('? ',integerStart);
  await command('-7.5');
  output=await command('PRINT "NUMBER ";I');
  assert.match(output,/\nNUMBER -7 *\r?\n/,output);
  checks.push('numeric input accepts real and typed integer variables');
  await command('RESTORE 2');
  output=await command('READ I');assert.match(output,/Invalid type/,output);
  await command('READ A$');
  output=await command('PRINT A$');assert.match(output,/\nCPU6:VT100,OK /,output);
  await command('RESTORE 5');
  output=await command('READ I');assert.match(output,/out of range/,output);
  await command('READ X');
  output=await command('PRINT X');assert.match(output,/\n32768 /,output);
  output=await command('READ X');assert.match(output,/Out of DATA/,output);
  output=await command('RESTORE 99');assert.match(output,/Out of DATA/,output);
  await command('RESTORE');
  await command('READ I');
  output=await command('PRINT I');assert.match(output,/\n-7 /,output);
  await command('170 DATA 1+2');
  output=await command('.RUN');assert.match(output,/Bytecode error at 170/,output);
  await command('170');
  output=await command('.RUN 80');assert.match(output,/\nDATA -7,CPU6:VT100,OK,2\.5,-6\.25/,output);
  checks.push('DATA and READ handle mixed types and arrays, with ordinal RESTORE and checked errors');
  await command('.EDIT 20','Ctrl-D cancels');await command('\x1b[C'.repeat(3)+'\b5');
  output=await command('.RUN');assert.ok(output.includes('SUM 60'),output);await command('20 S=0');
  checks.push('inline navigation, edit, commit and program execution');
  output=await command('.SAVE "BDEMO"');assert.ok(output.includes('Saved'),output);
  const dirNames=text=>text.replace(/\x1b\[[0-9;?]*[A-Za-z]/g,'').split(/\r?\n/)
   .map(line=>line.trim()).filter(line=>/^[A-Z0-9_@?]+$/.test(line));
  async function listDirectory(query){
   const text=await browse(query+'\r',/Q\/Enter: exit|\n> /);
   if(text.includes(listHelp)){
    await browse('q','\x1b[?1l');
    await s.waitFor('> ',s.output.lastIndexOf('\x1b[?1l'),20000);
   }
   return text;
  }
  output=await listDirectory('.DIR');
  const names=dirNames(output);
  assert.deepEqual(names,[...names].sort(),output);
  assert.ok(!names.includes('BASIC')&&!names.includes('BASICEXE')&&!names.includes('@PDIR0'),output);
  if(!output.includes(listHelp)){
   assert.ok(names.includes('BDEMO'),output);
   assert.match(output,new RegExp('\\n'+names.length+' files'));
  }
  output=await listDirectory('.DIR "b*d*o"');
  const expected=names.filter(name=>/^B.*D.*O$/.test(name));
  assert.deepEqual(dirNames(output).slice(0,expected.length),expected,output);
  output=await command('.DIR "BDEMO"');assert.match(output,/\nBDEMO *\r?\n1 files/,output);
  output=await command('.DIR "BASIC"');assert.match(output,/\n0 files/,output);
  checks.push('DIR lists only numbered text programs in alphabetical order and preserves wildcards');
  await command('.NEW');output=await command('.LOAD "BDEMO"');assert.ok(output.includes('Loaded'),output);
  output=await command('.RUN');assert.ok(output.includes('SUM 55'),output);
  assert.match(output,/\nDATA -7,CPU6:VT100,OK,2\.5,-6\.25/,output);
  assert.match(output,/\nRESTORE CPU6:VT100,OK,2\.5/,output);
  assert.match(output,/\nWHILE 2,\*{6}!/,output);
  checks.push('sample runs, saves as native BDEMO and reloads');
  await command('.BYE','FOR SYSRDR');await sleep(400);await command('.STA','STATUS DISPLAY');
  const warm=await command('BASIC');
  assert.doesNotMatch(warm,/\.LOAD BASIC\b/);
  assert.match(warm,/\.RUN SBASIC\.BASICEXE\b/,warm);
  output=await command('.LOAD "BDEMO"');assert.ok(output.includes('Loaded'),output);
  output=await command('.RUN');assert.ok(output.includes('SUM 55'),output);
  await s.send('.BYE\r');await sleep(600);await command('.STA','STATUS DISPLAY');
  checks.push('the same BASIC command reenters the REPL with the loaded module and returns to CENTOS');
  if(process.env.BASIC_GAMES==='1'){
   const prefix=process.env.BASIC_GAME_PREFIX||'';
   const terminal='Enter for VT100): ';
   const connect=await command('.RUN '+prefix+'XCONNECT4',/Enter for VT100\): |Column \(1-7\), or Q to quit: /);
   if(connect.includes(terminal))await command('','Column (1-7), or Q to quit: ');
   await command('Q','End of Connect Four.');await command('.STA','STATUS DISPLAY');
   const chess=await command('.RUN '+prefix+'XCHESS',/Enter for VT100\): |\(W\/B\/2\/Q\): /);
   if(chess.includes(terminal))await command('','(W/B/2/Q): ');
   await command('Q','End of Chess.');await command('.STA','STATUS DISPLAY');
   checks.push('both games run while the BASIC module remains loaded');
  }
  s.saveNew(process.env.BASIC_OUTPUT);
  fs.writeFileSync(process.env.BASIC_RESULT,JSON.stringify({result:'PASS',checks},null,2)+'\n');
 }finally{fs.writeFileSync(process.env.BASIC_CONSOLE,s.output);s.stop();}
})().then(()=>process.exit(0),error=>{console.error(error);process.exit(1)});
