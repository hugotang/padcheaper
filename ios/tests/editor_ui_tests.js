// Generated HTML with an isolated native bridge fixture; no game process is accessed.
const {chromium}=require(process.env.PH_PLAYWRIGHT_MODULE || 'playwright');
const fs=require('node:fs');
fs.mkdirSync('build/editor-browser',{recursive:true});
const assert=require('node:assert/strict');
const path=require('node:path');
const {pathToFileURL}=require('node:url');
(async()=>{
 const browser=await chromium.launch({channel:process.env.PH_BROWSER_CHANNEL || (process.platform==='win32'?'msedge':undefined),headless:true});
 const context=await browser.newContext({viewport:{width:390,height:844},deviceScaleFactor:2,isMobile:true,hasTouch:true});
 const page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
 await page.route('https://mapaler.github.io/**',r=>r.abort());
 await page.addInitScript(()=>{
  window.__requests=[];window.__hold=false;window.__fail=false;window.__dismiss=0;window.__apply=0;
  window.__ack=(r,ok=true)=>{
   if(ok){window.__saved=JSON.parse(JSON.stringify(r.library));window.__battle.automaticTeamTitle=r.library.teams.find(t=>t.id===r.library.selected).team.title;}
   PHFormationUI.saveResult({requestID:r.requestID,ok,message:ok?'saved':'測試寫入失敗',battle:window.__battle});
  };
  window.webkit={messageHandlers:{formation:{postMessage({action,value}){
   if(action==='ready'){
    const m=PHFormation.createModel(PH_CATALOG);let a=m.empty();a.title='隊伍 A';
    const ids=[[14238,14132,153],[14246,13334,142],[14240,8484,111],[14244,13711,142],[14234,13535,142],[14234,12460,142]];
    ids.forEach(([b,w,s],i)=>{a=m.selectCard(a,i,'body',b);a=m.selectCard(a,i,'assist',w);a.slots[i].body.superAwakening=s;});
    let lib=m.createLibrary(a),b=m.empty();b.title='隊伍 B';lib=m.addLibraryTeam(lib,b,'team-b');lib=m.selectLibraryTeam(lib,'legacy');window.__saved=lib;
    window.__battle={present:true,hpKnown:true,currentHP:254913,canApply:false,canRestore:true,activeCount:6,entryApplied:true,initialBoost:19,automaticConfigured:true,automaticSwitchesReady:true,automaticTeamTitle:'隊伍 A',gate:'單人操作階段',members:a.slots.map((s,i)=>({active:true,readable:true,leased:true,appliedRecipe:s,slot:i+1}))};
    queueMicrotask(()=>PHFormationUI.receive({library:lib,battle:window.__battle}));
   } else if(action==='persist'){window.__requests.push(value);if(!window.__hold)setTimeout(()=>window.__ack(value,!window.__fail),30);}
   else if(action==='dismiss')window.__dismiss++;
   else if(action==='applySaved')window.__apply++;
  }}}};
 });
 await page.goto(pathToFileURL(path.resolve('build/editor-preview.html')).href);
 await page.waitForFunction(()=>PHFormationUI.initialized);
 const fits=()=>page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth);
 for(const [w,h] of [[320,700],[390,844],[430,932],[768,1024],[844,390]]){
  await page.setViewportSize({width:w,height:h});assert.equal(await fits(),true,'overview width '+w);
  await page.screenshot({path:path.resolve('build/editor-browser/overview-'+w+'.png'),fullPage:true});
 }
 await page.setViewportSize({width:390,height:844});
 assert.equal(await page.locator('#body-grid .leased:not(.other)').count(),6);
 await page.selectOption('#team-choice','team-b');await page.evaluate(()=>PHFormationUI.whenSaved());
 assert.equal(await page.locator('#body-grid .leased.other').count(),6,'other team has no applied recipe matches');
 assert.match(await page.locator('#battle-summary').innerText(),/其他配方/);
 await page.selectOption('#team-choice','legacy');await page.evaluate(()=>PHFormationUI.whenSaved());
 await page.locator('#body-grid .card').first().click();await page.locator('#editor-dialog').waitFor({state:'visible'});
 assert.equal(await page.evaluate(()=>getComputedStyle(document.getElementById('main')).visibility),'hidden','background overview is hidden while editing');
 const pan=await page.evaluate(()=>{
  const vv=window.visualViewport,resize=new Event('resize'),scroll=new Event('scroll');
  Object.defineProperty(vv,'offsetTop',{configurable:true,value:120});
  Object.defineProperty(vv,'height',{configurable:true,value:500});
  vv.dispatchEvent(resize);vv.dispatchEvent(scroll);
  const rect=document.getElementById('editor-done').getBoundingClientRect();
  const visible=rect.top>=120&&rect.bottom<=620;
  delete vv.offsetTop;delete vv.height;vv.dispatchEvent(resize);vv.dispatchEvent(scroll);
  return visible;
 });
 assert.equal(pan,true,'editor navigation follows the visual viewport when the keyboard pans it');
 assert.equal(await page.locator('#level').inputValue(),'120');
 assert.equal(await page.evaluate(()=>PHFormationUI.getTeam().slots[0].body.level),0,'displayed maximum retains sentinel');
 await page.locator('#level').fill('110');await page.locator('#level').dispatchEvent('change');
 assert.equal(await page.evaluate(()=>PHFormationUI.getTeam().slots[0].body.level),110);
 await page.locator('#editor-undo').click();assert.equal(await page.locator('#level').inputValue(),'120');
 await page.locator('#next-slot').click();assert.equal(await page.locator('#slots-label').innerText(),'2 / 6');
 await page.locator('#previous-slot').click();
 await page.locator('#kind-assist').click();assert.equal(await page.locator('#super-section').isVisible(),false);
 await page.locator('#kind-body').click();
 const original=await page.evaluate(()=>PHFormationUI.getTeam());
 await page.locator('.slot-tools summary').click();await page.selectOption('#slot-destination','1');await page.locator('#swap-slot').click();
 let swapped=await page.evaluate(()=>PHFormationUI.getTeam());assert.deepEqual(swapped.slots[1],original.slots[0]);assert.deepEqual(swapped.slots[0],original.slots[1]);
 await page.locator('#editor-undo').click();assert.deepEqual(await page.evaluate(()=>PHFormationUI.getTeam()),original);
 await page.selectOption('#slot-destination','2');await page.locator('#copy-slot').click();
 assert.deepEqual((await page.evaluate(()=>PHFormationUI.getTeam())).slots[2],original.slots[0]);
 await page.locator('#editor-undo').click();assert.deepEqual(await page.evaluate(()=>PHFormationUI.getTeam()),original);
 await page.locator('#search').fill('14234');await page.locator('#results .result').first().click();
 assert.equal(await page.locator('#recent button').count(),1);assert.equal(await page.locator('#level').inputValue(),'120');
 await page.screenshot({path:path.resolve('build/editor-browser/editor-390.png'),fullPage:true});
 await page.locator('#editor-undo').click();await page.locator('#editor-done').click();
 assert.deepEqual(await page.evaluate(()=>PHFormationUI.getTeam()),original);
 await page.evaluate(()=>{window.__hold=true;});await page.locator('#team-title').fill('送出的版本');await page.locator('#save').click();
 assert.equal(await page.evaluate(()=>PHFormationUI.getSaveState().pending),true);
 await page.locator('#team-title').fill('送出之後的新變更');await page.locator('#team-title').dispatchEvent('change');
 await page.evaluate(()=>PHFormationUI.saveResult({requestID:'unknown',ok:true}));assert.equal(await page.evaluate(()=>PHFormationUI.getSaveState().pending),true);
 await page.evaluate(()=>__ack(__requests[__requests.length-1]));
 assert.equal(await page.locator('#team-title').inputValue(),'送出之後的新變更');assert.equal(await page.evaluate(()=>PHFormationUI.getSaveState().dirty),true);
 assert.equal(await page.evaluate(()=>__saved.teams.find(t=>t.id===__saved.selected).team.title),'送出的版本');
 await page.evaluate(()=>{window.__hold=false;window.__fail=true;});await page.locator('#close').click();await page.evaluate(()=>PHFormationUI.whenSaved());
 assert.equal(await page.evaluate(()=>__dismiss),0);assert.equal(await page.evaluate(()=>PHFormationUI.getSaveState().dirty),true);
 assert.match(await page.locator('#save-status').innerText(),/未完成/);
 await page.screenshot({path:path.resolve('build/editor-browser/save-failure-390.png'),fullPage:true});
 await page.evaluate(()=>{window.__fail=false;});await page.locator('#save').click();await page.evaluate(()=>PHFormationUI.whenSaved());
 assert.equal(await page.evaluate(()=>PHFormationUI.getSaveState().dirty),false);
 await page.evaluate(()=>PHFormationUI.setState({...__battle,automaticConfigured:false,multiplayerDisabled:true,canApply:false,canRestore:false,activeCount:0,members:[]}));
 assert.match(await page.locator('#entry-state').innerText(),/關閉/);assert.equal(await page.locator('#apply-all').isEnabled(),false);
 assert.equal(await page.evaluate(()=>[...document.querySelectorAll('input,select')].every(e=>parseFloat(getComputedStyle(e).fontSize)>=16)),true);
 assert.doesNotMatch(await page.locator('meta[name=viewport]').getAttribute('content'),/user-scalable\s*=\s*no|maximum-scale/);
 assert.deepEqual(errors,[]);await context.close();await browser.close();console.log('Editor browser checks passed: mobile sizes, full recipes, edits, history, recent cards, pending/stale/failed saves, entry toggles, multiplayer gate.');
})().catch(e=>{console.error(e);process.exit(1)});
