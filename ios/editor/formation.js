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
  // IDs verified against the Japanese latent TAMADRA catalog. Stored IDs are
  // never translated when labels change. 52 is reserved by the native range
  // but has no named TAMADRA in the supported catalog.
  const latentNames = Object.freeze({
    1:'HP 強化',2:'攻擊強化',3:'回復強化',4:'操作時間延長',5:'自動回復',
    6:'火傷害減輕',7:'水傷害減輕',8:'木傷害減輕',9:'光傷害減輕',10:'闇傷害減輕',
    11:'技能延遲耐性',12:'全能力強化',13:'隊長交換耐性',14:'邪魔掉落耐性',15:'毒掉落耐性',
    16:'進化用殺手',17:'覺醒用殺手',18:'強化合成用殺手',19:'販賣用殺手',
    20:'神殺手',21:'龍殺手',22:'惡魔殺手',23:'機械殺手',24:'平衡殺手',
    25:'攻擊殺手',26:'體力殺手',27:'回復殺手',28:'HP 強化＋',29:'攻擊強化＋',30:'回復強化＋',
    31:'操作時間延長＋',32:'火傷害減輕＋',33:'水傷害減輕＋',34:'木傷害減輕＋',35:'光傷害減輕＋',36:'闇傷害減輕＋',
    37:'傷害無效貫通',38:'屬性吸收貫通',39:'傷害吸收貫通',40:'轉轉回復',41:'消除不可回復',
    42:'傷害上限解放',43:'HP 強化＋＋',44:'攻擊強化＋＋',45:'回復強化＋＋',
    46:'雲／操作不可回復',47:'技能加速＋＋',48:'武裝無效回復',49:'傷害上限解放（高倍率）',
    50:'部位追擊',51:'無視防禦力'
  });
  const latentName = id => latentNames[id] || `未定義潛覺（ID ${id}）`;
  const awakeningNames = Object.freeze({
    0:'不啟用',1:'HP 強化',2:'攻擊強化',3:'回復強化',4:'火傷害減輕',5:'水傷害減輕',
    6:'木傷害減輕',7:'光傷害減輕',8:'闇傷害減輕',9:'自動回復',10:'封鎖耐性',
    11:'暗闇耐性',12:'邪魔耐性',13:'毒耐性',14:'火珠強化',15:'水珠強化',16:'木珠強化',
    17:'光珠強化',18:'闇珠強化',19:'操作時間延長',20:'封鎖回復',21:'技能加速',
    22:'火橫列強化',23:'水橫列強化',24:'木橫列強化',25:'光橫列強化',26:'闇橫列強化',
    27:'雙體攻擊',28:'技能封鎖耐性',29:'回復珠強化',30:'協力強化',31:'龍殺手',32:'神殺手',
    33:'惡魔殺手',34:'機械殺手',35:'平衡殺手',36:'攻擊殺手',37:'體力殺手',38:'回復殺手',
    39:'進化用殺手',40:'覺醒用殺手',41:'強化合成用殺手',42:'販賣用殺手',43:'連擊強化',
    44:'防禦破壞',45:'追加攻擊',46:'隊伍 HP 強化',47:'隊伍回復強化',48:'傷害無效貫通',
    49:'武裝覺醒',50:'超追加攻擊',51:'技能充能',52:'封鎖耐性＋',53:'操作時間延長＋',
    54:'雲耐性',55:'操作不可耐性',56:'技能加速＋',57:'HP 80％以上強化',58:'HP 50％以下強化',
    59:'L 字消除減傷',60:'L 字消除攻擊',61:'超連擊強化',62:'連擊珠',63:'語音',64:'地下城獎勵',
    65:'HP 弱化',66:'攻擊弱化',67:'回復弱化',68:'暗闇耐性＋',69:'邪魔耐性＋',70:'毒耐性＋',
    71:'邪魔珠加護',72:'毒珠加護',73:'火連珠強化',74:'水連珠強化',75:'木連珠強化',76:'光連珠強化',
    77:'闇連珠強化',78:'十字消除攻擊',79:'三色攻擊強化',80:'四色攻擊強化',81:'五色攻擊強化',
    82:'超連珠強化',83:'附加龍類型',84:'附加神類型',85:'附加惡魔類型',86:'附加機械類型',
    87:'附加平衡類型',88:'附加攻擊類型',89:'附加體力類型',90:'附加回復類型',91:'副屬性變火',
    92:'副屬性變水',93:'副屬性變木',94:'副屬性變光',95:'副屬性變闇',96:'雙體攻擊＋',
    97:'技能充能＋',98:'自動回復＋',99:'火珠強化＋',100:'水珠強化＋',101:'木珠強化＋',
    102:'光珠強化＋',103:'闇珠強化＋',104:'回復珠強化＋',105:'技能加速－',106:'浮游',
    107:'連擊強化＋',108:'L 字消除攻擊＋',109:'傷害無效貫通＋',110:'十字消除攻擊＋',
    111:'超連擊強化＋',112:'三色攻擊強化＋',113:'四色攻擊強化＋',114:'五色攻擊強化＋',
    115:'封鎖回復＋',116:'火橫列強化×3',117:'水橫列強化×3',118:'木橫列強化×3',
    119:'光橫列強化×3',120:'闇橫列強化×3',121:'火連珠強化＋',122:'水連珠強化＋',
    123:'木連珠強化＋',124:'光連珠強化＋',125:'闇連珠強化＋',126:'T 字消除攻擊',
    127:'全能力強化',128:'陽之加護',129:'陰之加護',130:'熟成',131:'部位破壞',132:'下午茶',
    133:'火水同時攻擊',134:'水木同時攻擊',135:'木火同時攻擊',136:'技能延遲耐性',137:'全珠強化',
    138:'同色武裝強化',139:'自力',140:'操作時間縮短耐性',141:'達人多色',142:'全能力強化＋',
    143:'加速',144:'15 連擊強化',145:'希望之魂',146:'勇氣之魂',147:'命運之魂',148:'武裝無效耐性',
    149:'火直列消除攻擊',150:'水直列消除攻擊',151:'木直列消除攻擊',152:'光直列消除攻擊',153:'闇直列消除攻擊'
  });
  const awakeningName = id => awakeningNames[id] || `未定義覺醒（ID ${id}）`;
  const libraryCapacity = 64;
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
    function libraryID(value) {
      if (typeof value !== 'string' || !/^[A-Za-z0-9_-]{1,80}$/.test(value)) throw new Error('隊伍識別碼無效');
      return value;
    }
    function normalizeLibrary(value) {
      if (!value || value.schema !== 1 || !Array.isArray(value.teams) || !value.teams.length ||
          value.teams.length > libraryCapacity) throw new Error(`隊伍庫需要 1 至 ${libraryCapacity} 個隊伍`);
      const selected = libraryID(value.selected), ids = new Set();
      const teams = value.teams.map(record => {
        if (!record || typeof record !== 'object' || Array.isArray(record)) throw new Error('隊伍庫記錄格式不正確');
        const id = libraryID(record.id);
        if (ids.has(id)) throw new Error('隊伍識別碼重複');
        ids.add(id);
        return {id,team:normalize(record.team)};
      });
      if (!ids.has(selected)) throw new Error('目前選取的隊伍不存在');
      return {schema:1,selected,teams};
    }
    function createLibrary(value) {
      const id = value ? 'legacy' : 'default';
      return normalizeLibrary({schema:1,selected:id,teams:[{id,team:value || empty()}]});
    }
    function selectedTeam(value) {
      const library = normalizeLibrary(value);
      return library.teams.find(record=>record.id===library.selected).team;
    }
    function updateLibraryTeam(value, draft) {
      const library = normalizeLibrary(value), team = normalize(draft);
      library.teams.find(record=>record.id===library.selected).team = team;
      return library;
    }
    function selectLibraryTeam(value, id) {
      const library = normalizeLibrary(value);
      if (!library.teams.some(record=>record.id===id)) throw new Error('要切換的隊伍不存在');
      library.selected = id;
      return library;
    }
    function addLibraryTeam(value, draft, id) {
      const library = normalizeLibrary(value);
      libraryID(id);
      if (library.teams.length >= libraryCapacity) throw new Error(`最多可儲存 ${libraryCapacity} 個隊伍`);
      if (library.teams.some(record=>record.id===id)) throw new Error('隊伍識別碼重複');
      library.teams.push({id,team:normalize(draft)});
      library.selected = id;
      return library;
    }
    function deleteLibraryTeam(value, id = value.selected) {
      const library = normalizeLibrary(value), index = library.teams.findIndex(record=>record.id===id);
      if (index < 0) throw new Error('要刪除的隊伍不存在');
      if (library.teams.length === 1) throw new Error('請至少保留一個隊伍');
      library.teams.splice(index,1);
      if (library.selected === id) library.selected = library.teams[Math.min(index,library.teams.length-1)].id;
      return library;
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
    function moveSlot(team, source, destination, copy = false) {
      integer(source,0,5,'來源位置');
      integer(destination,0,5,'目標位置');
      const result = normalize(team), original = clone(result.slots[source]);
      if (!copy) result.slots[source] = clone(result.slots[destination]);
      result.slots[destination] = original;
      return result;
    }
    function matchesRecipe(slot, recipe) {
      if (!recipe?.body?.id || !slot?.body?.id) return false;
      const effective = (value, assist) => {
        value = validateCard(value,assist,true);
        if (!value.id) return null;
        const card = cards.get(value.id);
        return {id:value.id,level:value.level || card.maxLevel,skill:value.skill || card.maxSkill,
          awakenings:value.awakenings < 0 ? card.awakenings.length : value.awakenings,
          superAwakening:assist ? 0 : value.superAwakening < 0 ? card.supers[0] || card.sync || 0 : value.superAwakening,
          plus:value.plus,latents:value.latents};
      };
      try {
        return ['body','assist'].every(kind=>JSON.stringify(effective(slot[kind],kind==='assist')) ===
          JSON.stringify(effective(recipe[kind],kind==='assist')));
      } catch (_) { return false; }
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
    return {cards,empty,normalize,selectCard,moveSlot,matchesRecipe,parseImport,exportLink,payload,search,validateCard,latentCost,
      latentName,latentNames,awakeningName,awakeningNames,libraryCapacity,createLibrary,normalizeLibrary,selectedTeam,updateLibraryTeam,
      selectLibraryTeam,addLibraryTeam,deleteLibraryTeam};
  }
  const api = {createModel,latentCost,latentName,latentNames,awakeningName,awakeningNames,libraryCapacity};
  if (typeof module !== 'undefined') module.exports = api;
  else root.PHFormation = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
