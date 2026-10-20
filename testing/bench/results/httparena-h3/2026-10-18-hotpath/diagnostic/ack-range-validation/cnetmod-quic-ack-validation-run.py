from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

stage = sys.argv[1]
out = Path(sys.argv[2]).resolve()
if '--namespace' not in sys.argv:
    raise SystemExit(subprocess.run(['unshare', '--mount', '--propagation', 'private', sys.executable, str(Path(__file__).resolve()), stage, str(out), '--namespace']).returncode)
build = Path('/tmp/cnetmod-h3-arena-build')
build.mkdir(parents=True, exist_ok=True)
subprocess.run(['mount', '--bind', '/var/tmp/cnetmod-h3-arena-build', str(build)], check=True)
out.mkdir(parents=True, exist_ok=True)
cache = (build / 'CMakeCache.txt').read_text()
assert 'CNETMOD_USE_MIMALLOC:BOOL=OFF' in cache
assert not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT')
(out / 'CMakeCache.txt').write_text(cache)
targets = ['test_quic_loss_detection'] if stage == 'old' else ['test_quic_loss_detection', 'test_quic_congestion', 'test_quic_stream']
command = ['/var/tmp/cnetmod-ninja-1.11/bin/ninja', '-j2'] + targets
(out / 'build-command.json').write_text(json.dumps(command, indent=2) + '\n')
started = time.time()
with (out / 'build.log').open('w') as log:
    result = subprocess.run(command, cwd=build, stdout=log, stderr=subprocess.STDOUT)
(out / 'build-result.json').write_text(json.dumps({'returncode': result.returncode, 'elapsed_seconds': time.time() - started}) + '\n')
assert result.returncode == 0, 'build failed'
manifest = {}
for target in targets:
    found = [p for p in build.rglob(target) if p.is_file()]
    assert len(found) == 1, found
    manifest[target] = {'path': str(found[0]), 'sha256': hashlib.sha256(found[0].read_bytes()).hexdigest()}
(out / 'binaries.json').write_text(json.dumps(manifest, indent=2) + '\n')
pattern = '^(' + '|'.join(targets) + ')$'
listing = subprocess.check_output(['ctest', '--show-only=json-v1', '-R', pattern], cwd=build, text=True)
(out / 'tests-list.json').write_text(listing)
assert len(json.loads(listing)['tests']) == len(targets)
command = ['ctest', '--output-on-failure', '-j1', '--timeout', '75', '-R', pattern, '--output-junit', str(out / 'tests.xml')]
(out / 'tests-command.json').write_text(json.dumps(command, indent=2) + '\n')
with (out / 'tests.log').open('w') as log:
    result = subprocess.run(command, cwd=build, stdout=log, stderr=subprocess.STDOUT)
tree = ET.parse(out / 'tests.xml')
tests = tree.findall('.//testcase')
bad = [t.attrib.get('name') for t in tests if any(t.find(tag) is not None for tag in ['failure', 'skipped', 'error'])]
summary = {'stage': stage, 'returncode': result.returncode, 'executed': len(tests), 'bad': bad}
(out / 'tests-result.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary), flush=True)
if stage == 'old':
    assert result.returncode != 0 and bad == ['test_quic_loss_detection']
else:
    assert result.returncode == 0 and len(tests) == 3 and not bad
