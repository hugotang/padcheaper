// Strict fixed-team plan data and pure selectors. Included by PHOrbProfiles.m.
static NSArray *FixedEffects(void) {
    return @[@"damage_absorb",@"attribute_absorb",@"damage_void",@"awoken_bind",@"member_bind",@"skill_seal",
        @"unmatchable",@"assist_void",@"spinner"];
}
static BOOL FixedEffectListValid(id effects) {
    if (![effects isKindOfClass:NSArray.class] || ![effects count] || [effects count]>FixedEffects().count) return NO;
    NSSet *set=[NSSet setWithArray:effects];
    return set.count==[effects count] && [set isSubsetOfSet:[NSSet setWithArray:FixedEffects()]];
}
static BOOL FixedEnemyStateValid(id enemies) {
    if (![enemies isKindOfClass:NSArray.class] || ![enemies count] || [enemies count]>8) return NO;
    NSMutableSet *seen=[NSMutableSet set];
    for (NSDictionary *enemy in enemies) {
        if (![enemy isKindOfClass:NSDictionary.class] ||
            !KeysWithOptional(enemy,@[@"monster"],@[@"alive",@"maximumPercent"]) ||
            !Integer(enemy[@"monster"],INT32_MAX) || ![enemy[@"monster"] unsignedIntValue] ||
            [seen containsObject:enemy[@"monster"]] ||
            (enemy[@"alive"] && !PHOrbProfileIsBoolean(enemy[@"alive"])) ||
            (enemy[@"maximumPercent"] && (!Integer(enemy[@"maximumPercent"],100) ||
                ![enemy[@"maximumPercent"] unsignedIntValue] || ![enemy[@"alive"] boolValue]))) return NO;
        [seen addObject:enemy[@"monster"]];
    }
    return YES;
}
static BOOL FixedNumber(id value, BOOL empty) {
    uint16_t catalog = 0;
    return Integer(value,UINT16_MAX) && (empty && ![value unsignedIntValue] ? YES :
        [value unsignedIntValue] && PHBattleCatalogIDForNumber([value unsignedIntValue],&catalog));
}
static BOOL FixedSkills(id value) {
    if (![value isKindOfClass:NSArray.class] || [value count] > 16) return NO;
    NSMutableSet *seen = [NSMutableSet set];
    for (id skill in value) {
        if (!Integer(skill,0x1ffff) || ![skill unsignedIntValue] || [seen containsObject:skill]) return NO;
        [seen addObject:skill];
    }
    return YES;
}
static NSDictionary *FixedMemberForm(NSDictionary *member, NSNumber *number) {
    for (NSDictionary *form in member[@"forms"]) if ([form[@"number"] isEqual:number]) return form;
    return nil;
}
static BOOL FixedActionValid(id action, NSArray *members) {
    if (![action isKindOfClass:NSDictionary.class] ||
        !PHOrbProfileIsBoolean(action[@"optional"]) ||
        ![@[@"once",@"each"] containsObject:action[@"frequency"]]) return NO;
    if ([action[@"kind"] isEqual:@"target"]) return Keys(action,@[@"kind",@"target",@"monster",@"optional",@"frequency"]) &&
        ![action[@"optional"] boolValue] && [@[@"part",@"enemy"] containsObject:action[@"target"]] && Integer(action[@"monster"],INT32_MAX);
    if (![action[@"kind"] isEqual:@"skill"] ||
        !KeysWithOptional(action,@[@"kind",@"slot",@"source",@"monster",@"skill",@"afterMonster",@"optional",@"frequency",@"when"],@[@"alternatives",@"consumeAssist",@"loop",@"optionalAfterFirst",@"requireReady"]) ||
        !Integer(action[@"slot"],6) || ![action[@"slot"] unsignedIntValue] ||
        ![@[@"body",@"assist"] containsObject:action[@"source"]] ||
        !FixedNumber(action[@"monster"],NO) || !FixedNumber(action[@"afterMonster"],YES) ||
        !Integer(action[@"skill"],0x1ffff) || ![action[@"skill"] unsignedIntValue] ||
        !([@[@"always",@"attack_ready"] containsObject:action[@"when"]] || [FixedEffects() containsObject:action[@"when"]])) return NO;
    if ((action[@"consumeAssist"] && (!PHOrbProfileIsBoolean(action[@"consumeAssist"]) || ![action[@"source"] isEqual:@"assist"])) ||
        (action[@"loop"] && (!PHOrbProfileIsBoolean(action[@"loop"]) || ![action[@"source"] isEqual:@"body"] || [action[@"afterMonster"] unsignedIntValue]))) return NO;
    if (action[@"requireReady"] && (!PHOrbProfileIsBoolean(action[@"requireReady"]) ||
        ![action[@"loop"] boolValue] || [action[@"optional"] boolValue])) return NO;
    // Only duplicate, non-transforming body actions may declare a retry skip.
    // The stage validator and runtime also require an earlier required action
    // and a fresh native acknowledgement in this very puzzle.
    if (action[@"optionalAfterFirst"] && (!PHOrbProfileIsBoolean(action[@"optionalAfterFirst"]) ||
        ![action[@"frequency"] isEqual:@"each"] || [action[@"optional"] boolValue] ||
        ![action[@"source"] isEqual:@"body"] || [action[@"afterMonster"] unsignedIntValue] ||
        [action[@"loop"] boolValue])) return NO;
    // Runtime positions can change through the explicitly declared leader swap.
    NSDictionary *member = nil, *form = nil;
    for (NSDictionary *candidate in members) if ((form = FixedMemberForm(candidate,action[@"monster"]))) { member = candidate; break; }
    if (action[@"alternatives"]) {
        if (![action[@"alternatives"] isKindOfClass:NSArray.class] || ![action[@"alternatives"] count] || [action[@"alternatives"] count] > 5 ||
            [NSSet setWithArray:action[@"alternatives"]].count != [action[@"alternatives"] count]) return NO;
        for (id slot in action[@"alternatives"]) if (!Integer(slot,6) || ![slot unsignedIntValue] || [slot isEqual:action[@"slot"]]) return NO;
    }
    if (!form || ([action[@"afterMonster"] unsignedIntValue] &&
        !FixedMemberForm(member,action[@"afterMonster"]))) return NO;
    if ([action[@"source"] isEqual:@"assist"]) return member[@"assist"] != NSNull.null && [member[@"assist"] unsignedIntValue] &&
        ![action[@"afterMonster"] unsignedIntValue] && [member[@"assistSkills"] containsObject:action[@"skill"]];
    return [form[@"skills"] containsObject:action[@"skill"]];
}
static BOOL FixedUntilValid(id until) {
    if (![until isKindOfClass:NSDictionary.class]) return NO;
    NSString *kind = until[@"kind"];
    if ([@[@"puzzles",@"floor_change",@"parts_absent"] containsObject:kind]) return Keys(until,@[@"kind"]);
    if ([kind isEqual:@"skill_ready"]) return Keys(until,@[@"kind",@"slot",@"monster",@"skill"]) &&
        Integer(until[@"slot"],6) && [until[@"slot"] unsignedIntValue] && FixedNumber(until[@"monster"],NO) &&
        Integer(until[@"skill"],0x1ffff) && [until[@"skill"] unsignedIntValue];
    if ([kind isEqual:@"enemy_hp_at_most"]) return Keys(until,@[@"kind",@"percent"]) &&
        Integer(until[@"percent"],100) && [until[@"percent"] unsignedIntValue];
    if ([kind isEqual:@"enemy_state_or_floor_change"]) return Keys(until,@[@"kind",@"enemies"]) && FixedEnemyStateValid(until[@"enemies"]);
    return [kind isEqual:@"effects_clear"] && Keys(until,@[@"kind",@"effects"]) && FixedEffectListValid(until[@"effects"]);
}
static BOOL FixedRepeatValid(id repeat) {
    return [repeat isKindOfClass:NSDictionary.class] && Keys(repeat,@[@"minimumPuzzles",@"maximumPuzzles",@"until"]) &&
        Integer(repeat[@"minimumPuzzles"],999) &&
        Integer(repeat[@"maximumPuzzles"],999) &&
        [repeat[@"maximumPuzzles"] unsignedIntValue] &&
        [repeat[@"maximumPuzzles"] unsignedIntValue] >= [repeat[@"minimumPuzzles"] unsignedIntValue] &&
        FixedUntilValid(repeat[@"until"]) &&
        (![[repeat[@"until"] objectForKey:@"kind"] isEqual:@"puzzles"] || [repeat[@"minimumPuzzles"] unsignedIntValue]);
}
BOOL PHOrbFixedPlanSameSkill(NSDictionary *a, NSDictionary *b) {
    return [a[@"kind"] isEqual:@"skill"] && [b[@"kind"] isEqual:@"skill"] &&
        [a[@"monster"] isEqual:b[@"monster"]] && [a[@"skill"] isEqual:b[@"skill"]] &&
        [a[@"source"] isEqual:b[@"source"]] && [a[@"slot"] isEqual:b[@"slot"]] &&
        [(a[@"alternatives"] ?: @[]) isEqual:(b[@"alternatives"] ?: @[])];
}
BOOL PHOrbFixedPlanValid(id plan) {
    if (![plan isKindOfClass:NSDictionary.class] || !KeysWithOptional(plan,@[@"schema",@"members",@"floors"],@[@"leaderSwap"]) ||
        !Integer(plan[@"schema"],1) || ![plan[@"schema"] isEqual:@1] ||
        ![plan[@"members"] isKindOfClass:NSArray.class] || [plan[@"members"] count] != 6 ||
        ![plan[@"floors"] isKindOfClass:NSArray.class] || ![plan[@"floors"] count] || [plan[@"floors"] count] > 99) return NO;
    if (plan[@"leaderSwap"] && (!Integer(plan[@"leaderSwap"],5) || [plan[@"leaderSwap"] unsignedIntValue] < 2)) return NO;
    for (NSDictionary *member in plan[@"members"]) {
        if (![member isKindOfClass:NSDictionary.class] || !Keys(member,@[@"body",@"assist",@"forms",@"assistSkills"]) ||
            !FixedNumber(member[@"body"],NO) || !(member[@"assist"] == NSNull.null || FixedNumber(member[@"assist"],YES)) || !FixedSkills(member[@"assistSkills"]) ||
            ((member[@"assist"] == NSNull.null || ![member[@"assist"] unsignedIntValue]) && [member[@"assistSkills"] count]) ||
            ![member[@"forms"] isKindOfClass:NSArray.class] || ![member[@"forms"] count] || [member[@"forms"] count] > 16) return NO;
        NSMutableSet *numbers = [NSMutableSet set];
        for (NSDictionary *form in member[@"forms"]) {
            if (![form isKindOfClass:NSDictionary.class] || !Keys(form,@[@"number",@"skills"]) ||
                !FixedNumber(form[@"number"],NO) || !FixedSkills(form[@"skills"]) || [numbers containsObject:form[@"number"]]) return NO;
            [numbers addObject:form[@"number"]];
        }
        if (![member[@"forms"][0][@"number"] isEqual:member[@"body"]]) return NO;
    }
    NSUInteger expected = 1, totalStages = 0, totalActions = 0;
    for (NSDictionary *floor in plan[@"floors"]) {
        if (![floor isKindOfClass:NSDictionary.class] || !Keys(floor,@[@"number",@"variants"]) ||
            !Integer(floor[@"number"],99) || [floor[@"number"] unsignedIntegerValue] != expected++ ||
            ![floor[@"variants"] isKindOfClass:NSArray.class] || ![floor[@"variants"] count] || [floor[@"variants"] count] > 2) return NO;
        NSMutableSet *variants = [NSMutableSet set];
        for (NSDictionary *variant in floor[@"variants"]) {
            if (![variant isKindOfClass:NSDictionary.class] || !Keys(variant,@[@"kind",@"stages"]) ||
                ![@[@"ordinary",@"parts",@"any"] containsObject:variant[@"kind"]] || [variants containsObject:variant[@"kind"]] ||
                ![variant[@"stages"] isKindOfClass:NSArray.class] || ![variant[@"stages"] count] || [variant[@"stages"] count] > 24) return NO;
            [variants addObject:variant[@"kind"]]; NSMutableSet *stageIDs = [NSMutableSet set];
            NSUInteger stageIndex = 0;
            for (NSDictionary *stage in variant[@"stages"]) {
                if (++totalStages > 256 || ![stage isKindOfClass:NSDictionary.class] ||
                    !KeysWithOptional(stage,@[@"id",@"actions",@"board",@"repeat"],@[@"absorbStall",@"retryRequiresBypass",@"requireEnemyState",@"requireEffectsClear",@"holdLoops"]) ||
                    (stage[@"absorbStall"] && !PHOrbProfileIsBoolean(stage[@"absorbStall"])) || !Identifier(stage[@"id"],NO) ||
                    [stageIDs containsObject:stage[@"id"]] || ![stage[@"actions"] isKindOfClass:NSArray.class] ||
                    [stage[@"actions"] count] > 24 || !FixedRepeatValid(stage[@"repeat"])) return NO;
                [stageIDs addObject:stage[@"id"]];
                if ((stage[@"requireEnemyState"] && !FixedEnemyStateValid(stage[@"requireEnemyState"])) ||
                    (stage[@"requireEffectsClear"] && !FixedEffectListValid(stage[@"requireEffectsClear"]))) return NO;
                if (stage[@"holdLoops"]) {
                    NSArray *slots=stage[@"holdLoops"];
                    if (![slots isKindOfClass:NSArray.class] || !slots.count || slots.count>6 ||
                        [NSSet setWithArray:slots].count!=slots.count) return NO;
                    for (id slot in slots) if (!Integer(slot,6) || ![slot unsignedIntValue]) return NO;
                }
                NSDictionary *until = stage[@"repeat"][@"until"];
                if (stage[@"retryRequiresBypass"] && (!PHOrbProfileIsBoolean(stage[@"retryRequiresBypass"]) ||
                    ![until[@"kind"] isEqual:@"floor_change"] ||
                    [stage[@"repeat"][@"minimumPuzzles"] unsignedIntValue]<1 ||
                    [stage[@"repeat"][@"maximumPuzzles"] unsignedIntValue]<2 || [stage[@"absorbStall"] boolValue])) return NO;
                if ([until[@"kind"] isEqual:@"skill_ready"]) {
                    BOOL declared = NO;
                    for (NSDictionary *member in plan[@"members"]) {
                        NSDictionary *form = FixedMemberForm(member,until[@"monster"]);
                        declared |= [form[@"skills"] containsObject:until[@"skill"]];
                    }
                    if (!declared) return NO;
                }
                NSDictionary *board = stage[@"board"];
                if (![board isKindOfClass:NSDictionary.class] || !KeysWithOptional(board,@[@"policy",@"assistRecoveryL",@"maxGestureMS"],@[@"lColors",@"unmatchableRecovery"]) ||
                    ![@[@"one_combo",@"six_colors_no_fours",@"six_colors_fours",@"six_colors",@"l_only"] containsObject:board[@"policy"]] ||
                    !PHOrbProfileIsBoolean(board[@"assistRecoveryL"]) || !Integer(board[@"maxGestureMS"],4500)) return NO;
                if (board[@"unmatchableRecovery"] && (!PHOrbProfileIsBoolean(board[@"unmatchableRecovery"]) ||
                    ![board[@"policy"] isEqual:@"six_colors_fours"] || [board[@"assistRecoveryL"] boolValue] ||
                    board[@"lColors"] || [stage[@"absorbStall"] boolValue] ||
                    [floor[@"number"] unsignedIntValue]!=2)) return NO;
                if (([board[@"policy"] isEqual:@"l_only"] && !board[@"lColors"]) ||
                    (board[@"lColors"] && (!Integer(board[@"lColors"],31) || ![board[@"lColors"] unsignedIntValue])) ||
                    (board[@"lColors"] && [@[@"one_combo",@"six_colors_no_fours"] containsObject:board[@"policy"]])) return NO;
                NSMutableArray *required = [NSMutableArray array];
                for (NSDictionary *action in stage[@"actions"]) {
                    if (++totalActions > 1024 || !FixedActionValid(action,plan[@"members"])) return NO;
                    if ([action[@"optionalAfterFirst"] boolValue]) {
                        BOOL prior = NO;
                        for (NSDictionary *a in required) prior |= PHOrbFixedPlanSameSkill(a,action);
                        if (!prior || ![stage[@"retryRequiresBypass"] boolValue]) return NO;
                    } else if ([action[@"kind"] isEqual:@"skill"] && ![action[@"optional"] boolValue] &&
                        [action[@"frequency"] isEqual:@"each"] && ![action[@"afterMonster"] unsignedIntValue] &&
                        [action[@"when"] isEqual:@"always"]) [required addObject:action];
                }
                if (++stageIndex == [variant[@"stages"] count] && ![stage[@"repeat"][@"until"][@"kind"] isEqual:@"floor_change"]) return NO;
            }
        }
        if ([variants containsObject:@"any"] && variants.count > 1) return NO;
        if (![variants containsObject:@"any"] &&
            !(variants.count == 2 && [variants containsObject:@"ordinary"] && [variants containsObject:@"parts"])) return NO;
    }
    return YES;
}
BOOL PHOrbFixedPlanMatchesTeamOrder(NSDictionary *plan, const PHOrbSnapshot *state, BOOL swapped) {
    if (!PHOrbFixedPlanValid(plan) || !state || !state->valid || !state->present || !state->automation.valid ||
        state->multiplayerDisabled || !PHBattleIsSinglePlayer(state->identity.entryMode,state->identity.multiplayer) ||
        state->identity.sourceMode != 0) return NO;
    for (unsigned slot = 0; slot < 6; ++slot) {
        const PHOrbSkillMember *live = &state->automation.members[slot];
        unsigned swap = [plan[@"leaderSwap"] unsignedIntValue];
        if (swapped && !swap) return NO;
        unsigned source = swapped && slot == 0 ? swap-1 : swapped && slot == swap-1 ? 0 : slot;
        NSDictionary *member = plan[@"members"][source]; uint16_t body = 0, assist = 0;
        PHBattleCatalogIDForNumber([member[@"body"] unsignedIntValue],&body);
        const BOOL nativeAssist = member[@"assist"] == NSNull.null;
        if (!nativeAssist && [member[@"assist"] unsignedIntValue]) PHBattleCatalogIDForNumber([member[@"assist"] unsignedIntValue],&assist);
        if (!live->active || !live->temporaryKnown || live->temporaryTurns ||
            live->sourceCatalogIDs[0] != body || (!nativeAssist && live->sourceCatalogIDs[1] != assist) ||
            !FixedMemberForm(member,@(live->number))) return NO;
        if (nativeAssist) continue; // Session pins all observed sources when B1 is bound.
        if (live->assistCatalogID != assist) {
            if (!assist || live->assistCatalogID) return NO;
            if (PHBattleAssistIsRetained(live->assistState) &&
                (!state->assistRecovery.valid || !state->assistRecovery.turns ||
                 state->assistRecovery.sourceAssists[slot]!=assist || !(state->assistRecovery.retainedAssists&(1u<<slot)))) return NO;
        }
    }
    return YES;
}
BOOL PHOrbFixedPlanMatchesTeam(NSDictionary *plan, const PHOrbSnapshot *state) {
    return PHOrbFixedPlanMatchesTeamOrder(plan,state,NO) || (plan[@"leaderSwap"] && PHOrbFixedPlanMatchesTeamOrder(plan,state,YES));
}
NSDictionary *PHOrbFixedPlanVariantForFloor(NSDictionary *plan, NSUInteger wave, const PHOrbSnapshot *state) {
    if (!PHOrbFixedPlanValid(plan) || !state || !state->automation.valid || !state->automation.enemiesKnown ||
        wave >= [plan[@"floors"] count] || wave != state->wave) return nil;
    NSArray *variants = plan[@"floors"][wave][@"variants"];
    for (NSDictionary *variant in variants) if ([variant[@"kind"] isEqual:@"any"]) return variant;
    for (unsigned i = 0; i < 8; ++i) if ((state->automation.partsMask & (1u<<i)) && !state->automation.parts[i].known) return nil;
    NSString *kind = state->automation.partsMask ? @"parts" : @"ordinary";
    for (NSDictionary *variant in variants) if ([variant[@"kind"] isEqual:kind]) return variant;
    return nil;
}
NSDictionary *PHOrbFixedPlanStage(NSDictionary *plan, NSUInteger wave, NSString *variant, NSUInteger index) {
    if (!PHOrbFixedPlanValid(plan) || wave >= [plan[@"floors"] count]) return nil;
    for (NSDictionary *entry in plan[@"floors"][wave][@"variants"]) if ([entry[@"kind"] isEqual:variant])
        return index < [entry[@"stages"] count] ? entry[@"stages"][index] : nil;
    return nil;
}
PHOrbSkillRequest PHOrbFixedPlanActionRequest(NSDictionary *action) {
    return (PHOrbSkillRequest){[action[@"source"] isEqual:@"assist"] ? PHOrbSkillAssist : PHOrbSkillBody,
        [action[@"skill"] unsignedIntValue]};
}
BOOL PHOrbFixedPlanActionMatchesTeam(NSDictionary *action, const PHOrbSnapshot *state) {
    if (![action[@"kind"] isEqual:@"skill"] || !Integer(action[@"slot"],6) || ![action[@"slot"] unsignedIntValue] ||
        !state || !state->automation.valid) return NO;
    const PHOrbSkillMember *member = &state->automation.members[[action[@"slot"] unsignedIntegerValue]-1];
    return member->active && member->temporaryKnown && !member->temporaryTurns &&
        member->number == [action[@"monster"] unsignedIntValue];
}
static PHOrbFixedPlanMatch FixedEffectPresent(NSString *effect, const PHOrbFixedPlanEvidence *e, BOOL covered) {
    if (!e) return PHOrbFixedPlanUnknown;
    unsigned enemyFlag = [effect isEqual:@"damage_absorb"] ? PHOrbEnemyDamageAbsorb :
        [effect isEqual:@"attribute_absorb"] ? PHOrbEnemyAttributeAbsorb : [effect isEqual:@"damage_void"] ? PHOrbEnemyDamageVoid : 0;
    if (enemyFlag) return !e->enemiesKnown ? PHOrbFixedPlanUnknown :
        (e->enemyEffects & enemyFlag) && !(covered && (e->playerBypass & enemyFlag)) ? PHOrbFixedPlanYes : PHOrbFixedPlanNo;
    unsigned playerFlag = [effect isEqual:@"awoken_bind"] ? PHOrbPlayerAwokenBind :
        [effect isEqual:@"member_bind"] ? PHOrbPlayerMemberBind : [effect isEqual:@"skill_seal"] ? PHOrbPlayerSkillSeal : 0;
    if (playerFlag) return !e->statusKnown ? PHOrbFixedPlanUnknown : e->playerEffects & playerFlag ? PHOrbFixedPlanYes : PHOrbFixedPlanNo;
    if ([effect isEqual:@"assist_void"]) return !e->assistVoidKnown ? PHOrbFixedPlanUnknown : e->assistVoidTurns ? PHOrbFixedPlanYes : PHOrbFixedPlanNo;
    if ([effect isEqual:@"unmatchable"]) return !e->unmatchableKnown ? PHOrbFixedPlanUnknown : e->unmatchableTurns ? PHOrbFixedPlanYes : PHOrbFixedPlanNo;
    if ([effect isEqual:@"spinner"]) return !e->spinnerKnown ? PHOrbFixedPlanUnknown : e->spinnerTurns ? PHOrbFixedPlanYes : PHOrbFixedPlanNo;
    return PHOrbFixedPlanUnknown;
}
PHOrbFixedPlanMatch PHOrbFixedPlanActionCondition(NSDictionary *action, const PHOrbFixedPlanEvidence *evidence) {
    if ([action[@"kind"] isEqual:@"target"] || [action[@"when"] isEqual:@"always"]) return PHOrbFixedPlanYes;
    if ([action[@"when"] isEqual:@"attack_ready"]) return !evidence || !evidence->enemiesKnown ? PHOrbFixedPlanUnknown :
        (evidence->enemyEffects & ~evidence->playerBypass & (PHOrbEnemyDamageAbsorb|PHOrbEnemyAttributeAbsorb)) ? PHOrbFixedPlanNo : PHOrbFixedPlanYes;
    return FixedEffectPresent(action[@"when"],evidence,YES);
}
PHOrbFixedPlanMatch PHOrbFixedPlanEnemyState(NSArray *enemies, const PHOrbFixedPlanEvidence *e) {
    if (!FixedEnemyStateValid(enemies) || !e || !e->enemiesKnown || !e->enemyCount || e->enemyCount>8) return PHOrbFixedPlanUnknown;
    const unsigned mask=(1u<<e->enemyCount)-1u;
    if ((e->identityKnownMask&mask)!=mask || (e->hpKnownMask&mask)!=mask) return PHOrbFixedPlanUnknown;
    // This is an exact roster of bodies, including the dead. An extra enemy,
    // a part, or a duplicate ID must never stand in for a missing identity.
    if (e->enemyCount!=enemies.count || e->bodyMask!=mask) return PHOrbFixedPlanNo;
    unsigned matched=0; BOOL satisfied=YES;
    for (unsigned i=0;i<e->enemyCount;++i) {
        if (!e->enemyMaximumHP[i] || e->enemyHP[i]>e->enemyMaximumHP[i]) return PHOrbFixedPlanUnknown;
        NSUInteger index=0;
        for (NSDictionary *enemy in enemies) {
            if ([enemy[@"monster"] unsignedIntValue]==e->enemyIDs[i]) {
                if (matched&(1u<<index)) return PHOrbFixedPlanNo;
                matched|=1u<<index;
                if (enemy[@"alive"]) satisfied &= (e->enemyHP[i]>0)==[enemy[@"alive"] boolValue];
                if (enemy[@"maximumPercent"]) {
                    const uint64_t max=e->enemyMaximumHP[i];
                    const unsigned percent=[enemy[@"maximumPercent"] unsignedIntValue];
                    satisfied &= e->enemyHP[i]<=max/100u*percent+max%100u*percent/100u;
                }
                break;
            }
            ++index;
        }
        if (index==enemies.count) return PHOrbFixedPlanNo;
    }
    return satisfied && matched==mask ? PHOrbFixedPlanYes : PHOrbFixedPlanNo;
}
PHOrbFixedPlanMatch PHOrbFixedPlanRepeatSatisfied(NSDictionary *repeat, NSUInteger puzzles, NSUInteger floorWave,
    const PHOrbFixedPlanEvidence *e) {
    if (!FixedRepeatValid(repeat) || !e) return PHOrbFixedPlanUnknown;
    if (puzzles < [repeat[@"minimumPuzzles"] unsignedIntegerValue]) return PHOrbFixedPlanNo;
    NSDictionary *until = repeat[@"until"]; NSString *kind = until[@"kind"];
    if ([kind isEqual:@"puzzles"]) return PHOrbFixedPlanYes;
    if ([kind isEqual:@"floor_change"]) return e->wave != floorWave ? PHOrbFixedPlanYes : PHOrbFixedPlanNo;
    if ([kind isEqual:@"enemy_state_or_floor_change"]) return e->wave==floorWave ?
        PHOrbFixedPlanEnemyState(until[@"enemies"],e) : e->wave==floorWave+1 ? PHOrbFixedPlanYes : PHOrbFixedPlanNo;
    if ([kind isEqual:@"parts_absent"]) return !e->partsKnown ? PHOrbFixedPlanUnknown : e->partsMask ? PHOrbFixedPlanNo : PHOrbFixedPlanYes;
    if ([kind isEqual:@"skill_ready"]) {
        const unsigned mask = 1u << ([until[@"slot"] unsignedIntValue]-1);
        return !(e->readinessKnownSlots & mask) ? PHOrbFixedPlanUnknown : (e->readySlots & mask) ? PHOrbFixedPlanYes : PHOrbFixedPlanNo;
    }
    if ([kind isEqual:@"enemy_hp_at_most"]) {
        if (!e->enemiesKnown || !e->aliveMask || (e->hpKnownMask & e->aliveMask) != e->aliveMask) return PHOrbFixedPlanUnknown;
        for (unsigned i = 0; i < 8; ++i) if (e->aliveMask & (1u<<i)) {
            if (!e->enemyMaximumHP[i] || e->enemyHP[i] > e->enemyMaximumHP[i]) return PHOrbFixedPlanUnknown;
            // Split the integer ratio to avoid overflow or floating-point
            // rounding at the exact native HP boundary.
            const uint64_t maximum = e->enemyMaximumHP[i];
            const unsigned percent = [until[@"percent"] unsignedIntValue];
            const uint64_t threshold = maximum / 100u * percent + maximum % 100u * percent / 100u;
            if (e->enemyHP[i] > threshold) return PHOrbFixedPlanNo;
        }
        return PHOrbFixedPlanYes;
    }
    BOOL unknown = NO;
    for (NSString *effect in until[@"effects"]) {
        PHOrbFixedPlanMatch match = FixedEffectPresent(effect,e,YES);
        if (match == PHOrbFixedPlanYes) return PHOrbFixedPlanNo;
        unknown |= match == PHOrbFixedPlanUnknown;
    }
    return unknown ? PHOrbFixedPlanUnknown : PHOrbFixedPlanYes;
}
NSDictionary *PHOrbFixedPlanFromPackage(id package, NSString **reason) {
    if (reason) *reason = @"固定隊伍計畫格式、來源身分或樓層步驟無效；未匯入";
    if (![package isKindOfClass:NSDictionary.class] || !Keys(package,@[@"format",@"schema",@"target",@"profile"]) ||
        ![package[@"format"] isEqual:@"padcheaper-fixed-plan"] || !Integer(package[@"schema"],1) ||
        ![package[@"schema"] isEqual:@1] || !PHDataTargetIsSupported(package[@"target"]) ||
        ![package[@"profile"] isKindOfClass:NSDictionary.class] || !package[@"profile"][@"fixedPlan"] ||
        !PHOrbProfilesValid(@{@"schema":@1,@"selected":@"",@"autoSelect":@NO,@"profiles":@[package[@"profile"]]})) return nil;
    NSMutableDictionary *profile = [package[@"profile"] mutableCopy]; profile[@"settings"] = PHOrbCanonicalSettings(profile[@"settings"]);
    NSData *json = [NSJSONSerialization dataWithJSONObject:profile options:0 error:NULL];
    if (reason) *reason = nil;
    return json ? [NSJSONSerialization JSONObjectWithData:json options:0 error:NULL] : nil;
}
NSDictionary *PHOrbFixedPlanPackageFromProfile(NSDictionary *profile) {
    if (!profile[@"fixedPlan"] || !PHOrbProfilesValid(@{@"schema":@1,@"selected":@"",@"autoSelect":@NO,@"profiles":@[profile]})) return nil;
    return @{@"format":@"padcheaper-fixed-plan",@"schema":@1,@"target":@(PHTargetID),@"profile":profile};
}
NSString *PHOrbFixedPlanDescription(NSDictionary *profile) {
    if (!profile[@"fixedPlan"] || !PHOrbFixedPlanValid(profile[@"fixedPlan"])) return @"固定隊伍計畫無效";
    NSMutableArray *lines = [NSMutableArray arrayWithObjects:profile[@"name"] ?: @"固定隊伍計畫",
        @"單人原生固定隊伍；不套隊、不入場、不重打。每層依序確認技能及轉珠後才推進。",nil];
    for (NSDictionary *floor in profile[@"fixedPlan"][@"floors"]) for (NSDictionary *variant in floor[@"variants"])
        for (NSDictionary *stage in variant[@"stages"]) [lines addObject:[NSString stringWithFormat:@"B%@ · %@ · %@：%@，至少 %@ 盤，上限 %@ 盤",
            floor[@"number"],variant[@"kind"],stage[@"id"],stage[@"board"][@"policy"],stage[@"repeat"][@"minimumPuzzles"],stage[@"repeat"][@"maximumPuzzles"]]];
    return [lines componentsJoinedByString:@"\n"];
}
