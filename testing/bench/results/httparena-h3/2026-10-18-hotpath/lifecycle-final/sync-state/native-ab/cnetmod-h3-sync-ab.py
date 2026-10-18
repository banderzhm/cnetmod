"""Serial native A/B; fixed workload, fresh servers, not an official ranking."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import time
import uuid
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--before', required=True, type=Path)
p.add_argument('--before-sha', required=True)
p.add_argument('--after', required=True, type=Path)
p.add_argument('--after-sha', required=True)
a = p.parse_args()
out = Path('/var/tmp/cnetmod-h3-sync-ab-' + uuid.uuid4().hex)
out.mkdir()
arena = Path('/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena')
h2load = '/var/tmp/cnetmod-h3-loadgen169/h2load'
report = {'scope': __doc__, 'rounds': [], 'valid': False,
          'order': 'before/after, after/before, before/after for each profile',
          'binary_sha256': {}}
for label in ('before', 'after'):
    source = getattr(a, label)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    assert digest == getattr(a, label + '_sha').lower(), (label, digest)
    shutil.copy2(source, out / label)
    report['binary_sha256'][label] = digest
report['loadgen_sha256'] = hashlib.sha256(Path(h2load).read_bytes()).hexdigest()
report['gai_conf_sha256'] = hashlib.sha256(Path('/etc/gai.conf').read_bytes()).hexdigest()
report['fixture_sha256'] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
    for path in sorted(arena.rglob('*')) if path.is_file()
    and any(part in ('static', 'requests', 'certs') for part in path.parts)}
anchor = 'cnetmod-sync-ab-' + uuid.uuid4().hex
image = 'sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
subprocess.run(['docker', 'run', '-d', '--name', anchor, '--network', 'none',
                '--entrypoint', '/bin/sleep', image, 'infinity'], check=True,
               stdout=subprocess.DEVNULL)
pid = subprocess.check_output(['docker', 'inspect', anchor, '--format',
                               '{{.State.Pid}}'], text=True).strip()
prefix = ['nsenter', '-t', pid, '-n']
server = None
try:
    for profile in ('baseline-h3', 'static-h3'):
        for pair, order in enumerate((('before', 'after'), ('after', 'before'),
                                     ('before', 'after')), 1):
            for label in order:
                name = f'{profile}-{pair}-{label}'
                server_args = prefix + [str(out / label), str(arena / 'certs/server.crt'),
                    str(arena / 'certs/server.key'), '8443', '32']
                if profile == 'static-h3':
                    server_args.append(str(arena / 'data/static'))
                with (out / (name + '-server.log')).open('w') as log:
                    server = subprocess.Popen(server_args, stdout=log, stderr=subprocess.STDOUT)
                    for _ in range(200):
                        sockets = subprocess.check_output(prefix + ['ss', '-H', '-u', '-l',
                            '-n', 'sport = :8443'], text=True)
                        if len(sockets.splitlines()) == 32:
                            break
                        assert server.poll() is None
                        time.sleep(.05)
                    else:
                        raise AssertionError('32 workers not ready')
                    ready = subprocess.run(prefix + [h2load, '--alpn-list=h3',
                        'https://localhost:8443/baseline2?a=1&b=1', '-n', '1',
                        '-c', '1', '-m', '1', '-t', '1'], capture_output=True,
                        text=True, timeout=15)
                    assert ready.returncode == 0 and re.search(r'\b1 2xx', ready.stdout)
                    time.sleep(2)
                    command = prefix + ['taskset', '-c', '16-31', h2load, '--alpn-list=h3']
                    if profile == 'static-h3':
                        command += ['-i', str(arena / 'requests/static-h2-uris.txt'),
                                    '-H', 'Accept-Encoding: br;q=1, gzip;q=0.8']
                    else:
                        command += ['https://localhost:8443/baseline2?a=1&b=1']
                    command += ['-c', '64', '-m', '64', '-t', '64', '-D', '5s']
                    result = subprocess.run(command, capture_output=True, text=True, timeout=50)
                    text = result.stdout + result.stderr
                    (out / (name + '-h2load.log')).write_text(text)
                    assert result.returncode == 0 and all(re.search(r'\b0 ' + field, text)
                        for field in ('failed', 'errored', 'timeout')), text[-2000:]
                    rate = re.search(r'finished in ([\d.]+)(ms|s|m),\s*([\d.]+) req/s', text)
                    counts = re.search(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout', text)
                    statuses = re.search(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx', text)
                    assert rate and counts and statuses, text[-2000:]
                    done, succeeded = int(counts[3]), int(counts[4])
                    successful = int(statuses[1])
                    assert done == succeeded and done > 0 and successful >= done
                    assert all(int(statuses[i]) == 0 for i in (2, 3, 4))
                    elapsed = float(rate[1]) * {'ms': .001, 's': 1, 'm': 60}[rate[2]]
                    smaps = Path(f'/proc/{server.pid}/smaps_rollup').read_text()
                    (out / (name + '-smaps.txt')).write_text(smaps)
                    row = dict(profile=profile, pair=pair, variant=label,
                               printed_rps=float(rate[3]), elapsed_seconds=elapsed,
                               done=done, succeeded=succeeded, status_2xx=successful,
                               header_completion_delta=successful - done,
                               completed_per_elapsed_second=done / elapsed,
                               status_2xx_per_elapsed_second=successful / elapsed,
                               command=command, server_command=server_args)
                    report['rounds'].append(row)
                    print(json.dumps(row), flush=True)
                    (out / 'summary.json').write_text(json.dumps(report, indent=2))
                    server.terminate()
                    server.wait(timeout=10)
                    server = None
                time.sleep(2)
    report['valid'] = True
finally:
    if server is not None and server.poll() is None:
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
    subprocess.run(['docker', 'rm', '-f', anchor], check=True, stdout=subprocess.DEVNULL)
    (out / 'summary.json').write_text(json.dumps(report, indent=2))
    print('RESULTS', out, flush=True)
