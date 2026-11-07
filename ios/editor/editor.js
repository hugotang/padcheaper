/* global PHFormation, PH_CATALOG, PH_BADGE_CATALOG, PHTeamView */
(() => {
  'use strict';
  const model = PHFormation.createModel(PH_CATALOG), $ = id => document.getElementById(id);
  const clone = value => JSON.parse(JSON.stringify(value));
  const badgeCatalog = new Map(PH_BADGE_CATALOG.entries.map(entry=>[entry.id,entry]));
  const histories = new Map();
  let library = model.createLibrary(), team = model.selectedTeam(library), savedLibrary = null;
  let selected = {slot:0,kind:'body'}, battle = {}, recent = [], savedJSON = '', pending = null;
  let latentChoice = 11;
  let sequence = 0, saveTimer, toastTimer, lastSaveError = '', savePromise = Promise.resolve({ok:true}), resolveSave;
  const teamView = PHTeamView.create(model,()=>team,portrait,()=>{
    showWorkspace('calculator');
    $('calc-preset').focus({preventScroll:true});
    $('calc-preset').scrollIntoView({block:'center'});
  });
  const bridge = (action,value) => {
    const handler = window.webkit?.messageHandlers?.formation;
    if (!handler) return false;
    handler.postMessage({action,value}); return true;
  };
  const member = () => team.slots[selected.slot][selected.kind];
  const history = () => histories.get(library.selected) || [];
  function showWorkspace(name,focus=false) {
    if(!['formation','calculator','dungeon'].includes(name))return;
    for(const key of ['formation','calculator','dungeon']) {
      const active=key===name,tab=$('tab-'+key);
      tab.classList.toggle('active',active);tab.setAttribute('aria-selected',String(active));tab.tabIndex=active?0:-1;
      $('panel-'+key).hidden=!active;
    }
    $('calc-icon-info').hidden=true;
    if(focus)$('tab-'+name).focus({preventScroll:true});
  }
  function message(id,value) { $(id).textContent=value; $(id).hidden=!value; }
  function error(value) { message('error',value); message('editor-error',value); }
  function toast(value) {
    message('toast',value); message('editor-notice',value); clearTimeout(toastTimer);
    toastTimer=setTimeout(()=>{message('toast','');message('editor-notice','');},4500);
  }
  function guarded(fn) { try { error(''); return fn(); } catch (e) { error(e.message); renderState(); } }
  function text(className,value) { const e=document.createElement('div');e.className=className;e.textContent=value;return e; }
  function portrait(id) {
    const p=document.createElement('div');p.className='portrait'+(!id?' empty':'');p.setAttribute('aria-hidden','true');
    p.dataset.cardId=String(id);
    if(id) {
      const page=Math.ceil(id/100),index=(id-1)%100;
      // The 1024px atlas contains 100px portraits at a 102px pitch, not ten
      // equal tiles. Clip one portrait so neighboring artwork cannot bleed in.
      p.style.setProperty('--portrait-column',index%10);p.style.setProperty('--portrait-row',Math.floor(index/10));
      const fallback=text('portrait-fallback','#'+id),image=document.createElement('img');
      image.alt='';image.decoding='async';image.referrerPolicy='no-referrer';
      image.onload=()=>{p.classList.add('loaded');p.classList.remove('unavailable');};
      image.onerror=()=>{p.classList.remove('loaded');p.classList.add('unavailable');p.title='角色圖示暫時無法載入';};
      image.src='https://mapaler.github.io/PADDashFormation/images/cards_ja/CARDS_'+String(page).padStart(3,'0')+'.PNG';
      p.append(fallback,image);
    }
    return p;
  }
  function options(element,values,value) { element.replaceChildren();for(const [id,label] of values)element.add(new Option(label,String(id)));element.value=String(value); }
  function badgeIcon(id) {
    const icon=document.createElement('span'),entry=badgeCatalog.get(id);
    icon.className='badge-icon';icon.setAttribute('aria-hidden','true');
    if(entry?.icon){icon.style.setProperty('--badge-x',entry.icon.x);icon.style.setProperty('--badge-y',entry.icon.y);}
    else {icon.classList.add('empty');icon.textContent=id===undefined?'—':'?';}
    return icon;
  }
  function renderBadge() {
    const entry=badgeCatalog.get(team.badge),selectedBadge=team.badge===undefined?'inherit':String(team.badge);
    const choices=[['inherit','沿用遊戲內徽章'],...PH_BADGE_CATALOG.entries.map(item=>[item.id,item.name])];
    if(team.badge!==undefined&&!entry)choices.push([team.badge,'未收錄徽章 #'+team.badge+'（保留設定）']);
    options($('team-badge-choice'),choices,selectedBadge);
    $('team-badge-icon').replaceChildren(badgeIcon(team.badge));
    $('team-badge-note').textContent=team.badge===undefined?
      '此隊不指定徽章，沿用遊戲內的選擇。':
      team.badge===129?'月卡徽章選擇會隨隊伍保存；月卡資格與效果仍由遊戲判定。':
      !entry?'已保留匯入的徽章編號；目前圖鑑未收錄，請在遊戲內確認。':
      '徽章選擇會隨隊伍、複製及分享資料保存。效果與可用資格以遊戲為準。';
    for(const button of $('team-badge-options').children) {
      const active=button.dataset.badge===selectedBadge;
      button.classList.toggle('active',active);button.setAttribute('aria-pressed',String(active));
    }
  }
  function chooseBadge(value) {
    guarded(()=>{readFields();commitTeam(model.selectBadge(team,value==='inherit'?undefined:Number(value)),'更換徽章');
      render();$('team-badge-picker').open=false;$('team-badge-choice').focus({preventScroll:true});});
  }
  function candidateFromFields() {
    const next=clone(team),v=member(),assist=selected.kind==='assist';
    const numeric=(id,original)=>$(id).value===$(id).dataset.rendered ? original : Number($(id).value);
    Object.assign(next.slots[selected.slot][selected.kind],{
      level:numeric('level',v.level),skill:numeric('skill',v.skill),awakenings:Number($('awakenings').value),
      plus:assist?v.plus:['hp-plus','atk-plus','rcv-plus'].map(id=>Number($(id).value))
    });
    next.title=$('team-title').value;return next;
  }
  function dirty() {
    try { return JSON.stringify(model.updateLibraryTeam(library,model.normalize(candidateFromFields())))!==savedJSON; }
    catch (_) { return true; }
  }
  function commitTeam(next,label='編輯') {
    next=model.normalize(next);
    if(JSON.stringify(next)!==JSON.stringify(team)) {
      const steps=history();steps.push({team:clone(team),selected:{...selected},label});
      if(steps.length>30)steps.shift();histories.set(library.selected,steps);team=next;
    }
    return team;
  }
  function readFields() { return commitTeam(candidateFromFields()); }
  function update(changes,label) {
    guarded(()=>{const next=candidateFromFields();Object.assign(next.slots[selected.slot][selected.kind],changes);commitTeam(next,label);render();});
  }
  function undo() {
    guarded(()=>{readFields();const previous=history().pop();if(!previous)return;team=previous.team;selected=previous.selected;
      resetFields();render();toast('已撤銷'+previous.label+'；可儲存這個版本。');});
  }
  function resetFields() {
    $('team-title').value=team.title;$('search').value='';$('results').hidden=true;
    $('latent-picker').open=false;
  }
  function matching(index) {
    const live=battle.members?.[index];
    try {
      const slot=index===selected.slot?candidateFromFields().slots[index]:team.slots[index];
      return Boolean(live?.leased && live.active && live.readable && model.matchesRecipe(slot,live.appliedRecipe));
    } catch (_) { return false; }
  }
  function renderGrid() {
    ['body','assist'].forEach(kind=>{
      const grid=$(kind+'-grid');grid.replaceChildren();
      team.slots.forEach((slot,index)=>{
        const v=slot[kind],c=model.cards.get(v.id),button=document.createElement('button');
        const isSelected=selected.slot===index&&selected.kind===kind;
        button.className='card '+kind+(isSelected?' active':'');button.dataset.slot=index;button.dataset.kind=kind;
        button.setAttribute('aria-label','位置 '+(index+1)+' '+(kind==='body'?'本體':'武裝')+' '+(c?'#'+c.id+' '+c.name:'未設定'));
        button.append(portrait(v.id),text('number',v.id?'#'+v.id:'未設定'),text('name',c?.name || '點擊選擇'),
          text('stats',c?'Lv.'+(v.level||c.maxLevel)+'\n+'+v.plus.reduce((a,b)=>a+b,0):'選擇'));
        if(kind==='body'&&battle.members?.[index]?.leased) {
          const match=matching(index),badge=text('leased'+(match?'':' other'),match?'✓':'·');
          badge.setAttribute('aria-label',match?'本場已套用此配方':'本場套用其他配方');button.append(badge);
        }
        button.onclick=()=>openEditor(index,kind);grid.append(button);
      });
    });
  }
  function renderLibrary() {
    options($('team-choice'),model.libraryTeamOptions(library,team),library.selected);
    $('library-count').textContent=library.teams.length+' / '+model.libraryCapacity+' 隊 · 切換時儲存目前編輯';
  }
  function renderFields() {
    const v=member(),c=model.cards.get(v.id),assist=selected.kind==='assist';
    teamView.renderSelection(v);
    $('team-title').value=team.title;
    $('editor-title').textContent='位置 '+(selected.slot+1)+(selected.slot===0?' · 隊長':selected.slot===5?' · 助人':'');
    $('slots-label').textContent=(selected.slot+1)+' / 6';
    $('previous-slot').disabled=selected.slot===0;$('next-slot').disabled=selected.slot===5;
    ['body','assist'].forEach(kind=>{const active=selected.kind===kind;$('kind-'+kind).className=active?'active':'';$('kind-'+kind).setAttribute('aria-pressed',String(active));});
    $('selected-portrait').replaceChildren(portrait(v.id));
    $('selected-name').textContent=c?'#'+c.id+' '+c.name:'尚未選擇'+(assist?'武裝':'本體');
    $('selected-hint').textContent=c?'最高 Lv.'+c.maxLevel+' · 技能最高 '+c.maxSkill+' 級 · '+c.awakenings.length+' 個普通覺醒':'搜尋圖鑑編號或名稱來選擇角色。';
    for(const [id,value,max] of [['level',v.level||c?.maxLevel||1,c?.maxLevel||120],['skill',v.skill||c?.maxSkill||1,c?.maxSkill||999]]) {
      $(id).value=value;$(id).dataset.rendered=String(value);$(id).max=max;
    }
    options($('awakenings'),[[-1,'全部開啟'],...Array.from({length:(c?.awakenings.length||0)+1},(_,n)=>[n,n+' 個'])],v.awakenings);
    const common=v.plus.every(p=>p===v.plus[0])?v.plus[0]:-2;
    options($('plus-preset'),assist?[[99,'+297'],[0,'+0']]:[[297,'+891'],[99,'+297'],[0,'+0'],[-2,'自訂三項']],common);
    $('plus-detail').hidden=assist;
    ['hp-plus','atk-plus','rcv-plus'].forEach((id,i)=>$(id).value=v.plus[i]);
    $('super-section').hidden=assist;$('latent-section').hidden=assist;$('super-options').replaceChildren();
    if(!assist) {
      const available=c?.supers.length?c.supers:c?.sync?[c.sync]:[];
      const choices=[[-1,available.length?'自動 · '+model.awakeningName(available[0]):'自動 · 無超覺醒'],[0,'不啟用'],...available.map(id=>[id,model.awakeningName(id)])];
      for(const [value,label] of choices) {const b=document.createElement('button');if(value>0)b.append(teamView.icon(value));b.append(document.createTextNode(label));b.className=value===v.superAwakening?'active':'';b.dataset.super=value;b.onclick=()=>update({superAwakening:value});$('super-options').append(b);}
      const used=v.latents.reduce((n,id)=>n+model.latentCost(id),0);
      $('latent-title').textContent='潛在覺醒 · '+used+' / '+(c?.latentSlots||6)+' 格（點擊移除）';
      $('latent-list').replaceChildren();v.latents.forEach((id,index)=>{const b=document.createElement('button');b.type='button';
        b.append(teamView.icon(id,true),document.createTextNode(model.latentName(id)+' ×'));
        b.setAttribute('aria-label','移除第 '+(index+1)+' 個潛覺：'+model.latentName(id));
        b.onclick=()=>update({latents:v.latents.filter((_,i)=>i!==index)});$('latent-list').append(b);});
      renderLatentChoice();
    }
    const destination=Number($('slot-destination').value);
    options($('slot-destination'),team.slots.map((_,i)=>[i,'位置 '+(i+1)]).filter(([i])=>i!==selected.slot),destination!==selected.slot&&destination>=0?destination:(selected.slot+1)%6);
    $('clear-card').textContent=assist?'移除此武裝':'移除此本體';renderRecent();
  }
  function renderLatentChoice() {
    const value=member(),card=model.cards.get(value.id),used=value.latents.reduce((n,id)=>n+model.latentCost(id),0);
    const remaining=Math.max(0,(card?.latentSlots||6)-used);
    $('latent-choice').replaceChildren(teamView.icon(latentChoice,true),text('latent-label',model.latentName(latentChoice)+' · '+model.latentCost(latentChoice)+' 格'));
    $('latent-add').disabled=!card||model.latentCost(latentChoice)>remaining;
    $('latent-space').textContent=card?'剩餘 '+remaining+' 格':'先選擇本體角色';
    for(const button of $('latent-options').children) {
      const id=Number(button.dataset.latent);
      button.classList.toggle('active',id===latentChoice);button.setAttribute('aria-pressed',String(id===latentChoice));
      button.disabled=!card||model.latentCost(id)>remaining;
    }
  }
  function renderState() {
    const unsaved=dirty(),configured=battle.automaticConfigured??battle.automaticEnabled;
    $('save-status-row').dataset.tone=lastSaveError?'error':unsaved?'dirty':'saved';
    $('save-status').textContent=pending?'正在儲存，等待確認…':lastSaveError?'儲存未完成 · 草稿仍保留':unsaved?'有未儲存的變更':'已儲存目前隊伍';
    const storedTeam=savedLibrary?.teams.find(record=>record.id===library.selected)?.team;
    const badgeStored=Boolean(storedTeam)&&storedTeam.badge===team.badge;
    const submittedTeam=pending?.library.teams.find(record=>record.id===library.selected)?.team;
    const badgeSubmitted=Boolean(submittedTeam)&&submittedTeam.badge===team.badge;
    const badgeStatus=pending?(badgeSubmitted?'正在儲存徽章設定。':'新徽章選擇尚未儲存；正在儲存先前版本。'):
      !badgeStored?'徽章選擇尚未儲存。':'徽章選擇已儲存。';
    $('team-badge-status').dataset.tone=team.badge===undefined?'':'notice';
    $('team-badge-status').textContent=badgeStatus+(team.badge===undefined?' 儲存或套隊不會更改遊戲內徽章。':
      ' 請在遊戲的隊伍畫面選用；本場套隊與入場自動套隊暫不切換徽章。');
    const scheduled=battle.automaticTeamTitle || (savedLibrary?model.selectedTeam(savedLibrary).title:'未設定');
    $('entry-state').textContent=!configured?'入場自動套用：關閉。可在 PC 面板開啟。':
      battle.automaticSwitchesReady===false?'入場自動套用：等待總開關與戰鬥隊員替換。':
      battle.enabled===false?'入場自動套用：等待遊戲校驗與登入，可先儲存隊伍。':
      '下次單人入場：'+scheduled+(unsaved?'。新變更儲存後才會使用。':'（已儲存版本）。');
    const matches=team.slots.reduce((n,_,i)=>n+Number(matching(i)),0),filled=team.slots.filter(s=>s.body.id).length;
    $('battle-summary').textContent=battle.multiplayerDisabled?'多人戰鬥已停用套隊；仍可編輯與儲存。':
      battle.pendingMask?'本場：等待遊戲套用隊伍…':battle.restorePending?'本場：等待還原…':
      battle.activeCount&&!battle.present?'本場：已有替換紀錄，等待遊戲讀回確認。':
      matches?'本場：目前配方 '+matches+' / '+filled+' 格已套用':
      battle.activeCount?'本場：已套用其他配方（'+battle.activeCount+' 格）':'本場尚未套用這份隊伍。';
    $('connection').textContent=[battle.message,battle.gate].filter(Boolean).join(' · ')||'等待戰鬥檢查；現在可以編輯隊伍。';
    $('entry-sb').textContent=battle.entryApplied?String(battle.initialBoost??'—'):'—';
    $('current-hp').textContent=battle.present&&battle.hpKnown?Number(battle.currentHP).toLocaleString():'—';
    $('apply-all').disabled=Boolean(pending)||!battle.canApply||!filled||Boolean(battle.activeCount);
    $('apply-one').disabled=Boolean(pending)||!battle.canApply||!team.slots[selected.slot].body.id||!battle.members?.[selected.slot]?.replaceable;
    $('restore-one').disabled=Boolean(pending)||!battle.canRestore||!battle.members?.[selected.slot]?.leased;
    $('restore-all').disabled=Boolean(pending)||!battle.canRestore;
    $('save').disabled=$('close').disabled=$('team-choice').disabled=Boolean(pending);
    $('save').textContent=pending?'儲存中…':'儲存隊伍';
    $('team-new').disabled=$('team-copy').disabled=Boolean(pending)||library.teams.length>=model.libraryCapacity;
    $('team-delete').disabled=Boolean(pending)||library.teams.length===1;
    $('delete-yes').disabled=$('discard-close').disabled=Boolean(pending);
    $('apply-hint').textContent=battle.multiplayerDisabled?'多人期間僅提供編輯與儲存':battle.activeCount?'修改本場隊伍前，請先還原要替換的位置':
      battle.canApply?'手動套用會先儲存，並保留目前技能充能':'立即套用須等待單人操作階段';
    $('slot-state').textContent=matching(selected.slot)?'本場已套用此格的本體、武裝與設定。':battle.members?.[selected.slot]?.leased?'本場此位置正在使用另一份配方，請先還原。':battle.gate||'可先編輯，等單人操作階段再套用。';
    $('undo').disabled=$('editor-undo').disabled=!history().length;
    const steps=history(),last=steps[steps.length-1];$('undo').setAttribute('aria-label',last?'撤銷'+last.label:'沒有可撤銷的編輯');
  }
  function render() { renderLibrary();renderFields();renderGrid();renderBadge();renderState();teamView.render(); }
  function chooseCard(id) {
    guarded(()=>{readFields();commitTeam(model.selectCard(team,selected.slot,selected.kind,id),'選擇角色');
      if(id){recent=[id,...recent.filter(n=>n!==id)].slice(0,24);bridge('recent',recent);}
      $('results').hidden=true;$('search').value='';$('search').blur();render();$('editor-content').scrollTop=0;});
  }
  function renderRecent() {
    const cards=recent.map(id=>model.cards.get(id)).filter(c=>c&&(selected.kind!=='assist'||c.canAssist)).slice(0,10);
    $('recent-section').hidden=!cards.length;$('recent').replaceChildren();
    for(const c of cards) {const b=document.createElement('button');b.append(portrait(c.id),document.createTextNode('#'+c.id));b.setAttribute('aria-label','最近使用 #'+c.id+' '+c.name);b.onclick=()=>chooseCard(c.id);$('recent').append(b);}
  }
  function showResults() {
    const results=$('results');results.hidden=false;results.replaceChildren();const found=model.search($('search').value,selected.kind==='assist');
    if(!found.length){results.append(text('quiet','沒有符合的角色，請確認圖鑑編號或名稱。'));return;}
    for(const card of found){const b=document.createElement('button');b.className='result';b.append(portrait(card.id));const name=document.createElement('span');name.textContent='#'+card.id+' '+card.name;b.append(name);b.onclick=()=>chooseCard(card.id);results.append(b);}
  }
  function openEditor(slot,kind) {
    guarded(()=>{readFields();selected={slot,kind};resetFields();render();$('editor-dialog').hidden=false;
      $('main').inert=true;$('main').setAttribute('aria-hidden','true');document.body.classList.add('editing');
      $('editor-content').scrollTop=0;$('editor-done').focus({preventScroll:true});});
  }
  function closeEditor() {
    guarded(()=>{readFields();render();$('latent-picker').open=false;$('editor-dialog').hidden=true;$('main').inert=false;$('main').removeAttribute('aria-hidden');document.body.classList.remove('editing');
      document.querySelector('#'+selected.kind+'-grid [data-slot="'+selected.slot+'"]').focus({preventScroll:true});});
  }
  function navigate(slot,kind=selected.kind) {
    guarded(()=>{readFields();selected={slot,kind};resetFields();render();$('editor-content').scrollTop=0;});
  }
  function uniqueTeamID() {
    let id;do{id=window.crypto?.randomUUID?window.crypto.randomUUID():'team-'+Date.now().toString(36)+'-'+Math.random().toString(36).slice(2,12);}while(library.teams.some(record=>record.id===id));return id;
  }
  function persist(successMessage='已儲存目前隊伍',intent={}) {
    if(pending)return;
    readFields();library=model.updateLibraryTeam(library,team);
    const requestID=String(++sequence),submitted=model.normalizeLibrary(library);
    pending={requestID,library:submitted,successMessage,intent};lastSaveError='';
    clearTimeout(toastTimer);message('toast','');message('editor-notice','');
    savePromise=new Promise(resolve=>{resolveSave=resolve;});
    renderState();
    saveTimer=setTimeout(()=>receiveSave({requestID,ok:false,message:'未收到儲存確認；草稿仍保留，請重試。'}),10000);
    if(!bridge('persist',{requestID,library:submitted}))receiveSave({requestID,ok:false,message:'未連接原生儲存；草稿尚未保存。'});
  }
  function receiveSave(result) {
    if(!pending||result.requestID!==pending.requestID)return;
    clearTimeout(saveTimer);const request=pending;pending=null;
    if(result.battle)battle=result.battle;
    if(result.ok===true) {
      savedLibrary=model.normalizeLibrary(request.library);savedJSON=JSON.stringify(savedLibrary);lastSaveError='';
      error('');toast(request.successMessage);
    } else {lastSaveError=result.message||'儲存失敗；草稿仍保留。';error(lastSaveError);}
    renderLibrary();renderState();resolveSave?.({ok:result.ok===true,message:result.message});resolveSave=null;
    if(result.ok===true&&!dirty()) {
      if(request.intent.close)bridge('dismiss');
      else if(request.intent.applySlot!==undefined)bridge('applySaved',model.payload(model.selectedTeam(savedLibrary),request.intent.applySlot));
    } else if(result.ok===true&&(request.intent.close||request.intent.applySlot!==undefined))toast('已儲存送出的版本；尚有新變更，請再次儲存後繼續。');
  }
  function useLibrary(value,notice) {
    if(pending)return;
    library=model.normalizeLibrary(value);team=model.selectedTeam(library);selected={slot:0,kind:'body'};
    resetFields();$('management').open=false;$('delete-confirm').hidden=true;render();persist(notice);
  }
  function apply(slot) { guarded(()=>{readFields();model.payload(team,slot);persist('已儲存，正在排入套用。',{applySlot:slot});}); }
  window.PHFormationUI={
    initialized:false,
    receive(value) {
      guarded(()=>{
        if(value.library||value.team) {
          library=value.library?model.normalizeLibrary(value.library):model.createLibrary(value.team);team=model.selectedTeam(library);
          selected={slot:0,kind:'body'};histories.clear();resetFields();
          if(value.library){savedLibrary=value.stored===false?null:model.normalizeLibrary(library);savedJSON=savedLibrary?JSON.stringify(savedLibrary):'';}
        }
        if(value.recent)recent=[...new Set(value.recent)].filter(id=>model.cards.has(id)).slice(0,24);
        battle=value.battle||battle;render();
        if(value.dungeonGuides)teamView.dungeon.setGuides(value.dungeonGuides);
        if(value.dungeonComparison)this.showDungeonComparison(value.dungeonComparison);
        this.initialized=true;
      });
    },
    setState(value) {battle=value||{};renderGrid();renderState();},
    saveResult:receiveSave,
    importText(value) {guarded(()=>{readFields();const imported=model.parseImport(value);commitTeam(imported.team,'匯入隊伍');resetFields();$('management').open=false;render();toast((imported.notice||'已匯入隊伍')+' 尚未儲存。');});},
    getTeam() {return clone(team);},
    getTeamCalculation() {return clone(teamView.getResult());},
    getDungeonComparison() {return clone(teamView.dungeon.getResult());},
    importDungeonText(value) {teamView.dungeon.importText(value);},
    importDungeonGuides(value) {teamView.dungeon.importGuides(value);},
    getDungeonGuides() {return clone(teamView.dungeon.getGuides());},
    guideSaveResult(value) {teamView.dungeon.guideSaveResult(value);},
    showDungeonComparison(value) {
      closeEditor();
      teamView.dungeon.importText(value);
      showWorkspace('dungeon');
      requestAnimationFrame(()=>$('calc-dungeon-details').scrollIntoView({block:'start'}));
    },
    getLibrary() {return model.updateLibraryTeam(library,team);},
    getSaveState() {return {pending:Boolean(pending),dirty:dirty(),error:lastSaveError,savedLibrary:clone(savedLibrary)};},
    whenSaved() {return savePromise;},
    notify:toast
  };
  const workspaceNames=['formation','calculator','dungeon'];
  for(const name of workspaceNames) {
    $('tab-'+name).onclick=()=>showWorkspace(name);
    $('tab-'+name).onkeydown=event=>{
      const index=workspaceNames.indexOf(name);
      const next=event.key==='ArrowRight'?(index+1)%3:event.key==='ArrowLeft'?(index+2)%3:
        event.key==='Home'?0:event.key==='End'?2:-1;
      if(next<0)return;event.preventDefault();showWorkspace(workspaceNames[next],true);
    };
  }
  for(let i=0;i<6;i++)$('positions').append(text('position',i===0?'1 隊長':i===5?'6 助人':String(i+1)));
  for(const entry of [{name:'沿用遊戲內徽章'},...PH_BADGE_CATALOG.entries]) {
    const button=document.createElement('button');button.type='button';
    button.dataset.badge=entry.id===undefined?'inherit':String(entry.id);
    button.append(badgeIcon(entry.id),text('badge-label',entry.name));
    button.setAttribute('aria-label',entry.name);button.onclick=()=>chooseBadge(button.dataset.badge);
    $('team-badge-options').append(button);
  }
  $('team-badge-choice').onchange=()=>chooseBadge($('team-badge-choice').value);
  for(const id of Object.keys(model.latentNames).map(Number)) {
    const button=document.createElement('button');button.type='button';button.dataset.latent=String(id);
    button.append(teamView.icon(id,true),text('latent-label',model.latentName(id)),text('latent-cost',model.latentCost(id)+' 格'));
    button.onclick=()=>{latentChoice=id;renderLatentChoice();$('latent-picker').open=false;$('latent-picker-toggle').focus({preventScroll:true});};
    $('latent-options').append(button);
  }
  $('team-choice').onchange=()=>guarded(()=>{const id=$('team-choice').value;if(pending){renderLibrary();return;}
    try{useLibrary(model.selectLibraryTeam(model.updateLibraryTeam(library,readFields()),id),'已儲存並切換隊伍');}finally{renderLibrary();}});
  $('team-new').onclick=()=>guarded(()=>{if(pending)return;const draft=model.empty();draft.title='隊伍 '+(library.teams.length+1);useLibrary(model.addLibraryTeam(model.updateLibraryTeam(library,readFields()),draft,uniqueTeamID()),'已新增並儲存空白隊伍');});
  $('team-copy').onclick=()=>guarded(()=>{if(pending)return;const draft=clone(readFields());draft.title=(draft.title||'我的隊伍').slice(0,77)+' 副本';useLibrary(model.addLibraryTeam(library,draft,uniqueTeamID()),'已另存為獨立的新隊伍');});
  $('team-delete').onclick=()=>{$('management').open=false;$('delete-confirm').hidden=false;$('delete-message').textContent='刪除「'+(team.title||'未命名隊伍')+'」？';};
  $('delete-cancel').onclick=()=>{$('delete-confirm').hidden=true;};
  $('delete-yes').onclick=()=>guarded(()=>useLibrary(model.deleteLibraryTeam(library),'已刪除並儲存隊伍庫'));
  $('discard-close').onclick=()=>{$('management').open=false;if(!dirty())bridge('dismiss');else $('discard-confirm').hidden=false;};
  $('discard-cancel').onclick=()=>{$('discard-confirm').hidden=true;};$('discard-yes').onclick=()=>bridge('dismiss');
  $('search').oninput=showResults;$('search-toggle').onclick=()=>{$('search').focus();showResults();};
  $('clear-card').onclick=()=>chooseCard(0);
  $('max-level').onclick=()=>update({level:0});$('max-skill').onclick=()=>update({skill:0});
  ['level','skill','awakenings','hp-plus','atk-plus','rcv-plus','team-title'].forEach(id=>{
    $(id).onchange=()=>guarded(()=>{readFields();render();});$(id).addEventListener('input',renderState);
  });
  $('plus-preset').onchange=()=>{const n=Number($('plus-preset').value);if(n>=0)update({plus:[n,n,n]});};
  $('latent-add').onclick=()=>update({latents:[...member().latents,latentChoice]});
  $('previous-slot').onclick=()=>navigate(selected.slot-1);$('next-slot').onclick=()=>navigate(selected.slot+1);
  $('kind-body').onclick=()=>navigate(selected.slot,'body');$('kind-assist').onclick=()=>navigate(selected.slot,'assist');
  $('editor-done').onclick=closeEditor;$('undo').onclick=$('editor-undo').onclick=undo;
  for(const [id,copy] of [['swap-slot',false],['copy-slot',true]])$(id).onclick=()=>guarded(()=>{
    readFields();const destination=Number($('slot-destination').value),source=selected.slot;
    commitTeam(model.moveSlot(team,source,destination,copy),copy?'複製整格':'交換位置');selected.slot=destination;resetFields();render();
    toast('位置 '+(source+1)+(copy?' 已複製到 ':' 已交換至 ')+(destination+1)+'，包含本體與武裝。可撤銷上一步。');
  });
  $('import').onclick=()=>bridge('paste');$('export').onclick=()=>guarded(()=>bridge('copy',model.exportLink(readFields())));$('reference').onclick=()=>bridge('reference');
  $('save').onclick=()=>guarded(()=>persist());$('close').onclick=()=>guarded(()=>persist('已儲存目前隊伍',{close:true}));
  $('apply-one').onclick=()=>apply(selected.slot);$('apply-all').onclick=()=>apply(-1);
  $('restore-one').onclick=()=>bridge('restore',selected.slot);$('restore-all').onclick=()=>bridge('restore',6);
  document.addEventListener('keydown',event=>{
    if($('editor-dialog').hidden)return;
    if(event.key==='Escape'){event.preventDefault();
      if($('latent-picker').open){$('latent-picker').open=false;$('latent-picker-toggle').focus({preventScroll:true});}
      else closeEditor();
    }
    if(event.key==='Tab') {
      const focusable=[...$('editor-dialog').querySelectorAll('button,input,select,summary')].filter(e=>!e.disabled&&e.getClientRects().length);
      const first=focusable[0],last=focusable[focusable.length-1];
      if(event.shiftKey&&document.activeElement===first){event.preventDefault();last?.focus();}
      else if(!event.shiftKey&&document.activeElement===last){event.preventDefault();first?.focus();}
    }
  });
  function resizeEditor() {
    if(!window.visualViewport||Math.abs(visualViewport.scale-1)>.01)return;
    // iOS can pan the visual viewport while focusing an input. Height alone
    // leaves the fixed header above the visible area during that pan.
    document.documentElement.style.setProperty('--editor-height',visualViewport.height+'px');
    document.documentElement.style.setProperty('--editor-top',visualViewport.offsetTop+'px');
  }
  window.visualViewport?.addEventListener('resize',resizeEditor);
  window.visualViewport?.addEventListener('scroll',resizeEditor);resizeEditor();
  resetFields();render();bridge('ready');
})();
