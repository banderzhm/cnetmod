"""Replay of measured suite; requires the exclusive performance token."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('action', choices=['profile', 'matrix'])
p.add_argument('label')
p.add_argument('binary')
p.add_argument('revision')
a = p.parse_args()
repo = Path(__file__).resolve().parents[4]
root = Path('/root/cnetmod-grpc-artifacts')
out = root / a.label
out.mkdir(exist_ok=False)
cert, key = root / 'cert.pem', root / 'key.pem'
if not cert.exists():
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                    '-keyout', str(key), '-out', str(cert), '-days', '2',
                    '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
                   check=True, capture_output=True)
python = '/opt/cnetmod-ci-venv/bin/python'
h2 = str(root / 'h2load')
payload = root / 'grpc-sum.bin'
payload.write_bytes(bytes.fromhex('000000000408011002'))
for tls in ([False] if a.action == 'profile' else [False, True]):
    mode = 'tls' if tls else 'clear'
    url = ('https' if tls else 'http') + '://localhost:18123'
    cmd = ['taskset', '-c', '0-3', a.binary, '18123', '4']
    if tls:
        cmd += [str(cert), str(key)]
    with (out / (mode + '-server.log')).open('w') as log:
        server = subprocess.Popen(cmd, stdout=log, stderr=log)
        try:
            check = [python, str(repo / 'testing/bench/grpc_arena_check.py'), 'localhost:18123']
            if tls:
                check += ['--cert', str(cert)]
            subprocess.run(check, check=True, timeout=20)
            if a.action == 'matrix':
                for connections in [256, 1024]:
                    run = [python, str(repo / 'testing/bench/grpc_arena_bench.py'),
                           '--server-pid', str(server.pid), '--server-binary', a.binary,
                           '--revision', a.revision, '--server-cpus', '0-3',
                           '--client-cpus', '4-7', '--threads', '4',
                           '--connections', str(connections), '--url', url,
                           '--h2load', h2, '--output', str(out / f'{mode}-{connections}')]
                    if tls:
                        run += ['--cert', str(cert)]
                    subprocess.run(run, check=True)
            else:
                load = ['taskset', '-c', '4-7', h2, url + '/benchmark.BenchmarkService/GetSum',
                        '-d', str(payload), '-H', 'content-type: application/grpc',
                        '-H', 'te: trailers', '-c', '256', '-m', '100', '-t', '4', '-D', '8', '-p', 'h2c']
                commands = []
                for kind, prefix in [('record', ['perf', 'record', '-e', 'cpu-clock', '-F', '499', '-g',
                                                  '-p', str(server.pid), '-o', str(out / 'perf.data'), '--']),
                                     ('stat', ['perf', 'stat', '-e', 'task-clock,context-switches,cpu-migrations,page-faults',
                                               '-p', str(server.pid), '-o', str(out / 'perf-stat.txt'), '--'])]:
                    commands.append(prefix + load)
                    with (out / f'{kind}-h2load.txt').open('w') as record:
                        subprocess.run(prefix + load, stdout=record, stderr=subprocess.STDOUT, check=True, timeout=30)
                (out / 'commands.json').write_text(json.dumps(commands, indent=2))
                with (out / 'perf-report.txt').open('w') as report:
                    subprocess.run(['perf', 'report', '-i', str(out / 'perf.data'), '--stdio', '--no-children',
                                    '--percent-limit', '0.4'], stdout=report, check=True)
                with (out / 'perf-children.txt').open('w') as report:
                    subprocess.run(['perf', 'report', '-i', str(out / 'perf.data'), '--stdio',
                                    '--percent-limit', '1'], stdout=report, check=True)
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=5)
print('DONE', a.action, a.label)
