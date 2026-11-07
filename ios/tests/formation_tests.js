const assert = require('node:assert/strict');
const catalog = require('../editor/catalog.json');
const model = require('../editor/formation.js').createModel(catalog);
assert.equal(model.cards.get(14238).name,'猫猫');
assert.equal(model.cards.get(14138).name.includes('ユキネ'),true);
const fixtures = [[14238,14132,153],[14246,13334,142],[14240,8484,111],[14244,13711,142],[14234,13535,142],[14234,12460,142]];
let team=model.empty();
fixtures.forEach(([body,assist,superAwakening],slot)=>{
  team=model.selectCard(team,slot,'body',body);
  team=model.selectCard(team,slot,'assist',assist);
  team.slots[slot].body.superAwakening=superAwakening;
});
team=model.normalize(team);
assert.equal(model.payload(team).mask,63);
assert.equal(model.payload(team,5).mask,32);
assert.deepEqual(team.slots[0].body.plus,[297,297,297]);
assert.deepEqual(team.slots[5].assist.plus,[99,99,99]);
assert.equal(team.slots[5].assist.skill,1);
const imported=model.parseImport(model.exportLink(team)).team;
fixtures.forEach(([body,assist,superAwakening],slot)=>{
  assert.equal(imported.slots[slot].body.id,body);
  assert.equal(imported.slots[slot].assist.id,assist);
  assert.equal(imported.slots[slot].body.superAwakening,superAwakening);
  assert.equal(imported.slots[slot].assist.skill,1);
  assert.equal(imported.slots[slot].body.level,120);
});
assert.equal(model.search('猫猫',false).some(c=>c.id===14238),true);
assert.equal(model.search('#14238',false)[0].id,14238);
assert.equal(model.search('14238',true).length,0);
let invalid=JSON.parse(JSON.stringify(team));invalid.slots[0].body.superAwakening=123;
assert.throws(()=>model.normalize(invalid),/超覺醒/);
invalid=JSON.parse(JSON.stringify(team));invalid.slots[0].assist.id=14238;
assert.throws(()=>model.normalize(invalid),/武裝/);
invalid=JSON.parse(JSON.stringify(team));invalid.slots[0].assist.skill=999;
assert.throws(()=>model.normalize(invalid),/技能/);
invalid=JSON.parse(JSON.stringify(team));invalid.slots[0].body.latents=[13,14];
assert.throws(()=>model.normalize(invalid),/格數/);
assert.throws(()=>model.parseImport('https://mapaler.github.io/PADDashFormation/solo.html?s=en&d={}'),/日服/);
assert.throws(()=>model.parseImport(JSON.stringify({v:5,f:[[],[]]})),/單人/);
assert.throws(()=>model.parseImport('x'.repeat(65537)),/過長/);
assert.throws(()=>model.payload(model.empty()),/本體/);
const old={v:4,f:[[[[14238,120,9,297,[],1,2]],[]]]};
assert.equal(model.parseImport(JSON.stringify(old)).team.slots[0].body.superAwakening,153);
const explicit={v:5,t:'native test',f:[[[[14238,120,9,297,[11,11],153,2]],[[14132,99,8,99,[],0,1]],1,2]],r:[[]]};
const result=model.parseImport(JSON.stringify(explicit));
assert.equal(result.team.slots[0].body.skill,2);
assert.deepEqual(result.team.slots[0].body.latents,[11,11]);
assert.match(result.notice,/徽章/);
const before=JSON.stringify(team);
assert.throws(()=>model.selectCard(team,0,'body',65435));
assert.equal(JSON.stringify(team),before,'invalid edits must preserve all six drafts');
const custom=JSON.parse(JSON.stringify(team));custom.slots[0].body.plus=[198,99,99];
assert.deepEqual(model.parseImport(model.exportLink(custom)).team.slots[0].body.plus,[198,99,99]);
for(const value of ['null','[]','false','{"s":"en","v":5,"f":[]}']) assert.throws(()=>model.parseImport(value));

