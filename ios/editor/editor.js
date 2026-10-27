/* global PHFormation, PH_CATALOG */
(() => {
  'use strict';
  const model = PHFormation.createModel(PH_CATALOG), $ = id => document.getElementById(id);
  const names = {0:'不啟用',56:'技能加速＋',70:'毒耐性＋',111:'超連擊強化＋',131:'浮游',142:'全能力強化＋',153:'闇直列消除攻擊'};
  const latentNames = {1:'HP 強化',2:'攻擊強化',3:'回復強化',4:'操作時間延長',5:'自動回復',6:'火傷害減輕',7:'水傷害減輕',8:'木傷害減輕',9:'光傷害減輕',10:'闇傷害減輕',11:'技能延遲耐性',13:'屬性吸收貫通',14:'傷害無效貫通',15:'毒掉落耐性',37:'邪魔掉落耐性',38:'隊長交換耐性',43:'傷害上限解放',47:'技能加速＋＋'};
  const awakeningName = id => names[id] || `覺醒 #${id}`;
  const latentName = id => latentNames[id] || `潛覺 #${id}`;
  let team = model.empty(), selected = {slot:0,kind:'body'}, battle = {}, toastTimer;
  const bridge = (action,value) => window.webkit?.messageHandlers?.formation?.postMessage({action,value});
  function toast(message) { $('toast').textContent=message; $('toast').hidden=false; clearTimeout(toastTimer); toastTimer=setTimeout(()=>$('toast').hidden=true,4500); }
  function guarded(fn) { try { $('error').textContent=''; fn(); } catch (e) { $('error').textContent=e.message; } }
  const member = () => team.slots[selected.slot][selected.kind];
  function portrait(id) {
    const p=document.createElement('div');p.className='portrait'+(!id?' empty':'');
    if(id) {
      const page=Math.ceil(id/100),index=(id-1)%100;
      p.style.backgroundImage=`url(https://mapaler.github.io/PADDashFormation/images/cards_ja/CARDS_${String(page).padStart(3,'0')}.PNG)`;
      p.style.backgroundPosition=`${index%10*100/9}% ${Math.floor(index/10)*100/9}%`;
    }
    return p;
  }
  function text(className,value){const e=document.createElement('div');e.className=className;e.textContent=value;return e;}
  function renderGrid() {
    ['body','assist'].forEach(kind=>{
      const grid=$(kind+'-grid');grid.replaceChildren();
      team.slots.forEach((slot,index)=>{
        const v=slot[kind],c=model.cards.get(v.id),button=document.createElement('button');
        button.className='card '+kind+(selected.slot===index&&selected.kind===kind?' active':'');
        button.dataset.slot=index;button.dataset.kind=kind;
        button.setAttribute('aria-label',`位置 ${index+1} ${kind==='body'?'本體':'武裝'} ${c ? '#'+c.id+' '+c.name:'未設定'}`);
        button.append(portrait(v.id),text('number',v.id?'#'+v.id:'未設定'),text('name',c?.name || (kind==='body'?'選擇本體':'選擇武裝')),
          text('stats',c?`Lv.${v.level||c.maxLevel}\n+${v.plus.reduce((a,b)=>a+b,0)}`:'點擊選擇'));
        if (kind==='body' && battle.members?.[index]?.leased) button.append(text('leased','已套用'));
        button.onclick=()=>{selected={slot:index,kind};$('search').value='';$('results').hidden=true;render();};
        grid.append(button);
      });
    });
  }
  function options(element,values,selectedValue){element.replaceChildren();for(const [value,label] of values){const o=new Option(label,String(value));element.add(o);}element.value=String(selectedValue);}
  function renderFields() {
    const v=member(),c=model.cards.get(v.id),assist=selected.kind==='assist';
    $('editor-title').textContent=`位置 ${selected.slot+1} · ${assist?'武裝':'本體'}`;
    $('slots-label').textContent=`${selected.slot+1} / 6`;
    $('selected-name').textContent=c?`#${c.id} ${c.name}`:'尚未選擇角色';
    $('selected-hint').textContent=c?`最高 Lv.${c.maxLevel} · 技能最高 ${c.maxSkill} 級 · ${c.awakenings.length} 個普通覺醒`:'搜尋圖鑑編號或名稱來選擇角色。';
    $('level').value=v.level;$('level').max=c?.maxLevel||120;$('skill').value=v.skill;$('skill').max=c?.maxSkill||999;
    options($('awakenings'),[[-1,'全部開啟'],...Array.from({length:(c?.awakenings.length||0)+1},(_,n)=>[n,`${n} 個`])],v.awakenings);
    const common=v.plus.every(p=>p===v.plus[0])?v.plus[0]:-2;
    options($('plus-preset'),assist?[[99,'+297'],[0,'+0']]:[[297,'+891'],[99,'+297'],[0,'+0'],[-2,'自訂三項']],common);
    $('plus-detail').hidden=assist;
    ['hp-plus','atk-plus','rcv-plus'].forEach((id,i)=>$(id).value=v.plus[i]);
    $('super-section').hidden=assist;$('latent-section').hidden=assist;
    $('super-options').replaceChildren();
    if (!assist) {
      const available=c?.supers.length?c.supers:c?.sync?[c.sync]:[];
      const choices=[[-1,available.length?`自動 · ${awakeningName(available[0])}`:'自動 · 無超覺醒'],[0,'不啟用'],...available.map(id=>[id,awakeningName(id)])];
      for(const [value,label] of choices){const b=document.createElement('button');b.textContent=label;b.className=value===v.superAwakening?'active':'';b.dataset.super=value;b.onclick=()=>update({superAwakening:value});$('super-options').append(b);}
      const used=v.latents.reduce((n,id)=>n+model.latentCost(id),0);
      $('latent-title').textContent=`潛在覺醒 · ${used} / ${c?.latentSlots||6} 格（點擊移除）`;
      $('latent-list').replaceChildren();v.latents.forEach((id,index)=>{const b=document.createElement('button');b.textContent=`${latentName(id)} ×`;b.onclick=()=>update({latents:v.latents.filter((_,i)=>i!==index)});$('latent-list').append(b);});
    }
  }
  function render(){renderGrid();renderFields();renderState();}
  function update(changes){guarded(()=>{const next=JSON.parse(JSON.stringify(team));Object.assign(next.slots[selected.slot][selected.kind],changes);team=model.normalize(next);render();});}
  function readFields(){
    const v=member(),next=JSON.parse(JSON.stringify(team)),assist=selected.kind==='assist';
    Object.assign(next.slots[selected.slot][selected.kind],{level:Number($('level').value),skill:Number($('skill').value),
      awakenings:Number($('awakenings').value),plus:assist?v.plus:['hp-plus','atk-plus','rcv-plus'].map(id=>Number($(id).value))});
    next.title=$('team-title').value;team=model.normalize(next);return team;
  }
  function showResults(){
    const results=$('results');results.hidden=false;results.replaceChildren();
    const found=model.search($('search').value,selected.kind==='assist');
    if(!found.length){results.append(text('quiet','沒有符合的角色，請確認圖鑑編號或名稱。'));return;}
    for(const card of found){const b=document.createElement('button');b.className='result';b.append(portrait(card.id));const name=document.createElement('span');name.textContent=`#${card.id} ${card.name}`;b.append(name);b.onclick=()=>guarded(()=>{team=model.selectCard(team,selected.slot,selected.kind,card.id);results.hidden=true;$('search').value='';$('search').blur();render();});results.append(b);}
  }
  function renderState(){
    $('connection').textContent=[battle.message,battle.gate].filter(Boolean).join(' · ')||'等待戰鬥檢查；現在可以編輯隊伍。';
    $('apply-all').disabled=!battle.canApply||!team.slots.some(s=>s.body.id)||Boolean(battle.activeCount);
    $('apply-one').disabled=!battle.canApply||!team.slots[selected.slot].body.id||Boolean(battle.members?.[selected.slot]?.leased);
    $('restore-one').disabled=!battle.canRestore||!battle.members?.[selected.slot]?.leased;
    $('restore-all').disabled=!battle.canRestore;
  }
  window.PHFormationUI={
    initialized:false,
    receive(value){if(value.team){guarded(()=>{team=model.normalize(value.team);$('team-title').value=team.title;});}battle=value.battle||battle;render();this.initialized=true;},
    setState(value){battle=value||{};renderGrid();renderState();},
    importText(value){guarded(()=>{const imported=model.parseImport(value);team=imported.team;$('team-title').value=team.title;render();toast(imported.notice||'已匯入隊伍');});},
    getTeam(){return JSON.parse(JSON.stringify(team));},
    notify:toast
  };
  for(let i=0;i<6;i++)$('positions').append(text('position',i===0?'1 隊長':i===5?'6 助人':String(i+1)));
  options($('latent-choice'),Array.from({length:52},(_,i)=>[i+1,`${latentName(i+1)} · ${model.latentCost(i+1)} 格`]),11);
  $('search').oninput=showResults;$('search-toggle').onclick=()=>{$('search').focus();showResults();};
  $('clear-card').onclick=()=>guarded(()=>{team=model.selectCard(team,selected.slot,selected.kind,0);render();});
  $('max-level').onclick=()=>update({level:0});$('max-skill').onclick=()=>update({skill:0});
  $('level').onchange=()=>update({level:Number($('level').value)});$('skill').onchange=()=>update({skill:Number($('skill').value)});
  $('awakenings').onchange=()=>update({awakenings:Number($('awakenings').value)});
  $('plus-preset').onchange=()=>{const n=Number($('plus-preset').value);if(n>=0)update({plus:[n,n,n]});};
  ['hp-plus','atk-plus','rcv-plus'].forEach(id=>$(id).onchange=()=>guarded(()=>{readFields();render();}));
  $('latent-add').onclick=()=>update({latents:[...member().latents,Number($('latent-choice').value)]});
  $('import').onclick=()=>bridge('paste');$('export').onclick=()=>guarded(()=>bridge('copy',model.exportLink(readFields())));
  $('reference').onclick=()=>bridge('reference');
  $('save').onclick=()=>guarded(()=>bridge('save',readFields()));$('close').onclick=()=>guarded(()=>bridge('close',readFields()));
  $('apply-one').onclick=()=>guarded(()=>bridge('apply',model.payload(readFields(),selected.slot)));
  $('apply-all').onclick=()=>guarded(()=>bridge('apply',model.payload(readFields())));
  $('restore-one').onclick=()=>bridge('restore',selected.slot);$('restore-all').onclick=()=>bridge('restore',6);
  render();bridge('ready');
})();
