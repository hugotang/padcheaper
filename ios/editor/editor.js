/* global PHFormation, PH_CATALOG */
(() => {
  'use strict';
  const model = PHFormation.createModel(PH_CATALOG), $ = id => document.getElementById(id);
  const awakeningName = model.awakeningName;
  const latentName = model.latentName;
  let library = model.createLibrary(), team = model.selectedTeam(library), selected = {slot:0,kind:'body'}, battle = {}, toastTimer;
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
  function renderLibrary() {
    options($('team-choice'),library.teams.map(record=>[record.id,record.team.title || '未命名隊伍']),library.selected);
    $('library-count').textContent=`已儲存 ${library.teams.length} / ${model.libraryCapacity} 隊 · 入場自動套用使用目前選取的隊伍`;
    $('team-new').disabled=$('team-copy').disabled=library.teams.length>=model.libraryCapacity;
    $('team-delete').disabled=library.teams.length===1;
  }
  function uniqueTeamID() {
    let id;
    do {
      id=window.crypto?.randomUUID ? window.crypto.randomUUID() : `team-${Date.now().toString(36)}-${Math.random().toString(36).slice(2,12)}`;
    } while (library.teams.some(record=>record.id===id));
    return id;
  }
  function useLibrary(value,message) {
    const next=model.normalizeLibrary(value), draft=model.selectedTeam(next);
    library=next;team=draft;selected={slot:0,kind:'body'};
    $('team-title').value=team.title;$('search').value='';$('results').hidden=true;$('delete-confirm').hidden=true;
    render();bridge('library',library);if(message)toast(message);
  }
  function saveDraft() {
    const draft=readFields();library=model.updateLibraryTeam(library,draft);renderLibrary();return draft;
  }
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
  function render(){renderLibrary();renderGrid();renderFields();renderState();}
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
    receive(value){if(value.library || value.team){guarded(()=>{library=value.library?model.normalizeLibrary(value.library):model.createLibrary(value.team);team=model.selectedTeam(library);$('team-title').value=team.title;});}battle=value.battle||battle;render();this.initialized=true;},
    setState(value){battle=value||{};renderGrid();renderState();},
    importText(value){guarded(()=>{const imported=model.parseImport(value);team=imported.team;$('team-title').value=team.title;render();toast(imported.notice||'已匯入隊伍');});},
    getTeam(){return JSON.parse(JSON.stringify(team));},
    getLibrary(){return model.normalizeLibrary(library);},
    notify:toast
  };
  for(let i=0;i<6;i++)$('positions').append(text('position',i===0?'1 隊長':i===5?'6 助人':String(i+1)));
  options($('latent-choice'),Object.keys(model.latentNames).map(Number).map(id=>[id,`${latentName(id)} · ${model.latentCost(id)} 格`]),11);
  $('team-choice').onchange=()=>guarded(()=>{
    const id=$('team-choice').value;
    try { useLibrary(model.selectLibraryTeam(model.updateLibraryTeam(library,readFields()),id)); }
    finally { renderLibrary(); }
  });
  $('team-new').onclick=()=>guarded(()=>{
    const draft=model.empty();draft.title=`隊伍 ${library.teams.length+1}`;
    useLibrary(model.addLibraryTeam(model.updateLibraryTeam(library,readFields()),draft,uniqueTeamID()),'已新增空白隊伍');
  });
  $('team-copy').onclick=()=>guarded(()=>{
    // Copy the current draft without overwriting its previously saved record.
    const draft=readFields();draft.title=(draft.title || '我的隊伍').slice(0,77)+' 副本';
    useLibrary(model.addLibraryTeam(library,draft,uniqueTeamID()),'已另存為獨立的新隊伍');
  });
  $('team-delete').onclick=()=>{$('delete-confirm').hidden=false;$('delete-message').textContent=`刪除「${team.title || '未命名隊伍'}」？`;};
  $('delete-cancel').onclick=()=>{$('delete-confirm').hidden=true;};
  $('delete-yes').onclick=()=>guarded(()=>useLibrary(model.deleteLibraryTeam(library),'已刪除隊伍'));
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
  $('save').onclick=()=>guarded(()=>bridge('save',saveDraft()));$('close').onclick=()=>guarded(()=>bridge('close',saveDraft()));
  $('apply-one').onclick=()=>guarded(()=>bridge('apply',model.payload(saveDraft(),selected.slot)));
  $('apply-all').onclick=()=>guarded(()=>bridge('apply',model.payload(saveDraft())));
  $('restore-one').onclick=()=>bridge('restore',selected.slot);$('restore-all').onclick=()=>bridge('restore',6);
  render();bridge('ready');
})();