let library=model.createLibrary(team);
assert.equal(library.selected,'legacy');
assert.deepEqual(model.selectedTeam(library),team,'legacy single-team migration retains every slot');
assert.equal(model.createLibrary().selected,'default');
const stored=JSON.stringify(library);
const draft=structuredClone(team);draft.title='副本';draft.slots[0].body.skill=1;
library=model.addLibraryTeam(library,draft,'copy');
draft.slots[0].body.skill=2;
assert.equal(model.selectedTeam(library).slots[0].body.skill,1,'copy has independent nested member settings');
assert.deepEqual(library.teams[0].team,team,'save-as does not overwrite the source saved record');
let copyDraft=model.selectedTeam(library);copyDraft.title='重新命名';copyDraft.slots[5].assist.skill=0;
library=model.updateLibraryTeam(library,copyDraft);
copyDraft.slots[5].assist.skill=1;
assert.equal(model.selectedTeam(library).slots[5].assist.skill,0,'saved records do not alias later draft edits');
library=model.selectLibraryTeam(library,'legacy');
assert.deepEqual(model.selectedTeam(library),team,'switching retrieves the original independent team');
library=model.selectLibraryTeam(library,'copy');
assert.equal(model.selectedTeam(library).title,'重新命名');
assert.deepEqual(model.normalizeLibrary(JSON.parse(JSON.stringify(library))),library,'library survives a JSON restart');
assert.equal(model.deleteLibraryTeam(library).selected,'legacy');
assert.throws(()=>model.deleteLibraryTeam(model.createLibrary()),/至少/);
for(const change of [
  {selected:'missing'}, {selected:true}, {teams:[]}, {schema:true},
  {teams:[{id:'bad id',team}]}, {teams:[{id:'x',team},{id:'x',team}]},
  {teams:[{id:'legacy',team:{...team,slots:[]}}]}
]) assert.throws(()=>model.normalizeLibrary({...JSON.parse(stored),...change}));
const unchanged=JSON.stringify(library);
assert.throws(()=>model.addLibraryTeam(library,team,'copy'),/重複/);
assert.throws(()=>model.selectLibraryTeam(library,'missing'),/不存在/);
assert.equal(JSON.stringify(library),unchanged,'failed management operations cannot alter saved teams');
let full=model.createLibrary();
for(let i=1;i<model.libraryCapacity;i++) full=model.addLibraryTeam(full,model.empty(),`team-${i}`);
assert.equal(full.teams.length,64);
assert.throws(()=>model.addLibraryTeam(full,team,'overflow'),/最多/);
assert.equal(full.teams.length,64);
assert.equal(model.deleteLibraryTeam(full).teams.length,63);

const duplicates=model.addLibraryTeam(model.createLibrary(team),team,'same-name');
assert.deepEqual(model.libraryTeamOptions(duplicates),[
  ['legacy',`${team.title}（同名 1/2）`],['same-name',`${team.title}（同名 2/2）`]
],'same-title teams keep distinct IDs and readable choices');
assert.deepEqual(model.libraryTeamOptions(duplicates,{...team,title:'修改版'}),[
  ['legacy',team.title],['same-name','修改版']
],'the unsaved selected title is reflected without renaming another record');
assert.equal(duplicates.teams[1].team.title,team.title,'display labels never modify saved titles');

const swapped=model.moveSlot(team,0,4);
assert.deepEqual(swapped.slots[4],team.slots[0],'swap carries all body, assist and training values');
assert.deepEqual(swapped.slots[0],team.slots[4]);
assert.deepEqual(model.moveSlot(swapped,0,4),team,'swapping twice restores the original team');
const copied=model.moveSlot(team,0,4,true);
copied.slots[4].body.plus[0]=0;
assert.deepEqual(copied.slots[0],team.slots[0],'copy must not alias its source');
assert.deepEqual(team,JSON.parse(before),'slot operations never mutate the source');
assert.throws(()=>model.moveSlot(team,-1,0),/來源/);
assert.throws(()=>model.moveSlot(team,0,6),/目標/);
assert.equal(model.matchesRecipe(team.slots[0],team.slots[0]),true);
assert.equal(model.matchesRecipe(team.slots[0],imported.slots[0]),true,'explicit maximums match automatic maximums');
for(const field of ['level','skill','awakenings','superAwakening']) {
  const changed=structuredClone(team.slots[0]);changed.body[field]=field==='level'?110:field==='skill'?1:0;
  assert.equal(model.matchesRecipe(changed,team.slots[0]),false,'changed '+field+' is not the applied recipe');
}
const changedLatent=structuredClone(team.slots[0]);changedLatent.body.latents=[11];
assert.equal(model.matchesRecipe(changedLatent,team.slots[0]),false,'latents are included in applied identity');
const changedAssist=structuredClone(team.slots[0]);changedAssist.assist.level=98;
assert.equal(model.matchesRecipe(changedAssist,team.slots[0]),false,'assist training is included in applied identity');
assert.equal(model.matchesRecipe(team.slots[0],{}),false,'a bare native lease is insufficient to claim this recipe applied');
assert.equal(model.matchesRecipe(team.slots[0],team.slots[1]),false,'switching teams cannot inherit the previous applied badge');

for(let id=1;id<=51;id++) assert.ok(model.latentNames[id] && !model.latentName(id).includes('未定義'),`latent ${id} has a verified label`);
assert.equal(model.latentName(13),'隊長交換耐性');
assert.equal(model.latentName(14),'邪魔掉落耐性');
assert.equal(model.latentName(37),'傷害無效貫通');
assert.equal(model.latentName(38),'屬性吸收貫通');
assert.equal(model.latentName(42),'傷害上限解放');
assert.equal(model.latentName(43),'HP 強化＋＋');
assert.equal(model.latentName(50),'部位追擊');
assert.equal(model.latentName(51),'無視防禦力');
const latentDraft=structuredClone(team);latentDraft.slots[0].body.latents=[13];
assert.deepEqual(model.parseImport(model.exportLink(latentDraft)).team.slots[0].body.latents,[13],'corrected labels never remap saved latent IDs');
for(const card of model.cards.values()) for(const id of [...card.awakenings,...card.supers,card.sync].filter(Boolean))
  assert.ok(model.awakeningNames[id],`card ${card.id}: awakening ${id} has a label`);
