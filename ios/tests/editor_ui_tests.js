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
  window.__requests=[];window.__hold=false;window.__fail=false;window.__dismiss=0;window.__apply=0;window.__guideRequests=[];window.__guideFail=false;
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
   else if(action==='saveDungeonGuides'){window.__guideRequests.push(value);setTimeout(()=>{
    if(!window.__guideFail)window.__savedGuides=value.library;
    PHFormationUI.guideSaveResult({requestID:value.requestID,ok:!window.__guideFail,message:'模擬攻略寫入失敗'});
   },20);}
  }}}};
 });
 await page.goto(pathToFileURL(path.resolve('build/editor-preview.html')).href);
 await page.waitForFunction(()=>PHFormationUI.initialized);
 const fits=()=>page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth);
 for(const [w,h] of [[320,700],[390,844],[430,932],[768,1024],[844,390]]){
  await page.setViewportSize({width:w,height:h});assert.equal(await fits(),true,'overview width '+w);
  const clippedIDs=await page.locator('.card .number').evaluateAll(elements=>elements.filter(e=>{
   const card=e.closest('.card').getBoundingClientRect(),range=document.createRange();range.selectNodeContents(e);
   const label=range.getBoundingClientRect();return label.left<card.left+1||label.right>card.right-1;
  }).map(e=>e.textContent));
  assert.deepEqual(clippedIDs,[],'complete card numbers at width '+w);
  await page.screenshot({path:path.resolve('build/editor-browser/overview-'+w+'.png'),fullPage:true});
 }
 await page.setViewportSize({width:390,height:844});
 // Per-team badge is saved data, never an implicit native selection or touch.
 const badgeStart=await page.evaluate(()=>({requests:__requests.length,apply:__apply}));
 assert.equal(await page.locator('#team-badge-choice').inputValue(),'inherit');
 await page.selectOption('#team-badge-choice','89');
 assert.equal(await page.evaluate(()=>PHFormationUI.getTeam().badge),89);
 assert.match(await page.locator('#team-badge-status').innerText(),/尚未儲存/);
 assert.match(await page.locator('#team-badge-status').innerText(),/暫不切換徽章/);
 assert.deepEqual(await page.evaluate(()=>({requests:__requests.length,apply:__apply})),badgeStart);
 await page.locator('#undo').click();assert.equal(await page.locator('#team-badge-choice').inputValue(),'inherit');
 await page.selectOption('#team-badge-choice','89');
 await page.evaluate(()=>{window.__fail=true;});await page.locator('#save').click();await page.evaluate(()=>PHFormationUI.whenSaved());
 assert.equal(await page.evaluate(()=>PHFormationUI.getTeam().badge),89);
 assert.equal(await page.evaluate(()=>__saved.teams.find(t=>t.id==='legacy').team.badge),undefined);
 assert.match(await page.locator('#save-status').innerText(),/儲存未完成/);
 await page.evaluate(()=>{window.__fail=false;});await page.locator('#save').click();await page.evaluate(()=>PHFormationUI.whenSaved());
 assert.match(await page.locator('#team-badge-status').innerText(),/選擇已儲存/);
 await page.selectOption('#team-choice','team-b');await page.evaluate(()=>PHFormationUI.whenSaved());
 assert.equal(await page.locator('#team-badge-choice').inputValue(),'inherit');
 await page.selectOption('#team-badge-choice','129');
 await page.selectOption('#team-choice','legacy');await page.evaluate(()=>PHFormationUI.whenSaved());
 assert.equal(await page.locator('#team-badge-choice').inputValue(),'89');
 assert.equal(await page.evaluate(()=>__saved.teams.find(t=>t.id==='team-b').team.badge),129);
 await page.evaluate(()=>PHFormationUI.receive({library:__saved,battle:__battle,stored:true}));
 assert.equal(await page.locator('#team-badge-choice').inputValue(),'89','badge survives a fresh editor receive');
 await page.locator('#team-badge-picker > summary').click();
 for(const [width,height] of [[320,700],[390,844]]) {
  await page.setViewportSize({width,height});assert.equal(await fits(),true,'badge picker width '+width);
  assert.equal(await page.locator('#team-badge-options button').count(),122);
 }
 await page.setViewportSize({width:390,height:844});
 await page.locator('#team-badge-options [data-badge="0"]').click();assert.equal(await page.evaluate(()=>PHFormationUI.getTeam().badge),0);
 await page.locator('#undo').click();assert.equal(await page.locator('#team-badge-choice').inputValue(),'89');
 const badgeIcons=await page.locator('#team-badge-icon .badge-icon').evaluate(el=>({image:getComputedStyle(el).backgroundImage,width:el.offsetWidth,height:el.offsetHeight}));
 assert.match(badgeIcons.image,/^url\("?data:image\/png;base64,/);assert.equal(badgeIcons.width,51);assert.equal(badgeIcons.height,38);
 await page.evaluate(()=>{window.__hold=true;});await page.selectOption('#team-badge-choice','34');await page.locator('#save').click();
 await page.selectOption('#team-badge-choice','89');
 assert.match(await page.locator('#team-badge-status').innerText(),/新徽章選擇尚未儲存.*先前版本/);
 await page.evaluate(()=>__ack(__requests.at(-1)));await page.evaluate(()=>PHFormationUI.whenSaved());
 assert.equal(await page.evaluate(()=>PHFormationUI.getTeam().badge),89,'late badge save acknowledgement cannot overwrite a newer draft');
 assert.equal(await page.evaluate(()=>__saved.teams.find(t=>t.id==='legacy').team.badge),34);
 assert.match(await page.locator('#team-badge-status').innerText(),/尚未儲存/);
 await page.evaluate(()=>{window.__hold=false;});await page.locator('#save').click();await page.evaluate(()=>PHFormationUI.whenSaved());
 await page.evaluate(()=>{const lib=JSON.parse(JSON.stringify(__saved));lib.teams.find(t=>t.id==='legacy').team.badge=255;PHFormationUI.receive({library:lib,battle:__battle,stored:true});});
 assert.equal(await page.locator('#team-badge-choice').inputValue(),'255');assert.match(await page.locator('#team-badge-choice option:checked').innerText(),/未收錄/);
 await page.selectOption('#team-badge-choice','1');await page.locator('#undo').click();assert.equal(await page.locator('#team-badge-choice').inputValue(),'255');
 await page.locator('#save').click();await page.evaluate(()=>PHFormationUI.whenSaved());
 assert.equal(await page.evaluate(()=>__saved.teams.find(t=>t.id==='legacy').team.badge),255,'unknown saved selection is not replaced by a catalog default');
 await page.selectOption('#team-badge-choice','inherit');
 assert.match(await page.locator('#team-badge-status').innerText(),/尚未儲存/,'removing a saved override is also an unsaved change');
 await page.locator('#undo').click();assert.equal(await page.locator('#team-badge-choice').inputValue(),'255');
 assert.equal(await page.evaluate(()=>__apply),badgeStart.apply);
 assert.equal(await page.locator('#body-grid .leased:not(.other)').count(),6);
 await page.selectOption('#team-choice','team-b');await page.evaluate(()=>PHFormationUI.whenSaved());
 assert.equal(await page.locator('#body-grid .leased.other').count(),6,'other team has no applied recipe matches');
 assert.match(await page.locator('#battle-summary').innerText(),/其他配方/);
 await page.selectOption('#team-choice','legacy');await page.evaluate(()=>PHFormationUI.whenSaved());
 await page.locator('#body-grid .card').first().click();await page.locator('#editor-dialog').waitFor({state:'visible'});
 assert.equal(await page.locator('#selected-portrait .portrait').getAttribute('data-card-id'),'14238');
 await page.waitForFunction(()=>document.querySelector('#selected-portrait .portrait').classList.contains('unavailable'));
 assert.equal(await page.locator('#selected-portrait .portrait-fallback').innerText(),'#14238','blocked artwork leaves a readable identity');
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
 await page.setViewportSize({width:390,height:390});
 await page.locator('#search').focus();
 const keyboardControls=await page.evaluate(()=>['editor-done','kind-body','kind-assist','previous-slot','next-slot','search'].map(id=>{
  const el=document.getElementById(id),r=el.getBoundingClientRect(),hit=document.elementFromPoint(r.left+r.width/2,r.top+r.height/2);
  return {id,visible:r.top>=0&&r.bottom<=innerHeight&&el.contains(hit)};
 }));
 assert.deepEqual(keyboardControls.filter(r=>!r.visible),[],'navigation and search remain reachable with a 390px keyboard viewport and the full awakening block');
 await page.screenshot({path:path.resolve('build/editor-browser/editor-keyboard-390.png')});
 await page.setViewportSize({width:390,height:844});
 assert.equal(await page.locator('#level').inputValue(),'120');
 assert.equal(await page.evaluate(()=>PHFormationUI.getTeam().slots[0].body.level),0,'displayed maximum retains sentinel');
 await page.locator('#level').fill('110');await page.locator('#level').dispatchEvent('change');
 assert.equal(await page.evaluate(()=>PHFormationUI.getTeam().slots[0].body.level),110);
 await page.locator('#editor-undo').click();assert.equal(await page.locator('#level').inputValue(),'120');
 await page.locator('#next-slot').click();assert.equal(await page.locator('#slots-label').innerText(),'2 / 6');
 await page.locator('#previous-slot').click();
 await page.locator('#kind-assist').click();assert.equal(await page.locator('#super-section').isVisible(),false);
 assert.equal(await page.locator('#selected-portrait .portrait').getAttribute('data-card-id'),'14132');
 assert.equal(await page.locator('#latent-section').isVisible(),false);
 await page.locator('#kind-body').click();
 const beforeLatents=await page.evaluate(()=>({team:PHFormationUI.getTeam(),requests:__requests.length,apply:__apply}));
 await page.locator('#latent-picker-toggle').click();
 assert.equal(await page.locator('#latent-options .latent-icon').count(),51,'every named latent has a visual choice');
 for(const width of [320,390,430]) {
  await page.setViewportSize({width,height:844});
  assert.deepEqual(await page.locator('#latent-options button,#latent-picker-toggle').evaluateAll(elements=>elements.filter(e=>{
   const r=e.getBoundingClientRect();return r.left<0||r.right>innerWidth||e.scrollWidth>e.clientWidth;
  }).map(e=>e.textContent)),[],'latent icons and long names fit width '+width);
 }
 await page.setViewportSize({width:390,height:844});
 await page.locator('#latent-options [data-latent="48"]').click();
 assert.deepEqual(await page.evaluate(()=>({team:PHFormationUI.getTeam(),requests:__requests.length,apply:__apply})),beforeLatents,
  'choosing an icon does not add, save or apply it');
 assert.equal(await page.locator('#latent-choice .latent-icon').evaluate(e=>e.style.getPropertyValue('--icon-id')),'48');
 await page.locator('#latent-add').click();
 assert.deepEqual(await page.evaluate(()=>PHFormationUI.getTeam().slots[0].body.latents),[48]);
 assert.equal(await page.locator('#latent-list .latent-icon').count(),1);
 assert.equal(await page.locator('#latent-add').isDisabled(),true,'another six-slot latent cannot overfill the card');
 await page.locator('#latent-picker-toggle').click();
 assert.equal(await page.locator('#latent-options [data-latent="48"]').isDisabled(),true);
 await page.locator('#latent-options [data-latent="11"]').click();await page.locator('#latent-add').click();
 assert.deepEqual(await page.evaluate(()=>PHFormationUI.getTeam().slots[0].body.latents),[48,11]);
 const latentAtlas=await page.locator('#latent-list .latent-icon').first().evaluate(async e=>{
  const style=getComputedStyle(e),image=new Image();image.src=style.backgroundImage.slice(4,-1).replaceAll('"','');await image.decode();
  return {width:image.naturalWidth,height:image.naturalHeight,size:e.getBoundingClientRect().width};
 });
 assert.deepEqual(latentAtlas,{width:108,height:1664,size:24},'latent artwork decodes offline at the expected sprite dimensions');
 await page.locator('#latent-list button').first().click();
 assert.deepEqual(await page.evaluate(()=>PHFormationUI.getTeam().slots[0].body.latents),[11],'remove only the tapped latent');
 for(let i=0;i<3;i++)await page.locator('#editor-undo').click();
 assert.deepEqual(await page.evaluate(()=>PHFormationUI.getTeam()),beforeLatents.team,'latent edits retain the ordinary undo path');
 const original=await page.evaluate(()=>PHFormationUI.getTeam());
 await page.locator('.slot-tools summary').click();await page.selectOption('#slot-destination','1');await page.locator('#swap-slot').click();
 let swapped=await page.evaluate(()=>PHFormationUI.getTeam());assert.deepEqual(swapped.slots[1],original.slots[0]);assert.deepEqual(swapped.slots[0],original.slots[1]);
 await page.locator('#editor-undo').click();assert.deepEqual(await page.evaluate(()=>PHFormationUI.getTeam()),original);
 await page.selectOption('#slot-destination','2');await page.locator('#copy-slot').click();
 assert.deepEqual((await page.evaluate(()=>PHFormationUI.getTeam())).slots[2],original.slots[0]);
 await page.locator('#editor-undo').click();assert.deepEqual(await page.evaluate(()=>PHFormationUI.getTeam()),original);
 await page.locator('#search').fill('14234');await page.locator('#results .result').first().click();
 assert.equal(await page.locator('#recent button').count(),1);assert.equal(await page.locator('#level').inputValue(),'120');
 await page.waitForFunction(()=>document.querySelector('#selected-portrait .portrait').classList.contains('unavailable'));
 const portraitLayout=await page.locator('#selected-portrait .portrait').evaluate(async e=>{
  // A local image fixture exercises successful loading without depending on
  // GitHub availability. Production still uses its existing image-only origin.
  const image=e.querySelector('img');
  const loaded=new Promise((resolve,reject)=>{image.addEventListener('load',resolve,{once:true});image.addEventListener('error',reject,{once:true});});
  image.src='data:image/svg+xml,'+encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024"><rect x="306" y="306" width="100" height="100" fill="red"/></svg>');
  await loaded;await image.decode();
  const parent=e.getBoundingClientRect(),r=image.getBoundingClientRect();
  return {id:e.dataset.cardId,loaded:e.classList.contains('loaded'),fallback:getComputedStyle(e.querySelector('.portrait-fallback')).display,
   size:r.width/parent.width,x:(parent.left-r.left)/parent.width,y:(parent.top-r.top)/parent.height,clipped:getComputedStyle(e).overflow};
 });
 assert.equal(portraitLayout.id,'14234');assert.equal(portraitLayout.loaded,true);assert.equal(portraitLayout.fallback,'none');
 assert.equal(portraitLayout.clipped,'hidden');
 assert.ok(Math.abs(portraitLayout.size-10.24)<.01&&Math.abs(portraitLayout.x-3.06)<.01&&Math.abs(portraitLayout.y-3.06)<.01,
  'portrait clips a 100px cell at its 102px atlas pitch');
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
 const beforeDuplicateFixture=await page.evaluate(()=>PHFormationUI.getLibrary());
 await page.evaluate(()=>{
  const library=PHFormationUI.getLibrary(),copy=JSON.parse(JSON.stringify(library.teams.find(t=>t.id===library.selected)));
  copy.id='duplicate-label';library.teams.push(copy);PHFormationUI.receive({library});
 });
 assert.match(await page.locator('#team-choice option:checked').textContent(),/同名 1\/2/);
 assert.match(await page.locator('#team-choice option[value="duplicate-label"]').textContent(),/同名 2\/2/);
 await page.selectOption('#team-choice','duplicate-label');await page.evaluate(()=>PHFormationUI.whenSaved());
 assert.equal(await page.evaluate(()=>__saved.selected),'duplicate-label','identically named options select distinct saved IDs');
 await page.evaluate(library=>PHFormationUI.receive({library}),beforeDuplicateFixture);
 await page.evaluate(()=>PHFormationUI.setState({...__battle,automaticConfigured:false,multiplayerDisabled:true,canApply:false,canRestore:false,activeCount:0,members:[]}));
 assert.match(await page.locator('#entry-state').innerText(),/關閉/);assert.equal(await page.locator('#apply-all').isEnabled(),false);
 // The calculator runs outside a dungeon, even with all network requests blocked.
 const uryu=JSON.parse(fs.readFileSync('ios/templates/ishida-uryu-gamewith-574233/formation.json','utf8'));
 await page.evaluate(team=>PHFormationUI.receive({team,battle:{present:false,canApply:false,canRestore:false}}),uryu);
 const beforeTabs=await page.evaluate(()=>({team:PHFormationUI.getTeam(),requests:__requests.length,apply:__apply}));
 assert.equal(await page.locator('#panel-formation').isVisible(),true);
 assert.equal(await page.locator('#panel-calculator').isVisible(),false);
 await page.locator('#tab-formation').focus();await page.keyboard.press('ArrowRight');
 assert.equal(await page.locator('#tab-calculator').getAttribute('aria-selected'),'true');
 assert.equal(await page.locator('#panel-calculator').isVisible(),true);
 await page.keyboard.press('End');assert.equal(await page.locator('#panel-dungeon').isVisible(),true);
 await page.keyboard.press('Home');assert.equal(await page.locator('#panel-formation').isVisible(),true);
 assert.deepEqual(await page.evaluate(()=>({team:PHFormationUI.getTeam(),requests:__requests.length,apply:__apply})),beforeTabs,'tabs do not edit, persist or apply the team');
 await page.locator('#tab-calculator').click();
 assert.equal(await page.locator('#calc-hp').innerText(),'409,219');
 assert.match(await page.locator('#calc-comparison').innerText(),/397,834.*409,219/);
 assert.equal(await page.evaluate(()=>PHFormationUI.getTeamCalculation().members[0].id),14038);
 assert.equal(await page.locator('#calc-members .calc-member').count(),6);
 assert.equal(await page.locator('#calc-members .awakening-icon').count(),106);
 assert.equal(await page.locator('#calc-awakenings [data-awakening="46"]').innerText(),'3');
 assert.equal(await page.locator('#calc-protection .protection-item').count(),6);
 assert.match(await page.locator('#calc-protection').innerText(),/毒\s*100%/);
 const beforeScenario=await page.evaluate(()=>({team:PHFormationUI.getTeam(),requests:__requests.length,apply:__apply}));
 await page.selectOption('#calc-preset','light-heart');
 assert.equal(await page.locator('#calc-shield').innerText(),'96.76%');assert.equal(await page.locator('#calc-rcv').innerText(),'244,430');
 const unohana=page.locator('#calc-skills [data-skill="2:body:0"]'),haschwalth=page.locator('#calc-skills [data-skill="3:body:0"]');
 assert.match(await unohana.innerText(),/最大 HP ×3 · 1 回合/);assert.match(await haschwalth.innerText(),/35% 減傷 · 5 回合/);
 await haschwalth.locator('button').click();await unohana.locator('button').click();
 assert.equal(await page.locator('#calc-hp').innerText(),'1,227,657');assert.equal(await page.locator('#calc-rcv').innerText(),'733,290');
 assert.equal(await page.locator('#calc-shield').innerText(),'97.894%');assert.equal(await page.locator('#calc-board').inputValue(),'7x6');
 assert.equal(await page.evaluate(()=>PHFormationUI.getTeamCalculation().recovery.startingHP),409219);
 await page.locator('#calc-skills [data-skill="4:body:0"] button').click();
 assert.equal(await page.locator('#calc-hp').innerText(),'613,829');assert.equal(await page.locator('#calc-rcv').innerText(),'733,290');
 assert.match(await unohana.innerText(),/覆蓋/);
 await page.locator('#calc-skills [data-skill="4:body:0"] button').click();
 await unohana.locator('input').fill('1');await unohana.locator('input').dispatchEvent('change');
 assert.equal(await page.locator('#calc-hp').innerText(),'409,219');assert.equal(await page.locator('#calc-board').inputValue(),'6x5');
 assert.equal(await page.locator('#calc-shield').innerText(),'97.894%');
 await haschwalth.locator('input').fill('5');await haschwalth.locator('input').dispatchEvent('change');
 assert.equal(await page.locator('#calc-shield').innerText(),'96.76%');
 await unohana.locator('button').click();await haschwalth.locator('button').click();
 assert.deepEqual(await page.evaluate(()=>({team:PHFormationUI.getTeam(),requests:__requests.length,apply:__apply})),beforeScenario,'active skill simulation never casts, persists or applies');
 await page.selectOption('#calc-preset','light-l-heart4');
 assert.equal(await page.locator('#calc-shield').innerText(),'96.76%');const heart4=await page.evaluate(()=>PHFormationUI.getTeamCalculation().recovery.heart);
 await page.selectOption('#calc-preset','light-t-heart4');
 assert.equal(await page.locator('#calc-rcv').innerText(),'488,860');assert.equal(await page.locator('#calc-shield').innerText(),'96.76%');
 assert.ok((await page.evaluate(()=>PHFormationUI.getTeamCalculation().recovery.heart))>heart4);
 await page.locator('#calc-awakenings [data-awakening="46"]').click();assert.match(await page.locator('#calc-icon-info').innerText(),/隊伍 HP 強化 ×3/);
 await page.locator('#calc-conditions summary').click();await page.locator('#calc-shield-buff').fill('35');await page.locator('#calc-shield-buff').dispatchEvent('change');
 assert.equal(await page.locator('#calc-shield').innerText(),'97.894%');
 await page.locator('#calc-hp-percent').fill('');await page.locator('#calc-hp-percent').dispatchEvent('change');
 assert.equal(await page.locator('#calc-hp').innerText(),'—');assert.equal(await page.evaluate(()=>PHFormationUI.getTeamCalculation()),null);
 await page.locator('#calc-hp-percent').fill('50');await page.locator('#calc-hp-percent').dispatchEvent('change');
 assert.equal(await page.locator('#calc-hp').innerText(),'409,219');
 assert.deepEqual(await page.evaluate(()=>({team:PHFormationUI.getTeam(),requests:__requests.length,apply:__apply})),beforeScenario,'scenarios never persist or apply a formation');
 await page.locator('#calc-conditions summary').click();
 const dungeonFixture=await page.evaluate(()=>{
  const hex=a=>Array.from(new Uint8Array(a.buffer)).map(x=>x.toString(16).padStart(2,'0')).join('');
  const defs=[],catalogs=[];
  for(let slot=1;slot<=3;slot++){
   const raw=new DataView(new ArrayBuffer(220));raw.setInt32(0,1000+slot,true);raw.setInt8(12,1);raw.setBigUint64(128,1n,true);raw.setBigUint64(136,100850n,true);raw.setFloat32(144,1,true);raw.setUint16(176,10,true);
   const action=new DataView(new ArrayBuffer(72)),op=slot===2?15:92,p=slot===2?[10,10,100,0,0,0,0,0]:[1,1,1,0,0,0,0,0],attached=slot===2?0:100;
   action.setInt32(0,slot,true);action.setUint16(4,op,true);p.forEach((v,i)=>action.setInt32(16+i*4,v,true));action.setInt32(68,attached,true);
   defs.push({actionID:slot,opcode:op,parameters:p,attachedAttackPercentRaw:attached,known:true,rawHex:hex(action)});
   catalogs.push({slot,catalogID:1000+slot,level:10,catalogPrefixRawHex:hex(raw),rows:[{row:1,actionID:slot,operandsRaw:[100,0,0,0]}]});
  }
  return {schema:1,kind:'PADCheaper-dungeon-report',target:'jp.gungho.pad-ios-23.6.1-107553',dungeon:12,floor:3,totalFloors:1,allDefinitionsCaptured:true,
   floorDefinitions:[{wave:0,rawCaptureComplete:true,catalogs,definitions:defs}],observations:[{wave:0,enemies:[{slot:1,catalogID:1001,level:10,baseAttack:2521250,identityKnown:true,memoryKnown:true}]}]};
 });
 await page.locator('#tab-dungeon').click();
 await page.locator('#calc-dungeon-file').setInputFiles({name:'dungeon-fixture.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(dungeonFixture))});
 await page.waitForFunction(()=>PHFormationUI.getDungeonComparison()?.floors.length===1);
 assert.match(await page.locator('#calc-dungeon-status').innerText(),/ATK ×25/);
 assert.match(await page.locator('.dungeon-floor > summary').innerText(),/需提高 HP/);
 await page.locator('.dungeon-floor > summary').click();
 assert.match(await page.locator('.dungeon-comparison').innerText(),/637,176/);
 assert.equal(await page.evaluate(()=>PHFormationUI.getDungeonComparison().survivalAuthorized),false);
 await page.screenshot({path:path.resolve('build/editor-browser/team-dungeon-390.png'),fullPage:true});
 const beforeAdjust=await page.evaluate(()=>({calculation:PHFormationUI.getTeamCalculation(),dungeon:PHFormationUI.getDungeonComparison(),team:PHFormationUI.getTeam(),requests:__requests.length,apply:__apply}));
 await page.getByRole('button',{name:'調整消珠／HP／技能條件',exact:true}).click();
 assert.equal(await page.locator('#tab-calculator').getAttribute('aria-selected'),'true');
 assert.equal(await page.locator('#panel-calculator').isVisible(),true);
 assert.equal(await page.locator('#panel-dungeon').isVisible(),false);
 assert.equal(await page.locator('#calc-preset').evaluate(e=>document.activeElement===e),true);
 await page.locator('#tab-dungeon').click();
 assert.deepEqual(await page.evaluate(()=>({calculation:PHFormationUI.getTeamCalculation(),dungeon:PHFormationUI.getDungeonComparison(),team:PHFormationUI.getTeam(),requests:__requests.length,apply:__apply})),beforeAdjust,'adjusting conditions navigates without resetting either scenario or saving the team');
 await page.evaluate(()=>PHFormationUI.importDungeonText('{"schema":99}'));
 assert.equal(await page.evaluate(()=>PHFormationUI.getDungeonComparison()),null);
 assert.equal(await page.locator('#calc-dungeon-results').innerText(),'');
 assert.match(await page.locator('#calc-dungeon-error').innerText(),/相容版本/);
 await page.evaluate(value=>PHFormationUI.showDungeonComparison(value),dungeonFixture);
 assert.equal(await page.locator('#panel-dungeon').isVisible(),true);
 assert.equal(await page.locator('#tab-dungeon').getAttribute('aria-selected'),'true');
 assert.equal(await page.evaluate(()=>PHFormationUI.getDungeonComparison().floors.length),1);
 assert.equal(await page.locator('#calc-dungeon-error').innerText(),'');
 assert.equal(await page.locator('#calc-dungeon-guide').innerText(),'');
 const externalGuides={schema:1,kind:'PADCheaper-dungeon-guides',guides:[{dungeon:12,subDungeon:3,totalFloors:1,title:'新關卡資料包',
  checkedAt:'2026-11-01',sources:['https://example.com/guide'],notes:['無需重編測試'],floors:[{number:1,text:'新關卡留技提示'}]}]};
 await page.locator('#calc-guide-management > summary').click();
 await page.locator('#calc-guide-file').setInputFiles({name:'new-dungeon-guides.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(externalGuides))});
 await page.waitForFunction(()=>PHFormationUI.getDungeonComparison()?.guide?.title==='新關卡資料包');
 assert.match(await page.locator('#calc-dungeon-guide').innerText(),/無需重編測試/);
 assert.equal(await page.evaluate(()=>PHFormationUI.getDungeonGuides().guides.length),2,'new guide preserves packaged guide');
 await page.evaluate(()=>{window.__guideFail=true;});await page.locator('#calc-guide-save').click();
 await page.waitForFunction(()=>document.getElementById('calc-guide-status').textContent.includes('目前只在本頁'));
 assert.equal(await page.evaluate(()=>window.__savedGuides),undefined);
 await page.evaluate(()=>{window.__guideFail=false;});await page.locator('#calc-guide-save').click();
 await page.waitForFunction(()=>document.getElementById('calc-guide-status').textContent.includes('攻略庫已儲存'));
 assert.equal(await page.evaluate(()=>window.__savedGuides.guides.length),2);
 const updated=JSON.parse(JSON.stringify(externalGuides));updated.guides[0].title='同關卡更新';
 await page.evaluate(value=>PHFormationUI.importDungeonGuides(value),updated);
 assert.equal(await page.evaluate(()=>PHFormationUI.getDungeonGuides().guides.length),2);
 assert.equal(await page.evaluate(()=>PHFormationUI.getDungeonComparison().guide.title),'同關卡更新');
 await page.evaluate(()=>PHFormationUI.importDungeonGuides('{"schema":99}'));
 assert.match(await page.locator('#calc-guide-status').innerText(),/原攻略庫保留/);
 await page.locator('#calc-guide-management > summary').click();
 await page.locator('#calc-dungeon-clear').click();await page.locator('#tab-calculator').click();
 assert.deepEqual(await page.evaluate(()=>({team:PHFormationUI.getTeam(),requests:__requests.length,apply:__apply})),beforeScenario,'dungeon imports never persist or apply a formation');
 await page.locator('#calc-member-details summary').click();
 for(const [width,height] of [[320,700],[390,844],[430,932],[768,1024],[844,390]]) {
  await page.setViewportSize({width,height});assert.equal(await fits(),true,'offline calculator width '+width);
  const clipped=await page.locator('#calc-members .member-stat').evaluateAll(elements=>elements.filter(e=>e.scrollWidth>e.clientWidth).map(e=>e.textContent));
  assert.deepEqual(clipped,[],'complete per-member numbers at width '+width);
  await page.locator('#team-lab').scrollIntoViewIfNeeded();
  await page.screenshot({path:path.resolve('build/editor-browser/team-lab-'+width+'.png'),fullPage:true});
 }
 await page.setViewportSize({width:390,height:844});
 await page.selectOption('#calc-preset','custom');await page.locator('#calc-add-group').click();
 await page.selectOption('#calc-groups select[aria-label="第 1 組珠型"]','t:5');await page.locator('#calc-add-group').click();
 await page.selectOption('#calc-groups select[aria-label="第 2 組珠色"]','5');
 assert.equal(await page.locator('#calc-rcv').innerText(),'488,860');
 await page.selectOption('#calc-groups select[aria-label="第 1 組珠型"]','match:30');
 assert.match(await page.locator('#calc-error').innerText(),/超過盤面/);assert.equal(await page.locator('#calc-hp').innerText(),'—');
 await page.locator('#calc-clear-groups').click();assert.equal(await page.locator('#calc-hp').innerText(),'409,219');
 await page.locator('#tab-formation').click();
 await page.locator('#body-grid .card').first().click();
 assert.equal(await page.locator('#selected-awakenings .awakening-icon').count(),4);
 await page.locator('#selected-awakenings button').first().click();assert.match(await page.locator('#selected-icon-info').innerText(),/封鎖耐性/);
 await page.selectOption('#awakenings','1');assert.equal(await page.locator('#selected-awakenings .inactive').count(),3);
 await page.locator('#editor-undo').click();await page.locator('#editor-done').click();
 await page.evaluate(()=>{const f=PHFormation.createModel(PH_CATALOG);PHFormationUI.receive({team:f.selectCard(f.empty(),0,'body',8614),battle:{present:false}});});
 await page.locator('#tab-calculator').click();
 assert.match(await page.locator('#calc-forms').innerText(),/指定型態/);assert.equal(await page.locator('#calc-hp').innerText(),'未完整');
 assert.equal(await page.locator('#calc-members .awakening-icon').count(),0);assert.match(await page.locator('#calc-awakening-title').innerText(),/未完整/);
 assert.match(await page.locator('#calc-warning').innerText(),/僅計算已設定位置/);
 await page.selectOption('#calc-forms select','8615');assert.equal(await page.evaluate(()=>PHFormationUI.getTeamCalculation().members[0].id),8615);
 assert.equal(await page.evaluate(()=>PHFormationUI.getTeamCalculation().survivalAuthorized),false);
 // A library the bundled catalog cannot read must never be replaced by the
 // empty placeholder: saving, team management and apply stay blocked.
 await page.locator('#tab-formation').click();
 const blockedStart=await page.evaluate(()=>({requests:__requests.length,dismiss:__dismiss}));
 await page.evaluate(()=>{const f=PHFormation.createModel(PH_CATALOG);const lib=f.createLibrary(f.selectCard(f.empty(),0,'body',8614));
  lib.teams[0].team.slots[1].body.id=65000;PHFormationUI.receive({library:lib,stored:true,battle:{present:false}});});
 assert.match(await page.locator('#error').innerText(),/隊伍庫無法載入.*65000/);
 assert.match(await page.locator('#save-status').innerText(),/已停用儲存/);
 for(const id of ['#save','#team-new','#team-copy','#team-delete','#team-choice','#delete-yes','#apply-all','#apply-one'])
  assert.equal(await page.locator(id).isDisabled(),true,'blocked '+id);
 await page.evaluate(()=>PHFormationUI.importText(JSON.stringify(PHFormationUI.getTeam())));
 assert.match(await page.locator('#error').innerText(),/隊伍庫無法載入/);
 await page.locator('#close').click();
 assert.deepEqual(await page.evaluate(()=>({requests:__requests.length,dismiss:__dismiss})),{requests:blockedStart.requests,dismiss:blockedStart.dismiss+1});
 await page.evaluate(()=>{const f=PHFormation.createModel(PH_CATALOG);PHFormationUI.receive({library:f.createLibrary(f.selectCard(f.empty(),0,'body',8614)),stored:true,battle:{present:false}});});
 assert.equal(await page.locator('#error').isHidden(),true);assert.equal(await page.locator('#save').isDisabled(),false);
 assert.equal(await page.evaluate(()=>[...document.querySelectorAll('input,select')].every(e=>parseFloat(getComputedStyle(e).fontSize)>=16)),true);
 assert.doesNotMatch(await page.locator('meta[name=viewport]').getAttribute('content'),/user-scalable\s*=\s*no|maximum-scale/);
 assert.deepEqual(errors,[]);await context.close();await browser.close();console.log('Editor browser checks passed: mobile sizes, full recipes, edits, history, recent cards, pending/stale/failed saves, entry toggles, multiplayer gate; offline team stats, final forms, shield/recovery scenarios, awakening icons and resistance, invalid input and no native writes.');
})().catch(e=>{console.error(e);process.exit(1)});
