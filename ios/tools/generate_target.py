#!/usr/bin/env python3
"""Validate the static target manifest and emit the small runtime descriptor."""

import argparse
import json
from pathlib import Path
import uuid


def integer(value):
    return int(value, 0) if isinstance(value, str) else value


def generate(manifest):
    target = manifest["target"]
    base = integer(target["preferred_image_base"])
    if target["architecture"] != "arm64" or integer(target["cpu_subtype"]) != 0:
        raise ValueError("Only the analyzed arm64 target is supported")
    fields = {field["name"]: field for field in manifest["globals"]["save_fields"]}
    symbols = {
        "PHSaveRVA": manifest["globals"]["save_object"]["rva"],
        "PHGameMainRVA": manifest["globals"]["game_main"]["rva"],
        "PHSceneTableRVA": manifest["globals"]["scene_table"]["rva"],
        "PHPlayerIDOffset": fields["player_id_candidate"]["save_offset"],
        "PHPrimaryDecksOffset": fields["primary_decks"]["save_offset"],
        "PHSecondaryDecksOffset": fields["secondary_decks"]["save_offset"],
        "PHPrimaryCountOffset": fields["primary_deck_count"]["save_offset"],
        "PHSecondaryCountOffset": fields["secondary_deck_count"]["save_offset"],
        "PHMaxDecksOffset": fields["max_decks"]["save_offset"],
        "PHSkinOwnedOffset": fields["skin_owned_bitmap"]["save_offset"],
        "PHBGMOwnedOffset": fields["bgm_owned_bitmap"]["save_offset"],
        "PHEntryCountOffset": fields["skin_bgm_entry_count"]["save_offset"],
        "PHEntryPointerOffset": fields["skin_bgm_entry_pointer"]["save_offset"],
        "PHTimerImplOffset": manifest["structures"]["timer"]["game_main_impl_pointer_offset"],
        "PHTimerFlagsOffset": manifest["structures"]["timer"]["flags_impl_offset"],
        # Additional IDA-verified request-pump fields, recorded in runtime notes.
        "PHAPIContextPointerOffset": "0xf65638",
        "PHAPIActiveRequestOffset": "0xe08",
        "PHAPIErrorOffset": "0x101748",
        "PHNetworkStateRVA": "0x2ae8ee8",
        "PHNetworkPhaseRVA": "0x2ae8eec",
        "PHEditDirtyOffset": "0x5305ac",
        "PHSaveDecksRequestID": manifest["restore"]["request_ids"]["save_decks"],
    }
    symbols.update(manifest["music_playback"]["symbols"])
    for field in fields.values():
        if integer(field["va"]) - base != integer(field["rva"]):
            raise ValueError(f"RVA mismatch: {field['name']}")
    functions = {function["role"]: function for function in manifest["functions"]}
    symbols["PHDrawFrameRVA"] = functions["view_draw_frame"]["rva"]
    symbols["PHRestoreBackgroundRVA"] = functions["view_restore_from_background"]["rva"]
    lines = [
        "// Generated from ios/targets/pad-23.6.0.json. Do not edit.",
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
        f'static const char PHTargetBundleID[] = "{target["bundle_id"]}";',
        f'static const char PHTargetVersion[] = "{target["marketing_version"]}";',
        f'static const char PHTargetBuild[] = "{target["ios_build"]}";',
        f'static const char PHTargetUUIDString[] = "{target["uuid"]}";',
        "static const uint8_t PHTargetUUID[16] = {" +
        ", ".join(f"0x{byte:02x}" for byte in uuid.UUID(target["uuid"]).bytes) + "};",
    ]
    for name, value in symbols.items():
        lines.append(f"static const uintptr_t {name} = 0x{integer(value):x}u;")
    lines += [
        "typedef struct { uintptr_t rva; size_t length; uint8_t bytes[64]; } PHTextGuard;",
        "static const PHTextGuard PHTextGuards[] = {",
    ]
    guards = manifest["runtime_gate"]["guards"]
    if len(guards) < 3:
        raise ValueError("The runtime gate needs multiple independent guards")
    for guard in guards:
        raw = bytes.fromhex(guard["bytes_hex"])
        if len(raw) != guard["length_bytes"] or len(raw) != 64:
            raise ValueError(f"Guard length mismatch: {guard['id']}")
        if integer(guard["va"]) - base != integer(guard["rva"]):
            raise ValueError(f"Guard RVA mismatch: {guard['id']}")
        lines.append("    {0x%xu, %du, {%s}}," % (
            integer(guard["rva"]), len(raw), ", ".join(f"0x{byte:02x}" for byte in raw)))
    lines += ["};", "static const size_t PHTextGuardCount = sizeof(PHTextGuards) / sizeof(PHTextGuards[0]);"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=Path("ios/targets/pad-23.6.0.json"))
    parser.add_argument("--output", type=Path, default=Path("ios/generated/PHTargetGenerated.h"))
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    content = generate(manifest)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8", newline="\n")
    print(f"Generated {args.output}")


if __name__ == "__main__":
    main()
