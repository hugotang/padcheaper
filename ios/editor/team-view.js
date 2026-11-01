/* global PHTeamModel, PH_CATALOG, PH_TEAM_CATALOG */
// Read-only view of a normalized formation. Simulation choices are transient
// and never enter native presets, persisted libraries, or battle mutations.
(function(root) {
  'use strict';
  const $=id=>document.getElementById(id);
  const element=(tag,className,value)=>{const e=document.createElement(tag);e.className=className;if(value!==undefined)e.textContent=value;return e;};
  const number=n=>n===null||n===undefined?'未完整':n.toLocaleString('en-US');
  const percent=n=>n===null?'未完整':(n*100).toFixed(3).replace(/\.?0+$/,'')+'%';
  function icon(id,latent=false,inactive=false) {
    const e=element('span',(latent?'latent-icon':'awakening-icon')+(inactive?' inactive':''));
    e.style.setProperty('--icon-id',id);e.setAttribute('aria-hidden','true');return e;
  }
  function create(formation,getTeam,portrait) {
    const model=PHTeamModel.createModel(PH_CATALOG,PH_TEAM_CATALOG,formation);
    let phase='final',lastKey='',lastResult=null,finalForms={},formationKey='',custom=[],infoTimer;
    const inputs={'calc-hp-percent':'hpPercent','calc-shield-buff':'activeShield','calc-hp-buff':'hpMultiplier',
      'calc-rcv-buff':'rcvMultiplier','calc-floor':'floor','calc-parts':'parts','calc-skill-uses':'skillUses'};
    function iconButton(id,latent=false,info='calc-icon-info',suffix='',inactive=false) {
      const label=(latent?formation.latentName(id):formation.awakeningName(id))+suffix;
      const b=element('button','icon-button');b.type='button';b.title=label;b.setAttribute('aria-label',label);
      b.append(icon(id,latent,inactive));b.onclick=()=>{$(info).hidden=false;$(info).textContent=label;
        if(info==='calc-icon-info'){clearTimeout(infoTimer);infoTimer=setTimeout(()=>{$(info).hidden=true;},6500);}};return b;
    }
    function settings() {
      const c={phase,finalForms,benefit:$('calc-benefit').value};
      for(const [id,key] of Object.entries(inputs)) {
        if($(id).value.trim()==='')throw new Error('請填寫完整試算條件');c[key]=Number($(id).value);
      }
      [c.width,c.height]=$('calc-board').value.split('x').map(Number);
      const g=(attr,n,shape='match')=>({attr,n,shape});
      c.matches={none:[], 'light-heart':[g(3,3),g(5,3)],'light-l-heart4':[g(3,5,'l'),g(5,4,'four')],
        'light-t-heart4':[g(3,5,'t'),g(5,4,'four')],'light-heart-l':[g(3,3),g(5,5,'l')],custom}[$('calc-preset').value];
      return c;
    }
    function renderGroups() {
      $('calc-groups').replaceChildren();
      custom.forEach((g,index)=>{
        const row=element('div','calc-group'),attr=element('select',''),shape=element('select',''),remove=element('button','','移除');
        attr.setAttribute('aria-label','第 '+(index+1)+' 組珠色');shape.setAttribute('aria-label','第 '+(index+1)+' 組珠型');
        PHTeamModel.attrNames.forEach((n,i)=>attr.add(new Option(n,String(i))));attr.value=String(g.attr);
        const [width,height]=$('calc-board').value.split('x').map(Number);
        const choices=[['match:3','三連'],['four:4','四連'],['l:5','L 五顆'],['t:5','T 五顆'],['cross:5','十字五顆'],
          ['square:9','方塊九顆'],['row:'+width,'整橫列'],['column:'+height,'整直列'],...Array.from({length:38},(_,i)=>['match:'+(i+5),(i+5)+' 連珠'])];
        choices.forEach(([value,label])=>shape.add(new Option(label,value)));shape.value=g.shape+':'+g.n;
        attr.onchange=()=>{g.attr=Number(attr.value);render();};shape.onchange=()=>{const [s,n]=shape.value.split(':');g.shape=s;g.n=Number(n);render();};
        remove.onclick=()=>{custom.splice(index,1);renderGroups();render();};row.append(attr,shape,remove);$('calc-groups').append(row);
      });
      $('calc-add-group').disabled=custom.length>=14;
    }
    function clearResult(error) {
      for(const id of ['calc-hp','calc-rcv','calc-shield','calc-heal'])$(id).textContent='—';
      for(const id of ['calc-after','calc-resists','calc-protection','calc-rules','calc-warning','calc-quick','calc-comparison','calc-forms','calc-members','calc-awakenings'])$(id).replaceChildren();
      $('calc-icon-info').hidden=true;
      $('calc-error').hidden=false;$('calc-error').textContent=error;lastResult=null;
    }
    function renderMembers(result) {
      $('calc-members').replaceChildren();
      result.members.forEach(m=>{
        const col=element('div','calc-member');
        col.append(element('div','member-title',m.slot===0?'隊長':m.slot===5?'助人':'位置 '+(m.slot+1)));
        if(m.known===false) {
          col.append(element('div','member-pending','待選型態'));$('calc-members').append(col);return;
        }
        if(m.id) {
          col.append(portrait(m.id),element('div','member-number','#'+m.id));
          col.title=m.name;
          col.append(element('div','member-stat','HP\n'+number(result.known?m.hp:null)),element('div','member-stat','回復\n'+number(result.known?m.rcv:null)));
          for(const [label,ids,latent] of [['本體',m.bodyAwakenings,false],['超／同步',m.extraAwakenings,false],['武裝',m.assistAwakenings,false],['潛覺',m.latents,true]]) {
            col.append(element('span','source-label',label));const list=element('div','awakening-list compact');
            for(const id of ids)list.append(iconButton(id,latent,'calc-icon-info',' · 位置 '+(m.slot+1)+' '+label+
              (!latent&&id===30?' · 單人不生效':''),!latent&&id===30));
            if(!ids.length)list.append(element('span','quiet','—'));col.append(list);
          }
        } else col.append(element('div','member-pending','未設定'));
        $('calc-members').append(col);
      });
    }
    function render() {
      let team,c;
      try {
        team=getTeam();const key=JSON.stringify(team.slots.map(s=>s.body.id));
        if(key!==formationKey){formationKey=key;finalForms={};}
        c=settings();const request=JSON.stringify([team,c]);if(request===lastKey)return;lastKey=request;
        const result=model.evaluate(team,c);lastResult=result;$('calc-error').hidden=true;
        const initial=model.evaluate(team,{...c,phase:'current',matches:[]}),final=model.evaluate(team,{...c,phase:'final',matches:[]});
        $('calc-comparison').textContent='HP 比較：編隊型態 '+number(initial.totals.hp)+' → 最終型態 '+number(final.totals.hp);
        $('calc-forms').replaceChildren();
        for(const m of result.members)if(m.form.reason) {
          const label=element('label','','位置 '+(m.slot+1)+' · '+m.form.reason),select=element('select','');
          select.add(new Option('請指定變身型態',''));for(const id of m.form.choices)select.add(new Option('#'+id+' '+model.cards.get(id).name,String(id)));
          select.value=finalForms[m.slot]===undefined?'':String(finalForms[m.slot]);
          select.onchange=()=>{if(select.value)finalForms[m.slot]=Number(select.value);else delete finalForms[m.slot];render();};label.append(select);$('calc-forms').append(label);
        }
        for(const mode of ['current','final']){$('calc-'+mode).classList.toggle('active',phase===mode);$('calc-'+mode).setAttribute('aria-pressed',String(phase===mode));}
        $('calc-hp').textContent=number(result.totals.hp);$('calc-rcv').textContent=number(result.totals.rcv);
        $('calc-shield').textContent=result.shield.total?percent(Math.min(...result.shield.total)):'未完整';
        $('calc-heal').textContent=number(result.recovery.heart);
        $('calc-after').textContent='消珠 '+c.matches.length+' 組＋隊長加算 '+result.bonusCombos+' = '+result.combos+' Combo · 回復後 HP 估值 '+number(result.recovery.afterHP);
        $('calc-resists').replaceChildren();if(result.shield.total)for(let a=0;a<5;a++)$('calc-resists').append(element('span','',PHTeamModel.attrNames[a]+' '+percent(result.shield.total[a])));
        const filled=team.slots.filter(s=>s.body.id).length;
        $('calc-quick').textContent=filled+' / 6 人 · '+(result.loadoutKnown?'此型態 SB '+result.skillBoost:'覺醒資料未完整')+
          ' · HP 覺醒 '+result.teamHP+'（+'+(result.teamHP*5)+'%）· 回復覺醒 '+result.teamRCV+'（+'+(result.teamRCV*20)+'%）';
        $('calc-warning').textContent=[...result.issues,...result.notes,
          ...(filled<6?['僅計算已設定位置；套用時空格會保留遊戲原隊員，此處未計入。']:[]),
          ...result.members.flatMap(m=>m.issues.map(s=>'位置 '+(m.slot+1)+'：'+s))].join(' ');
        $('calc-protection').replaceChildren();
        for(const [key,id,label] of [['dark',11,'暗闇'],['jammer',12,'邪魔'],['poison',13,'毒'],['skillSeal',28,'封技'],['cloud',54,'雲'],['tape',55,'封條']]) {
          const item=element('div','protection-item');item.append(icon(id),element('span','',label),
            element('strong','',result.loadoutKnown?percent(result.protection[key]):'未完整'));$('calc-protection').append(item);
        }
        $('calc-rules').replaceChildren();for(const r of result.rules) {
          const li=element('li','',(r.active?'✓ ':'○ ')+(r.slot===0?'隊長：':'助人：')+r.label);li.dataset.active=String(Boolean(r.active));$('calc-rules').append(li);
        }
        if(!result.rules.length)$('calc-rules').append(element('li','quiet','此隊長組合沒有已解析的條件盾／回復規則。'));
        $('calc-awakenings').replaceChildren();$('calc-awakening-title').textContent=result.loadoutKnown?'全隊裝備覺醒合計':'已確認位置的覺醒 · 資料未完整';
        for(const [key,n] of Object.entries(result.awakenings).sort((a,b)=>Number(a[0])-Number(b[0]))) {
          const id=Number(key),sources=result.members.filter(m=>m.known!==false&&m.awakenings.includes(id)).map(m=>m.slot+1).join('、');
          const b=iconButton(id,false,'calc-icon-info',' ×'+n+' · 位置 '+sources+(id===30?' · 單人不生效':''),id===30);
          b.dataset.awakening=key;b.append(element('span','',String(n)));$('calc-awakenings').append(b);
        }
        if(!Object.keys(result.awakenings).length)$('calc-awakenings').append(element('span','quiet',filled?'選好型態後顯示覺醒。':'先選擇隊員。'));
        $('calc-icon-info').hidden=true;renderMembers(result);
      } catch(e) {lastKey='';clearResult(e.message);}
    }
    for(const mode of ['current','final'])$('calc-'+mode).onclick=()=>{phase=mode;render();};
    for(const id of [...Object.keys(inputs),'calc-benefit','calc-skill-uses'])$(id).onchange=render;
    $('calc-board').onchange=()=>{renderGroups();render();};
    $('calc-preset').onchange=()=>{$('calc-custom').hidden=$('calc-preset').value!=='custom';renderGroups();render();};
    $('calc-add-group').onclick=()=>{if(custom.length<14)custom.push({attr:3,n:3,shape:'match'});renderGroups();render();};
    $('calc-clear-groups').onclick=()=>{custom=[];renderGroups();render();};
    function renderSelection(value) {
      const card=formation.cards.get(value.id);$('selected-awakenings').replaceChildren();$('selected-icon-info').hidden=true;
      if(card)card.awakenings.forEach((a,i)=>$('selected-awakenings').append(iconButton(a,false,'selected-icon-info',
        ' · 第 '+(i+1)+' 個'+(value.awakenings>=0&&i>=value.awakenings?' · 未開啟':''),value.awakenings>=0&&i>=value.awakenings)));
    }
    return {render,renderSelection,icon,iconButton,getResult:()=>lastResult};
  }
  root.PHTeamView={create,icon};
})(globalThis);
