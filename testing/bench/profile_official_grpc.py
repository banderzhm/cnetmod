#!/usr/bin/env python3
"""Separate software-perf diagnostics; never substitute these RPS for scores."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import re
import signal
import socket
import subprocess
import time

from analyze_official_grpc import parse_log
from grpc_quiet_host import QuietHost


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--official', type=Path, required=True)
    parser.add_argument('--binaries', type=Path, required=True)
    parser.add_argument('--client-image', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=32)
    parser.add_argument('--server-cpus')
    parser.add_argument('--call-graph', choices=['fp', 'dwarf,8192'], default='fp')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    resource.setrlimit(resource.RLIMIT_NOFILE, (1048576, 1048576))
    image = subprocess.check_output(['docker', 'image', 'inspect', '--format', '{{.Id}}',
                                     args.client_image], text=True).strip()
    official = args.official.resolve()
    sha = subprocess.check_output(['git', '-C', str(official), 'rev-parse', 'HEAD'], text=True).strip()
    if sha != '8977be2721a5aba7b320345cddfcd6f6b654e698':
        raise ValueError('Unpinned official checkout')
    wrapper = (f'docker run --rm --name cnetmod-grpc-profile-client --network host '
               f'--cpuset-cpus=16-31 --security-opt seccomp=unconfined --ulimit memlock=-1:-1 '
               f'--ulimit nofile=1048576:1048576 -v {official}/requests:{official}/requests:ro {image}')
    env = dict(os.environ, H2LOAD_CMD=wrapper, H2THREADS='64')
    script = ('source scripts/lib/common.sh; source scripts/lib/tools/h2load.sh; '
              'h2load_build_args grpc 1024 1 5s')
    client = subprocess.check_output(['bash', '-c', script], cwd=official, env=env, text=True).splitlines()
    records = []
    with QuietHost(args.output / 'quiet-host.json'):
        for name, binary in json.loads(args.binaries.read_text()).items():
            # An io_uring listener can briefly outlive its reaped owner while
            # the kernel retires outstanding accepts. Never stop a listener.
            for attempt in range(20):
                if not subprocess.check_output(['ss', '-Hltn', 'sport = :8080'], text=True).strip():
                    break
                time.sleep(.1)
            else:
                raise RuntimeError('Another listener occupies port 8080')
            directory = args.output / name
            directory.mkdir()
            server_command = [binary, '8080', str(args.workers)]
            if args.server_cpus:
                server_command = ['taskset', '-c', args.server_cpus, *server_command]
            with (directory / 'server.log').open('w') as server_log:
                server = subprocess.Popen(server_command, stdout=server_log, stderr=server_log)
                perf = None
                try:
                    for _ in range(100):
                        if server.poll() is not None:
                            raise RuntimeError('Server exited during startup')
                        try:
                            with socket.create_connection(('localhost', 8080), timeout=.1):
                                break
                        except OSError:
                            time.sleep(.05)
                    else:
                        raise RuntimeError('Server readiness timed out')
                    # listen() precedes server_context::run(): allow worker
                    # creation to finish before attaching to all existing TIDs.
                    time.sleep(.5)
                    (directory / 'attached-tids.json').write_text(json.dumps(
                        sorted(int(path.name) for path in Path(f'/proc/{server.pid}/task').iterdir())))
                    command = ['perf', 'record', '-e', 'cpu-clock', '-F', '499', '--call-graph', args.call_graph,
                               '-p', str(server.pid), '-o', str(directory / 'perf.data')]
                    with (directory / 'perf-record.log').open('w') as log:
                        perf = subprocess.Popen(command, stdout=log, stderr=log)
                        time.sleep(.3)
                        with (directory / 'h2load.stdout').open('w') as output:
                            subprocess.run(client, stdout=output, stderr=subprocess.STDOUT,
                                           check=True, timeout=45)
                        perf.send_signal(signal.SIGINT)
                        perf.wait(timeout=15)
                    coverage = re.search(r'(\d+) samples', (directory / 'perf-record.log').read_text())
                    if coverage is None or int(coverage[1]) < 1000:
                        raise RuntimeError('Insufficient worker sampling coverage; exclude this profile')
                    wrapped = directory / 'round.log'
                    wrapped.write_text(f'=== {name} / unary-grpc / 1024c ===\n[run 1/3]\n' +
                                       (directory / 'h2load.stdout').read_text())
                    row = parse_log(wrapped)[0]
                    if not row['valid'] or not row['progressing_beyond_initial_batches']:
                        raise ValueError('Failed or stalled profiling load')
                    for mode in ['self', 'inclusive']:
                        with (directory / f'perf-{mode}.txt').open('w') as report:
                            subprocess.run(['perf', 'report', '--stdio', '-i', str(directory / 'perf.data'),
                                            '--no-children' if mode == 'self' else '--children',
                                            '--call-graph', 'none', '--percent-limit', '.25'], stdout=report,
                                           stderr=subprocess.STDOUT, check=True)
                    records.append(dict(entry=name, binary=binary,
                        binary_sha256=hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
                        server_command=server_command, client_command=client, perf_command=command,
                        perf_sha256=hashlib.sha256((directory / 'perf.data').read_bytes()).hexdigest(),
                        load=row, official_sha=sha, diagnostic_only=True,
                        scope=f'Native {args.workers}-worker server; CPUs {args.server_cpus or "all eligible"}; same official client argv; sampling adds overhead'))
                    (args.output / 'profiles.json').write_text(json.dumps(records, indent=2))
                    print(name, 'profile complete', flush=True)
                finally:
                    if perf is not None and perf.poll() is None:
                        perf.send_signal(signal.SIGINT)
                        perf.wait(timeout=15)
                    server.terminate()
                    try:
                        server.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait(timeout=5)


if __name__ == '__main__':
    main()
