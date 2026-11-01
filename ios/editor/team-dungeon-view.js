/* global PHTeamDungeon */
(function(root){
  'use strict';
  const $=id=>document.getElementById(id),n=x=>x===null||x===undefined?'未知':x.toLocaleString('en-US');
  function node(tag,cls,text){const e=document.createElement(tag);e.className=cls;if(text!==undefined)e.textContent=text;return e;}
  function create(model,getTeam,getContext){
    let loaded=null,lastKey='',report=null,guideLibrary=PHTeamDungeon.parseGuides(),savedGuides=JSON.stringify(guideLibrary),pendingGuide=null,guideSequence=0;
    function guideMessage(text,error=false){$('calc-guide-status').textContent=text;$('calc-guide-status').className=error?'calc-error':'quiet';}
    function setGuides(value){
      try{guideLibrary=PHTeamDungeon.parseGuides(value);savedGuides=JSON.stringify(guideLibrary);lastKey='';guideMessage('攻略庫 '+guideLibrary.guides.length+' 關已載入。');render();}
      catch(e){guideMessage('已儲存攻略無法載入：'+e.message,true);}
    }
    function guideSaveResult(value){
      if(!pendingGuide||value?.requestID!==pendingGuide.requestID)return;
      if(value.ok===true){savedGuides=pendingGuide.json;guideMessage('攻略庫已儲存，重開後仍可使用。');}
      else guideMessage((value.message||'攻略未能儲存')+'；目前只在本頁使用，可重試儲存。',true);
      clearTimeout(pendingGuide.timer);pendingGuide=null;$('calc-guide-save').disabled=false;
    }
    function saveGuides(){
      if(pendingGuide)return;
      const handler=window.webkit?.messageHandlers?.formation;
      if(!handler){guideMessage('本頁沒有裝置儲存連線，可複製攻略 JSON 保存。');return;}
      const requestID='guides-'+(++guideSequence),json=JSON.stringify(guideLibrary);
      pendingGuide={requestID,json,timer:setTimeout(()=>guideSaveResult({requestID,ok:false,message:'未收到攻略儲存確認'}),8000)};
      $('calc-guide-save').disabled=true;guideMessage('正在儲存攻略庫…');handler.postMessage({action:'saveDungeonGuides',value:{requestID,library:guideLibrary}});
    }
    function importGuides(value){
      if(pendingGuide){guideMessage('正在儲存，完成後再匯入。');return;}
      try{
        const incoming=PHTeamDungeon.parseGuides(value),combined=new Map(guideLibrary.guides.map(g=>[g.dungeon+':'+g.subDungeon,g]));
        incoming.guides.forEach(g=>combined.set(g.dungeon+':'+g.subDungeon,g));
        guideLibrary=PHTeamDungeon.parseGuides({schema:1,kind:'PADCheaper-dungeon-guides',guides:[...combined.values()]});
        lastKey='';render();guideMessage('已匯入 '+incoming.guides.length+' 關；同關卡提示已更新，請按儲存攻略庫保留。');
      }catch(e){guideMessage('未匯入：'+e.message+'；原攻略庫保留。',true);}
    }
    function clear(){loaded=null;report=null;lastKey='';$('calc-dungeon-results').replaceChildren();$('calc-dungeon-guide').replaceChildren();$('calc-dungeon-status').textContent='尚未載入關卡資料';$('calc-dungeon-error').textContent='';}
    function importText(value){
      clear();
      try{loaded=PHTeamDungeon.parse(value);$('calc-dungeon-scale').value=loaded.inferredScale===null?'':String(loaded.inferredScale);render();}
      catch(e){$('calc-dungeon-error').textContent=e.message;}
    }
    function render(){
      if(!loaded)return;
      try{
        const team=getTeam(),context=getContext(),scale=$('calc-dungeon-scale').value.trim(),options={guides:guideLibrary,...(scale?{attackScale:Number(scale)}:{})};
        const key=JSON.stringify([team,context,options]);if(key===lastKey)return;
        lastKey=key;report=PHTeamDungeon.evaluate(loaded,model,team,context,options);
        $('calc-dungeon-preset').value=$('calc-preset').value;
        $('calc-dungeon-error').textContent='';$('calc-dungeon-results').replaceChildren();
        $('calc-dungeon-guide').replaceChildren();
        if(report.guide){
          const guide=report.guide,box=node('details','dungeon-guide'),summary=node('summary','','攻略提示 · '+guide.title);box.open=true;box.append(summary);
          box.append(node('p','quiet','核對日期 '+guide.checkedAt+'。以下是攻略提示；計算仍使用匯出的本次敵人資料。'));
          for(const text of guide.notes)box.append(node('p','calc-warning',text));
          for(const text of guide.matchedTeamNotes)box.append(node('p','calc-warning',text));
          const links=node('p','quiet','攻略來源：');
          guide.sources.forEach((source,i)=>{
            const a=node('a','','來源 '+(i+1)+' · '+new URL(source).hostname);a.href=source;a.target='_blank';a.rel='noopener noreferrer';
            a.onclick=event=>{const handler=window.webkit?.messageHandlers?.formation;if(handler){event.preventDefault();handler.postMessage({action:'dungeonReference',value:source});}};
            links.append(a,document.createTextNode('　'));
          });box.append(links);$('calc-dungeon-guide').append(box);
        }
        $('calc-dungeon-status').textContent='關卡 '+report.dungeon+'／'+report.subDungeon+' · '+report.floors.length+' / '+report.totalFloors+' 層 · '+
          (loaded.inferredScale===null?'未取得全關攻擊倍率':'觀察值推得 ATK ×'+loaded.inferredScale)+' · '+(report.completeCapture?'原始定義已收齊':'原始資料未完整');
        const first=report.floors[0];
        if(first){
          const daily=first.scenarios.find(s=>s.id==='cycle')||first.scenarios.find(s=>s.id==='shield')||first.scenarios[0],rescue=first.scenarios.find(s=>s.id==='rescue');
          $('calc-dungeon-results').append(node('p','quiet','隊伍：'+(team.title||'未命名隊伍')+'。以目前消珠條件自動比較各技能組合。'+(daily?daily.label+' HP '+n(daily.hp)+'；':'')+
            (rescue?rescue.label+' HP '+n(rescue.hp)+'。':'')+'起始血量與消珠取上方情境；各組技能假設已就緒，不把新增血量視作已補滿。'));
          $('calc-dungeon-results').append(node('p','quiet','此處分別計算隊長盾、日常 HP 與救援技能組合。上方手動填入的倍率及已勾選技能不額外疊加；敵人造成的減益需解除後才適用。'));
        }
        const conditions=node('p','calc-warning','目前消珠：'+(context.matches.length?context.matches.map(g=>PHTeamModel.attrNames[g.attr]+' '+g.n+' 顆'+(g.shape&&g.shape!=='match'?' '+({t:'T',l:'L',four:'四連',cross:'十字',square:'九宮格',row:'橫列',column:'直列'}[g.shape]||g.shape):'' )).join('、'):'尚未選擇，未計消珠觸發的隊長盾')+'。');
        const change=node('button','','調整消珠／HP／技能條件');change.type='button';change.onclick=()=>$('calc-preset').scrollIntoView({block:'center'});
        $('calc-dungeon-results').prepend(conditions,change);
        for(const floor of report.floors){
          const panel=node('details','dungeon-floor'),title=node('summary','');
          let needs=false,broken=false,unresolved=0;
          for(const enemy of floor.enemies)for(const action of enemy.actions){
            if(!action.sequence.complete)unresolved++;
            needs ||= action.variants.some(v=>v.componentNeedsRescue);
            broken ||= action.variants.some(v=>v.needsRescueWithoutShield);
          }
          const daily=floor.aggregate.find(s=>s.scenario==='cycle')||floor.aggregate.find(s=>s.scenario==='shield')||floor.aggregate[0],rescue=floor.aggregate.find(s=>s.scenario==='rescue');
          const aggregateNeeds=daily&&rescue&&daily.fullHPFits===false&&rescue.fullHPFits===true;
          needs ||= aggregateNeeds;
          title.textContent='第 '+floor.number+' 層 · '+(!floor.enemies.length?'樓層資料尚未收集':needs?'有需提高 HP 的候選情境':broken?'斷盾時需提高 HP':'尚未找出需提高 HP 的完整攻擊')+
            (unresolved?' · '+unresolved+' 招未完整':'');
          panel.append(title);panel.dataset.floor=String(floor.number);panel.dataset.danger=String(needs||broken);
          const guideNote=report.guide?.floors.find(f=>f.number===floor.number);
          if(guideNote)panel.append(node('p','calc-warning','攻略提示：'+guideNote.text));
          if(floor.aggregate.length){
            panel.append(node('p','calc-warning','多敵情境：假設所有存活敵人同回合各出目前列出的最大攻擊。實際倒數／選招未判定。'));
            const comparison=node('div','dungeon-comparison');
            const ids=floor.scenarios.some(s=>s.id==='cycle')?['cycle','rescue','cycle-no-shield']:['base','shield','rescue'];
            for(const s of floor.aggregate.filter(s=>ids.includes(s.scenario)))
              comparison.append(node('div','',floor.scenarios.find(v=>v.id===s.scenario).label+': '+n(s.damage)+' 承傷估值／'+n(s.hp)+' HP'+
                (s.fullHPFits===null?'':s.fullHPFits?' · 已讀傷害量可容納':' · 已讀傷害量超出容量')+(s.componentsComplete?'':' · 含未解析效果')));
            panel.append(comparison);
          }
          for(const enemy of floor.enemies){
            panel.append(node('p','quiet','敵人 '+enemy.slot+' · #'+enemy.id+' · ATK '+n(enemy.attack)+'（'+
              ({observed:'本層觀察值','projected-from-dungeon-scale':'關卡倍率推算',unknown:'未知'}[enemy.attackSource])+'）'));
            for(const action of enemy.actions){
              if(!action.sequence.events.length)continue;
              const row=node('details','dungeon-action'),summary=node('summary','');
              const worst=action.variants[action.variants.length-1],daily=worst.comparisons.find(s=>s.scenario==='cycle')||worst.comparisons.find(s=>s.scenario==='shield')||worst.comparisons[0],rescue=worst.comparisons.find(s=>s.scenario==='rescue');
              const hasHit=action.sequence.events.some(e=>['attack','gravity'].includes(e.kind));
              summary.textContent='表列 '+action.row+' · 行動 '+action.id+' · '+(hasHit?n(daily?.damage)+(daily?.complete?'':'（部分估值）'):'狀態／未解析行動')+
                (worst.enrage>1?' · 假設已怒 '+worst.enrage+' 倍':'');row.append(summary);
              if(hasHit)row.append(node('p','quiet',floor.scenarios.find(s=>s.id===daily.scenario).label+' HP '+n(daily?.hp)+'；已讀傷害量需 HP '+n(daily?.requiredHP)+'。'+
                (rescue?floor.scenarios.find(s=>s.id===rescue.scenario).label+' HP '+n(rescue.hp)+'；目前消珠後估值 '+n(rescue.afterPlayerEstimate)+'。':'')));
              const status=action.sequence.events.filter(e=>e.kind==='status').map(e=>e.label+' '+e.turns+([89,140].includes(e.op)&&e.maximum!==e.turns?'～'+e.maximum:'')+' 回合');
              if(status.length)row.append(node('p','calc-warning',status.join('；')+'。後續需另行解狀態。'));
              if(worst.componentNeedsRescue)row.append(node('p','calc-warning','已讀攻擊量超出日常 HP，救援技能可增加容量；仍需足夠回血、技能就緒與正確施放次序。'));
              if(worst.needsRescueWithoutShield)row.append(node('p','calc-warning','日常 HP 在技能盾到期時不足；優先保持盾，無法補盾時再檢查 HP 救援。'));
              if(action.sequence.unknown.length)row.append(node('p','calc-warning','尚未完整解析行動 '+action.sequence.unknown.join('、')+'；本列不是完整存活結論。'));
              if(action.availabilityNote)row.append(node('p','calc-warning',action.availabilityNote));
              panel.append(row);
            }
          }
          $('calc-dungeon-results').append(panel);
        }
        const limits=node('details',''),summary=node('summary','','試算範圍');limits.append(summary);
        for(const text of report.limitations)limits.append(node('p','quiet',text));$('calc-dungeon-results').append(limits);
      }catch(e){lastKey='';report=null;$('calc-dungeon-results').replaceChildren();$('calc-dungeon-guide').replaceChildren();$('calc-dungeon-error').textContent=e.message;}
    }
    $('calc-dungeon-file').onchange=async()=>{
      const file=$('calc-dungeon-file').files[0];if(!file)return;
      if(file.size>16000000){clear();$('calc-dungeon-error').textContent='關卡 JSON 超過 16 MB';return;}
      try{importText(await file.text());}catch(e){clear();$('calc-dungeon-error').textContent=e.message;}
      $('calc-dungeon-file').value='';
    };
    $('calc-dungeon-load').onclick=()=>importText($('calc-dungeon-text').value);
    $('calc-dungeon-clear').onclick=()=>{$('calc-dungeon-text').value='';clear();};
    $('calc-dungeon-scale').onchange=()=>{lastKey='';render();};clear();
    $('calc-dungeon-preset').replaceChildren(...[...$('calc-preset').options].map(o=>o.cloneNode(true)));
    $('calc-dungeon-preset').onchange=()=>{$('calc-preset').value=$('calc-dungeon-preset').value;$('calc-preset').dispatchEvent(new Event('change'));};
    $('calc-guide-load').onclick=()=>importGuides($('calc-guide-text').value);
    $('calc-guide-save').onclick=saveGuides;
    $('calc-guide-export').onclick=()=>{$('calc-guide-text').value=JSON.stringify(guideLibrary,null,2);$('calc-guide-text').focus();$('calc-guide-text').select();guideMessage('完整攻略庫已填入文字框，可全選複製後保存為 JSON。');};
    $('calc-guide-default').onclick=()=>{if(pendingGuide)return;guideLibrary=PHTeamDungeon.parseGuides();lastKey='';render();guideMessage('本頁已恢復隨版攻略；按儲存攻略庫後生效。');};
    $('calc-guide-file').onchange=async()=>{
      const file=$('calc-guide-file').files[0];if(!file)return;
      try{if(file.size>1000000)throw new Error('攻略 JSON 超過 1 MB');importGuides(await file.text());}catch(e){guideMessage(e.message,true);}
      $('calc-guide-file').value='';
    };
    guideMessage('隨版攻略 '+guideLibrary.guides.length+' 關；可匯入其他關卡，無需更新 dylib。');
    return {render,importText,setGuides,importGuides,guideSaveResult,getGuides:()=>guideLibrary,
      guidesDirty:()=>JSON.stringify(guideLibrary)!==savedGuides,getResult:()=>report};
  }
  root.PHTeamDungeonView={create};
})(globalThis);
