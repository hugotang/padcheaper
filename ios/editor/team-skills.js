// Independently decode selected active-skill facts for offline scenarios.
// Descriptions are display data. No skill is cast or assumed to be ready.
(function(root) {
  'use strict';
  function createModel(facts) {
    const skills=new Map((facts.activeSkills||[]).map(r=>[r[0],{id:r[0],type:r[1],p:r[2],maxLevel:r[3],cooldown:r[4],name:r[5],description:r[6]}]));
    function describe(card,loadout,slot,kind,transformed=false) {
      const rootSkill=skills.get(card?.active||0);if(!rootSkill)return [];
      const evolution=[232,233].includes(rootSkill.type),stageIDs=evolution?rootSkill.p:[rootSkill.id];
      return stageIDs.map((id,stage)=>{
        const s=skills.get(id),effects=[],notes=[],issues=[];
        function visit(nodeID,path=[],delay=0) {
          if(path.length>=16||path.includes(nodeID)||!skills.has(nodeID)){issues.push('技能鏈未完整');return;}
          const node=skills.get(nodeID),p=node.p;
          const children=ids=>ids.forEach(child=>visit(child,[...path,nodeID],delay));
          const effect=(kind,turns,value,attrs=31)=>effects.push({kind,turns,value,attrs,delay,skill:nodeID});
          switch(node.type) {
            case 116:children(p);break;
            case 248:p.slice(1).forEach(child=>visit(child,[...path,nodeID],delay+p[0]));break;
            case 3:effect('shield',p[0],p[1]);break;
            case 21:effect('shield',p[0],p[2],2**p[1]);break;
            case 237:effect('hp',p[0],p[1]/100);notes.push('提高 HP 上限不會補滿增加的血量');break;
            case 50:if(p[1]===5)effect('rcv',p[0],p[2]/100);break;
            case 244:
              if(![1,2,3].includes(p[1]))issues.push('盤面技能尺寸未支援');
              else effect('board',p[0],p[1]===1?'7x6':p[1]===2?'5x4':'6x5');break;
            case 118:issues.push('隨機技能效果不自動納入');break;
            case 232:case 233:issues.push('巢狀進化技能尚未支援');break;
            case 156:if(p[4]===3)issues.push('依覺醒數變動的技能盾尚未支援');else if(p[4]===1)notes.push('瞬間回復另計');break;
            case 228:case 231:if(p[node.type===228?4:7])issues.push('依隊伍數量變動的回復倍率尚未支援');break;
            case 202:case 236:issues.push('變身請用型態選擇；此施放不自動模擬');break;
            case 7:case 8:case 117:case 145:
              if(node.type!==117||p.slice(1,4).some(Boolean))notes.push('瞬間回復另計');break;
            case 179:notes.push('回合結束自動回復另計');break;
            case 84:case 85:case 86:case 87:case 195:notes.push('技能自減 HP 未計入起始 HP');break;
            case 225:case 234:case 255:case 268:case 275:case 279:notes.push('使用限制需另行滿足');break;
          }
        }
        visit(id);
        const configured=transformed?1:loadout.skill||Math.max(1,rootSkill.maxLevel);
        const level=Math.max(1,Math.min(configured,Math.max(1,rootSkill.maxLevel)));
        const cooldown=stage===0?Math.max(0,rootSkill.cooldown-level+1):Math.max(0,s?.cooldown||0);
        const minimumCooldown=stage===0?Math.max(0,rootSkill.cooldown-Math.max(1,rootSkill.maxLevel)+1):cooldown;
        for(const e of effects)if(!Number.isInteger(e.turns)||e.turns<1||e.turns>9999||!Number.isInteger(e.delay)||e.delay<0||e.delay>9999||
          e.kind==='shield'&&(!Number.isFinite(e.value)||e.value<0||e.value>100||!Number.isInteger(e.attrs)||e.attrs<1||e.attrs>31)||
          ['hp','rcv'].includes(e.kind)&&(!Number.isFinite(e.value)||e.value<=0||e.value>100))issues.push('技能數值未支援');
        return {key:slot+':'+kind+':'+stage,slot,kind,stage,stages:stageIDs.length,cycles:rootSkill.type===233,
          cardId:card.id,cardName:card.name,id,name:s?.name||rootSkill.name||'技能 #'+id,
          description:s?.description||rootSkill.description||'',level,cooldown,minimumCooldown,
          cooldownNote:kind==='assist'?'武裝 CD；實際需另加本體 CD':'',effects,notes:[...new Set(notes)],issues:[...new Set(issues)],
          selectable:effects.length>0&&!issues.length};
      });
    }
    function project(options,selections=[]) {
      if(!Array.isArray(selections)||selections.length>12)throw new Error('最多選十二個技能情境');
      const seen=new Set(),effects=[],selected=[];
      for(const choice of selections) {
        if(!choice||typeof choice.key!=='string'||seen.has(choice.key))throw new Error('技能情境重複或格式不符');
        seen.add(choice.key);
        const option=options.find(s=>s.key===choice.key);
        if(!option?.selectable)throw new Error('所選技能無法納入目前型態的試算');
        const elapsed=choice.elapsed??0;
        if(!Number.isInteger(elapsed)||elapsed<0||elapsed>9999)throw new Error('技能經過回合超出範圍');
        const source=option.slot+':'+option.kind;
        if(selected.some(s=>s.source===source))throw new Error('同一技能請只選一個進化階段');
        const resolved=option.effects.map(e=>({...e,active:false,overwritten:false,
          remaining:Math.max(0,e.turns-Math.max(0,elapsed-e.delay)),startsIn:Math.max(0,e.delay-elapsed)}));
        selected.push({key:choice.key,source,elapsed,effects:resolved});
      }
      // Resolve each effect category by activation time. A later HP skill
      // replaces HP only; it does not cancel the earlier skill's RCV/board.
      // Expiry does not resurrect a buff that was already overwritten.
      const latest=new Map();
      for(const selection of selected)for(const e of selection.effects) {
        const start=e.delay-selection.elapsed;
        if(start<=0&&(!latest.has(e.kind)||start>=latest.get(e.kind).start))latest.set(e.kind,{e,start,key:selection.key});
      }
      for(const selection of selected)for(const e of selection.effects) {
        e.overwritten=e.startsIn===0&&latest.get(e.kind)?.e!==e;
        e.active=e.startsIn===0&&!e.overwritten&&e.remaining>0;
        if(e.active)effects.push({...e,key:selection.key});
      }
      return {effects,selected};
    }
    return Object.freeze({describe,project});
  }
  const api={createModel};if(typeof module==='object'&&module.exports)module.exports=api;else root.PHTeamSkills=api;
})(typeof globalThis==='object'?globalThis:this);
