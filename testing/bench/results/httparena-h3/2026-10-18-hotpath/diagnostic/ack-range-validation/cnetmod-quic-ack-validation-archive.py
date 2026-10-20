from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET

repo = Path('C:/Users/25007/.codex/worktrees/2c3c/cnetmod')
run = Path('C:/Users/25007/.codex/tmp/cnetmod-quic-ack-validation-1792329600')
dest = repo / 'testing/bench/results/httparena-h3/2026-10-18-hotpath/diagnostic/ack-range-validation'
assert not dest.exists()
dest.mkdir(parents=True)
for stage in ['old', 'new']:
    shutil.copytree(run / stage, dest / stage)
shutil.copytree(run / 'prepared', dest / 'prepared')
for p in run.glob('before-*'):
    shutil.copy2(p, dest / p.name)
for name in ['cnetmod-quic-ack-validation-prepare.py', 'cnetmod-quic-ack-validation-run.py', 'cnetmod-quic-ack-validation-archive.py']:
    shutil.copy2(run.parent / name, dest / name)
inputs = json.loads((run / 'prepared/inputs.json').read_text())
actual = []
for item in inputs:
    path = item['path']
    source = repo / path
    assert source.read_text(encoding='utf-8') == (run / 'prepared/sources' / path).read_text(encoding='utf-8')
    copy = dest / 'applied-sources' / path
    copy.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, copy)
    actual.append({'path': path, 'before_sha256': item['before_sha256'], 'after_sha256': hashlib.sha256(source.read_bytes()).hexdigest()})
for path in ['src/protocol/quic/quic_connection.cpp', 'src/protocol/quic/quic_recovery.cpp', 'src/protocol/quic/congestion_control.cpp', 'CMakeLists.txt', 'testing/tests/CMakeLists.txt', 'testing/http3/h3-interop-results.json']:
    actual.append({'path': path, 'unchanged_during_this_stage_sha256': hashlib.sha256((repo / path).read_bytes()).hexdigest()})
(dest / 'source-manifest.json').write_text(json.dumps(actual, indent=2) + '\n')
results = {}
for stage in ['old', 'new']:
    cases = []
    for test in ET.parse(run / stage / 'tests.xml').findall('.//testcase'):
        text = test.findtext('system-out') or ''
        count = re.search(r'Running (\d+) test\(s\)', text)
        failures = re.findall(r'\[  FAILED  \] ([^\n]+?) \((\d+) failure\(s\)\)', text)
        cases.append({'target': test.attrib['name'], 'unit_case_count': int(count[1]) if count else None, 'failed_cases': [{'name': n, 'assertion_failures': int(c)} for n, c in failures]})
    results[stage] = {'ctest': json.loads((run / stage / 'tests-result.json').read_text()), 'unit_targets': cases}
assert results['old']['unit_targets'][0]['unit_case_count'] == 27
assert len(results['old']['unit_targets'][0]['failed_cases']) == 2
assert sum(t['unit_case_count'] for t in results['new']['unit_targets']) == 64
(dest / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
diffcheck = subprocess.run(['git', 'diff', '--check', '--'] + [i['path'] for i in inputs], cwd=repo, capture_output=True, text=True)
assert diffcheck.returncode == 0
(dest / 'diff-check.txt').write_text('git diff --check scoped to the three changed files: exit 0\n' + diffcheck.stdout + diffcheck.stderr)
head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
branch = subprocess.check_output(['git', 'branch', '--show-current'], cwd=repo, text=True).strip()
(dest / 'identity.json').write_text(json.dumps({'head': head, 'branch': branch, 'dirty': True, 'external_run': str(run), 'allocator': 'CNETMOD_USE_MIMALLOC=OFF (existing system-allocator build)', 'build': '/var/tmp/cnetmod-h3-arena-build privately bound at recorded /tmp/cnetmod-h3-arena-build', 'arena_not_relinked_in_this_stage': '2cb22a32818646fbf63575370adf6a0c48b3ba5df4bbbc3d3002ab2838003e0a'}, indent=2) + '\n')
(dest / 'README.md').write_text('''# Malformed ACK validation: old fails, correction passes

This correctness change is on the dirty `fix/http3-arena-performance` worktree. It does not implement the proposed ACK visitor optimization and is not a performance result. No commit was made. The Linux build uses the existing system allocator configuration (`CNETMOD_USE_MIMALLOC=OFF`); both stages preserve the cache in their evidence directory.

The original `on_ack_received` updates largest-ACK and RTT before validating the first range, then retires the first range before validating later gaps and lengths. A later rejection can leave the detector map and recovery map inconsistent. Public `ack_range::gap` values of UINT64_MAX or UINT64_MAX-1 also wrap the original `gap + 2` check. Wire QUIC varints are narrower; the full-width cases cover the public C++ structure.

The correction prevalidates every actual range before any state change, checks subtraction operands without overflow, and preserves existing error precedence and public signatures. The non-template validation implementation is in `loss_detection.cpp`; no callback/visitor or metadata-owner refactor is included. It does not add a count/vector consistency restriction to the public API.

Tests-only against the original implementation builds successfully and fails two of 27 unit cases (25 pass): the detector atomicity test has 31 failed assertions, and the recovery/shared-owner test has nine. Evidence includes changed RTT/PTO, flight shrinking from nine packets to seven despite rejection, max-gap values being accepted, and a later valid ACK failing to release ownership/flight. The valid zero-boundary, duplicate, stale ACK case passes on both versions.

After applying the correction, the three selected CTest targets pass with zero skips: loss detection 27, congestion 20, stream 17, totaling 64 unit cases. Five malformed-range variants cover invalid first range, later gap, later length, and both full-width gaps. Rejection must leave RTT/flight/PTO unchanged; a subsequent valid ACK must match an untouched detector and release the shared body. Existing PTO-alias, PMTU probe loss, discard/reuse and shared-body coverage also passes.

`prepared/fix.patch` and `prepared/tests.patch` are the exact applied increments relative to the recorded dirty source baseline. Before/after sources, actual byte hashes, original build/CTest/JUnit logs, cache, commands and executable hashes are retained. CRLF applied source hashes differ from LF-only prepared source hashes; normalized contents were verified equal. `source-manifest.json` identifies the unchanged connection, recovery and congestion inputs, preserving earlier GSO and PMTU fixes.

Only these unit targets were rebuilt. The arena executable `2cb22a...` was not relinked and does not include this fix; final all-target Linux/Windows validation is delegated to the coordinator. There was no service load, throughput run or new allocator comparison in this stage. No ELF, PCM or static archive is copied here. The protected repository interoperability JSON remains unchanged.
''', encoding='utf-8')
files = []
for p in sorted(dest.rglob('*')):
    if p.is_file():
        assert p.suffix not in ['.o', '.a', '.pcm', '.so']
        files.append({'path': p.relative_to(dest).as_posix(), 'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()})
(dest / 'manifest.json').write_text(json.dumps(files, indent=2) + '\n')
print(json.dumps({'archive': str(dest), 'files': len(files), 'bytes': sum(i['bytes'] for i in files), 'manifest_sha256': hashlib.sha256((dest / 'manifest.json').read_bytes()).hexdigest()}))
