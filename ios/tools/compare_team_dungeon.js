#!/usr/bin/env node
// Reusable local report. No account, game process or network access.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const packaged=fs.existsSync(path.join(__dirname,'../editor-source/team-dungeon.js')),editor=path.resolve(__dirname,packaged?'../editor-source':'../editor'),args=process.argv.slice(2);
const usage='node ios/tools/compare_team_dungeon.js DUMP.json FORMATION.json OUTPUT_DIRECTORY [SCENARIO.json] [GUIDES.json]';
if(args.length<3||args.length>5)throw new Error(usage);
const input=fs.readFileSync(args[0]),teamBytes=fs.readFileSync(args[1]);
const dungeon=require(editor+'/team-dungeon.js'),catalog=require(editor+'/catalog.json'),facts=require(editor+'/team-catalog.json');
const formation=require(editor+'/formation.js').createModel(catalog),model=require(editor+'/team-model.js').createModel(catalog,facts,formation);
const team=JSON.parse(teamBytes),scenario=args[3]?JSON.parse(fs.readFileSync(args[3],'utf8')):{matches:[{attr:3,n:3},{attr:5,n:3}]};
const guideBytes=args[4]?fs.readFileSync(args[4]):fs.readFileSync(path.join(editor,'dungeon-guides.json'));
const parsed=dungeon.parse(input.toString('utf8').replace(/^\uFEFF/,'')),report=dungeon.evaluate(parsed,model,team,scenario,{guides:dungeon.parseGuides(guideBytes.toString('utf8'))});
const digest=value=>crypto.createHash('sha256').update(value).digest('hex');
report.provenance={dumpSHA256:digest(input),formationSHA256:digest(teamBytes),guidesSHA256:digest(guideBytes),catalogRevision:facts.revision,
  sourceSHA256:Object.fromEntries(['team-dungeon.js','team-model.js','team-skills.js','team-catalog.json','dungeon-guides.json'].map(n=>[n,digest(fs.readFileSync(path.join(editor,n)))])),scenario};
const n=x=>x===null||x===undefined?'未知':x.toLocaleString('en-US'),lines=[`# 關卡 ${report.dungeon}／${report.subDungeon}：隊伍 HP／盾對比`,'',
  '此報告讀取使用者全層匯出及編隊配方，只作離線對策試算；不修改遊戲，也不授權 Auto 放寬消珠要求。','',
  `收錄 ${report.floors.length}／${report.totalFloors} 層；觀察推得敵人 ATK 倍率 ${report.attackScale===null?'未知':'×'+report.attackScale}。未觀察樓層由原生敵人曲線與此倍率推算，並非逐層實測。`,'',
  '## 技能與實際起始血量','',
  '同類效果逐項覆蓋，不相乘。需要最大 HP 救援時，較高 HP 倍率應在其他 HP 技能之後施放。提高 HP 上限不會補滿新增血量；此處技能均假設施放當回合且已就緒。','',
  '| 技能 | CD | 效果 |','|---|---:|---|'];
for(const s of report.skills)lines.push(`| #${s.slot} ${s.name.replace(/\|/g,'／')} | ${s.cooldown} | ${s.effects.map(e=>`${e.kind} ${e.value}，${e.turns} 回合`).join('；')} |`);
lines.push('','| 狀態 | HP 上限 | 起始 HP | 心珠回復估值 | 消珠後 HP 估值 |','|---|---:|---:|---:|---:|');
for(const s of report.floors[0]?.scenarios||[])lines.push(`| ${s.label} | ${n(s.hp)} | ${n(s.startingHP)} | ${n(s.heartHeal)} | ${n(s.afterPlayerEstimate)} |`);
lines.push('','## 各層容量對比','','以下每列取已讀攻擊中最大值；多敵樓層合計每隻各一招，假設它們同回合行動且均存活。不是所有分支的上界，也不是實際下一招。暴怒另列，不默認已生效。','',
 '| 層 | 已讀最大攻擊／條件 | 日常＋盾承傷估值 | 日常 HP | 救援 HP | 完整性 |','|---|---|---:|---:|---:|---|');
