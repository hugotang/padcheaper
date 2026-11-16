- (BOOL)readFixedPlan:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (!state) return NO;
    state->fixedPlan={};
    PHOrbSnapshot first=*state, second{};
    if (!NSThread.isMainThread || _staged || _multiplayerDisabled || !frame.ready || !frame.enabled ||
        frame.header!=state->identity.header || _verifiedHeader!=frame.header || !CheckFixedCode(frame.header) ||
        !ReadFixedFields(first) || ![self readAutomation:&second frame:frame reason:reason] ||
        !PHOrbSameAutomation(state,&second) || !ReadFixedFields(second) ||
        std::memcmp(&first.fixedPlan,&second.fixedPlan,sizeof(first.fixedPlan))) {
        if (reason) *reason=@"固定計畫：原生敵人／HP／目標資料未通過前後校驗";
        return NO;
    }
    state->fixedPlan=second.fixedPlan; return YES;
}
- (BOOL)stageFixedPlanTarget:(NSUInteger)slot expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason {
    PHOrbSnapshot live{};
    if (!expected || slot>=8 || !NSThread.isMainThread || _staged || _multiplayerDisabled ||
        ![self readAutomation:&live frame:frame reason:reason] || !PHOrbSameAutomation(expected,&live) ||
        ![self readFixedPlan:&live frame:frame reason:reason] || std::memcmp(&expected->fixedPlan,&live.fixedPlan,sizeof(live.fixedPlan)) ||
        !live.fixedPlan.targetAllowed || slot>=live.fixedPlan.count || !live.fixedPlan.enemies[slot].targetable ||
        live.fixedPlan.target==int(slot)) return NO;
    const auto e=live.fixedPlan.enemies[slot];
    // Native hit testing scans back-to-front. Refuse overlapping later targets.
    for (unsigned i=unsigned(slot)+1;i<live.fixedPlan.count;++i) {
        const auto &other=live.fixedPlan.enemies[i];
        if (other.targetable && std::abs(e.targetX-other.targetX)<=other.targetWidth/2 &&
            std::abs(e.targetY-other.targetY)<=other.targetHeight/2) return NO;
    }
    uintptr_t real=0; float raw[2];
    if (!PHReadPointer(frame.header+PHBattleInputRVA,&real) || real!=live.inputManager || (real&7u) ||
        !PHMemoryHasProtection(real,PHOrbInputBytes,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_EXECUTE) ||
        !RawPoint(frame.header,e.targetX,e.targetY,raw)) return NO;
    PHOrbSnapshot final{};
    if (![self readAutomation:&final frame:frame reason:reason] || !PHOrbSameAutomation(&live,&final) ||
        ![self readFixedPlan:&final frame:frame reason:reason] || std::memcmp(&live.fixedPlan,&final.fixedPlan,sizeof(final.fixedPlan))) return NO;
    _manager.fill(0);
    if (!PHReadMemory(real+2664,_manager.data()+2664,8)) return NO;
    Store<float>(_manager.data()+2648,-1); Store<float>(_manager.data()+2652,-1);
    Store<double>(_manager.data()+232,CFAbsoluteTimeGetCurrent()); Store<double>(_manager.data()+240,CFAbsoluteTimeGetCurrent()+3600);
    Store<float>(_manager.data()+224,raw[0]); Store<float>(_manager.data()+228,raw[1]);
    Store<float>(_manager.data()+4,raw[0]); Store<float>(_manager.data()+8,raw[1]);
    Store<float>(_manager.data()+36,e.targetX); Store<float>(_manager.data()+40,e.targetY);
    Store<float>(_manager.data()+176,e.targetX); Store<float>(_manager.data()+180,e.targetY);
    Store<uint32_t>(_manager.data()+208,4); _manager[213]=_manager[214]=_manager[2640]=1;
    _header=frame.header; _realManager=real;
    uintptr_t input=reinterpret_cast<uintptr_t>(_manager.data());
    if (!PHWriteData(frame.header+PHBattleInputRVA,&input,sizeof(input))) return NO;
    _staged=true; return YES;
}
