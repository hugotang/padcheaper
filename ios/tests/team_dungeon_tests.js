// Synthetic all-floor captures isolate arithmetic, provenance and limitations.
// The user's private dungeon dump is never required or committed.
const assert=require('node:assert/strict'),fs=require('node:fs');
const dungeon=require('../editor/team-dungeon.js'),catalog=require('../editor/catalog.json'),facts=require('../editor/team-catalog.json');
const formation=require('../editor/formation.js').createModel(catalog),model=require('../editor/team-model.js').createModel(catalog,facts,formation);
const team=require('../templates/ishida-uryu-gamewith-574233/formation.json'),matches=[{attr:3,n:3},{attr:5,n:3}];
let checks=0;function check(label,fn){fn();checks++;}
function record(id,op,params=[],attached=0){
 const b=Buffer.alloc(72);b.writeInt32LE(id,0);b.writeUInt16LE(op,4);
 const p=Array.from({length:8},(_,i)=>params[i]||0);p.forEach((x,i)=>b.writeInt32LE(x,16+i*4));b.writeInt32LE(attached,68);
 return {actionID:id,opcode:op,parameters:p,attachedAttackPercentRaw:attached,known:true,rawHex:b.toString('hex')};
}
function enemy(slot,attack,ids){
 const b=Buffer.alloc(220);b.writeInt32LE(1000+slot,0);b.writeInt8(1,12);
 b.writeBigUInt64LE(1n,128);b.writeBigUInt64LE(BigInt(attack),136);b.writeFloatLE(1,144);b.writeUInt16LE(10,176);
 return {slot,catalogID:1000+slot,level:10,catalogPrefixRawHex:b.toString('hex'),rows:ids.map((id,i)=>({row:i+1,actionID:id,operandsRaw:[100,0,0,0]}))};
}
const fixture=()=>({schema:1,kind:'PADCheaper-dungeon-report',target:'jp.gungho.pad-ios-23.6.0-106878',dungeon:12,floor:3,totalFloors:2,allDefinitionsCaptured:true,
 floorDefinitions:[{wave:0,rawCaptureComplete:true,catalogs:[enemy(1,100,[1])],definitions:[record(1,15,[3,3,35])]},
 {wave:1,rawCaptureComplete:true,catalogs:[enemy(1,100850,[2]),enemy(2,100850,[3]),enemy(3,100850,[4])],
 definitions:[record(2,92,[1,1,1],100),record(3,15,[10,10,100]),record(4,92,[1,2,2],100)]}],
 observations:[{wave:0,enemies:[{slot:1,catalogID:1001,level:10,identityKnown:true,memoryKnown:true,baseAttack:2500}]}]});
