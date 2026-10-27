// Independently implemented editor / PADDashFormation v2-v5 interchange reader.
// Card numbers here always mean encyclopedia numbers, never native catalog IDs.
(function (root) {
  'use strict';
  const clone = value => JSON.parse(JSON.stringify(value));
  const integer = (value, lo, hi, name) => {
    if (!Number.isInteger(value) || value < lo || value > hi) throw new Error(`${name}超出範圍`);
    return value;
  };
  const latentCost = id => {
    integer(id, 1, 52, '潛覺');
    if (id < 12) return 1;
    if ([13, 14, 15, 37, 38, 39, 40, 41, 42, 46, 47, 48, 49].includes(id)) return 6;
    return id === 51 ? 4 : 2;
  };
  function createModel(data) {
    const cards = new Map(data.cards.map(r => [r[0], {id:r[0],name:r[1],attrs:r[2],maxLevel:r[3],maxSkill:r[4],
      awakenings:r[5],supers:r[6],sync:r[7] || 0,canAssist:r[8],latentSlots:r[9],aliases:r[10]}]));
    const emptyCard = assist => ({id:0,level:0,skill:assist ? 1 : 0,awakenings:-1,
      plus:assist ? [99,99,99] : [297,297,297],superAwakening:-1,latents:[]});
    const empty = () => ({schema:1,title:'我的隊伍',slots:Array.from({length:6},()=>({body:emptyCard(false),assist:emptyCard(true)}))});
    function validateCard(value, assist, allowEmpty) {
      if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error('角色設定格式不正確');
      const id = integer(value.id, 0, 65435, '圖鑑編號');
      if (!id && !allowEmpty) throw new Error('請先選擇本體');
      const card = cards.get(id);
      if (id && !card) throw new Error(`#${id} 不在目前日服圖鑑資料中`);
      if (id && assist && !card.canAssist) throw new Error(`#${id} 不能作為武裝`);
      const level = integer(value.level, 0, card ? card.maxLevel : 120, '等級');
      const skill = integer(value.skill, 0, card ? card.maxSkill : 999, '技能等級');
      const awakenings = integer(value.awakenings, -1, card ? card.awakenings.length : 10, '覺醒數');
      if (!Array.isArray(value.plus) || value.plus.length !== 3) throw new Error('加值需要 HP／攻擊／回復三項');
      const plus = value.plus.map(p => integer(p, 0, assist ? 99 : 297, '加值'));
      if (assist && !plus.every(p => p === plus[0]) || assist && ![0,99].includes(plus[0])) throw new Error('武裝加值可選 +0 或 +297');
      if (!assist) {
        const extra = Math.max(0, Math.max(...plus) - 99);
        const bases = plus.map(p=>p-extra);
        if (bases.some(p=>p<0||p>99) || extra > bases.filter(p=>p===99).length*99)
          throw new Error('這组三項加值無法用遊戲的原生格式表示');
      }
      let superAwakening = integer(value.superAwakening, -1, 255, '超覺醒');
      const effectiveSuper = superAwakening < 0 && card ? (card.supers[0] || card.sync || 0) : superAwakening;
      if (card && !assist && effectiveSuper > 0) {
        if (!card.supers.includes(effectiveSuper) && !(card.supers.length === 0 && effectiveSuper === card.sync)) throw new Error('該角色沒有此超覺醒');
        if (effectiveSuper !== card.sync && ((level || card.maxLevel) < 100 || plus.some(p=>p<99))) throw new Error('超覺醒需要 Lv.100 以上與三項加值至少 +99');
      }
      if (!Array.isArray(value.latents) || value.latents.length > 8) throw new Error('潛覺格式不正確');
      const latents = value.latents.map(id => integer(id,1,52,'潛覺'));
      if (assist && latents.length) throw new Error('武裝不設定潛覺');
      if (card && latents.reduce((n,id)=>n+latentCost(id),0) > card.latentSlots) throw new Error('潛覺超過角色的格數');
      return {id,level,skill,awakenings,plus,superAwakening:assist ? 0 : superAwakening,latents};
    }
    function normalize(value) {
      if (!value || value.schema !== 1 || !Array.isArray(value.slots) || value.slots.length !== 6) throw new Error('隊伍需要六個位置');
      return {schema:1,title:typeof value.title === 'string' ? value.title.slice(0,80) : '我的隊伍',
        slots:value.slots.map(s=>({body:validateCard(s.body,false,true),assist:validateCard(s.assist,true,true)}))};
    }
    function selectCard(team, slot, kind, id) {
      integer(slot,0,5,'位置');
      if (!['body','assist'].includes(kind)) throw new Error('未知角色欄位');
      const candidate = clone(team), card = cards.get(id), isAssist = kind === 'assist';
      const value = emptyCard(isAssist);
      value.id = id;
      if (card && isAssist) value.level = Math.min(99,card.maxLevel);
      // A body without limit break keeps its actual cap; auto super remains off
      // when the card provides neither a super awakening nor a sync awakening.
      candidate.slots[slot][kind] = validateCard(value,isAssist,true);
      return normalize(candidate);
    }
    function readMember(row, assist, version) {
      if (row === null || row === undefined || Array.isArray(row) && !row.length) return emptyCard(assist);
      if (!Array.isArray(row) || row.length > 7) throw new Error('PADDashFormation 角色格式不支援');
      const id = integer(row[0],0,65435,'圖鑑編號'), card = cards.get(id);
      if (!id) return emptyCard(assist);
      const plus = Array.isArray(row[3]) ? row[3] : Array(3).fill(row[3] ?? 0);
      let superAwakening = row[5] ?? 0;
      if (!assist && version < 5) superAwakening = card?.supers[superAwakening] || 0;
      return validateCard({id,level:row[1] ?? 0,awakenings:row[2] ?? -1,plus,
        latents:assist ? [] : (row[4] || []),superAwakening:assist ? 0 : superAwakening,skill:row[6] ?? 0},assist,true);
    }
    function parseImport(text) {
      if (typeof text !== 'string' || text.length > 65536) throw new Error('分享資料過長');
      let content = text.trim(), region = 'ja', object;
      if (/^https?:\/\//i.test(content)) {
        const url = new URL(content);
        if (!url.searchParams.has('d')) throw new Error('連結沒有 PADDashFormation 隊伍資料');
        region = url.searchParams.get('s') || 'ja';
        content = url.searchParams.get('d');
      }
      if (region !== 'ja') throw new Error('目前只接受日服隊伍');
      try { object = JSON.parse(content); } catch (_) { throw new Error('無法讀取隊伍，請貼上完整分享連結或 JSON'); }
      if (!object || typeof object !== 'object' || Array.isArray(object)) throw new Error('隊伍資料需要 JSON 物件');
      if (object.s && object.s !== 'ja') throw new Error('目前只接受日服隊伍');
      if (object.schema === 1) return {team:normalize(object),notice:''};
      if (object.d && typeof object.d === 'object' && object.d.f) {
        if (object.s && object.s !== 'ja') throw new Error('目前只接受日服隊伍');
        object = object.d;
      }
      const version = object.v ?? 2;
      if (!Number.isInteger(version) || version < 2 || version > 5 || !Array.isArray(object.f) || object.f.length !== 1) throw new Error('請使用 PADDashFormation 的單人六人隊伍（v2–v5）');
      const [bodies,assists,badge,swap] = object.f[0];
      if (!Array.isArray(bodies) || !Array.isArray(assists) || bodies.length > 6 || assists.length > 6) throw new Error('隊伍位置超出六個');
      const team = {schema:1,title:typeof object.t === 'string' ? object.t.slice(0,80) : '匯入隊伍',
        slots:Array.from({length:6},(_,i)=>({body:readMember(bodies[i],false,version),assist:readMember(assists[i],true,version)}))};
      const omitted = [];
      if (badge) omitted.push('徽章');
      if (swap) omitted.push('入場後的隊長交換');
      if (object.r) omitted.push('關卡倍率');
      return {team:normalize(team),notice:omitted.length ? '已匯入角色設定；'+omitted.join('、')+'請在遊戲中設定。' : '已匯入六個位置的本體與武裝設定。'};
    }
    function exportLink(team) {
      team = normalize(team);
      const row = (value,assist) => {
        if (!value.id) return null;
        const c = cards.get(value.id);
        const sa = value.superAwakening < 0 ? c.supers[0] || c.sync || 0 : value.superAwakening;
        return [value.id,value.level || c.maxLevel,value.awakenings < 0 ? c.awakenings.length : value.awakenings,
          value.plus,value.latents,assist ? 0 : sa,value.skill || c.maxSkill];
      };
      const object = {v:5,t:team.title,f:[[team.slots.map(s=>row(s.body,false)),team.slots.map(s=>row(s.assist,true))]]};
      return 'https://mapaler.github.io/PADDashFormation/solo.html?s=ja&d='+encodeURIComponent(JSON.stringify(object));
    }
    function payload(team, selected = -1) {
      team = normalize(team);
      if (selected !== -1) integer(selected,0,5,'位置');
      let mask = 0;
      team.slots.forEach((s,i)=>{ if (s.body.id && (selected === -1 || selected === i)) mask |= 1<<i; });
      if (!mask) throw new Error('請先選擇本體');
      return {team,mask};
    }
    function search(query, assist) {
      query = String(query || '').normalize('NFKC').toLowerCase().trim().replace(/^#/, '');
      const matches = [...cards.values()].filter(c=>(!assist || c.canAssist) &&
        (!query || String(c.id) === query || (c.name+' '+c.aliases).toLowerCase().includes(query)));
      return matches.sort((a,b)=>Number(String(b.id)===query)-Number(String(a.id)===query) || b.id-a.id).slice(0,60);
    }
    return {cards,empty,normalize,selectCard,parseImport,exportLink,payload,search,validateCard,latentCost};
  }
  const api = {createModel,latentCost};
  if (typeof module !== 'undefined') module.exports = api;
  else root.PHFormation = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
