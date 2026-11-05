#!/usr/bin/env python3
"""Package a verified CI run into an immutable delivery folder and ZIP.

Usage (after the user authorized a build and the manual CI run succeeded):

    python ios/tools/package_delivery.py --run 41495269554 --dest-root E:/ipa \
        [--extra-log name=path ...] [--note key=value ...]

The script downloads the run's artifacts with `gh`, refuses to overwrite an
existing delivery, checks the dylib against SHA256SUMS, requires every CI step
and all four GUI reports to succeed, and writes verification.json,
PACKAGE_SHA256SUMS, <name>.zip and <name>.zip.sha256. It never marks a build
as device-verified.
"""
import argparse
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from release_metadata import read_json, verify_release_snapshot

GUI_REPORTS = ['overlay-probe.json', 'overlay-restart-probe.json', 'legacy-probe.json', 'legacy-restart-probe.json']


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def gh(*args):
    return subprocess.run(['gh', *args], check=True, capture_output=True, text=True, encoding='utf-8').stdout


def find_artifact(root, prefix):
    matches = [name for name in os.listdir(root) if name.startswith(prefix)]
    if len(matches) != 1:
        sys.exit(f'expected one artifact starting with {prefix}, found {matches}')
    return os.path.join(root, matches[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--run', required=True, help='GitHub Actions run database ID')
    parser.add_argument('--dest-root', required=True, help='directory that holds delivery folders, e.g. E:/ipa')
    parser.add_argument('--extra-log', action='append', default=[], metavar='NAME=PATH',
                        help='additional evidence copied into verification/')
    parser.add_argument('--note', action='append', default=[], metavar='KEY=VALUE',
                        help='additional verification.json string fields')
    args = parser.parse_args()

    run = json.loads(gh('run', 'view', args.run, '--json', 'url,conclusion,headSha,jobs,createdAt,number'))
    steps = {step['name']: step['conclusion'] for job in run['jobs'] for step in job['steps']}
    failed = {name: result for name, result in steps.items() if result != 'success'}
    if run['conclusion'] != 'success' or failed:
        sys.exit(f'CI run is not a complete success: {run["conclusion"]} {failed}')

    with tempfile.TemporaryDirectory() as work:
        gh('run', 'download', args.run, '--dir', work)
        artifact = find_artifact(work, 'PADCheaper-iOS-arm64-')
        gui = find_artifact(work, 'PADCheaper-GUI-probe-')
        browser = find_artifact(work, 'PADCheaper-Editor-browser-')
        info = json.load(open(os.path.join(artifact, 'build-info.json'), encoding='utf-8'))
        if info['commit'] != run['headSha']:
            sys.exit(f'build-info commit {info["commit"]} differs from run head {run["headSha"]}')
        release_path = os.path.join(artifact, 'release.json')
        if 'release_summary' in info or os.path.exists(release_path):
            verify_release_snapshot(info, read_json(release_path))
        expected = {}
        for line in open(os.path.join(artifact, 'SHA256SUMS'), encoding='utf-8'):
            digest, name = line.split(maxsplit=1)
            expected[name.strip().lstrip('*')] = digest
        actual_sha = sha256(os.path.join(artifact, 'PADCheaper.dylib'))
        if expected.get('PADCheaper.dylib') != actual_sha or info.get('sha256') != actual_sha:
            sys.exit('PADCheaper.dylib does not match SHA256SUMS and build-info')
        reports = {}
        for name in GUI_REPORTS:
            report = json.load(open(os.path.join(gui, name), encoding='utf-8'))
            reports[name] = {key: report.get(key) for key in ('phase', 'success', 'failures')}
            if report.get('phase') != 'done' or report.get('success') != 1 or report.get('failures'):
                sys.exit(f'GUI report {name} did not pass: {reports[name]}')

        name = 'PADCheaper-' + info['dylib_version']
        dest = os.path.join(args.dest_root, name)
        if os.path.exists(dest) or os.path.exists(dest + '.zip'):
            sys.exit(f'{dest} already exists; delivered versions are immutable')
        shutil.copytree(artifact, dest)
        evidence = os.path.join(dest, 'verification')
        os.makedirs(evidence)
        shutil.copytree(gui, os.path.join(evidence, 'gui'))
        shutil.copytree(browser, os.path.join(evidence, 'browser'))
        with open(os.path.join(evidence, 'ci-run.json'), 'w', encoding='utf-8') as handle:
            json.dump(run, handle, ensure_ascii=False, indent=2)
        with open(os.path.join(evidence, 'ci-run.log'), 'w', encoding='utf-8') as handle:
            handle.write(gh('run', 'view', args.run, '--log'))
        for item in args.extra_log:
            label, path = item.split('=', 1)
            shutil.copy(path, os.path.join(evidence, label))

    dylib = os.path.join(dest, 'PADCheaper.dylib')
    verification = {
        'version': info['dylib_version'],
        'commit': info['commit'],
        'verification_date': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'sha256': sha256(dylib),
        'size_bytes': os.path.getsize(dylib),
        'ci_run': run['url'],
        'ci_conclusion': run['conclusion'],
        'ci_required_steps': steps,
        'native_tests': 'passed on the same macOS CI run',
        'editor_browser_tests': 'passed',
        'gui_reports': reports,
        'device_verified': False,
    }
    for item in args.note:
        key, value = item.split('=', 1)
        verification[key] = value
    with open(os.path.join(dest, 'verification.json'), 'w', encoding='utf-8') as handle:
        json.dump(verification, handle, ensure_ascii=False, indent=2)

    lines = []
    for base, _, files in os.walk(dest):
        for file in files:
            path = os.path.join(base, file)
            relative = os.path.relpath(path, dest).replace(os.sep, '/')
            if relative != 'PACKAGE_SHA256SUMS':
                lines.append((relative, sha256(path)))
    with open(os.path.join(dest, 'PACKAGE_SHA256SUMS'), 'w', encoding='utf-8', newline='\n') as handle:
        handle.writelines(f'{digest}  {relative}\n' for relative, digest in sorted(lines))
    with zipfile.ZipFile(dest + '.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for base, _, files in os.walk(dest):
            for file in sorted(files):
                path = os.path.join(base, file)
                archive.write(path, os.path.join(name, os.path.relpath(path, dest)))
    with open(dest + '.zip.sha256', 'w', encoding='utf-8', newline='\n') as handle:
        handle.write(f'{sha256(dest + ".zip")}  {name}.zip\n')
    print(json.dumps({'delivery': dest, 'sha256': verification['sha256'], 'size_bytes': verification['size_bytes'],
                      'zip_sha256': sha256(dest + '.zip')}, indent=2))


if __name__ == '__main__':
    main()
