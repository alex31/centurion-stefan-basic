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
  await command('STARTBASIC');checks.push('native startup job loads MMU and opens REPL');
  let output=await command('PRINT "CHECK ";SIN(0);" ";COS(0);" ";LOG(EXP(2));" ";2^10');
  assert.ok(output.includes('CHECK 0 1 2 1024'),output);
  await command('10PRINT "'+'X'.repeat(115)+'"');
  await command('.EDIT 10','Ctrl-D cancels');
  output=await command('X');assert.ok(output.includes('\x07'),output);
  await command('.NEW');
  checks.push('maximum-length displayed line refuses unsafe insertion');
  output=await command('.DIR "BDEMO"');assert.ok(output.includes('0 files'),'BDEMO already exists; refusing to replace it');
  for(const line of fs.readFileSync(process.env.BASIC_SAMPLE,'ascii').trim().split(/\r?\n/))await command(line);
  output=await command('.RUN');assert.ok(output.includes('SUM 55')&&output.includes('SQRT2 1.41421'),output);
  await command('.EDIT 20','Ctrl-D cancels');await command('\x1b[C'.repeat(3)+'\b5');
  output=await command('.RUN');assert.ok(output.includes('SUM 60'),output);await command('20 S=0');
  checks.push('inline navigation, edit, commit and program execution');
  output=await command('.SAVE "BDEMO"');assert.ok(output.includes('Saved'),output);
  await command('.NEW');output=await command('.LOAD "BDEMO"');assert.ok(output.includes('Loaded'),output);
  output=await command('.RUN');assert.ok(output.includes('SUM 55'),output);
  checks.push('sample runs, saves as native BDEMO and reloads');
  await command('.BYE','FOR SYSRDR');await sleep(400);await command('.STA','STATUS DISPLAY');
  await command('.RUN '+process.env.BASIC_PROGRAM);
  output=await command('.LOAD "BDEMO"');assert.ok(output.includes('Loaded'),output);
  output=await command('.RUN');assert.ok(output.includes('SUM 55'),output);
  await s.send('.BYE\r');await sleep(600);await command('.STA','STATUS DISPLAY');
  checks.push('subsequent .RUN BASIC and return to CENTOS');
  if(process.env.BASIC_GAMES==='1'){
   await command('.RUN XCONNECT4','Column (1-7), or Q to quit: ');
   await command('Q','End of Connect Four.');await command('.STA','STATUS DISPLAY');
   await command('.RUN XCHESS','(W/B/2/Q): ');
   await command('Q','End of Chess.');await command('.STA','STATUS DISPLAY');
   checks.push('both games run while the BASIC module remains loaded');
  }
  s.saveNew(process.env.BASIC_OUTPUT);
  fs.writeFileSync(process.env.BASIC_RESULT,JSON.stringify({result:'PASS',checks},null,2)+'\n');
 }finally{fs.writeFileSync(process.env.BASIC_CONSOLE,s.output);s.stop();}
})().then(()=>process.exit(0),error=>{console.error(error);process.exit(1)});
