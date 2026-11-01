// Independent numerical expectations, real pinned card facts and malformed
// cases. No upstream calculator or live game is invoked.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const catalog=require('../editor/catalog.json'),facts=require('../editor/team-catalog.json');
const formation=require('../editor/formation.js').createModel(catalog);
const moduleModel=require('../editor/team-model.js');
const calc=moduleModel.createModel(catalog,facts,formation);
let checks=0;
function check(label,fn){fn();checks++;}
const real=require('../templates/ishida-uryu-gamewith-574233/formation.json');
const plain=[{attr:3,n:3},{attr:5,n:3}],lightL=[{attr:3,n:5,shape:'l'},{attr:5,n:4,shape:'four'}];
const lightT=[{attr:3,n:5,shape:'t'},{attr:5,n:4,shape:'four'}];
check('pinned input provenance',()=>{
 assert.equal(facts.revision,catalog.revision);assert.equal(facts.card_sha256,catalog.card_sha256);
 assert.deepEqual(facts.cards.map(r=>r[0]),catalog.cards.map(r=>r[0]));
 assert.throws(()=>moduleModel.createModel(catalog,{...facts,revision:'other'},formation),/版本/);
});
check('recorded team HP agrees with initial 397834 observation',()=>{
 const r=calc.evaluate(real,{phase:'current'});
 assert.equal(r.known,true);assert.equal(r.totals.hp,397834);
 assert.deepEqual(r.members.map(m=>m.hp),[59756,49866,70344,74427,71477,71964]);
});
check('final forms preserve loadout and input',()=>{
 const before=JSON.stringify(real),r=calc.evaluate(real);
 assert.equal(r.totals.hp,409219);assert.equal(r.members[0].id,14038);assert.equal(r.members[0].level,99);
 assert.equal(r.members[0].bodyAwakenings.length,9);assert.deepEqual(r.members[0].extraAwakenings,[138]);
 assert.equal(JSON.stringify(real),before);assert.equal(r.survivalAuthorized,false);
});
check('light+heart grants shields without a T awakening',()=>{
 const r=calc.evaluate(real,{matches:plain}),l=calc.evaluate(real,{matches:lightL}),t=calc.evaluate(real,{matches:lightT});
 assert.equal(r.shield.total[0],.9676);assert.equal(l.shield.total[0],.9676);assert.equal(t.shield.total[0],.9676);
 assert.equal(r.shield.heartL,0);assert.equal(l.shield.heartL,0);
 assert.equal(r.totals.rcv,244430);assert.equal(l.totals.rcv,244430);assert.equal(t.totals.rcv,488860);
 assert.equal(r.combos,6);assert.equal(t.combos,8);
 assert.equal(calc.evaluate(real).shield.total[0],0);
});
check('explicit active buffs, no implicit cast skills',()=>{
 const r=calc.evaluate(real,{matches:plain,activeShield:35,hpMultiplier:1.5,rcvMultiplier:3});
 assert.ok(Math.abs(r.shield.total[0]-.97894)<1e-12);assert.equal(r.totals.hp,613829);
 assert.equal(r.totals.rcv,733290);assert.equal(calc.evaluate(real).shield.active,0);
});
const skillContext={matches:plain,width:7,height:6,activeSkills:[{key:'3:body:0',elapsed:0},{key:'2:body:0',elapsed:0}]};
check('real Unohana and Haschwalth skills, duration and cooldown',()=>{
 const r=calc.evaluate(real,skillContext),hp=r.skillOptions.find(s=>s.key==='2:body:0'),shield=r.skillOptions.find(s=>s.key==='3:body:0');
 assert.equal(hp.cooldown,5);assert.equal(shield.cooldown,5);assert.equal(hp.minimumCooldown,5);
 assert.deepEqual(hp.effects.map(e=>[e.kind,e.turns,e.value]),[['board',1,'7x6'],['rcv',1,3],['hp',1,3]]);
 assert.deepEqual(shield.effects.map(e=>[e.kind,e.turns,e.value]),[['shield',5,35]]);
 assert.equal(r.totals.hp,1227657);assert.equal(r.totals.rcv,733290);assert.ok(Math.abs(r.shield.total[0]-.97894)<1e-12);
 assert.equal(r.context.skillUses,2);assert.equal(r.survivalAuthorized,false);
});
check('max HP skill does not heal the additional capacity',()=>{
 const r=calc.evaluate(real,{...skillContext,matches:[]});
 assert.equal(r.recovery.startingHP,409219);assert.equal(r.recovery.afterHP,409219);
 assert.equal(calc.evaluate(real,{...skillContext,matches:[],hpBasis:'effective'}).recovery.startingHP,1227657);
 assert.equal(calc.evaluate(real,{...skillContext,hpPercent:0}).recovery.afterHP,1227657);
 assert.throws(()=>calc.evaluate(real,{...skillContext,width:6,height:5}),/盤面/);
});
check('one-turn HP and five-turn shield expire at distinct turns',()=>{
 let r=calc.evaluate(real,{matches:plain,activeSkills:[{key:'2:body:0',elapsed:1},{key:'3:body:0',elapsed:4}]});
 assert.equal(r.totals.hp,409219);assert.equal(r.totals.rcv,244430);assert.ok(Math.abs(r.shield.total[0]-.97894)<1e-12);
 r=calc.evaluate(real,{matches:plain,activeSkills:[{key:'2:body:0',elapsed:1},{key:'3:body:0',elapsed:5}]});
 assert.equal(r.shield.total[0],.9676);
});
check('Byakuya replaces only HP; skill order preserves Unohana RCV',()=>{
 const c={...skillContext,activeSkills:[...skillContext.activeSkills,{key:'4:body:0',elapsed:0}]};
 let r=calc.evaluate(real,c);assert.equal(r.totals.hp,613829);assert.equal(r.totals.rcv,733290);
 assert.equal(r.activeSkills.selected.find(s=>s.key==='2:body:0').effects.find(e=>e.kind==='hp').overwritten,true);
 c.activeSkills=[{key:'4:body:0',elapsed:0},...skillContext.activeSkills];r=calc.evaluate(real,c);assert.equal(r.totals.hp,1227657);
 assert.throws(()=>calc.evaluate(real,{...skillContext,hpMultiplier:1.5}),/重複/);
});
check('actual skill level affects cooldown, expiry never restores an overwritten shield',()=>{
 const t=structuredClone(real);t.slots[3].body.skill=1;assert.equal(calc.evaluate(t).skillOptions.find(s=>s.key==='3:body:0').cooldown,9);
 const r=calc.evaluate(real,{matches:plain,activeSkills:[{key:'3:body:0',elapsed:3},{key:'5:assist:0',elapsed:2}]});
 assert.equal(r.shield.active,0);assert.equal(r.activeSkills.selected[0].effects[0].overwritten,true);
 assert.throws(()=>calc.evaluate(real,{activeSkills:[{key:'3:body:0',elapsed:-1}]}),/回合/);
 assert.throws(()=>calc.evaluate(real,{activeSkills:[{key:'3:body:0'},{key:'3:body:0'}]}),/重複/);
});
check('attribute shield applies only to its stated attribute',()=>{
 const r=calc.evaluate(real,{matches:plain,activeSkills:[{key:'4:assist:0'}]});
 assert.equal(r.shield.total[0],.9676);assert.equal(r.shield.total[4],1);
});
check('delayed and evolved skill choices retain timing and reject random effects',()=>{
 const sm=require('../editor/team-skills.js').createModel({activeSkills:[
  [1,232,[2,3],1,8,'stages',''],[2,3,[1,50],1,8,'first',''],[3,3,[5,35],1,5,'second',''],
  [4,248,[2,3],1,10,'delayed',''],[5,118,[2,3],1,3,'random',''],[6,244,[1,99],1,3,'bad','']]});
 const options=sm.describe({id:1,active:1},{skill:1},0,'body');assert.equal(options[1].cooldown,5);
 assert.throws(()=>sm.project(options,[{key:'0:body:0'},{key:'0:body:1'}]),/階段/);
 const delayed=sm.describe({id:4,active:4},{skill:1},1,'body');
 assert.equal(sm.project(delayed,[{key:'1:body:0',elapsed:1}]).effects.length,0);
 assert.equal(sm.project(delayed,[{key:'1:body:0',elapsed:2}]).effects[0].remaining,5);
 assert.equal(sm.project(delayed,[{key:'1:body:0',elapsed:7}]).effects.length,0);
 assert.equal(sm.describe({id:5,active:5},{skill:1},2,'body')[0].selectable,false);
 assert.equal(sm.describe({id:6,active:6},{skill:1},2,'body')[0].selectable,false);
});
check('heart L is separate from light L',()=>{
 const r=calc.evaluate(real,{matches:[{attr:3,n:3},{attr:5,n:5,shape:'l'}]});
 assert.equal(r.shield.heartL,.25);assert.ok(Math.abs(r.shield.total[0]-.9757)<1e-12);
});
check('branch and cyclic transformations require explicit form',()=>{
 let t=formation.selectCard(formation.empty(),0,'body',8614);
 let r=calc.evaluate(t);assert.equal(r.known,false);assert.equal(r.totals.hp,null);assert.match(r.issues.join(' '),/指定型態/);
 r=calc.evaluate(t,{finalForms:{0:8615}});assert.equal(r.members[0].id,8615);assert.equal(r.members[0].known,true);
 assert.throws(()=>calc.evaluate(t,{finalForms:{0:14038}}),/不屬於/);
 assert.equal(calc.evaluate(t,{phase:'current'}).members[0].id,8614);
});
check('invalid scenarios do not return previous plausible values',()=>{
 for(const c of [{mode:'coop'},{phase:'anything'},{floor:0},{hpPercent:101},{activeShield:-1},{parts:1.5},
  {hpMultiplier:Infinity},{matches:[{attr:3,n:4,shape:'t'}]},{matches:[{attr:6,n:3}]},{matches:[{attr:5,n:3,enhanced:NaN}]},
  {matches:[{attr:3,n:31}]}]) {
   assert.throws(()=>calc.evaluate(real,c));
 }
});
// Tiny synthetic catalogs isolate arithmetic and eligibility from changing
// real-card balance data. Formula expectations are explicit numbers.
function fixture(rows=[],skills=[]) {
 const identity={revision:'test',card_sha256:'cards',skill_sha256:'skills',region:'ja',numbering:'encyclopedia'};
 const defs=[{id:1,attrs:[3],types:[6],hp:1000,atk:100,rcv:100},...rows];
 const ec={...identity,cards:defs.map(c=>[c.id,'Card '+c.id,c.attrs||[3],c.limit?120:c.max||99,1,c.aw||[],c.supers||[],c.sync||0,true,8,''])};
 const fc={...identity,schema:1,cards:defs.map(c=>[c.id,c.types||[6],c.max||99,c.limit||0,
  [c.hp??1000,c.hp??1000,1],[c.atk??100,c.atk??100,1],[c.rcv??100,c.rcv??100,1],c.leader||0,c.to||[],c.rarity||6,c.collab||0]),skills};
 const f=require('../editor/formation.js').createModel(ec),m=moduleModel.createModel(ec,fc,f);
 const body=(id,changes={})=>({id,level:99,skill:1,awakenings:-1,plus:[0,0,0],superAwakening:0,latents:[],...changes});
 const team=f.empty();team.slots[0].body=body(1);
 return {f,m,team,body};
}
check('per-stat plus, same-attribute assist, no assist awakening stats',()=>{
 const {m,team,body}=fixture([{id:2,hp:1000,rcv:200,aw:[49,1,3]}]);
 team.slots[0].body.plus=[99,99,99];team.slots[0].assist=body(2,{plus:[99,99,99]});
 const r=m.evaluate(team).members[0];
 assert.equal(r.stats[0],5189); // 1000+990+3000+round((1000+990)*.1)
 assert.equal(r.stats[2],972); // 100+297+500+round((200+297)*.15)
 team.slots[0].assist.awakenings=0;
 assert.equal(m.evaluate(team).members[0].stats[0],2189);
});
check('weapon awakenings require enabled assist awakening',()=>{
 const {m,team,body}=fixture([{id:2,attrs:[1],aw:[49,46]}]);team.slots[0].assist=body(2);
 assert.equal(m.evaluate(team).totals.hp,1050); // Different attribute gets awakening, no stat inherit.
 team.slots[0].assist.awakenings=0;assert.equal(m.evaluate(team).totals.hp,1000);
});
check('limit break, super limit break, voice and latent rounding',()=>{
 const {m,team,body}=fixture([{id:2,limit:30,hp:1000,rcv:100,aw:[63]}]);team.slots[0].body=body(2,{level:120,latents:[1,3]});
 const r=m.evaluate(team).members[0];assert.equal(r.stats[0],1561); // 1400 + 140 voice + 21 latent
 assert.equal(r.stats[2],163); // 135 + round(13.5) + round(13.5)
});
check('native stat awakening rounding is not floor',()=>{
 const {m,team,body}=fixture([{id:2,hp:1001,rcv:101,aw:[145]}]);team.slots[0].body=body(2);
 const r=m.evaluate(team).members[0];assert.equal(r.stats[0],1502);assert.equal(r.stats[2],152);
 assert.equal(moduleModel.round(-1.5),-2);
});
check('weapon types change leader eligibility without adding another multiply',()=>{
 const {m,team,body}=fixture([{id:2,aw:[49,89]},{id:3,leader:10}],[[10,121,[8,4,200,100,100]]]);
 team.slots[0].body=body(3);team.slots[0].assist=body(2);
 assert.equal(m.evaluate(team).members[0].hpFactor,2);assert.ok(m.evaluate(team).members[0].types.includes(2));
});
check('same-colour assist enhancement uses natural type and strict primary colour',()=>{
 const {m,team,body}=fixture([{id:2,types:[5],aw:[49,88]},{id:3,aw:[138]},{id:4,attrs:[6],types:[6],aw:[49]}]);
 team.slots[0].body=body(3);team.slots[0].assist=body(2);
 assert.equal(m.evaluate(team).members[0].stats[0],1100); // Added attack type is not natural type.
 team.slots[0].assist=body(4);assert.equal(m.evaluate(team).members[0].stats[0],1100); // wildcard inherits, no matching-colour x3
});
check('stacked stat and attribute shields combine multiplicatively',()=>{
 const {m,team,body}=fixture([{id:2,aw:[4],leader:10}],[[10,16,[50]]]);
 team.slots[0].body=body(2,{latents:[6,32]});team.slots[5].body=body(2);
 const r=m.evaluate(team,{activeShield:20});assert.ok(Math.abs(r.shield.resist[0]-.175)<1e-12);
 assert.ok(Math.abs(r.shield.total[0]-.835)<1e-12);assert.equal(r.shield.total[1],.8);
});
check('unknown leader effects are explicit and never grant survival',()=>{
 const {m,team,body}=fixture([{id:2,leader:10}],[[10,999,[]]]);team.slots[0].body=body(2);
 const r=m.evaluate(team);assert.equal(r.known,false);assert.equal(r.totals.hp,null);assert.equal(r.shield.total,null);assert.equal(r.survivalAuthorized,false);
});
check('attribute attack/recovery leader type 28 includes recovery',()=>{
 const {m,team,body}=fixture([{id:2,attrs:[1],leader:10},{id:3,attrs:[3]}],[[10,28,[1,250]]]);
 team.slots[0].body=body(2);team.slots[1].body=body(3);
 const r=m.evaluate(team,{matches:[{attr:5,n:3}]});
 assert.equal(r.totals.hp,2000);assert.equal(r.totals.rcv,350);assert.equal(r.recovery.heart,350);
 assert.deepEqual(r.members.slice(0,2).map(m=>m.rcvFactor),[2.5,1]);
});
check('unknown final forms do not contribute fallback awakening totals',()=>{
 const {m,team,body}=fixture([{id:2,aw:[46],to:[3,4]},{id:3,aw:[47]},{id:4,aw:[56]}]);
 team.slots[0].body=body(2);
 const r=m.evaluate(team);assert.equal(r.loadoutKnown,false);assert.equal(r.teamHP,0);assert.deepEqual(r.awakenings,{});
 const selected=m.evaluate(team,{finalForms:{0:3}});assert.equal(selected.loadoutKnown,true);assert.equal(selected.teamRCV,1);
});
check('abnormality resistance includes plus awakenings and courage soul',()=>{
 const {m,team,body}=fixture([{id:2,aw:[68,12,13,28,146,54]},{id:3,aw:[49,70,28,28,55]}]);
 team.slots[0].body=body(2);team.slots[0].assist=body(3);
 assert.deepEqual(m.evaluate(team).protection,{dark:1,jammer:.2,poison:1,skillSeal:1,cloud:1,tape:1});
 team.slots[0].assist.awakenings=0;
 assert.equal(m.evaluate(team).protection.poison,.2);assert.equal(m.evaluate(team).protection.tape,0);
});
check('multicolor awakening leader needs the awakening owner to attack',()=>{
 const {m,team,body}=fixture([{id:2,attrs:[3],aw:[79],leader:10},{id:3,attrs:[0,1,2]}],[[10,271,[1024,100,80,0,0,200]]]);
 team.slots[0].body=body(2);team.slots[1].body=body(3);
 const matches=[{attr:0,n:3},{attr:1,n:3},{attr:2,n:3}];
 assert.equal(m.evaluate(team,{matches}).shield.total[0],0);
 assert.equal(m.evaluate(team,{matches:[...matches,{attr:3,n:3}]}).shield.total[0],.8);
});
check('one group cannot satisfy two same-colour group requirements',()=>{
 const {m,team,body}=fixture([{id:2,leader:10}],[[10,171,[8,8,0,0,2,100,80]]]);team.slots[0].body=body(2);
 assert.equal(m.evaluate(team,{matches:[{attr:3,n:6}]}).shield.total[0],0);
 assert.equal(m.evaluate(team,{matches:[{attr:3,n:3},{attr:3,n:3}]}).shield.total[0],.8);
});
check('T awakening needs an effective attacker of the matched attribute',()=>{
 const {m,team,body}=fixture([{id:2,attrs:[1],aw:[126],leader:10}],[[10,271,[16,100,80,0,0,200]]]);team.slots[0].body=body(2);
 assert.equal(m.evaluate(team,{matches:[{attr:3,n:5,shape:'t'}]}).shield.total[0],0);
 assert.equal(m.evaluate(team,{matches:[{attr:1,n:5,shape:'t'}]}).shield.total[0],.8);
 team.slots[0].body.awakenings=0;assert.equal(m.evaluate(team,{matches:[{attr:1,n:5,shape:'t'}]}).shield.total[0],0);
});
check('probabilistic shield is never guaranteed',()=>{
 const {m,team,body}=fixture([{id:2,leader:10}],[[10,43,[50,50,80]]]);team.slots[0].body=body(2);
 const r=m.evaluate(team);assert.equal(r.shield.total[0],0);assert.match(r.notes.join(' '),/機率/);
});
check('heart4 boosts the owner recovery only, not the whole team',()=>{
 const {m,team,body}=fixture([{id:2,aw:[29,29],rcv:100}]);team.slots[0].body=body(2);team.slots[1].body=body(1);
 assert.equal(m.evaluate(team,{matches:[{attr:5,n:3}]}).recovery.heart,200);
 assert.equal(m.evaluate(team,{matches:[{attr:5,n:4,shape:'four'}]}).recovery.heart,406);
 assert.equal(m.evaluate(team,{matches:[{attr:5,n:5,shape:'l'}]}).recovery.heart,300);
});
check('dungeon awakening contexts and active super selection',()=>{
 const {m,team,body}=fixture([{id:2,hp:1000,aw:[128,130,131],limit:0,sync:138},{id:3,aw:[49],types:[6]}]);
 team.slots[0].body=body(2);assert.equal(m.evaluate(team).totals.hp,1000);
 assert.equal(m.evaluate(team,{benefit:'sun',floor:10,parts:1}).totals.hp,4800);
 team.slots[0].body.superAwakening=138;team.slots[0].assist=body(3);assert.equal(m.evaluate(team).totals.hp,3300);
});
const report={checks,revision:facts.revision,recordedCurrentHP:397834,finalHP:409219,deviceVerified:false,survivalAuthorized:false};
fs.mkdirSync('build/team-model',{recursive:true});fs.writeFileSync('build/team-model/test-result.json',JSON.stringify(report,null,2)+'\n');
console.log('Solo team model: '+checks+' checks passed.');
