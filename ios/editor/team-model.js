// Independent single-player, offline calculation. Public IDs are encyclopedia
// numbers. This module never reads the game or grants permission to auto-play.
(function (root) {
  'use strict';
  const f32 = Math.fround;
  const round = n => n < 0 ? -Math.floor(-n + .5) : Math.floor(n + .5);
  const count = (values, id) => values.filter(v => v === id).length;
  const bits = mask => Array.from({length:31}, (_,i)=>i).filter(i=>mask & 2**i);
  const pow32 = (n, power) => f32(Math.pow(f32(n), power));
  const mult = percent => percent ? f32(percent / 100) : 1;
  const own = (o,k) => Object.prototype.hasOwnProperty.call(o,k);
  const attrNames = ['火','水','木','光','闇','心'];
  const awTriggers = [27,48,60,78,126,22,23,24,25,26,79,80,81,0,0,73,74,75,76,77,20,82,133,134,135];
  const equivalent = {52:[10,2],53:[19,2],56:[21,2],68:[11,5],69:[12,5],70:[13,5],96:[27,2],97:[51,2],98:[9,2],
    99:[14,2],100:[15,2],101:[16,2],102:[17,2],103:[18,2],104:[29,2],107:[43,2],108:[60,2],109:[48,2],
    110:[78,2],111:[61,2],112:[79,2],113:[80,2],114:[81,2],115:[20,2],116:[22,3],117:[23,3],118:[24,3],
    119:[25,3],120:[26,3],121:[73,2],122:[74,2],123:[75,2],124:[76,2],125:[77,2]};
  function effectiveCount(values,id) {return values.reduce((n,a)=>n+(a===id?1:equivalent[a]?.[0]===id?equivalent[a][1]:0),0);}
  function requireNumber(value,min,max,label,integer=false) {
    if (!Number.isFinite(value) || value<min || value>max || integer&&!Number.isInteger(value)) throw new Error(label+'超出範圍');
    return value;
  }
  function context(value={}) {
    if(value.mode!==undefined && value.mode!=='solo')throw new Error('隊伍試算僅限單人');
    const c={phase:'final',finalForms:{},width:6,height:5,floor:1,parts:0,benefit:'none',hpPercent:100,
      skillUses:0,activeShield:0,hpMultiplier:1,rcvMultiplier:1,matches:[],...value};
    if (!['current','final'].includes(c.phase) || !['none','sun','moon','both'].includes(c.benefit)) throw new Error('未知試算情境');
    if (![[6,5],[7,6],[5,4]].some(([w,h])=>w===c.width&&h===c.height)) throw new Error('盤面尺寸未支援');
    for(const [key,lo,hi,integer] of [['floor',1,100,true],['parts',0,20,true],['hpPercent',0,100,false],
      ['skillUses',0,30,true],['activeShield',0,100,false],['hpMultiplier',.01,100,false],['rcvMultiplier',0,100,false]])
      requireNumber(c[key],lo,hi,key,integer);
    if (!c.finalForms || typeof c.finalForms!=='object' || Array.isArray(c.finalForms)) throw new Error('型態選擇格式錯誤');
    if (!Array.isArray(c.matches) || c.matches.length>14) throw new Error('最多十四組消珠');
    c.matches=c.matches.map(g=>{
      const attr=requireNumber(g.attr,0,5,'珠色',true),n=requireNumber(g.n,3,42,'連珠數',true),shape=g.shape||'match';
      const size={l:5,t:5,cross:5,square:9,four:4,row:c.width,column:c.height};
      if (shape!=='match' && (!own(size,shape)||size[shape]!==n)) throw new Error('珠型與顆數不符');
      return {attr,n,shape,enhanced:requireNumber(g.enhanced??0,0,n,'強化珠數',true)};
    });
    if(c.matches.reduce((n,g)=>n+g.n,0)>c.width*c.height) throw new Error('消珠數超過盤面；本試算不計天降');
    return c;
  }
  function baseStat(card,level,index) {
    const [min,max,scale]=card.curves[index],ratio=card.baseMax<=1?1:Math.min(1,(level-1)/(card.baseMax-1));
    const base=round(min+(max-min)*Math.pow(ratio,scale));
    if(level<=99 || !card.limit) return base;
    const rate=level<=110?f32(f32(f32((level-99)/11)*card.limit)/100):
      f32(f32(card.limit/100)+f32(f32(Math.min(1,(level-110)/10))*f32(index===0?.1:.05)));
    return base+round(f32(f32(base)*rate));
  }
  function createModel(editor,facts,formation) {
    if(facts.schema!==1 || facts.region!=='ja' || facts.numbering!=='encyclopedia') throw new Error('隊伍資料格式不符');
    for(const k of ['revision','card_sha256','skill_sha256']) if(editor[k]!==facts[k]) throw new Error('隊伍與圖鑑資料版本不符');
    const cards=new Map(facts.cards.map(r=>{
      const e=formation.cards.get(r[0]);
      if(!e) throw new Error('缺少圖鑑資料 #'+r[0]);
      return [r[0],{...e,types:r[1],baseMax:r[2],limit:r[3],curves:r.slice(4,7),leader:r[7],to:r[8],rarity:r[9],collab:r[10]}];
    }));
    const skills=new Map(facts.skills.map(r=>[r[0],{id:r[0],type:r[1],p:r[2]}]));
    const resolvedSkills=new Map();
    function leaves(id,ancestors=[]) {
      if(!id)return [];
      if(ancestors.includes(id)||ancestors.length>=12||!skills.has(id))return [{id,type:-1,p:[]}];
      if(resolvedSkills.has(id))return resolvedSkills.get(id);
      const s=skills.get(id),result=s.type===138?s.p.flatMap(i=>leaves(i,[...ancestors,id])):[s];
      resolvedSkills.set(id,result);return result;
    }
    function forms(id) {
      if(!id)return {id:0,path:[],choices:[],reason:''};
      const seen=new Set(),pending=[id];
      while(pending.length && seen.size<128) {
        const next=pending.shift();if(seen.has(next))continue;seen.add(next);
        pending.push(...(cards.get(next)?.to||[]));
      }
      const choices=[...seen].filter(x=>cards.has(x));
      let current=id,path=[],reason='';
      while(current && path.length<32) {
        if(path.includes(current)){reason='循環變身，請指定型態';break;}
        path.push(current);
        const card=cards.get(current);
        if(!card){reason='變身資料缺失';break;}
        if(!card.to.length)return {id:current,path,choices,reason:''};
        if(card.to.length!==1){reason='隨機變身，請指定型態';break;}
        current=card.to[0];
      }
      return {id:null,path,choices,reason:reason||'變身鏈過長'};
    }
    function member(slot,index,c) {
      const input=slot.body,original=cards.get(input.id);
      if(!input.id)return {slot:index,sourceId:0,id:0,name:'未設定',attrs:[],types:[],stats:[0,0,0],
        bodyAwakenings:[],assistAwakenings:[],extraAwakenings:[],awakenings:[],latents:[],issues:[],form:{id:0,choices:[],path:[]}};
      if(!original)throw new Error('缺少數值資料 #'+input.id);
      const form=forms(input.id),chosen=c.finalForms[index];
      if(chosen!==undefined && (!Number.isInteger(chosen)||!form.choices.includes(chosen)))throw new Error('指定型態不屬於此角色');
      const id=c.phase==='current'?input.id:chosen??form.id,card=cards.get(id)||original;
      const transformed=id!==null && id!==input.id,issues=[];
      if(id===null)issues.push(form.reason);
      const level=Math.min(input.level||original.maxLevel,card.maxLevel);
      const bodyAwakenings=card.awakenings.slice(0,transformed?card.awakenings.length:input.awakenings<0?card.awakenings.length:input.awakenings);
      const selectedSuper=input.superAwakening<0?(original.supers[0]||original.sync||0):input.superAwakening;
      const superActive=selectedSuper>0 && (card.sync===selectedSuper || card.supers.includes(selectedSuper)&&level>=100&&input.plus.every(p=>p>=99));
      if(selectedSuper && !superActive)issues.push('此型態不啟用原本的超／同步覺醒');
      const extraAwakenings=superActive?[selectedSuper]:[],assist=cards.get(slot.assist.id);
      const assistUnlocked=assist?assist.awakenings.slice(0,slot.assist.awakenings<0?assist.awakenings.length:slot.assist.awakenings):[];
      const assistAwakenings=assistUnlocked.includes(49)?assistUnlocked:[];
      const awakenings=[...bodyAwakenings,...extraAwakenings,...assistAwakenings];
      const attrs=card.attrs.slice(),types=card.types.slice();
      // Weapon type/subattribute awakenings affect leader eligibility, but the
      // same-colour assist stat awakening compares the natural types instead.
      const addTypes=[4,5,7,8,1,6,2,3];
      for(const a of awakenings) {
        if(a>=83&&a<=90)types.push(addTypes[a-83]);
        if(a>=91&&a<=95)attrs[1]=a-91;
      }
      const stats=[0,1,2].map(i=>{
        const base=baseStat(card,level,i),voice=round(f32(f32(base*count(awakenings,63))*f32(.1)));
        const latentRates=[{1:.015,12:.03,28:.045,43:.1},{2:.01,12:.02,29:.03,44:.08},{3:.1,12:.2,30:.3,45:.35}][i];
        const latent=round(f32(base*input.latents.reduce((n,l)=>f32(n+f32(latentRates[l]||0)),0)));
        const flat=count(awakenings,i+1)*[3000,1000,500][i]-count(awakenings,65+i)*[2500,1000,2000][i];
        const same=assist&&(card.attrs[0]===assist.attrs[0]||card.attrs[0]===6||assist.attrs[0]===6);
        const inherit=same?round(f32(f32([10,5,15][i]*(baseStat(assist,slot.assist.level||assist.maxLevel,i)+slot.assist.plus[i]*[10,5,3][i]))/100)):0;
        let value=base+voice+latent+flat+input.plus[i]*[10,5,3][i]+inherit;
        if(i<2)value=Math.max(1,value);
        let factor=1;
        for(const [a,m] of [[127,1.5],[142,1.8],[145,1.5],[146,1.5],[147,1.5],[130,c.floor>=10?2:c.floor>=5?1.5:1],
          [143,i===1&&c.floor>=3?30*Math.min(5,Math.floor(c.floor/3)):1],[131,pow32(1.2,c.parts)],[132,1.25],
          [138,assist&&card.attrs[0]===assist.attrs[0]&&card.types.some(t=>assist.types.includes(t))?3:1],[139,assist?1:3]])
          factor*=pow32(m,count(awakenings,a));
        value+=round((factor-1)*value);
        let dungeonFactor=1;
        if(['sun','both'].includes(c.benefit))dungeonFactor=f32(dungeonFactor*pow32(i===1?5:2,count(awakenings,128)));
        if(['moon','both'].includes(c.benefit))dungeonFactor=f32(dungeonFactor*pow32(i===1?5:2,count(awakenings,129)));
        value=round(value*dungeonFactor);
        return i<2?Math.max(1,value):value;
      });
      return {slot:index,sourceId:input.id,id:id??input.id,name:card.name,level,attrs,types:[...new Set(types)],
        stats,bodyAwakenings,assistAwakenings,extraAwakenings,awakenings,latents:input.latents.slice(),
        form:{...form,chosen:id},transformed,issues,known:id!==null,card};
    }
    function evaluate(team,value={}) {
      team=formation.normalize(team);const c=context(value),members=team.slots.map((s,i)=>member(s,i,c));
      const issues=[],notes=[],rules=[];
      const issue=s=>{if(!issues.includes(s))issues.push(s);};
      for(const m of members) if(m.known===false)issue('位置 '+(m.slot+1)+'：'+m.form.reason);
      const allAwakenings=members.filter(m=>m.known!==false).flatMap(m=>m.awakenings);
      const memberMatches=(m,attrs=0,types=0)=>!attrs&&!types||m.attrs.some(a=>attrs&2**a)||m.types.some(t=>types&2**t);
      const groups=(mask=63)=>c.matches.filter(g=>mask&2**g.attr);
      const attackMask=members.reduce((n,m)=>m.attrs.reduce((n,a)=>a>=0&&a<5?n|2**a:n,n),32);
      const colors=mask=>new Set(groups(mask&attackMask).map(g=>g.attr)).size;
      const shape=(mask,s)=>groups(mask).some(g=>g.shape===s);
      const groupMatches=masks=>{
        // At most five requirements; one group may satisfy only one entry.
        function visit(i,used) {
          if(i===masks.length)return 0;
          let best=visit(i+1,used);
          c.matches.forEach((g,j)=>{if(!(used&2**j)&&(masks[i]&2**g.attr))best=Math.max(best,1+visit(i+1,used|2**j));});
          return best;
        }
        return visit(0,0);
      };
      function awakeningActive(a) {
        const simple={27:'four',48:'square',60:'l',78:'cross',126:'t'};
        if(simple[a])return members.some(m=>effectiveCount(m.awakenings,a)>0&&c.matches.some(g=>g.attr<5&&m.attrs.includes(g.attr)&&
          (a===27?g.n===4:g.shape===simple[a])));
        if(a>=22&&a<=26)return effectiveCount(allAwakenings,a)>0&&shape(2**(a-22),'row');
        if(a>=149&&a<=153)return members.some(m=>m.awakenings.includes(a)&&m.attrs.includes(a-149)&&shape(2**(a-149),'column'));
        const attackingOwner=predicate=>members.some(m=>effectiveCount(m.awakenings,a)>0&&c.matches.some(g=>
          g.attr<5&&m.attrs.includes(g.attr)&&predicate(g)));
        if(a>=79&&a<=81)return colors(31)>=a-76&&attackingOwner(()=>true);
        if(a>=73&&a<=77)return effectiveCount(allAwakenings,a)>0&&groups(attackMask&2**(a-73)).length>=2;
        if(a===20)return effectiveCount(allAwakenings,20)>0&&shape(32,'row');
        if(a===82)return attackingOwner(g=>g.n>=12);
        if(a>=133&&a<=135)return colors([3,6,5][a-133])===2&&attackingOwner(()=>true);
        return null;
      }
      const sourceSkills=[0,5].flatMap(slot=>members[slot].known===false?[]:leaves(members[slot].card?.leader||0).map(s=>({...s,slot})));
      // Stats granted by a leader must precede per-member stat calculation;
      // retain an explicit unknown rather than silently counting no awakenings.
      for(const s of sourceSkills) if(s.type===213)issue('隊長授予覺醒尚未支援（技能 '+s.id+'）');
      let bonusCombos=0,minMatch=3;
      function triggerList(flag,mapping) {
        if(!flag || flag>=2**mapping.length)return null;
        const states=bits(flag).map(b=>awakeningActive(mapping[b]));
        return states.includes(null)?null:states.every(Boolean);
      }
      // Only known leader-added combos enter recovery/combo conditions. No
      // awakening combo-orb, badge, skill, cascade or random skyfall credit.
      for(const s of sourceSkills) {
        const p=[...s.p,...Array(16).fill(0)],t=s.type;let active=false,bonus=0;
        if(t===158)minMatch=Math.max(minMatch,p[0]);
        if(t===192||t===219){active=groups(p[0]).some(g=>g.n>=p[1]);bonus=p[t===192?3:2];}
        if(t===194){active=colors(p[0])>=p[1];bonus=p[3];}
        if(t===206){active=groupMatches(p.slice(0,5).filter(Boolean))>=p[5];bonus=p[6];}
        if(t===209){active=shape(32,'cross');bonus=p[0];}
        if(t===220){active=shape(p[0],'l');bonus=p[1];}
        if(t===271||t===285){active=triggerList(p[0],t===271?awTriggers:[149,150,151,152,153]);bonus=p[3];}
        if(active)bonusCombos+=bonus;
      }
      if(c.matches.some(g=>g.n<minMatch))issue('此隊長要求至少 '+minMatch+' 顆才可消除');
      const combo=c.matches.length?c.matches.length+bonusCombos:0;
      const hpFactors=Array(6).fill(1),rcvFactors=Array(6).fill(1),leaderRemain=[1,1,1,1,1];
      let hpConditional=false;
      function stat(hp,rcv,attrs=0,types=0,active=true) {
        if(!active)return;
        members.forEach((m,i)=>{if(memberMatches(m,attrs,types)){hpFactors[i]*=mult(hp);rcvFactors[i]*=mult(rcv);}});
      }
      function shield(percent,mask=31,active=true) {
        if(!active || !percent)return;
        for(let a=0;a<5;a++)if(mask&2**a)leaderRemain[a]*=(100-percent)/100;
      }
      function rule(s,label,active) {rules.push({slot:s.slot,skill:s.id,label,active});}
      const atHP=(threshold,high)=>high?c.hpPercent>=threshold:c.hpPercent<=threshold;
      const rcvFlag=(p,i,j,m)=>p[i]===2||p[j]===2?p[m]:0;
      for(const s of sourceSkills) {
        const p=[...s.p,...Array(16).fill(0)],t=s.type;let active=true,n=0;
        switch(t) {
          case 16:shield(p[0]);rule(s,'常駐減傷 '+p[0]+'%',true);break;
          case 17:shield(p[1],2**p[0]);rule(s,attrNames[p[0]]+'屬性減傷 '+p[1]+'%',true);break;
          case 36:shield(p[2],2**p[0]|2**p[1]);rule(s,'指定屬性減傷 '+p[2]+'%',true);break;
          case 23:case 62:stat(p[1],0,0,2**p[0]);break;
          case 24:case 64:stat(0,p[1],0,2**p[0]);break;
          case 63:case 65:stat(p[1],p[1],0,2**p[0]);break;
          case 29:case 67:stat(p[1],p[1],2**p[0]);break;
          case 45:case 48:stat(p[1],0,2**p[0]);break;
          case 28:case 49:stat(0,p[1],2**p[0]);break;
          case 30:case 77:stat(p[2],0,0,2**p[0]|2**p[1]);break;
          case 79:stat(0,p[2],0,2**p[0]|2**p[1]);break;
          case 46:case 111:stat(p[2],0,2**p[0]|2**p[1]);break;
          case 114:stat(p[2],p[2],2**p[0]|2**p[1]);break;
          case 73:stat(p[2],0,2**p[0],2**p[1]);break;
          case 75:stat(0,p[2],2**p[0],2**p[1]);break;
          case 76:stat(p[2],p[2],2**p[0],2**p[1]);break;
          case 105:stat(0,p[0]);break;
          case 106:case 107:case 108:stat(p[0],0);break;
          case 121:case 129:case 163:case 177:case 186:
            stat(p[2],p[4],p[0],p[1]);if(t===129||t===163)shield(p[6],p[5]);break;
          case 178:case 185:stat(p[3],p[5],p[1],p[2]);if(t===178)shield(p[7],p[6]);break;
          case 158:stat(p[4],p[5],p[1],p[2]);break;
          case 136:case 137:
            for(const start of [0,4])stat(p[start+1],p[start+3],t===136?p[start]:0,t===137?p[start]:0);break;
          case 38:case 43:
            active=t===38&&p[0]===100?c.hpPercent===100:atHP(p[0],t===43);
            if(p[1]!==100){active=false;notes.push('機率盾不計入保證減傷（技能 '+s.id+'）');}
            shield(p[2],31,active);hpConditional=true;rule(s,'HP '+(t===43?'至少 ':'至多 ')+p[0]+'%：減傷 '+p[2]+'%',active);break;
          case 39:case 44:
            active=atHP(p[0],t===44);stat(0,rcvFlag(p,1,2,3),0,0,active);rule(s,'HP 條件回復倍率',active);break;
          case 94:case 95:case 96:case 97:
            active=atHP(p[0],t>=96);stat(0,rcvFlag(p,2,3,4),t===94||t===96?2**p[1]:0,t===95||t===97?2**p[1]:0,active);rule(s,'HP 條件回復倍率',active);break;
          case 122:case 123:case 130:case 131:
            active=atHP(p[0],t===123||t===131);stat(0,p[4],p[1],p[2],active);
            if(t>=130){shield(p[6],p[5],active);hpConditional=true;}rule(s,'HP 條件回復／減傷',active);break;
          case 183:
            active=p[2]>0&&c.hpPercent>=p[2];shield(p[4],31,active);stat(0,p[7],p[0],p[1],c.hpPercent<=(p[5]||p[2]));hpConditional=true;rule(s,'HP 條件減傷 '+p[4]+'%',active);break;
          case 100:active=c.skillUses>0;stat(0,rcvFlag(p,0,1,2),0,0,active);rule(s,'本回合使用技能',active);break;
          case 133:case 270:
            active=c.skillUses>=(t===270?p[0]:1);stat(0,p[t===270?2:3],t===270?31:p[0],t===270?0:p[1],active);rule(s,'本回合使用技能',active);break;
          case 103:case 104:
            active=combo>=p[0];stat(0,t===103?rcvFlag(p,1,2,3):rcvFlag(p,2,3,4),t===104?p[1]:0,0,active);rule(s,p[0]+' Combo 回復倍率',active);break;
          case 149:active=groups(32).some(g=>g.n===4);stat(0,p[0],0,0,active);rule(s,'心四連回復 ×'+p[0]/100,active);break;
          case 151:active=shape(32,'cross');stat(0,p[1],0,0,active);shield(p[2],31,active);rule(s,'心十字回復／減傷',active);break;
          case 164:n=groupMatches(p.slice(0,4).filter(Boolean));active=n>=p[4];stat(0,(p[6]||100)+Math.max(0,n-p[4])*p[7],0,0,active);rule(s,'指定珠組回復倍率',active);break;
          case 165:n=colors(p[0]);active=n>=p[1];stat(0,(p[3]||100)+Math.min(Math.max(0,n-p[1]),p[6])*p[5],0,0,active);rule(s,attrNames.filter((_,i)=>p[0]&2**i).join('＋')+' 同時消除：回復倍率',active);break;
          case 166:n=combo;active=n>=p[0];stat(0,(p[2]||100)+Math.max(0,Math.min(n,p[5]||p[0])-p[0])*p[4],0,0,active);rule(s,p[0]+' Combo 回復倍率',active);break;
          case 167:n=Math.max(0,...groups(p[0]).map(g=>g.n));active=n>=p[1];stat(0,(p[3]||100)+Math.max(0,Math.min(n,p[6]||p[1])-p[1])*p[5],0,0,active);rule(s,'連珠回復倍率',active);break;
          case 169:active=combo>=p[0];shield(p[2],31,active);rule(s,p[0]+' Combo：減傷 '+p[2]+'%',active);break;
          case 170:active=colors(p[0])>=p[1];shield(p[3],31,active);rule(s,p[1]+' 色同時攻擊：減傷 '+p[3]+'%',active);break;
          case 171:active=groupMatches(p.slice(0,4).filter(Boolean))>=p[4];shield(p[6],31,active);rule(s,'指定 '+p[4]+' 組珠：減傷 '+p[6]+'%',active);break;
          case 182:active=groups(p[0]).some(g=>g.n>=p[1]);shield(p[3],31,active);rule(s,attrNames.filter((_,i)=>p[0]&2**i).join('／')+' '+p[1]+' 連珠：減傷 '+p[3]+'%',active);break;
          case 193:active=shape(p[0],'l');stat(0,p[2],0,0,active);shield(p[3],31,active);rule(s,'L 消回復／減傷',active);break;
          case 271:case 285:
            active=triggerList(p[0],t===271?awTriggers:[149,150,151,152,153]);
            if(active===null)issue('未支援的覺醒發動條件（技能 '+s.id+'）');
            stat(0,p[5],0,0,active);shield(p[2],31,active);rule(s,'發動 '+bits(p[0]).map(b=>formation.awakeningName((t===271?awTriggers:[149,150,151,152,153])[b])).join('＋')+'：回復／減傷',active);break;
          case 155:break; // Multiplayer-only multiplier is inactive in solo.
          case 11:case 12:case 13:case 14:case 15:case 22:case 26:case 31:case 33:case 40:case 41:
          case 53:case 54:case 61:case 66:case 69:case 98:case 101:case 119:case 124:case 139:case 148:case 150:
          case 157:case 159:case 162:case 192:case 194:case 197:case 199:case 200:case 201:case 206:case 209:
          case 213:case 219:case 220:case 221:case 223:case 254:case 264:case 265:break;
          default:issue('尚未支援的隊長條件（類型 '+t+'，技能 '+s.id+'）');
        }
      }
      const counts={};allAwakenings.forEach(a=>counts[a]=(counts[a]||0)+1);
      const teamHP=count(allAwakenings,46),teamRCV=count(allAwakenings,47);
      const hpAwake=f32(1+f32(f32(teamHP)*f32(.05))),rcvAwake=f32(1+f32(f32(teamRCV)*f32(.2)));
      const perMember=members.map((m,i)=>({...m,hp:round(m.stats[0]*hpFactors[i]*hpAwake),rcv:round(m.stats[2]*rcvFactors[i]*rcvAwake),hpFactor:hpFactors[i],rcvFactor:rcvFactors[i]}));
      const hp=Math.min(0x7fffffff,round(perMember.reduce((n,m)=>n+m.hp,0)*c.hpMultiplier));
      const rcv=round(perMember.reduce((n,m)=>n+m.rcv,0)*c.rcvMultiplier);
      const heartL=shape(32,'l')?Math.min(1,count(allAwakenings,59)*.05):0;
      const resist=Array.from({length:5},(_,a)=>Math.min(1,count(allAwakenings,4+a)*.07+
        members.reduce((n,m)=>n+count(m.latents,6+a)*.01+count(m.latents,32+a)*.025,0)));
      const remaining=leaderRemain.map((n,a)=>n*(1-resist[a])*(1-heartL)*(1-c.activeShield/100));
      const known=!issues.length;
      const loadoutKnown=members.every(m=>m.known!==false)&&!sourceSkills.some(s=>s.type===213);
      const protection={dark:Math.min(1,effectiveCount(allAwakenings,11)*.2),jammer:Math.min(1,effectiveCount(allAwakenings,12)*.2),
        poison:Math.min(1,effectiveCount(allAwakenings,13)*.2),skillSeal:Math.min(1,(count(allAwakenings,28)+count(allAwakenings,146)*2)*.2),
        cloud:Number(allAwakenings.includes(54)),tape:Number(allAwakenings.includes(55))};
      const recoveryGroups=groups(32).map(g=>{
        const factor=(1+.25*(g.n-3))*(1+.06*g.enhanced)*(g.enhanced?1+.07*effectiveCount(allAwakenings,29):1);
        // Preserve fractional per-member recovery until the group/combo total;
        // the game applies heart-four power to its owner, never the whole team.
        return perMember.reduce((n,m)=>n+m.stats[2]*rcvFactors[m.slot]*rcvAwake*factor*
          (g.n===4?pow32(1.5,effectiveCount(m.awakenings,29)):1),0);
      });
      const heal=Math.max(0,Math.floor(recoveryGroups.reduce((a,b)=>a+b,0)*(combo?1+.25*(combo-1):0)*c.rcvMultiplier));
      const startingHP=Math.floor(hp*c.hpPercent/100),afterHP=Math.min(hp,startingHP+heal);
      if(hpConditional)notes.push('HP 條件按輸入百分比評估；連擊途中跨門檻須另算，不表示整段承傷均有盾。');
      if(sourceSkills.some(s=>[13,14,198].includes(s.type)))notes.push('根性、自動回復及回血門檻效果未計入心珠回復估值。');
      const skillBoost=effectiveCount(allAwakenings,21)+count(allAwakenings,145)*2-count(allAwakenings,105)+
        members.reduce((n,m)=>n+(m.awakenings.includes(59)?count(m.latents,47)*3:0),0);
      return {schema:1,mode:'solo-offline',context:c,known,loadoutKnown,issues,notes:[...new Set(notes)],members:perMember,skillBoost,protection,
        totals:{hp:known?hp:null,rcv:known?rcv:null},awakenings:counts,teamHP,teamRCV,bonusCombos,combos:combo,
        rules,shield:{leader:known?leaderRemain.map(n=>1-n):null,resist,heartL,active:c.activeShield/100,
          total:known?remaining.map(n=>1-n):null},recovery:{estimated:true,heart:known?heal:null,afterHP:known?afterHP:null},
        provenance:{revision:facts.revision,numbering:'encyclopedia'},survivalAuthorized:false};
    }
    return Object.freeze({evaluate,forms,cards,effectiveCount});
  }
  const api=Object.freeze({createModel,context,round,effectiveCount,attrNames});
  if(typeof module==='object'&&module.exports)module.exports=api;else root.PHTeamModel=api;
})(typeof globalThis==='object'?globalThis:this);
