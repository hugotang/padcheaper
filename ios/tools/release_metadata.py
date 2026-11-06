#!/usr/bin/env python3
"""Generate version declarations and describe the artifact actually built.

Header generation is a source-generation step. Finalization and summary are
called only by the explicitly requested build workflow; neither claims that
tests or device verification passed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "ios/release.json"
HEADER = ROOT / "ios/generated/PHReleaseGenerated.h"
FIELDS = {"schema", "dylib_version", "minimum_ios", "architecture", "platform", "signing", "mode",
          "summary_features", "features"}
RESERVED = FIELDS | {"commit", "sha256", "device_verified", "release_summary"}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"), object_pairs_hook=unique_object)


def validate_release(value):
    if not isinstance(value, dict) or set(value) != FIELDS or type(value["schema"]) is not int or value["schema"] != 1:
        raise ValueError("Unsupported release manifest fields or schema")
    if not isinstance(value["dylib_version"], str) or not re.fullmatch(r"\d+\.\d+\.\d+-[a-z0-9]+", value["dylib_version"]):
        raise ValueError("Invalid dylib version")
    validate_minimum(value["minimum_ios"])
    if value["architecture"] != "arm64" or value["platform"] != "iPhoneOS":
        raise ValueError("Unsupported release platform")
    for key in ("signing", "mode"):
        if not isinstance(value[key], str) or not value[key].strip():
            raise ValueError(f"Missing release {key}")
    features = value["features"]
    if not isinstance(features, dict) or not features:
        raise ValueError("Missing release features")
    for key, description in features.items():
        if key in RESERVED or not re.fullmatch(r"[a-z][a-z0-9_]*", key):
            raise ValueError(f"Reserved or invalid feature: {key}")
        if not isinstance(description, str) or not description.strip():
            raise ValueError(f"Missing feature description: {key}")
    summary = value["summary_features"]
    if not isinstance(summary, dict) or not summary:
        raise ValueError("Missing release summary selection")
    for label, key in summary.items():
        if not label.strip() or not isinstance(key, str) or key not in features:
            raise ValueError(f"Unknown summary feature: {label}")
    return value


def validate_minimum(value):
    if not isinstance(value, str) or not re.fullmatch(r"\d+\.\d+(?:\.\d+)?", value):
        raise ValueError("Invalid minimum iOS version")
    return value


def version_header(release):
    validate_release(release)
    return ('// Generated from ios/release.json. Do not edit.\n#pragma once\n'
            f'#define PH_RUNTIME_VERSION @"{release["dylib_version"]}"\n')


def build_metadata(release, commit, binary, minimum_ios):
    validate_release(release)
    validate_minimum(minimum_ios)
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Invalid source commit")
    # Keep schema 1 and its flat feature fields for existing delivery consumers.
    metadata = {"schema": 1, "commit": commit, "architecture": release["architecture"],
                "dylib_version": release["dylib_version"], **release["features"],
                "platform": release["platform"], "minimum_ios": minimum_ios,
                "signing": release["signing"], "mode": release["mode"],
                "device_verified": False, "sha256": hashlib.sha256(binary).hexdigest()}
    metadata["release_summary"] = {label: release["features"][key]
                                   for label, key in release["summary_features"].items()}
    return metadata


def verify_release_snapshot(metadata, release):
    validate_release(release)
    if not isinstance(metadata, dict) or type(metadata.get("schema")) is not int or metadata["schema"] != 1:
        raise ValueError("Unsupported build metadata schema")
    for key, pattern in (("commit", r"[0-9a-f]{40}"), ("sha256", r"[0-9a-f]{64}")):
        if not isinstance(metadata.get(key), str) or not re.fullmatch(pattern, metadata[key]):
            raise ValueError(f"Invalid build {key}")
    for key in ("dylib_version", "architecture", "platform", "signing", "mode"):
        if metadata.get(key) != release[key]:
            raise ValueError(f"Artifact release field differs: {key}")
    for key, value in release["features"].items():
        if metadata.get(key) != value:
            raise ValueError(f"Artifact feature differs: {key}")
    summary = {label: release["features"][key] for label, key in release["summary_features"].items()}
    if metadata.get("release_summary") != summary or metadata.get("device_verified") is not False:
        raise ValueError("Artifact summary or device verification differs")
    # The real compiler deployment target may override the manifest default.
    validate_minimum(metadata.get("minimum_ios"))


def rewrite_artifact_links(directory, commit=None):
    # Packaged notes are flattened by build.sh. Source documents stay untouched.
    for notes in directory.glob("*.md"):
        content = notes.read_text(encoding="utf-8")
        for old, new in (("](docs/", "]("), ("](../research/", "](research/"),
                         ("](../templates/", "](templates/"), ("](../third_party/", "](third_party/"),
                         ("](../README.md", "](README.md"), ("](../CHANGELOG.md", "](CHANGELOG.md"),
                         ("](../editor/assets/", "](third_party/PADDashFormation/"),
                         ("](../.github/workflows/ios-dylib.yml)",
                          "](https://github.com/hugotang/padcheaper/actions/workflows/ios-dylib.yml)")):
            content = content.replace(old, new)
        for source, target in (("pad-23.6.1.json", "target.json"),
                               ("battle-23.6.1.json", "battle-target.json"),
                               ("orbs-23.6.1.json", "orb-target.json"),
                               ("pad-23.6.0.json", "targets/pad-23.6.0.json"),
                               ("battle-23.6.0.json", "targets/battle-23.6.0.json"),
                               ("orbs-23.6.0.json", "targets/orbs-23.6.0.json")):
            content = content.replace("](../targets/" + source + ")", "](" + target + ")")
        if commit:
            # Implementation links refer to the source actually compiled; source
            # files are not duplicated into the flattened user documentation.
            for source_dir in ("src", "tests"):
                content = content.replace(f"](../{source_dir}/",
                    f"](https://github.com/hugotang/padcheaper/blob/{commit}/ios/{source_dir}/")
        notes.write_text(content, encoding="utf-8", newline="\n")
    for notes in (directory / "templates").glob("*/*.md"):
        notes.write_text(notes.read_text(encoding="utf-8").replace("](../../docs/", "](../../"),
                         encoding="utf-8", newline="\n")


def finalize(directory, release, minimum_ios):
    if HEADER.read_text(encoding="utf-8") != version_header(release):
        raise ValueError("Version header differs from the release manifest; regenerate and rebuild")
    binary = directory / "PADCheaper.dylib"
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    metadata = build_metadata(release, commit, binary.read_bytes(), minimum_ios)
    rewrite_artifact_links(directory, commit)
    for name, value in (("build-info.json", metadata), ("release.json", release)):
        (directory / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                                      encoding="utf-8", newline="\n")
    (directory / "SHA256SUMS").write_text(f'{metadata["sha256"]}  PADCheaper.dylib\n', encoding="utf-8", newline="\n")
    print(f"Built {metadata['architecture']} {metadata['platform']} dylib: {binary}")


def render_summary(metadata):
    # Read only captured artifact metadata, never today's source manifest or a
    # historical release paragraph. Test evidence belongs to CI/verification.json.
    lines = [f"Built **PADCheaper {metadata['dylib_version']}** for {metadata['architecture']} "
             f"{metadata['platform']}, minimum iOS {metadata['minimum_ios']}.", "",
             "Download the artifact for the existing Dopamine rootless setup; LiveContainer users must re-sign there.", ""]
    lines.extend(f"- **{label}**: {description}" for label, description in metadata["release_summary"].items())
    lines += ["", "The artifact retains `device_verified=false`. Build metadata describes source capabilities; "
              "test and delivery evidence are recorded by the CI run and `verification.json`.",
              "", f"Source: `{metadata['commit']}`", f"SHA-256: `{metadata['sha256']}`"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("header", help="generate the source version header; does not compile")
    commands.add_parser("minimum-ios", help="print the default deployment target")
    finish = commands.add_parser("finalize", help="describe an already built artifact")
    finish.add_argument("directory", type=Path)
    finish.add_argument("--minimum-ios", required=True)
    summary = commands.add_parser("summary", help="render only the artifact's captured metadata")
    summary.add_argument("directory", type=Path)
    args = parser.parse_args()
    if args.command == "summary":
        metadata = read_json(args.directory / "build-info.json")
        verify_release_snapshot(metadata, read_json(args.directory / "release.json"))
        print(render_summary(metadata), end="")
        return
    release = validate_release(read_json(MANIFEST))
    if args.command == "minimum-ios":
        print(release["minimum_ios"])
    elif args.command == "header":
        HEADER.parent.mkdir(parents=True, exist_ok=True)
        HEADER.write_text(version_header(release), encoding="utf-8", newline="\n")
        print(f"Generated {HEADER}")
    else:
        finalize(args.directory, release, args.minimum_ios)


if __name__ == "__main__":
    main()
