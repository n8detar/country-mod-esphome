"""Component CI: unit tests, then `esphome config` + `esphome compile` of every ci/tests/*.yaml.

Never flashes, never uses real secrets. Shared verbatim by Nate's ESPHome component repos
(hermes/country-mod-esphome, esphome-velit, esphome-bodega, esphome-maxxair-fan); keep copies identical.

  python ci/check_components.py                 # required lane: the image's ESPHome (pinned in ESPHOME_VERSION)
  python ci/check_components.py --lane latest   # early-warning lane: newest ESPHome release from PyPI
  ESPHOME_LATEST_OVERRIDE=2026.11.0b1 python ci/check_components.py --lane latest   # exercise a specific release
"""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / 'ci' / 'tests'
CACHE = Path(os.getenv('ESPHOME_CI_CACHE', '/cache'))
VERSION = re.compile(r'^\d{4}\.\d{1,2}\.\d+(?:(?:a|b|rc)\d+)?$')


class CIError(Exception):
    """A diagnostic safe to print."""


def run(args, timeout, cwd=ROOT):
    start = time.monotonic()
    p = subprocess.Popen(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, start_new_session=True)
    try:
        out, _ = p.communicate(timeout=timeout)
        return p.returncode, round(time.monotonic() - start, 1), out
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)
        p.communicate()
        return 124, round(time.monotonic() - start, 1), 'Command timed out; process group terminated'


def esphome_version(cmd):
    code, _, out = run(cmd + ['version'], 120)
    m = re.search(r'Version:\s*(\S+)', out)
    if code or not m:
        raise CIError('Could not determine ESPHome version')
    return m.group(1)


def latest_release():
    override = os.getenv('ESPHOME_LATEST_OVERRIDE', '').strip()
    if override:
        if not VERSION.fullmatch(override):
            raise CIError('ESPHOME_LATEST_OVERRIDE is not an ESPHome version')
        return override
    with urllib.request.urlopen('https://pypi.org/pypi/esphome/json', timeout=60) as r:
        version = json.load(r)['info']['version']
    if not VERSION.fullmatch(version):
        raise CIError('Unexpected PyPI version string')
    return version


def install(version):
    """Install ESPHome `version` into a cached venv and return its CLI command."""
    venv = CACHE / 'venvs' / ('esphome-' + version)
    exe = venv / 'bin' / 'esphome'
    if exe.exists():
        return [str(exe)]
    venv.parent.mkdir(parents=True, exist_ok=True)
    tmp = venv.with_name(venv.name + '.partial')
    run(['rm', '-rf', str(tmp)], 120)
    code, _, out = run([sys.executable, '-m', 'venv', str(tmp)], 300)
    if code == 0:
        code, _, out = run([str(tmp / 'bin' / 'python'), '-m', 'pip', 'install', '-q', 'esphome==' + version], 1800)
    elif run(['uv', '--version'], 60)[0] == 0:
        code, _, out = run(['uv', 'venv', '-q', str(tmp)], 300)
        if code == 0:
            code, _, out = run(['uv', 'pip', 'install', '-q', '--python', str(tmp / 'bin' / 'python'), 'esphome==' + version], 1800)
    if code:
        print('\n'.join(out.splitlines()[-25:]))
        raise CIError('Could not install ESPHome ' + version)
    os.replace(tmp, venv)
    return [str(exe)]


def unit_tests():
    if not (ROOT / 'tests').is_dir():
        print('unit tests: none in repository', flush=True)
        return True
    code, secs, out = run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-v'], 600)
    print(out, flush=True)
    print('unit tests:', 'PASS' if code == 0 else 'FAIL', str(secs) + 's', flush=True)
    return code == 0


def configs():
    found = sorted(p for p in TESTS.glob('*.yaml') if p.name != 'secrets.yaml')
    if not found:
        raise CIError('No ci/tests/*.yaml test configurations')
    for p in found:
        if '!secret' in p.read_text():
            raise CIError('Test configurations must not reference secrets: ' + p.name)
    return found


def link_cache(lane, version):
    # Per repo/version build directory inside the private dind cache volume; never shared with device repos.
    if not CACHE.is_dir():
        return
    repo = re.sub(r'[^A-Za-z0-9_.-]+', '_', os.getenv('GITHUB_REPOSITORY') or ROOT.name)
    store = CACHE / 'esphome-components' / repo / (lane + '-' + version)
    store.mkdir(parents=True, exist_ok=True)
    target = TESTS / '.esphome'
    if target.is_symlink() or target.exists():
        return
    target.symlink_to(store, target_is_directory=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lane', choices=('pinned', 'latest'), default='pinned')
    a = ap.parse_args()
    image_cmd = ['esphome']
    image_version = esphome_version(image_cmd)
    pinned = (ROOT / 'ESPHOME_VERSION').read_text().strip()
    if a.lane == 'pinned':
        if image_version != pinned:
            raise CIError('Image ESPHome ' + image_version + ' != ESPHOME_VERSION ' + pinned + '; bump both together')
        cmd, version = image_cmd, image_version
        ok = unit_tests()
    else:
        version = latest_release()
        if version == image_version and not os.getenv('ESPHOME_LATEST_OVERRIDE'):
            print('LATEST-LANE: newest ESPHome release', version, '== pinned image; covered by the required lane', flush=True)
            return 0
        cmd = install(version)
        installed = esphome_version(cmd)
        if installed != version:
            raise CIError('Installed ESPHome ' + installed + ' != requested ' + version)
        ok = True
    link_cache(a.lane, version)
    results = []
    for path in configs():
        rel = str(path.relative_to(ROOT))
        print('checking', rel, 'with ESPHome', version, flush=True)
        c, cs, out = run(cmd + ['config', rel], 600)
        b, bs = None, None
        if c == 0:
            b, bs, out = run(cmd + ['compile', rel], 3000)
        passed = c == 0 and b == 0
        print(rel, 'PASS' if passed else 'FAIL', 'config=' + str(c) + ' ' + str(cs) + 's', 'compile=' + str(b) + ' ' + str(bs) + 's', flush=True)
        if not passed:
            print('\n'.join(out.splitlines()[-40:]), flush=True)
        results.append({'config': rel, 'esphome': version, 'configExit': c, 'configSeconds': cs, 'compileExit': b, 'compileSeconds': bs, 'passed': passed})
        ok = ok and passed
    print(('LATEST-LANE ' if a.lane == 'latest' else 'PINNED-LANE ') + 'SUMMARY', json.dumps(results), flush=True)
    return 0 if ok else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as e:
        print('CI failed:', str(e) if isinstance(e, CIError) else type(e).__name__)
        sys.exit(1)
