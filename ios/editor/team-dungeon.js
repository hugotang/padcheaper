// Offline comparison of captured action definitions, never a live AI selector.
// Imported JSON is data only. Unmodeled branches/statuses remain explicit.
(function(root){
  'use strict';
  const guides=typeof module==='object'&&module.exports?require('./dungeon-guides.json'):
    typeof PH_DUNGEON_GUIDES==='object'?PH_DUNGEON_GUIDES:null;
  // Offline report parsing only. Preserve the source version; accepting an old
  // report never identifies the current game or authorizes a native operation.
  const f32=Math.fround,MAX=0x7fffffff,REPORT_TARGETS=new Set([
    'jp.gungho.pad-ios-23.6.1-107553','jp.gungho.pad-ios-23.6.0-106878']);
  const controls=new Set([21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,43,44,45,91]);
  const supported=new Set([15,17,18,19,47,50,82,88,89,107,140,141]);
  const effectNames={88:'覺醒無效',89:'技能延遲',107:'不可消',140:'指定位置技能延遲',141:'武裝無效'};
  const number=(n,min=0,max=MAX)=>Number.isSafeInteger(n)&&n>=min&&n<=max;
  function raw(hex,length){
    if(typeof hex!=='string'||hex.length!==length*2||!/^[0-9a-f]+$/i.test(hex))throw new Error('關卡原始資料長度或格式不符');
    return new DataView(Uint8Array.from(hex.match(/../g),b=>parseInt(b,16)).buffer);
  }
  function parseGuides(input=guides){
    if(typeof input==='string'){if(input.length>1000000)throw new Error('攻略 JSON 超過 1 MB');input=JSON.parse(input.replace(/^\uFEFF/,''));}
    if(!input||input.schema!==1||input.kind!=='PADCheaper-dungeon-guides'||!Array.isArray(input.guides)||input.guides.length>64)
      throw new Error('需要 schema 1 的 PADCheaper-dungeon-guides 攻略庫，最多 64 關');
    const text=(s,max)=>{if(typeof s!=='string'||!s.trim()||s.length>max)throw new Error('攻略文字缺失或過長');return s;};
    const list=(a,max)=>{if(!Array.isArray(a)||a.length>max)throw new Error('攻略資料數量不符');return a;};
    const seen=new Set();
    const normalized=input.guides.map(g=>{
      if(!g||!number(g.dungeon,1)||!number(g.subDungeon,1)||!number(g.totalFloors,1,100))throw new Error('攻略關卡編號／層數不符');
      const key=g.dungeon+':'+g.subDungeon;if(seen.has(key))throw new Error('攻略關卡重複');seen.add(key);
      const checkedAt=text(g.checkedAt,10);if(!/^\d{4}-\d{2}-\d{2}$/.test(checkedAt))throw new Error('攻略日期需用 YYYY-MM-DD');
      const sources=list(g.sources??[],8).map(s=>{const u=new URL(text(s,2048));if(u.protocol!=='https:'||u.username||u.password)throw new Error('攻略來源只接受 HTTPS 網址');return u.href;});
      const identityChecks=list(g.identityChecks??[],100).map(c=>{
        if(!c||!number(c.floor,1,g.totalFloors)||!number(c.catalog,1))throw new Error('攻略敵人配對條件不符');return {floor:c.floor,catalog:c.catalog};
      });
      const teamNotes=list(g.teamNotes??[],32).map(n=>{
        const cards=list(n?.cards,6);if(!cards.length||cards.some(id=>!number(id,1,65435)))throw new Error('攻略隊員條件不符');
        return {cards:[...new Set(cards)],text:text(n.text,2000)};
      });
      const floorSeen=new Set(),floors=list(g.floors,100).map(f=>{
        if(!f||!number(f.number,1,g.totalFloors)||floorSeen.has(f.number))throw new Error('攻略樓層重複或超出範圍');floorSeen.add(f.number);
        return {number:f.number,text:text(f.text,4000)};
      });
      let damageReference=null;
      if(g.damageReference!==undefined&&g.damageReference!==null){
        if(!number(g.damageReference.rawDamage))throw new Error('攻略參考傷害超出範圍');
        damageReference={label:text(g.damageReference.label,200),rawDamage:g.damageReference.rawDamage,captured:g.damageReference.captured===true};
      }
      return {dungeon:g.dungeon,subDungeon:g.subDungeon,totalFloors:g.totalFloors,title:text(g.title,200),checkedAt,sources,identityChecks,
        notes:list(g.notes??[],32).map(s=>text(s,2000)),teamNotes,floors,damageReference};
    });
    const result={schema:1,kind:'PADCheaper-dungeon-guides',guides:normalized};
    if(new TextEncoder().encode(JSON.stringify(result)).length>1000000)throw new Error('攻略庫超過 1 MB');return result;
  }
  function parse(input){
    if(typeof input==='string'){if(input.length>16000000)throw new Error('關卡 JSON 過大');input=JSON.parse(input.replace(/^\uFEFF/,''));}
    if(!input||input.schema!==1||input.kind!=='PADCheaper-dungeon-report'||!REPORT_TARGETS.has(input.target))throw new Error('需要相容版本的 PADCheaper 全層關卡匯出');
    if(!number(input.dungeon,1)||!number(input.floor,1)||!number(input.totalFloors,1,100)||!Array.isArray(input.floorDefinitions)||input.floorDefinitions.length>100||
      !Array.isArray(input.observations)||input.observations.length>512)throw new Error('關卡資料結構或數量不符');
    const floors=[],seen=new Set();
    for(const page of input.floorDefinitions){
      if(!page||!number(page.wave,0,input.totalFloors-1)||seen.has(page.wave))throw new Error('樓層資料不完整或重複');
      seen.add(page.wave);const definitions=new Map();
      if(page.rawCaptureComplete!==true&&page.catalogs===undefined&&page.definitions===undefined){
        floors.push({number:page.wave+1,definitions,enemies:[],complete:false});continue;
      }
      if(!Array.isArray(page.catalogs)||page.catalogs.length>8||!Array.isArray(page.definitions)||page.definitions.length>512)
        throw new Error('樓層資料數量不符');
      for(const source of page.definitions){
        if(!source||!number(source.actionID,1,131071)||definitions.has(source.actionID))throw new Error('行動編號不符或重複');
        if(source.known!==true){definitions.set(source.actionID,{id:source.actionID,known:false});continue;}
        const bytes=raw(source.rawHex,72),id=bytes.getInt32(0,true),op=bytes.getUint16(4,true),p=Array.from({length:8},(_,i)=>bytes.getInt32(16+i*4,true)),attached=bytes.getInt32(68,true);
        if(id!==source.actionID||source.opcode!==op||JSON.stringify(p)!==JSON.stringify(source.parameters)||attached!==source.attachedAttackPercentRaw)
          throw new Error('行動欄位與原始定義不一致');
        definitions.set(id,{id,op,p,attached,known:source.known===true});
      }
      const slots=new Set(),enemies=page.catalogs.map(c=>{
        if(!c||!number(c.slot,1,8)||slots.has(c.slot)||!number(c.catalogID)||!number(c.level,1,65535))
          throw new Error('敵人資料格式不符');
        slots.add(c.slot);
        if(c.definitionKnown===false)return {slot:c.slot,id:c.catalogID,level:c.level,attribute:null,catalogAttack:null,rows:[],complete:false};
        if(!Array.isArray(c.rows)||c.rows.length>64)throw new Error('敵人行動表數量不符');
        const bytes=raw(c.catalogPrefixRawHex,220),rowBytes=c.rowsRawHex===undefined?null:raw(c.rowsRawHex,512);
        if(bytes.getInt32(0,true)!==c.catalogID)throw new Error('敵人編號與原始資料不一致');
        const rows=c.rows.map(r=>{
          if(!r||!number(r.row,1,64)||!number(r.actionID,1,131071)||!Array.isArray(r.operandsRaw)||r.operandsRaw.length!==4||!r.operandsRaw.every(x=>number(x,0,255)))
            throw new Error('敵人行動表格式不符');
          if(rowBytes&&(rowBytes.getInt32((r.row-1)*8,true)!==r.actionID||r.operandsRaw.some((v,i)=>rowBytes.getUint8((r.row-1)*8+4+i)!==v)))
            throw new Error('行動表欄位與原始資料不一致');
          return {row:r.row,id:r.actionID,a:r.operandsRaw[0],b:r.operandsRaw[1]};
        });
        if(new Set(rows.map(r=>r.row)).size!==rows.length)throw new Error('敵人行動表列重複');
        rows.sort((a,b)=>a.row-b.row);
        const minimum=Number(bytes.getBigUint64(128,true)),maximum=Number(bytes.getBigUint64(136,true)),power=bytes.getFloat32(144,true),maximumLevel=bytes.getUint16(176,true);
        let catalogAttack=null;
        if(number(minimum)&&number(maximum)&&maximum>=minimum&&Number.isFinite(power)&&power>0&&power<=20&&maximumLevel>1){
          const calculated=c.level<=1?minimum:Math.floor(minimum+(maximum-minimum)*Math.pow((c.level-1)/(maximumLevel-1),power)+.5);
          if(number(calculated))catalogAttack=calculated;
        }
        return {slot:c.slot,id:c.catalogID,level:c.level,attribute:bytes.getInt8(12),catalogAttack,rows,complete:true};
      });
      floors.push({number:page.wave+1,definitions,enemies,complete:page.rawCaptureComplete===true});
    }
    floors.sort((a,b)=>a.number-b.number);
    const observations=[];
    for(const observation of input.observations){
      if(!observation||!number(observation.wave,0,input.totalFloors-1)||!Array.isArray(observation.enemies)||observation.enemies.length>8)continue;
      if(input.spawnTableSHA256&&observation.tableSHA256!==input.spawnTableSHA256)continue;
      for(const enemy of observation.enemies){
        if(enemy?.identityKnown===true&&enemy.memoryKnown===true&&enemy.stateKnown!==false&&number(enemy.baseAttack)&&number(enemy.slot,1,8))
          observations.push({floor:observation.wave+1,slot:enemy.slot,id:enemy.catalogID,level:enemy.level,attack:enemy.baseAttack});
      }
    }
    const ratios=[];
    for(const f of floors)for(const e of f.enemies){
      const observed=observations.filter(o=>o.floor===f.number&&o.slot===e.slot&&o.id===e.id&&o.level===e.level);
      e.observedAttack=observed.length&&observed.every(o=>o.attack===observed[0].attack)?observed[0].attack:null;
      if(e.catalogAttack>0&&e.observedAttack!==null)ratios.push(e.observedAttack/e.catalogAttack);
    }
    const inferredScale=ratios.length&&ratios.every(n=>Math.abs(n-ratios[0])<1e-8)?ratios[0]:null;
    return {schema:1,target:input.target,dungeon:input.dungeon,subDungeon:input.floor,totalFloors:input.totalFloors,floors,inferredScale,
      observationCount:input.observations.length,complete:input.allDefinitionsCaptured===true&&floors.length===input.totalFloors&&floors.every(f=>f.complete&&f.enemies.every(e=>e.complete))};
  }
  function describe(definitions,id){
    const events=[],unknown=[],root=definitions.get(id);
    function leaf(a,child=false){
      if(!a?.known){unknown.push(a?.id??id);events.push({kind:'unknown'});return;}
      const {op,p}=a;
      if(op===82){if(!child)events.push({kind:'attack',percent:100,id:a.id});return;}
      if(!supported.has(op)){
        // Expose a recorded damage component without treating an unreviewed
        // leaf (which may replace it) as a complete event sequence.
        if(a.attached>0)events.push({kind:'attack',percent:a.attached,id:a.id,partial:true});
        if([48,85,127].includes(op)&&p[0]>0)events.push({kind:'attack',percent:p[0],id:a.id,partial:true});
        unknown.push(a.id);events.push({kind:'unknown',id:a.id,op});return;
      }
      if(a.attached>0)events.push({kind:'attack',percent:a.attached,id:a.id,attached:true});
      if(a.attached<0){unknown.push(a.id);return;}
      if(op===15){
        if(!number(p[0],0,32767)||!number(p[1],p[0],32767)||!number(p[2])){unknown.push(a.id);return;}
        for(let i=0;i<Math.min(p[1],15);i++)events.push({kind:'attack',percent:p[2],id:a.id,optional:i>=Math.min(p[0],15)});
      }else if(op===47){if(number(p[1]))events.push({kind:'attack',percent:p[1],id:a.id});else unknown.push(a.id);}
      else if(op===50){if(number(p[0]))events.push({kind:'gravity',percent:p[0],id:a.id});else unknown.push(a.id);}
      else if([17,18,19].includes(op)){
        const turns=op===18?p[0]:p[1],percent=op===18?p[1]:p[2];
        if(number(turns,0,32767)&&number(percent))events.push({kind:'enrage',turns,percent,id:a.id});else unknown.push(a.id);
      }else {
        const valid=[88,141].includes(op)?number(p[0],0,511):op===107?number(p[0],0,511)&&number(p[1],-MAX,65535):
          number(p[0],0,32767)&&number(p[1],p[0],32767);
        if(valid)events.push({kind:'status',op,turns:p[0],maximum:p[1],id:a.id,label:effectNames[op]});else unknown.push(a.id);
      }
    }
    if(root?.known&&root.op===83){
      let terminated=false;
      for(const child of root.p){if(child<=0){terminated=true;break;}leaf(definitions.get(child),true);}
      if(!terminated)unknown.push(id);
    }else leaf(root);
    return {id,op:root?.op??null,events,unknown:[...new Set(unknown)],complete:unknown.length===0};
  }
  function reducedDamage(attack,percent,multiplier,reductions){
    let n=f32(f32(attack)*f32(f32(percent/100)*f32(multiplier)));
    n=Math.trunc(f32(n+.5));
    if(!number(n))return null;
    for(const reduction of reductions){
      if(!Number.isFinite(reduction)||reduction<0||reduction>1)return null;
      n=Math.floor(n*f32(reduction)+.5);
    }
    return number(n)?n:null;
  }
  function damage(sequence,attack,hp,reductions,enrage=1){
    if(!number(attack)||!number(hp,1)||!Number.isFinite(enrage)||enrage<0||
      !Array.isArray(reductions)||reductions.some(r=>!Number.isFinite(r)||r<0||r>1))return null;
    let total=0,currentHP=hp,multiplier=enrage,incomplete=!sequence.complete,hasDamage=false,statusBeforeHit=false;
    const hits=[];
    for(const event of sequence.events){
      if(event.kind==='enrage'){multiplier=event.turns>0?event.percent/100:1;continue;}
      if(event.kind==='unknown'){statusBeforeHit=true;continue;}
      if(event.kind==='status'){
        if([88,141].includes(event.op)&&event.turns>0)statusBeforeHit=true;
        continue;
      }
      if(statusBeforeHit)incomplete=true;
      let amount;
      if(event.kind==='gravity'){
        const raw=event.percent===100?currentHP:Math.trunc(f32(f32(f32(currentHP)*f32(event.percent))/100));
        amount=number(raw)?raw:null;
        for(const reduction of reductions)if(amount!==null)amount=Math.floor(amount*f32(reduction)+.5);
      }else amount=reducedDamage(attack,event.percent,multiplier,reductions);
      if(amount===null||!number(amount))return null;
      hasDamage=true;total+=amount;currentHP=Math.max(0,currentHP-amount);hits.push(amount);
    }
    return {damage:total,hits,remainingHP:currentHP,fullHPFits:hasDamage&&total<hp,hasDamage,complete:!incomplete,
      // Rounding of a team's assembled native leader layers is not exported;
      // comparisons use an explicit offline chain, never a certified bound.
      estimated:true};
  }
  function evaluate(dungeon,model,team,context={},options={}){
    const scale=options.attackScale??dungeon.inferredScale;
    if(scale!==null&&(!Number.isFinite(scale)||scale<=0||scale>100000))throw new Error('敵人攻擊倍率超出範圍');
    const baseline=model.evaluate(team,{...context,activeSkills:[],activeShield:0,hpMultiplier:1,rcvMultiplier:1});
    const available=baseline.skillOptions.filter(s=>s.selectable&&s.kind==='body');
    const shield=available.filter(s=>s.effects.some(e=>e.kind==='shield'&&e.attrs===31&&e.delay===0)).sort((a,b)=>Math.max(...b.effects.map(e=>e.kind==='shield'?e.turns:0))-Math.max(...a.effects.map(e=>e.kind==='shield'?e.turns:0)))[0];
    const hpSkills=available.filter(s=>s.effects.some(e=>e.kind==='hp'&&e.value>1&&e.delay===0));
    const ordinary=hpSkills.filter(s=>s.effects.some(e=>e.kind==='hp'&&e.value<3)).sort((a,b)=>a.cooldown-b.cooldown)[0];
    const rescue=hpSkills.filter(s=>s.effects.some(e=>e.kind==='hp'&&e.value>=3)).sort((a,b)=>Math.max(...b.effects.map(e=>e.kind==='hp'?e.value:0))-Math.max(...a.effects.map(e=>e.kind==='hp'?e.value:0)))[0];
    const scenarios=[{id:'base',label:'隊長盾',skills:[]},
      ...(shield?[{id:'shield',label:'隊長＋#'+(shield.slot+1)+' 技能盾',skills:[shield]}]:[]),
      ...(ordinary?[{id:'cycle',label:'#'+(ordinary.slot+1)+' HP 倍率'+(shield?'＋技能盾':''),skills:[shield,ordinary].filter(Boolean)},
        {id:'cycle-no-shield',label:'日常 HP 倍率・技能盾到期',skills:[ordinary]}]:[]),
      ...(rescue?[{id:'rescue',label:'#'+(rescue.slot+1)+' HP 倍率'+(shield?'＋技能盾':''),skills:[shield,rescue].filter(Boolean)},
        {id:'rescue-no-shield',label:'#'+(rescue.slot+1)+' HP 倍率・技能盾到期',skills:[rescue]}]:[])];
    const byFloor=[];
    for(const floor of dungeon.floors){
      const values=scenarios.map(s=>{
        const board=s.skills.flatMap(x=>x.effects).find(e=>e.kind==='board'&&e.delay===0),boardContext=board?{width:Number(board.value[0]),height:Number(board.value[2])}:{};
        const calculated=model.evaluate(team,{...context,...boardContext,floor:floor.number,activeShield:0,hpMultiplier:1,rcvMultiplier:1,
          activeSkills:[...new Map(s.skills.map(x=>[x.key,{key:x.key,elapsed:0}])).values()]});
        return {id:s.id,label:s.label,calculated};
      });
      const enemies=floor.enemies.map(enemy=>{
        const projectedAttack=enemy.catalogAttack===null||scale===null?null:Math.floor(enemy.catalogAttack*f32(scale)+.5);
        const attack=enemy.observedAttack??projectedAttack;
        const sequences=new Map(enemy.rows.map(r=>[r.id,describe(floor.definitions,r.id)]));
        const enrages=[1,...new Set([...sequences.values()].flatMap(s=>s.events.filter(e=>e.kind==='enrage'&&e.percent>100).map(e=>e.percent/100)))].sort((a,b)=>a-b);
        const actions=enemy.rows.filter(r=>!controls.has(floor.definitions.get(r.id)?.op)&&floor.definitions.get(r.id)?.op!==49).map(row=>{
          const sequence=sequences.get(row.id);
          const variants=enrages.map(enrage=>{
            const comparisons=values.map(s=>{
              const t=s.calculated;
              // Attribute changes are not forecast. Use the weakest of the
              // team's five attribute defenses for each hypothetical attack.
              const a=t.known?t.shield.total.indexOf(Math.min(...t.shield.total)):0;
              const reductions=t.known?[1-t.shield.activeByAttribute[a],1-t.shield.leader[a],1-t.shield.heartL,1-t.shield.resist[a]]:[];
              const full=t.known?damage(sequence,attack,t.totals.hp,reductions,enrage):null;
              const after=t.known&&t.recovery.afterHP>0?damage(sequence,attack,t.recovery.afterHP,reductions,enrage):null;
              if(t.shield.hpConditional){if(full)full.complete=false;if(after)after.complete=false;}
              const required=full?.hasDamage&&!sequence.events.some(e=>e.kind==='gravity')?full.damage+1:null;
              return {scenario:s.id,hp:t.totals.hp,afterPlayerEstimate:t.recovery.afterHP,requiredHP:required,defenseAttribute:a,
                remainingAfterEstimate:after?.remainingHP??null,estimatedAfterPlayerFits:after?.hasDamage?after.fullHPFits:null,...full};
            });
            const get=id=>comparisons.find(s=>s.scenario===id),cycle=get('cycle')||get('shield')||get('base'),rescueValue=get('rescue'),noShield=get('cycle-no-shield'),rescueNoShield=get('rescue-no-shield');
            const damageWatch=!!(cycle?.hasDamage&&rescueValue?.hasDamage&&!cycle.fullHPFits&&rescueValue.fullHPFits);
            return {enrage,comparisons,componentNeedsRescue:damageWatch,
              needsRescue:!!(cycle?.complete&&rescueValue?.complete&&cycle.hasDamage&&!cycle.fullHPFits&&rescueValue.fullHPFits),
              needsRescueWithoutShield:!!(noShield?.complete&&rescueNoShield?.complete&&noShield.hasDamage&&!noShield.fullHPFits&&rescueNoShield.fullHPFits),
              stillExceedsRescue:!!(rescueValue?.complete&&rescueValue.hasDamage&&!rescueValue.fullHPFits)};
          });
          const definition=floor.definitions.get(row.id);
          const targets=definition?.op===46?definition.p.slice(0,5).filter(a=>number(a,0,4)):[];
          const unavailableAtCatalogAttribute=definition?.op===46&&targets.length>0&&targets.every(a=>a===enemy.attribute);
          return {row:row.row,id:row.id,opcode:sequence.op,sequence,variants,branchKnown:false,
            availabilityNote:unavailableAtCatalogAttribute?'此變色招在敵人仍為圖鑑屬性時不符合使用條件；需先發生其他屬性變更。':''};
        });
        return {slot:enemy.slot,id:enemy.id,level:enemy.level,attribute:enemy.attribute,attack,
          attackSource:enemy.observedAttack!==null?'observed':attack!==null?'projected-from-dungeon-scale':'unknown',actions};
      });
      // One action per surviving enemy. This is a conditional envelope, not
      // a prediction that their countdowns and branches coincide.
      const aggregate=enemies.length>1?values.map(s=>{
        const selected=enemies.map(e=>{
          const choices=e.actions.filter(a=>!a.sequence.events.some(ev=>ev.kind==='gravity')).map(a=>({id:a.id,
            comparison:a.variants[0].comparisons.find(v=>v.scenario===s.id)})).filter(x=>x.comparison?.hasDamage);
          return choices.sort((a,b)=>b.comparison.damage-a.comparison.damage)[0];
        });
        const known=selected.every(Boolean),sum=known?selected.reduce((n,x)=>n+x.comparison.damage,0):null;
        return {scenario:s.id,damage:sum,hp:s.calculated.totals.hp,afterPlayerEstimate:s.calculated.recovery.afterHP,
          requiredHP:sum===null?null:sum+1,fullHPFits:sum===null||s.calculated.totals.hp===null?null:sum<s.calculated.totals.hp,
          selectedActions:selected.map(x=>x?.id??null),componentsComplete:known&&selected.every(x=>x.comparison.complete),
          simultaneousActionKnown:false,estimated:true};
      }):[];
      byFloor.push({number:floor.number,complete:floor.complete,aggregate,scenarios:values.map(({id,label,calculated:c})=>({id,label,hp:c.totals.hp,
        rcv:c.totals.rcv,shield:c.shield.total,startingHP:c.recovery.startingHP,heartHeal:c.recovery.heart,afterPlayerEstimate:c.recovery.afterHP})),enemies});
    }
    const guide=parseGuides(options.guides).guides.find(g=>g.dungeon===dungeon.dungeon&&g.subDungeon===dungeon.subDungeon&&g.totalFloors===dungeon.totalFloors&&
      g.identityChecks.every(check=>dungeon.floors.find(f=>f.number===check.floor)?.enemies.some(e=>e.id===check.catalog)));
    return {schema:1,target:dungeon.target,dungeon:dungeon.dungeon,subDungeon:dungeon.subDungeon,totalFloors:dungeon.totalFloors,attackScale:scale,
      guide:guide?{...guide,matchedTeamNotes:guide.teamNotes.filter(n=>n.cards.every(id=>baseline.members.some(m=>m.id===id))).map(n=>n.text)}:null,
      scaleSource:options.attackScale!==undefined?'explicit':'inferred-from-observation',floors:byFloor,completeCapture:dungeon.complete,
      skills:[shield,ordinary,rescue].filter(Boolean).map(s=>({key:s.key,slot:s.slot+1,name:s.name,cooldown:s.cooldown,effects:s.effects})),
      limitations:['後續樓層 ATK 依已觀察倍率推算，未逐層讀回。','敵人行動表不代表下一招；先制、根性後分支與擊殺結果未預測。',
        '連擊逐下取整，採五屬性中最低減傷；使用離線盾鏈，原生合併條件盾的取整邊界仍待校對。','預設未封鎖、覺醒／武裝有效；狀態變更後的承傷不判定完整。',
        '技能 CD、延遲與跨層持續時間需依實戰確認。1 回合 HP 技能不保證跨層先制仍有效。','心珠回復是估值；技能 HP 倍率不會補滿增加的血量。'],
      enemyForecastKnown:false,survivalAuthorized:false};
  }
  const api={parse,parseGuides,describe,damage,reducedDamage,evaluate};
  if(typeof module==='object'&&module.exports)module.exports=api;else root.PHTeamDungeon=api;
})(typeof globalThis==='object'?globalThis:this);
