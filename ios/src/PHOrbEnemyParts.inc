// Included after the protected readers. No native getters or writes.
bool CheckEnemyPartsCode(uintptr_t header) {
    for (const auto &guard : PHOrbEnemyPartsTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) return false;
    }
    return true;
}

bool ReadEnemyPart(uintptr_t impl, unsigned count, unsigned slot, uint32_t key1, uint32_t key2,
                   PHOrbEnemyPartState &out) {
    std::memset(&out, 0, sizeof(out));
    if (count > 8 || slot >= count) return false;
    const uintptr_t enemy = impl + PHOrbEnemyOffset + slot * PHOrbEnemyStride;
    PHOrbEnemyPartState part{};
    uint32_t mode = 0, low = 0, high = 0; uint8_t owner = 0;
    if (!Decode(enemy + 2704, key1, key2, 0x6a875a8bu, mode) ||
        !PHReadU8(enemy + 36, &part.index) || !PHReadU8(enemy + 37, &owner)) return false;
    part.mode = int8_t(mode); part.owner = int8_t(owner);
    if (mode != uint8_t(part.mode) && mode != uint32_t(int32_t(part.mode))) return false;
    // 0x10002414C / 0x1004903CC: raw active bytes and ordinary HP are not
    // evidence of a part's liveness. A negative mode skips its stale HP.
    part.hp = UINT64_MAX;
    if (part.mode >= 0) {
        if (!Decode(enemy + 2672, key1, key2, 0x6a875a8bu, high) ||
            !Decode(enemy + 2688, key1, key2, 0x6a875a8bu, low)) return false;
        part.hp = (uint64_t(high) << 32) | low;
    }
    if (part.hp && part.hp <= INT64_MAX) {
        // Native skill tap counts a living part only with a living ordinary
        // owner. Validate the signed index before looking up that owner.
        if (part.owner < 0 || unsigned(part.owner) >= count || unsigned(part.owner) == slot) return false;
        const uintptr_t parent = impl + PHOrbEnemyOffset + unsigned(part.owner) * PHOrbEnemyStride;
        uint16_t flags = 0;
        if (!PHReadU16(parent + 532, &flags) || (flags & 0x100u) ||
            !Decode(parent + 64, key1, key2, 0x766cfc6du, low) ||
            !Decode(parent + 80, key1, key2, 0x766cfc6du, high)) return false;
        const uint64_t hp = (uint64_t(high) << 32) | low;
        if (!hp || hp > INT64_MAX) return false;
    }
    part.known = true;
    // Preserve initialized padding for the snapshot's bytewise race checks.
    out.hp = part.hp; out.mode = part.mode; out.owner = part.owner;
    out.index = part.index; out.known = part.known;
    return true;
}