for(const floor of report.floors){
 const normal=floor.scenarios.find(s=>s.id==='cycle')||floor.scenarios.find(s=>s.id==='shield')||floor.scenarios[0],rescue=floor.scenarios.find(s=>s.id==='rescue');
 const candidate=floor.enemies.flatMap(e=>e.actions.flatMap(a=>a.variants.filter(v=>v.enrage===1).map(v=>({e,a,v,c:v.comparisons.find(c=>c.scenario===normal.id)}))))
  .filter(x=>x.c?.hasDamage).sort((a,b)=>b.c.damage-a.c.damage)[0];
 const all=floor.aggregate.find(s=>s.scenario===normal.id),unknown=floor.enemies.flatMap(e=>e.actions).filter(a=>!a.sequence.complete).length;
 const amount=all?.damage??candidate?.c.damage,source=all?'假設所有存活敵人同回合出招':candidate?'行動 '+candidate.a.id:'尚無已讀攻擊';
 lines.push(`| ${floor.number} | ${source} | ${n(amount)} | ${n(normal.hp)} | ${n(rescue?.hp)} | ${unknown} 招未完整；選招未知 |`);
}
lines.push('','## 需要預留 HP 救援的候選情境','');
let count=0;
for(const floor of report.floors){
 const cycle=floor.aggregate.find(s=>s.scenario==='cycle'),rescue=floor.aggregate.find(s=>s.scenario==='rescue');
 if(cycle?.fullHPFits===false&&rescue?.fullHPFits){
  count++;lines.push(`- 第 ${floor.number} 層多敵同回合：已讀攻擊合計約 ${n(cycle.damage)}，日常 HP ${n(cycle.hp)} 不足，救援 HP ${n(rescue.hp)} 可容納這部分傷害。僅此部分便需 HP ≥ ${n(cycle.requiredHP)}；${cycle.componentsComplete?'已讀攻擊序列完整':'含未完整的狀態／行動'}，各敵倒數及擊殺結果仍須確認。`);
 }
 for(const enemy of floor.enemies)for(const action of enemy.actions)for(const variant of action.variants){
  if(!variant.componentNeedsRescue)continue;count++;
  const c=variant.comparisons.find(s=>s.scenario==='cycle'),r=variant.comparisons.find(s=>s.scenario==='rescue');
  lines.push(`- 第 ${floor.number} 層敵 ${enemy.slot}，行動 ${action.id}，${variant.enrage} 倍暴怒假設：已讀攻擊部分約 ${n(c.damage)}，超出日常 HP ${n(c.hp)}，救援 HP ${n(r.hp)} 可容納。${action.availabilityNote||''} 行動／暴怒可達性未證明，不當作固定樓層必開技能。`);
 }
 const without=floor.enemies.flatMap(e=>e.actions).filter(a=>a.variants.some(v=>v.needsRescueWithoutShield));
 if(without.length)lines.push(`- 第 ${floor.number} 層：行動 ${without.map(a=>a.id).join('、')} 存在「日常 HP 在盾到期時不足、HP 救援容量足夠」的情境。優先保持技能盾；到期時仍需按剩餘敵人與暴怒狀態確認。`);
}
if(!count)lines.push('尚未在已解析的攻擊中找到超出日常容量而可由救援提高容量的情境；這不代表所有樓層安全。');
if(report.guide){
 const g=report.guide;
 lines.push('','## 攻略資料對照','',g.title+'，核對於 '+g.checkedAt+'。','',
  ...g.sources.map((url,i)=>`- [來源 ${i+1}](${url})`),'',...g.notes.map(s=>'- '+s),'',...g.matchedTeamNotes.map(s=>'- '+s),'');
 for(const f of g.floors)lines.push(`- **B${f.number}**：${f.text}`);
 if(g.damageReference)lines.push('','攻略參考「'+g.damageReference.label+'」原始傷害 '+n(g.damageReference.rawDamage)+'；保留為攻略來源，不補造 dump 原始行動定義。');
}
lines.push('','## 狀態、重力與施放時機','',
  '- 技能回合數能覆蓋 CD 不等於實戰不斷檔：技能延遲、封技、武裝覆蓋與跨層結算仍須確認。',
  '- 覺醒／武裝失效及不可消會改變隊伍能力或盾發動條件；提高 HP 本身不改善百分比重力比例。',
  '- 先制必須在進層前已有有效防禦；不能進層挨打後才開技。此報告沒有推定 1 回合 HP buff 穿過擊殺／換層後仍能覆蓋先制。',
  '- dump 尚不能把每個行動精確定位為首次、超根性後、低血或特定回合。對比 JSON 保留行動 ID／表列／分段傷害，供後續解析。','',
  '## 範圍','',...report.limitations.map(x=>'- '+x),'',
  '`survivalAuthorized=false`。圖示與隊伍資訊見編隊頁；本報告不包含原始私有 dump。','');
const out=path.resolve(args[2]);fs.mkdirSync(out,{recursive:true});
fs.writeFileSync(path.join(out,'comparison.json'),JSON.stringify(report,null,2)+'\n');fs.writeFileSync(path.join(out,'comparison.md'),lines.join('\n'));
console.log(JSON.stringify({directory:out,floors:report.floors.length,attackScale:report.attackScale,candidateScenarios:count,survivalAuthorized:false}));
