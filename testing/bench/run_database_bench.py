"""Serial, paired real-server benchmarks. Run only with the exclusive test token.

Requires Linux Docker, taskset, and already-built control/candidate executables.
Never builds, tunes the host, prunes Docker, or stops unrelated containers.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import resource
import socket
import statistics
import subprocess
import time

from grpc_quiet_host import QuietHost


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--control', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=3)
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--profiles', action='store_true')
    args = parser.parse_args()
    if args.seconds < 1 or args.rounds < 3:
        parser.error('at least one second and three rounds required')
    args.output.mkdir(parents=True, exist_ok=False)
    records = []
    owned = []
    metadata = {'binaries': {}, 'servers': [], 'seconds': args.seconds,
                'rounds': args.rounds, 'client_cpus': '0', 'source_of_truth': 'validated operations per measured wall time',
                'note': 'Pool tests exclude queries. MySQL pool skips reset explicitly; reset is a separate case. Redis batch counts batches of 64 commands.'}

    def command(argv, **kw):
        try:
            return subprocess.run([str(x) for x in argv], text=True, capture_output=True,
                                  check=True, timeout=kw.pop('timeout', 60), **kw)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            (args.output / 'failure.json').write_text(json.dumps({'argv': [str(x) for x in argv],
                'error': str(error), 'stdout': str(error.stdout), 'stderr': str(error.stderr)}, indent=2))
            raise

    servers = [
        ('postgresql', 'postgres:18', 15432, '16-19',
         ['-e', 'POSTGRES_USER=bench', '-e', 'POSTGRES_PASSWORD=bench', '-e', 'POSTGRES_DB=benchmark'],
         ['postgres', '-p', '15432', '-c', 'listen_addresses=127.0.0.1', '-c', 'max_connections=96', '-c', 'shared_buffers=128MB'],
         ['pg_isready', '-p', '15432', '-U', 'bench', '-d', 'benchmark']),
        ('mysql', 'mysql:8.4', 13306, '20-23',
         ['-e', 'MYSQL_ROOT_PASSWORD=bench', '-e', 'MYSQL_USER=bench', '-e', 'MYSQL_PASSWORD=bench', '-e', 'MYSQL_DATABASE=benchmark'],
         ['--port=13306', '--bind-address=127.0.0.1', '--mysqlx=OFF', '--max-connections=96', '--innodb-buffer-pool-size=128M', '--performance-schema=OFF', '--skip-log-bin'],
         ['mysqladmin', 'ping', '-h127.0.0.1', '-P13306', '-uroot', '-pbench']),
        ('redis', 'redis:8', 16379, '24-27', [],
         ['redis-server', '--bind', '127.0.0.1', '--port', '16379', '--save', '', '--appendonly', 'no', '--maxmemory', '256mb'],
         ['redis-cli', '-p', '16379', 'PING']),
    ]
    try:
        for entry, binary in [('control', args.control), ('candidate', args.candidate)]:
            metadata['binaries'][entry] = {'path': str(binary.resolve()),
                                         'sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
        for database, image, port, cpus, environment, extra, health in servers:
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', port))
            inspection = json.loads(command(['docker', 'image', 'inspect', image]).stdout)[0]
            name = f'cnetmod-db-perf-{database}-20261020'
            argv = ['docker', 'run', '-d', '--rm', '--name', name, '--network', 'host',
                    '--cpuset-cpus', cpus, '--cpus', '4', '--memory', '1536m',
                    *environment, inspection['Id'], *extra]
            container = command(argv).stdout.strip()
            owned.append((name, container))
            metadata['servers'].append({'database': database, 'image': image,
                'id': inspection['Id'], 'repo_digests': inspection.get('RepoDigests'), 'argv': argv, 'container': container})
            deadline = time.monotonic() + 100
            while True:
                status = subprocess.run(['docker', 'exec', container, *health], text=True, capture_output=True)
                if status.returncode == 0:
                    break
                if time.monotonic() > deadline:
                    raise RuntimeError(f'{database} did not become ready: {status.stderr}')
                time.sleep(1)
            if database == 'mysql':
                # The existing plaintext client supports caching_sha2 fast
                # authentication, while its full-password exchange requires
                # TLS. Populate the server cache with the official TLS client
                # before either binary connects; authentication is not timed.
                bootstrap = ['docker', 'exec', container, 'mysql', '--ssl-mode=REQUIRED',
                             '-h127.0.0.1', '-P13306', '-ubench', '-pbench', '-e', 'SELECT 1']
                status = command(bootstrap)
                metadata['servers'][-1]['auth_bootstrap'] = bootstrap
                (args.output / 'mysql-auth-bootstrap.log').write_text(status.stdout + status.stderr)
        (args.output / 'environment.json').write_text(json.dumps(metadata, indent=2))
        cases = [(db, 'pool', cap) for db in ['postgresql', 'mysql', 'redis'] for cap in [1, 64]]
        cases += [('postgresql', 'query', 1), ('postgresql', 'rows', 1),
                  ('mysql', 'query', 1), ('mysql', 'reset', 1), ('redis', 'query', 1), ('redis', 'batch', 1)]
        with QuietHost(args.output / 'quiet-host.json'):
            for database, mode, capacity in cases:
                for run in range(1, args.rounds + 1):
                    order = ['control', 'candidate'] if run % 2 else ['candidate', 'control']
                    for entry in order:
                        binary = args.control if entry == 'control' else args.candidate
                        argv = ['taskset', '-c', '0', str(binary), database, mode, str(args.seconds), str(capacity)]
                        before = resource.getrusage(resource.RUSAGE_CHILDREN)
                        process_start = time.monotonic()
                        result = command(argv, timeout=args.seconds + 60)
                        process_elapsed = time.monotonic() - process_start
                        after = resource.getrusage(resource.RUSAGE_CHILDREN)
                        name = f'{database}-{mode}-{capacity}-{run}-{entry}'
                        (args.output / (name + '.log')).write_text(result.stdout + result.stderr)
                        match = re.search(r'RESULT database=(\w+) mode=(\w+) operations=(\d+) seconds=([\d.]+) ops_per_second=([\d.]+)', result.stdout + result.stderr)
                        if not match or match[1] != database or match[2] != mode or int(match[3]) == 0:
                            raise RuntimeError(f'invalid result: {name}')
                        record = {'database': database, 'mode': mode, 'capacity': capacity, 'round': run, 'entry': entry,
                                  'operations': int(match[3]), 'seconds': float(match[4]), 'ops_per_second': float(match[5]),
                                  'process_wall_seconds': process_elapsed,
                                  'child_user_seconds': after.ru_utime - before.ru_utime,
                                  'child_system_seconds': after.ru_stime - before.ru_stime,
                                  'argv': argv, 'log': name + '.log'}
                        records.append(record)
                        (args.output / 'rounds.json').write_text(json.dumps(records, indent=2))
                        print(name, match[5], flush=True)
                        time.sleep(.5)
            if args.profiles:
                for database, mode in [('postgresql', 'rows'), ('mysql', 'query'), ('redis', 'batch')]:
                    for entry in ['control', 'candidate']:
                        binary = args.control if entry == 'control' else args.candidate
                        stem = args.output / f'profile-{database}-{entry}'
                        workload = ['taskset', '-c', '0', str(binary), database, mode, '3', '1']
                        result = command(['strace', '-f', '-c', '-o', str(stem) + '-syscalls.txt', *workload])
                        (Path(str(stem) + '-load.log')).write_text(result.stdout + result.stderr)
                for entry in ['control', 'candidate']:
                    binary = args.control if entry == 'control' else args.candidate
                    stem = args.output / f'perf-pool-{entry}'
                    result = command(['perf', 'record', '-e', 'cpu-clock', '-F', '499', '--call-graph', 'fp', '-o', str(stem) + '.data',
                                      'taskset', '-c', '0', str(binary), 'redis', 'pool', '3', '64'])
                    (Path(str(stem) + '-record.log')).write_text(result.stdout + result.stderr)
                    for flag, suffix in [('--no-children', '-self.txt'), ('--children', '-inclusive.txt')]:
                        report = command(['perf', 'report', '--stdio', '-i', str(stem) + '.data', flag, '--sort', 'symbol'])
                        (Path(str(stem) + suffix)).write_text(report.stdout)
        summary = []
        for database, mode, capacity in cases:
            values = {entry: [r['ops_per_second'] for r in records if r['database'] == database and r['mode'] == mode and r['capacity'] == capacity and r['entry'] == entry]
                      for entry in ['control', 'candidate']}
            assert all(len(v) == args.rounds for v in values.values())
            medians = {entry: statistics.median(v) for entry, v in values.items()}
            summary.append({'database': database, 'mode': mode, 'capacity': capacity, 'rounds': values, 'median': medians,
                            'gain_percent': (medians['candidate'] / medians['control'] - 1) * 100})
        (args.output / 'summary.json').write_text(json.dumps(summary, indent=2))
    finally:
        cleaned = []
        for name, container in reversed(owned):
            logs = subprocess.run(['docker', 'logs', container], text=True, capture_output=True)
            (args.output / (name + '-server.log')).write_text(logs.stdout + logs.stderr)
            status = subprocess.run(['docker', 'stop', container], text=True, capture_output=True)
            cleaned.append({'name': name, 'id': container, 'stop_returncode': status.returncode})
        (args.output / 'cleanup.json').write_text(json.dumps(cleaned, indent=2))


if __name__ == '__main__':
    main()