assert.equal(model.awakeningName(106),'浮游');
assert.equal(model.awakeningName(131),'部位破壞');
assert.equal(model.awakeningName(48),'傷害無效貫通');
assert.equal(model.awakeningName(145),'希望之魂');
// Optional team-level badges keep legacy behaviour distinct from a chosen
// default. Written regression cases; execution follows the build/test batch.
assert.equal(Object.prototype.hasOwnProperty.call(model.empty(),'badge'),false,'a new team initially retains the game selection');
assert.equal(Object.prototype.hasOwnProperty.call(model.normalize(team),'badge'),false,'legacy team normalization never invents a default badge');
for (const badge of [0,1,34,89,129,255]) {
  const selected=model.selectBadge(team,badge);
  assert.equal(selected.badge,badge);
  assert.equal(Object.prototype.hasOwnProperty.call(team,'badge'),false,'badge edits cannot mutate the previous draft');
  assert.equal(model.moveSlot(selected,0,4).badge,badge,'moving members keeps the team-wide badge');
  assert.equal(model.selectCard(selected,0,'body',fixtures[0][0]).badge,badge,'replacing one member retains its team badge');
  assert.equal(model.payload(selected).team.badge,badge,'the reviewed apply payload retains the explicit badge');
  assert.equal(model.parseImport(JSON.stringify(selected)).team.badge,badge,'native JSON retains the exact badge');
  assert.equal(model.parseImport(model.exportLink(selected)).team.badge,badge,'v5 sharing retains zero, PAD Pass and unknown selections without native-ID coercion');
  let badges=model.addLibraryTeam(model.createLibrary(selected),model.selectBadge(team,0),'badge-copy');
  badges=model.normalizeLibrary(JSON.parse(JSON.stringify(badges)));
  assert.equal(model.selectedTeam(badges).badge,0,'JSON restart keeps the copy default distinct');
  badges=model.selectLibraryTeam(badges,'legacy');
  assert.equal(model.selectedTeam(badges).badge,badge,'switching back retrieves the original badge');
  badges=model.updateLibraryTeam(badges,model.selectBadge(model.selectedTeam(badges),undefined));
  assert.equal(Object.prototype.hasOwnProperty.call(model.selectedTeam(badges),'badge'),false,'choosing game selection removes the optional override');
}
const sharedLegacy=JSON.parse(new URL(model.exportLink(team)).searchParams.get('d'));
assert.equal(sharedLegacy.f[0].length,2,'sharing a legacy team never invents a badge choice');
assert.equal(Object.prototype.hasOwnProperty.call(model.parseImport(JSON.stringify(sharedLegacy)).team,'badge'),false,
  'v5 missing badge remains absent after a direct roundtrip');
for (const version of [2,3]) {
  const converted=[1,2,3,4,5,6,7,8,9,11,17,18,19,21,10,12,13,14,129];
  converted.forEach((badge,index)=>assert.equal(model.parseImport(JSON.stringify({v:version,f:[[[],[],index]]})).team.badge,badge,
    `v${version} legacy badge index ${index} uses the upstream mapping`));
  assert.equal(model.parseImport(JSON.stringify({v:version,f:[[[],[]]]})).team.badge,1,'upstream legacy missing defaults to selection 1');
}
assert.equal(model.parseImport(JSON.stringify({v:4,f:[[[],[],50]]})).team.badge,129,'v4 PAD Pass becomes its v5 synthetic selection');
assert.equal(model.parseImport(JSON.stringify({v:5,f:[[[],[],50]]})).team.badge,50,'v5 ID 50 is not remapped as legacy PAD Pass');
for (const version of [4,5]) assert.equal(Object.prototype.hasOwnProperty.call(
  model.parseImport(JSON.stringify({v:version,f:[[[],[]]]})).team,'badge'),false,'v4/v5 missing remains unspecified');
for (const version of [2,3,4,5]) {
  const placeholder=model.parseImport(JSON.stringify({v:version,f:[[[],[],null,2]]})).team;
  if (version < 4) assert.equal(placeholder.badge,1,'legacy null placeholder follows upstream default conversion');
  else assert.equal(Object.prototype.hasOwnProperty.call(placeholder,'badge'),false,'null array gap before leader swap is missing, not explicit no-badge');
}
for (const badge of [-1,256,1.5,true,false,null,'34',[],{}]) {
  assert.throws(()=>model.normalize({...team,badge}),/徽章/);
  assert.throws(()=>model.selectBadge(team,badge),/徽章/);
  if (badge !== null) for (const version of [2,3,4,5]) assert.throws(()=>model.parseImport(JSON.stringify({v:version,f:[[[],[],badge]]})),/徽章/);
}
console.log('Formation editor model/import/library/awakening-label tests: passed');
