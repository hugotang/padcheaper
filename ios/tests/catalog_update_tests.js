// Public inventory coverage and consumer behavior, including incomplete facts.
const assert=require('node:assert/strict'),fs=require('node:fs');
const catalog=require('../editor/catalog.json'),facts=require('../editor/team-catalog.json');
const formation=require('../editor/formation.js').createModel(catalog);
const calc=require('../editor/team-model.js').createModel(catalog,facts,formation);
const source=require('../data/editor-supplement/latest-jp.json');
const published=source.mons.filter(r=>r.mon.num>14337).map(r=>r.mon);
assert.equal(published.length,89);assert.equal(Math.max(...published.map(c=>c.num)),14442);
for(const c of published) {
 const result=formation.search(String(c.num),false).find(r=>r.id===c.num);
 assert.ok(result,'every published new form is searchable: '+c.num);
 assert.equal(result.name,c.name);assert.deepEqual(result.awakenings,c.awokens);
 assert.equal(result.sync,c.sync_awoken);assert.equal(result.maxLevel,c.displayStats.level);
 const team=formation.selectCard(formation.empty(),0,'body',c.num);
 const link=formation.exportLink(team),roundtrip=formation.parseImport(link).team;
 assert.equal(roundtrip.slots[0].body.id,c.num);
 assert.equal(formation.exportLink(roundtrip),link,'sharing resolves defaults without changing the effective loadout');
 const r=calc.evaluate(team);
 assert.equal(r.known,false,'missing stat/leader parameters cannot become a known total');
 assert.equal(r.totals.hp,null);assert.equal(r.shield.total,null);assert.equal(r.survivalAuthorized,false);
 assert.ok(r.members[0].id);assert.ok(!r.members[0].form.reason,'known transform pairs resolve without cycles');
 for(const s of r.skillOptions) {assert.equal(s.id,null);assert.equal(s.selectable,false);}
}
assert.equal(formation.cards.get(14442).maxLevel,120);
assert.equal(formation.cards.get(14354).maxLevel,1);
assert.equal(formation.cards.get(14409).maxSkill,11);
assert.ok(formation.search('マグナード',false).some(c=>c.id===14359));
assert.ok(formation.search('14361',true).some(c=>c.id===14361),'Magnard weapon is included too');
assert.equal(formation.search('14359',true).length,0,'non-assist forms cannot be weapons');
assert.ok(formation.search('14424',true).some(c=>c.id===14424),'alternative high-altitude weapon is present');
const dir='ios/templates/magnard-gamewith-589197/';
const team=formation.parseImport(fs.readFileSync(dir+'formation.json','utf8')).team;
assert.deepEqual(team.slots.map(s=>s.body.id),[14359,14407,14422,14389,14383,14359]);
assert.deepEqual(team.slots.map(s=>s.assist.id),[14394,14409,13682,12968,14394,14291]);
assert.deepEqual(team.slots.map(s=>s.body.superAwakening),Array(6).fill(138));
assert.deepEqual(team.slots.map(s=>s.assist.skill),Array(6).fill(1));
assert.equal(team.badge,100);assert.deepEqual(team.slots[1].body.latents,[48]);assert.deepEqual(team.slots[3].body.latents,[41]);
assert.deepEqual(formation.parseImport(fs.readFileSync(dir+'formation-share.txt','utf8')).team,team);
const saved=formation.createLibrary(team);
assert.deepEqual(formation.selectedTeam(formation.normalizeLibrary(JSON.parse(JSON.stringify(saved)))),team);
const current=calc.evaluate(team,{phase:'current'}),final=calc.evaluate(team);
assert.deepEqual(final.members.map(m=>m.id),[14360,14408,14423,14390,14384,14360]);
assert.equal(current.skillOptions.find(s=>s.key==='0:body:0').cooldown,20);
assert.equal(final.skillOptions.find(s=>s.key==='0:body:0').cooldown,4);
assert.equal(final.skillOptions.find(s=>s.key==='1:assist:0').cooldown,15);
assert.equal(final.skillOptions.find(s=>s.key==='1:assist:0').minimumCooldown,5);
const skillModel=require('../editor/team-skills.js').createModel(facts);
assert.throws(()=>skillModel.project(final.skillOptions,[{key:'0:body:0'}]),/無法/);
assert.throws(()=>require('../editor/team-model.js').createModel(catalog,{...facts,supplement:{...facts.supplement,sha256:'other'}},formation),/版本/);
// A supplemental sub or weapon also withholds totals under fully known leaders.
const old=require('../templates/ishida-uryu-gamewith-574233/formation.json');
for(const kind of ['body','assist']) {
 const mixed=formation.selectCard(old,2,kind,kind==='body'?14442:14424);
 assert.equal(calc.evaluate(mixed).known,false);
}
console.log('Catalog update: all 89 public new forms, Magnard formation/share/library, CD/transform and incomplete-data gates passed.');