check('native enemy curves and observed multiplier have separate provenance',()=>{
 const d=dungeon.parse(fixture());assert.equal(d.inferredScale,25);assert.equal(d.floors[0].enemies[0].observedAttack,2500);
 const r=dungeon.evaluate(d,model,team,{matches});assert.equal(r.floors[0].enemies[0].attackSource,'observed');
 assert.equal(r.floors[1].enemies[0].attack,2521250);assert.equal(r.floors[1].enemies[0].attackSource,'projected-from-dungeon-scale');
 assert.equal(r.enemyForecastKnown,false);assert.equal(r.survivalAuthorized,false);
});
check('simultaneous enemy envelope identifies rescue and retains incomplete effects',()=>{
 const r=dungeon.evaluate(dungeon.parse(fixture()),model,team,{matches}),f=r.floors[1];
 const normal=f.aggregate.find(s=>s.scenario==='cycle'),rescue=f.aggregate.find(s=>s.scenario==='rescue');
 assert.equal(normal.damage,637176);assert.equal(normal.hp,613829);assert.equal(normal.fullHPFits,false);
 assert.equal(rescue.hp,1227657);assert.equal(rescue.fullHPFits,true);assert.equal(normal.componentsComplete,false);
 assert.equal(normal.simultaneousActionKnown,false);assert.deepEqual(normal.selectedActions,[2,3,4]);
});
check('missing observations do not assume a dungeon multiplier',()=>{
 const f=fixture();f.observations=[];const d=dungeon.parse(f);assert.equal(d.inferredScale,null);
 assert.equal(dungeon.evaluate(d,model,team,{matches}).floors[0].enemies[0].attack,null);
 assert.equal(dungeon.evaluate(d,model,team,{matches},{attackScale:25}).floors[0].enemies[0].attack,2500);
});
check('partial captures preserve missing floors, enemies and definitions as unknown',()=>{
 const f=fixture();f.floorDefinitions[0]={wave:0,rawCaptureComplete:false,status:'not-captured'};
 f.floorDefinitions[1].definitions[0]={actionID:2,known:false};
 f.floorDefinitions[1].catalogs[2]={slot:3,catalogID:1003,level:10,definitionKnown:false};
 const d=dungeon.parse(f),r=dungeon.evaluate(d,model,team,{matches},{attackScale:25});
 assert.equal(d.complete,false);assert.equal(r.floors[0].complete,false);assert.equal(r.floors[0].enemies.length,0);
 assert.equal(r.floors[1].enemies[0].actions[0].sequence.complete,false);
 assert.equal(r.floors[1].aggregate[0].damage,null);
});
check('stale observation table hashes cannot supply a current scale',()=>{
 const f=fixture();f.spawnTableSHA256='current';f.observations[0].tableSHA256='previous';
 assert.equal(dungeon.parse(f).inferredScale,null);
 f.observations[0].tableSHA256='current';assert.equal(dungeon.parse(f).inferredScale,25);
});
check('GameWith notes are scoped to dungeon and matching enemy identities',()=>{
 const f=fixture();f.dungeon=7802;f.floor=5;f.totalFloors=17;
 f.floorDefinitions=[{wave:5,rawCaptureComplete:false,catalogs:[{slot:1,catalogID:203591,level:10,definitionKnown:false}],definitions:[]},
  {wave:11,rawCaptureComplete:false,catalogs:[{slot:1,catalogID:212228,level:10,definitionKnown:false}],definitions:[]},
  {wave:16,rawCaptureComplete:false,catalogs:[{slot:1,catalogID:305956,level:10,definitionKnown:false}],definitions:[]}];
 const r=dungeon.evaluate(dungeon.parse(f),model,team,{matches});
 assert.equal(r.guide.checkedAt,'2026-11-01');assert.equal(r.guide.matchedTeamNotes.length,2);
 assert.match(r.guide.floors.find(x=>x.number===9).text,/覺醒無效/);
 f.dungeon=1;assert.equal(dungeon.evaluate(dungeon.parse(f),model,team,{matches}).guide,null);
 f.dungeon=7802;f.floorDefinitions[2].catalogs[0].catalogID=1;
 assert.equal(dungeon.evaluate(dungeon.parse(f),model,team,{matches}).guide,null);
});
check('external guides support arbitrary dungeons without executable strategy code',()=>{
 const f=fixture(),guides={schema:1,kind:'PADCheaper-dungeon-guides',guides:[{dungeon:12,subDungeon:3,totalFloors:2,
  title:'本地新關卡',checkedAt:'2026-11-01',sources:['https://example.com/guide'],notes:['全新提示'],
  teamNotes:[{cards:[14060],text:'這隊留技'},{cards:[1],text:'其他隊伍'}],floors:[{number:2,text:'第二層解狀態'}]}]};
 const r=dungeon.evaluate(dungeon.parse(f),model,team,{matches},{guides});
 assert.equal(r.guide.title,'本地新關卡');assert.deepEqual(r.guide.matchedTeamNotes,['這隊留技']);
 assert.equal(r.guide.floors[0].number,2);assert.equal(r.floors[1].aggregate.find(s=>s.scenario==='cycle').damage,637176);
 for(const change of [g=>g.guides[0].sources=['javascript:alert(1)'],g=>g.guides[0].sources=['https://user:pass@example.com'],
  g=>g.guides[0].floors[0].number=3,g=>g.guides.push(g.guides[0]),g=>g.guides[0].teamNotes[0].cards=[0]]){
  const invalid=JSON.parse(JSON.stringify(guides));change(invalid);assert.throws(()=>dungeon.parseGuides(invalid));
 }
});
check('raw fields override neither mismatched identity nor action parameters',()=>{
 for(const change of [f=>f.target='other',f=>f.floorDefinitions[0].definitions[0].parameters[0]=9,
  f=>f.floorDefinitions[0].catalogs[0].catalogID=999,f=>f.floorDefinitions.push(f.floorDefinitions[0]),
  f=>f.floorDefinitions[0].definitions[0].rawHex='00']){const f=fixture();change(f);assert.throws(()=>dungeon.parse(f));}
});
check('duplicate children retain hit order, skipped sentinel child stays absent',()=>{
 const defs=new Map([[1,{id:1,op:83,p:[2,2,3,0,0,0,0,0],known:true}],
  [2,{id:2,op:15,p:[1,1,100],attached:0,known:true}],[3,{id:3,op:82,p:[],attached:0,known:true}]]);
 const s=dungeon.describe(defs,1);assert.equal(s.events.length,2);assert.equal(s.complete,true);
 const r=dungeon.damage(s,1000,1500,[.5]);assert.deepEqual(r.hits,[500,500]);assert.equal(r.remainingHP,500);
});
check('equal HP is lethal, per-hit rounding is retained, rage applies before a hit',()=>{
 const seq={complete:true,events:[{kind:'attack',percent:100},{kind:'attack',percent:100}]};
 assert.equal(dungeon.damage(seq,1,2,[.5]).fullHPFits,false);assert.deepEqual(dungeon.damage(seq,1,3,[.5]).hits,[1,1]);
 assert.equal(dungeon.damage({complete:true,events:[{kind:'enrage',turns:5,percent:200},{kind:'attack',percent:100}]},100,500,[1]).damage,200);
 assert.equal(dungeon.reducedDamage(0x7fffffff,100,2,[1]),null);
});
check('gravity depends on current HP and HP x3 alone does not beat lethal gravity',()=>{
 const seq={complete:true,events:[{kind:'gravity',percent:100}]};
 assert.equal(dungeon.damage(seq,0,300,[1]).fullHPFits,false);assert.equal(dungeon.damage(seq,0,900,[1]).fullHPFits,false);
 const two={complete:true,events:[{kind:'gravity',percent:50},{kind:'gravity',percent:50}]};
 assert.deepEqual(dungeon.damage(two,0,100,[1]).hits,[50,25]);
});
check('awoken bind or an unknown effect before a hit cannot become complete survival',()=>{
 for(const first of [{kind:'status',op:88,turns:1},{kind:'status',op:141,turns:1},{kind:'unknown'}]){
  const r=dungeon.damage({complete:true,events:[first,{kind:'attack',percent:100}]},100,1000,[.1]);assert.equal(r.complete,false);
 }
});
check('pure damage parity with the native arithmetic fixture',()=>{
 // PHEnemyThreat's supported float32 RawHit and double*float Reduce operations.
 const r=dungeon.damage({complete:true,events:Array.from({length:3},()=>({kind:'attack',percent:35}))},25000000,1227657,[.65,.0324]);
 assert.deepEqual(r.hits,[184275,184275,184275]);assert.equal(r.damage,552825);
});
const report={checks,privateDumpRequired:false,deviceVerified:false,enemyForecastKnown:false,survivalAuthorized:false};
fs.mkdirSync('build/team-model',{recursive:true});fs.writeFileSync('build/team-model/dungeon-test-result.json',JSON.stringify(report,null,2)+'\n');
console.log('Offline dungeon comparison: '+checks+' checks passed.');
