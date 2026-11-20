"""Pinned public display facts missing from the upstream Japanese catalog.

No skill IDs, stat curves or leader parameters are inferred from descriptions.
The native game remains authoritative when a formation is actually applied.
"""
import hashlib
import json
from pathlib import Path

DEFAULT_MANIFEST = Path(__file__).resolve().parents[1] / "data/editor-supplement/manifest.json"


def load_supplement(manifest_path=DEFAULT_MANIFEST):
    manifest_bytes = Path(manifest_path).read_bytes()
    manifest = json.loads(manifest_bytes)
    assert manifest["schema"] == 1
    assert manifest["request"]["region"] == "jp" and manifest["request"]["language"] == "ja"
    assert Path(manifest["file"]).name == manifest["file"]
    raw = (Path(manifest_path).parent / manifest["file"]).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == manifest["sha256"], "Supplement source hash mismatch"
    source = json.loads(raw)
    assert source["requestedLanguage"] == "ja" and source["page"] == 1
    assert source["searchSort"] == manifest["request"]["sort"]
    assert source["searchConditionRoot"] == manifest["request"]["root"]
    numbers = [item["mon"]["num"] for item in source["mons"]]
    assert numbers == sorted(set(numbers), reverse=True)
    assert numbers[0] == manifest["newestNumber"] and numbers[-1] <= manifest["baseMaxNumber"]
    cards = []
    for item in source["mons"]:
        card = item["mon"]
        if card["num"] <= manifest["baseMaxNumber"]:
            continue
        assert item["presentationStatus"] == "available"
        assert item["ref"] == {"region": "jp", "number": card["num"]}
        assert card["region"] == "jp" and card["requestedLanguage"] == "ja"
        assert isinstance(card["name"], str) and card["name"]
        assert type(card["num"]) is int and 0 < card["num"] <= 65435
        assert card["displayStats"]["level"] in (1, 99, 120)
        for key, lo, hi in (("attributes", -1, 6), ("types", 0, 15),
                            ("awokens", 1, 153), ("super_awokens", 1, 153), ("transforms", 1, 65435)):
            assert isinstance(card[key], list) and all(type(v) is int and lo <= v <= hi for v in card[key])
        assert type(card["sync_awoken"]) is int and 0 <= card["sync_awoken"] <= 153
        assert type(card["assistable"]) is bool and type(card["latent_extendable"]) is bool
        chain = card["transforms"]
        assert not chain or len(chain) == 2 and len(set(chain)) == 2 and card["num"] in chain
        cd = card["skillCooldown"]
        if cd is not None:
            assert all(type(cd[k]) is int for k in ("atLevelOne", "atMaximumLevel", "maximumSkillLevel"))
            assert 1 <= cd["maximumSkillLevel"] <= 99 and 0 <= cd["atMaximumLevel"] <= cd["atLevelOne"] <= 9999
            assert cd["maximumSkillLevel"] == cd["atLevelOne"] - cd["atMaximumLevel"] + 1
        for skills in (card["showing_active_skills"], card["showing_leader_skills"]):
            assert isinstance(skills, list)
            assert all(isinstance(s["name"], str) and isinstance(s["description"], str) for s in skills)
        cards.append(card)
    assert len(cards) == manifest["expectedSupplementCount"]
    # Git checks out LF on macOS and may use CRLF on Windows. The manifest's
    # semantics, plus the independently hash-checked response bytes, bind data.
    canonical_manifest = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
    return sorted(cards, key=lambda c: c["num"]), {
        "source": manifest["source"], "retrievedOn": manifest["retrievedOn"],
        "sha256": hashlib.sha256(canonical_manifest + raw).hexdigest(),
        "newestNumber": manifest["newestNumber"], "records": len(cards),
    }


def editor_row(card):
    cd = card["skillCooldown"]
    return [card["num"], card["name"], card["attributes"], card["displayStats"]["level"],
            cd["maximumSkillLevel"] if cd else 1, card["awokens"], card["super_awokens"],
            card["sync_awoken"], card["assistable"], 8 if card["latent_extendable"] else 6, ""]


def team_row(card):
    chain = card["transforms"]
    to = [chain[1]] if chain and card["num"] == chain[0] else []
    # Null is deliberately distinct from a known zero/no-skill/no-collaboration.
    return [card["num"], card["types"], None, None, None, None, None,
            None, to, card["rarity"], None, None]


def display_facts(card):
    return {"stats": card["displayStats"], "cooldown": card["skillCooldown"],
            "active": card["showing_active_skills"], "leader": card["showing_leader_skills"]}
