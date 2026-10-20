"""One short static CPU profile of an explicitly supplied current frozen binary.
Prepared runner only. Execute serially after correctness/official runs; not ranking.
Source snapshot is Windows-git metadata.json plus source.diff; no WSL git invocation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import subprocess
import time
import uuid


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def profile_limits():
    resource.setrlimit(resource.RLIMIT_FSIZE, (256 * 1024 * 1024,) * 2)


def sample(pid):
    fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    smaps = Path(f'/proc/{pid}/smaps_rollup').read_text()
    return dict(cpu_seconds=(int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK'),
        memory_kib={key: int(value) for key, value in re.findall(r'^(Rss|Pss|Private_Clean|Private_Dirty):\s+(\d+) kB$', smaps, re.M)})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, required=True)
    p.add_argument('--binary-sha', required=True)
    p.add_argument('--gso', choices=('off', 'on'), default='off')
    p.add_argument('--allocator-proof', type=Path, required=True)
    p.add_argument('--source-snapshot', type=Path, required=True)
    p.add_argument('--source-root', type=Path, default=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod'))
    p.add_argument('--arena', type=Path, default=Path('/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena'))
    a = p.parse_args()
    assert a.binary.is_file() and sha(a.binary) == a.binary_sha.lower()
    assert a.binary.read_bytes()[:4] == b'\x7fELF'
    allocator_proof = json.loads(a.allocator_proof.read_text(encoding='utf-8-sig'))
    assert allocator_proof['system_allocator_verified'] and allocator_proof['cmake_use_mimalloc_off']
    assert allocator_proof['binary_sha256'].lower() == a.binary_sha.lower()
    snapshot_metadata = a.source_snapshot / 'metadata.json'
    snapshot_diff = a.source_snapshot / 'source.diff'
    snapshot = json.loads(snapshot_metadata.read_text(encoding='utf-8-sig'))
    assert re.fullmatch(r'[0-9a-f]{40}', snapshot['head'])
    assert sha(snapshot_diff) == snapshot['diff_sha256'].lower()
    source_inputs = [dict(path=path, sha256=digest) for path, digest in snapshot['sources'].items()]
    assert len(source_inputs) >= 4
    assert {'src/protocol/quic/quic_connection.cpp', 'src/protocol/quic/quic_connection.cppm'} <= {i['path'] for i in source_inputs}
    for item in source_inputs:
        resolved = (a.source_root / item['path']).resolve()
        assert resolved.is_relative_to(a.source_root.resolve())
        assert sha(resolved) == item['sha256'].lower(), item['path']
    assert set(range(16, 32)) <= os.sched_getaffinity(0), 'fixed client CPU set16-31 is unavailable'
    assert shutil.disk_usage('/var/tmp').free >= 2 * 1024**3
    root = Path('/var/tmp/cnetmod-h3-system-perf-' + uuid.uuid4().hex)
    root.mkdir()
    binary = root / 'arena_h3_server'
    shutil.copy2(a.binary, binary)
    shutil.copy2(snapshot_metadata, root / 'supplied-source-metadata.json')
    shutil.copy2(a.allocator_proof, root / 'allocator-proof.json')
    shutil.copy2(snapshot_diff, root / 'actual-dirty-source.patch')
    perf_path = Path('/usr/sbin/perf')
    h2load = Path('/var/tmp/cnetmod-h3-loadgen169/h2load')
    assert perf_path.is_file() and h2load.is_file()
    image = 'sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
    subprocess.run(['docker', 'image', 'inspect', image], check=True, stdout=subprocess.DEVNULL)
    name = 'cnetmod-current-perf-' + uuid.uuid4().hex
    server = profiler = None
    anchor_started = False
    result = dict(valid=False, scope=__doc__, binary_source=str(a.binary), binary_sha256=sha(binary),
        source_snapshot_sha256=sha(snapshot_metadata), source_diff_sha256=sha(snapshot_diff),
        allocator='system', allocator_proof_sha256=sha(a.allocator_proof),
        actual_head=snapshot['head'], source_inputs=source_inputs,
        loadgen_sha256=sha(h2load), perf_sha256=sha(perf_path), gai_conf_sha256=sha('/etc/gai.conf'),
        workers=32, connections=64, max_streams=64, client_threads=64, duration_seconds=5,
        client_cpus='16-31', perf_frequency=99, call_graph='fp', image=image,
        fixture_sha256={str(f.relative_to(a.arena)): sha(f) for directory in ('certs', 'requests', 'data/static') for f in sorted((a.arena / directory).rglob('*')) if f.is_file()})
    try:
        subprocess.run(['docker', 'run', '--pull=never', '-d', '--name', name, '--network', 'none', '--entrypoint', '/bin/sleep', image, 'infinity'], check=True, stdout=subprocess.DEVNULL)
        anchor_started = True
        ns_pid = subprocess.check_output(['docker', 'inspect', name, '--format', '{{.State.Pid}}'], text=True).strip()
        prefix = ['nsenter', '-t', ns_pid, '-n']
        command = prefix + [str(binary), str(a.arena / 'certs/server.crt'), str(a.arena / 'certs/server.key'), '8443', '32', str(a.arena / 'data/static')]
        result['server_command'] = command
        environment = os.environ.copy()
        assert not environment.get('LD_PRELOAD'), 'unexpected preload in system allocator experiment'
        environment.pop('CNETMOD_QUIC_GSO', None)
        if a.gso == 'on':
            environment['CNETMOD_QUIC_GSO'] = '1'
        result['gso'] = a.gso
        with (root / 'server.log').open('w') as server_log, (root / 'perf-record.log').open('w') as perf_log:
            server = subprocess.Popen(command, env=environment, stdout=server_log, stderr=subprocess.STDOUT)
            result['server_pid'] = server.pid
            for _ in range(200):
                assert server.poll() is None, 'server exited before32 sockets ready'
                sockets = subprocess.check_output(prefix + ['ss', '-H', '-u', '-l', '-n', 'sport = :8443'], text=True)
                if len(sockets.splitlines()) == 32:
                    break
                time.sleep(.05)
            else:
                raise RuntimeError('32 workers not ready')
            ready = subprocess.run(prefix + [str(h2load), '--alpn-list=h3', 'https://localhost:8443/baseline2?a=1&b=1', '-n', '1', '-c', '1', '-m', '1', '-t', '1'], capture_output=True, text=True, timeout=15)
            (root / 'readiness.log').write_text(ready.stdout + ready.stderr)
            assert ready.returncode == 0 and re.search(r'requests: 1 total, 1 started, 1 done, 1 succeeded, 0 failed, 0 errored, 0 timeout', ready.stdout)
            time.sleep(2)
            workload = prefix + ['taskset', '-c', '16-31', str(h2load), '--alpn-list=h3', '-i', str(a.arena / 'requests/static-h2-uris.txt'), '-H', 'Accept-Encoding: br;q=1, gzip;q=0.8', '-c', '64', '-m', '64', '-t', '64', '-D', '5s']
            perf_command = [str(perf_path), 'record', '-e', 'cpu-clock', '-F', '99', '--call-graph', 'fp', '-p', str(server.pid), '-o', str(root / 'static-perf.data')]
            result.update(workload_command=workload, perf_command=perf_command)
            before = sample(server.pid)
            profiler = subprocess.Popen(perf_command, stdout=perf_log, stderr=subprocess.STDOUT, preexec_fn=profile_limits)
            time.sleep(.25)
            assert profiler.poll() is None, 'perf attachment failed before load'
            print('PROFILE START', root, flush=True)
            load = subprocess.run(workload, capture_output=True, text=True, timeout=50)
            (root / 'h2load.log').write_text(load.stdout + load.stderr)
            result['after_load'] = sample(server.pid)
            result['before_load'] = before
            print('PROFILE LOAD DONE', load.returncode, flush=True)
            profiler.send_signal(signal.SIGINT)
            result['perf_record_exit_code'] = profiler.wait(timeout=15)
            assert result['perf_record_exit_code'] in (0, -signal.SIGINT, 128 + signal.SIGINT), 'unexpected perf termination; inspect perf-record.log'
            result['perf_stop_requested'] = 'SIGINT after workload completion'
            profiler = None
            text = load.stdout + load.stderr
            counts = re.search(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout', text)
            codes = re.search(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx', text)
            rate = re.search(r'finished in ([\d.]+)(ms|s|m),\s*([\d.]+) req/s', text)
            assert load.returncode == 0 and counts and codes and rate
            assert int(counts[3]) == int(counts[4]) > 0 and all(int(counts[i]) == 0 for i in (5, 6, 7))
            assert int(codes[1]) >= int(counts[3]) and all(int(codes[i]) == 0 for i in (2, 3, 4))
            elapsed = float(rate[1]) * {'ms': .001, 's': 1, 'm': 60}[rate[2]]
            cpu_seconds = result['after_load']['cpu_seconds'] - before['cpu_seconds']
            result.update(done=int(counts[3]), status_2xx=int(codes[1]), elapsed_seconds=elapsed,
                printed_rps=float(rate[3]), completed_per_elapsed_second=int(counts[3])/elapsed,
                server_cpu_seconds=cpu_seconds, cpu_microseconds_per_completed=cpu_seconds*1e6/int(counts[3]))
            assert (root / 'static-perf.data').stat().st_size < 256 * 1024**2
            for label, options in [('self', ['--no-children', '-g', 'none']), ('stacks', ['-g', 'graph,0.5,caller'])]:
                with (root / f'static-perf-{label}.txt').open('w') as output:
                    subprocess.run([str(perf_path), 'report', '--stdio', '--percent-limit', '.5', '--sort', 'symbol,dso', '-i', str(root / 'static-perf.data'), *options], stdout=output, stderr=subprocess.STDOUT, check=True, timeout=60)
            assert sha(a.binary) == result['binary_sha256'], 'supplied freeze changed during profile'
            assert all(sha(a.source_root / i['path']) == i['sha256'].lower() for i in source_inputs)
            assert sha(snapshot_metadata) == result['source_snapshot_sha256']
            assert sha(snapshot_diff) == result['source_diff_sha256']
            with (root / 'static-perf-header.txt').open('w') as header:
                subprocess.run([str(perf_path), 'report', '--stdio', '--header-only', '-i', str(root / 'static-perf.data')], stdout=header, stderr=subprocess.STDOUT, check=True, timeout=30)
            result['valid'] = True
    except BaseException as error:
        result['error'] = repr(error)
        raise
    finally:
        if profiler is not None and profiler.poll() is None:
            profiler.send_signal(signal.SIGINT)
            try:
                profiler.wait(timeout=10)
            except subprocess.TimeoutExpired:
                stop(profiler)
        stop(server)
        if anchor_started:
            cleanup = subprocess.run(['docker', 'rm', '-f', name], capture_output=True, text=True)
            result['namespace_cleanup'] = dict(returncode=cleanup.returncode, stdout=cleanup.stdout, stderr=cleanup.stderr)
            if cleanup.returncode:
                result['valid'] = False
        result['limitations'] = ['Single perturbed diagnostic run; no ranking or stable speedup claim.', 'fp call graphs may truncate optimized frames; self samples are more reliable than inclusive allocation attribution.', 'No syscall/packet/allocator count is inferred from sampling percentages.']
        (root / 'summary.json').write_text(json.dumps(result, indent=2))
        print('PROFILE RESULTS', root, 'valid=' + str(result['valid']), flush=True)
    return 0 if result['valid'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
