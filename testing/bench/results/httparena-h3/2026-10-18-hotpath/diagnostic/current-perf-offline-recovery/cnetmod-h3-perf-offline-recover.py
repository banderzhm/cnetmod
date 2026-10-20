"""Offline-only recovery: never starts a server, client or perf record."""
import hashlib
import json
from pathlib import Path
import re
import subprocess

root = Path('/var/tmp/cnetmod-h3-current-perf-be6beaa74a4e423c8c78ff0b8f4fe39c')
out = root / 'offline-recovery'
out.mkdir(exist_ok=False)
repo = Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod')
def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()
original_summary_sha = sha(root / 'summary.json')
original = json.loads((root / 'summary.json').read_text())
source = json.loads((root / 'supplied-source-metadata.json').read_text(encoding='utf-8-sig'))
assert original['valid'] is False
assert sha(root / 'actual-dirty-source.patch') == source['diff_sha256']
rows = []
for relative, expected in source['sources'].items():
    file = repo / relative
    actual = sha(file) if file.is_file() else None
    if actual != expected:
        rows.append(dict(path=relative, expected=expected, actual=actual))
production_mismatches = [r for r in rows if r['path'].startswith(('src/', 'include/', 'cmake/')) or r['path'] in ('CMakeLists.txt', 'testing/bench/arena_h3_server.cpp')]
report_codes = {}
for label, options in [('header', ['--header-only']), ('self', ['--no-children', '-g', 'none']), ('stacks', ['-g', 'graph,0.5,caller'])]:
    command = ['/usr/sbin/perf', 'report', '--stdio', '--percent-limit', '.5', '--sort', 'symbol,dso', '-i', str(root / 'static-perf.data'), *options]
    with (out / f'static-perf-{label}.txt').open('w') as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=60)
    report_codes[label] = dict(command=command, returncode=result.returncode)
text = (root / 'h2load.log').read_text()
counts = re.search(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout', text)
codes = re.search(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx', text)
rate = re.search(r'finished in ([\d.]+)(ms|s|m),\s*([\d.]+) req/s', text)
assert counts and codes and rate
elapsed = float(rate[1]) * {'ms': .001, 's': 1, 'm': 60}[rate[2]]
workload_counts_valid = int(counts[3]) == int(counts[4]) > 0 and all(int(counts[i]) == 0 for i in (5,6,7)) and int(codes[1]) >= int(counts[3]) and all(int(codes[i]) == 0 for i in (2,3,4))
record_log = (root / 'perf-record.log').read_text()
samples = re.search(r'\((\d+) samples\)', record_log)
expected_binary = '05fe27108dd833f86493aa2d00eefe7efef9df47805be198ef3eacd09042c479'
cpu = original['after_load']['cpu_seconds'] - original['before_load']['cpu_seconds']
recovery = dict(scope=__doc__, original_runner_valid=False, original_summary_sha256=original_summary_sha,
    original_error=original['error'], original_perf_record_exitcode=None,
    exitcode_limitation='Original runner asserted profiler.wait()==0 without persisting its nonzero value; the actual code cannot be recovered.',
    source_snapshot_head=source['head'], source_diff_sha256=source['diff_sha256'],
    source_input_count=len(source['sources']), current_source_mismatches=rows, production_source_mismatches=production_mismatches,
    binary_sha256=sha(root / 'arena_h3_server'), binary_matches_expected=sha(root / 'arena_h3_server') == expected_binary == original['binary_sha256'],
    data_sha256=sha(root / 'static-perf.data'), data_bytes=(root / 'static-perf.data').stat().st_size,
    captured_samples=int(samples[1]) if samples else None, perf_report=report_codes,
    workload=dict(counts_valid=workload_counts_valid, total=int(counts[1]), started=int(counts[2]), done=int(counts[3]), succeeded=int(counts[4]),
        failed=int(counts[5]), errored=int(counts[6]), timeout=int(counts[7]), status_2xx=int(codes[1]), header_completion_delta=int(codes[1])-int(counts[3]),
        printed_rps=float(rate[3]), elapsed_seconds=elapsed, completed_per_elapsed_second=int(counts[3])/elapsed,
        server_cpu_seconds=cpu, cpu_microseconds_per_completed=cpu*1e6/int(counts[3])),
    limitations=['Original valid=false remains unchanged; this is separately assessed offline evidence.', 'No original profiler exit code is invented.', 'Single perturbed diagnostic run, not a performance acceptance or ranking.', 'Frame-pointer call graphs can truncate optimized frames; self percentages are not exact allocation attribution.'])
recovery['usable_for_qualified_cpu_diagnosis'] = workload_counts_valid and recovery['binary_matches_expected'] and not production_mismatches and all(item['returncode']==0 for item in report_codes.values())
assert sha(root / 'summary.json') == original_summary_sha
(out / 'assessment.json').write_text(json.dumps(recovery, indent=2) + '\n')
print(json.dumps(recovery, indent=2))
print('SELF_REPORT_START')
print((out / 'static-perf-self.txt').read_text()[:14500])
