#!/usr/bin/env python3
"""Emit the battle descriptor; reject incomplete fingerprints and invalid RVAs."""
import hashlib
import json
from pathlib import Path
import re


def generate(manifest):
    if manifest["schema"] != 1 or manifest["target_id"] != "jp.gungho.pad-ios-23.6.0-106878":
        raise ValueError("Unsupported battle target")
    if manifest["gate"] != {"mode": 0, "source_mode": 0, "multiplayer": 0, "player_input_phase": 2}:
        raise ValueError("Battle gate differs from the reviewed prototype")
    lines = ["// Generated from ios/targets/battle-23.6.0.json. Do not edit.", "#pragma once",
             "#include <stdint.h>", "#include <stddef.h>"]
    for name, value in manifest["fields"].items():
        if not re.fullmatch(r"PHBattle[A-Za-z0-9]+", name):
            raise ValueError("Invalid field name")
        number = int(value, 0)
        if not 0 < number < 0x3000000:
            raise ValueError(f"Invalid field {name}")
        lines.append(f"static const uintptr_t {name} = 0x{number:x}u;")
    required = {"PHBattleSet16RVA", "PHBattleRebuildMemberRVA", "PHBattleRefreshTeamRVA",
                "PHBattleDigestRVA", "PHBattleMaxChargeRVA", "PHBattleRadiusRVA", "PHBattleDecode32RVA",
                "PHBattleSet32RVA", "PHBattleDismissDialogRVA", "PHBattleWaitTimerRVA"}
    guards = manifest["guards"]
    if len(guards) != len(required) or {g["name"] for g in guards} != required:
        raise ValueError("Incomplete or duplicate battle call guards")
    for guard in guards:
        rva = int(guard["rva"], 0)
        if int(guard["va"], 0) - 0x100000000 != rva or not 0x4000 <= rva < 0x1110000:
            raise ValueError("Invalid call RVA")
        raw = bytes.fromhex(guard["bytes"])
        if len(raw) != guard["length"] or len(raw) != 64 or hashlib.sha256(raw).hexdigest() != guard["sha256"]:
            raise ValueError("Invalid call fingerprint")
        lines.append(f"static const uintptr_t {guard['name']} = 0x{rva:x}u;")
    lines += ["typedef struct { uintptr_t rva; uint8_t bytes[64]; } PHBattleTextGuard;",
              "static const PHBattleTextGuard PHBattleTextGuards[] = {"]
    for guard in guards:
        raw = bytes.fromhex(guard["bytes"])
        lines.append("    {0x%xu, {%s}}," % (int(guard["rva"], 0), ", ".join(f"0x{x:02x}" for x in raw)))
    lines += ["};", "static const size_t PHBattleTextGuardCount = sizeof(PHBattleTextGuards) / sizeof(PHBattleTextGuards[0]);"]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    manifest = json.loads(Path("ios/targets/battle-23.6.0.json").read_text(encoding="utf-8"))
    output = Path("ios/generated/PHBattleGenerated.h")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(generate(manifest), encoding="utf-8", newline="\n")
    print(f"Generated {output}")
