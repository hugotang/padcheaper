// Read-only counterpart of 0x1005FB690. Native protected getters accept a
// majority; automation requires all three copies to agree before using HP.
// No native getter, healing, cooldown or effect mutation is performed here.
bool ReadDungeonHP(uintptr_t header, uintptr_t dungeon, int32_t &policy) {
    policy = 0;
    uint32_t offset = 0;
    if (!dungeon || dungeon > UINTPTR_MAX - 128 ||
        !PHReadU32(dungeon + PHBattleDungeonRulesOffset, &offset)) return false;
    if (!offset) return true;
    uintptr_t object = 0, pool = 0;
    if (!PHReadPointer(header + PHSaveRVA + PHBattleDungeonRulePoolOffset, &object) || !object ||
        object > UINTPTR_MAX - 24 || !PHReadPointer(object + 16, &pool) || !pool ||
        pool > UINTPTR_MAX - offset || pool + offset > UINTPTR_MAX - 8192) return false;
    std::array<char, 8192> text{};
    size_t length = 0;
    bool ended = false;
    while (length < text.size() && !ended) {
        size_t count = std::min(size_t(64), text.size() - length);
        if (!PHReadMemory(pool + offset + length, text.data() + length, count)) {
            count = 1; // A valid short string may end just before a region boundary.
            if (!PHReadMemory(pool + offset + length, text.data() + length, count)) return false;
        }
        const void *end = std::memchr(text.data() + length, 0, count);
        if (end) { length = static_cast<const char *>(end) - text.data(); ended = true; }
        else length += count;
    }
    if (!ended) return false;
    NSString *options = [[NSString alloc] initWithBytes:text.data() length:length encoding:NSASCIIStringEncoding];
    if (!options) return false;
    BOOL hasFixed = NO, hasPercent = NO;
    int32_t fixed = 0, percent = 0;
    // Native looks up the first hpfix: before the first hpper:, irrespective
    // of their order in the option string. A zero hpfix: still shadows hpper:.
    for (NSString *option in [options componentsSeparatedByString:@"|"]) {
        NSRange colon = [option rangeOfString:@":"];
        if (colon.location == NSNotFound) continue;
        NSString *name = [[option substringToIndex:colon.location] lowercaseString];
        BOOL isFixed = [name isEqual:@"hpfix"], isPercent = [name isEqual:@"hpper"];
        if ((!isFixed && !isPercent) || (isFixed && hasFixed) || (isPercent && hasPercent)) continue;
        NSScanner *scanner = [NSScanner scannerWithString:[option substringFromIndex:colon.location + 1]];
        long long value = 0;
        if (![scanner scanLongLong:&value] || !scanner.isAtEnd || value < 0 || value > INT32_MAX) return false;
        if (isFixed) { fixed = int32_t(value); hasFixed = YES; }
        else { percent = int32_t(value); hasPercent = YES; }
    }
    policy = hasFixed ? fixed : hasPercent ? -percent : 0;
    return true;
}

bool ReadMaximumHP(const PHOrbSnapshot &state, uintptr_t dungeon, uint32_t key1, uint32_t key2, uint32_t &maximum) {
    const uintptr_t impl = state.identity.impl;
    int32_t policy = 0;
    if (!ReadDungeonHP(state.identity.header, dungeon, policy)) return false;
    if (policy > 0) { maximum = uint32_t(policy); return true; }
    uint16_t turns = 0;
    uint32_t modifier = 0, low = 0, high = 0;
    if (!policy) {
        if (!PHReadU16(impl + PHOrbHPModifierTurnsOffset, &turns)) return false;
        if (PositiveDuration(turns) && !Decode(impl + PHOrbHPModifierOffset, key1, key2, 0x6a875a8bu, modifier)) return false;
        if (modifier && !(modifier & 0x80000000u)) { maximum = modifier; return true; }
    }
    if (!Decode(impl + PHOrbHPBaseLowOffset, key1, key2, 0x766cfc6du, low) ||
        !Decode(impl + PHOrbHPBaseHighOffset, key1, key2, 0x766cfc6du, high) || (high & 0x80000000u)) return false;
    const uint64_t base = (uint64_t(high) << 32) | low;
    if (policy < 0) {
        // The dungeon percentage path deliberately rounds in float32.
        const float ratio = float(-int64_t(policy)) / 10000.0f;
        const float scaled = ratio * float(base);
        const float rounded = scaled + 0.5f;
        if (!std::isfinite(rounded) || rounded < 1) return false;
        maximum = double(rounded) >= INT32_MAX ? uint32_t(INT32_MAX) : uint32_t(rounded);
    } else if (modifier) {
        if (modifier == 0x80000000u) return false;
        // Temporary percentage limits use float64, a minimum of one, and
        // the INT32_MAX ceiling before native round-to-nearest conversion.
        const double scaled = (double(0x100000000ULL - modifier) / 100.0) * double(base);
        if (!std::isfinite(scaled)) return false;
        const double bounded = std::max(1.0, std::min(scaled, double(INT32_MAX)));
        maximum = uint32_t(std::min(std::floor(bounded + 0.5), double(INT32_MAX)));
    } else {
        if (high || !low || low > INT32_MAX) return false;
        maximum = low;
    }
    return maximum > 0 && maximum <= INT32_MAX;
}

void ReadSurvival(PHOrbSnapshot &state, uintptr_t dungeon, uint32_t key1, uint32_t key2, uint32_t hp) {
    auto &out = state.automation;
    out.hp = hp;
    uint32_t maximum = 0, shadow = 0;
    out.hpKnown = hp <= INT32_MAX && PHReadU32(state.identity.impl + PHBattleHPShadowOffset, &shadow) &&
        (shadow ^ hp) == 0x8085a6beu && ReadMaximumHP(state, dungeon, key1, key2, maximum) && hp <= maximum;
    out.maximumHP = out.hpKnown ? maximum : 0;
    uint16_t awakening = 0;
    uint8_t flags = 0;
    out.statusKnown = dungeon && dungeon <= UINTPTR_MAX - 34 &&
        PHReadU8(dungeon + 33, &flags) && PHReadU16(state.identity.impl + PHBattleAwakeningStateOffset, &awakening);
    if (out.statusKnown) {
        out.awakeningRestricted = (flags & 4u) != 0;
        if (PositiveDuration(awakening)) out.playerEffects |= PHOrbPlayerAwokenBind;
    }
}
