#!/usr/bin/env python3
"""Prepared only: pinned official H3 comparison, explicit PMTU/GSO and system allocation.

Execution requires --execute and exclusive ownership of the benchmark window.
Official requests, h2load, profile constants, three rounds and winner selection
are unchanged. Only staging/isolation, provenance and fail-closed gates differ.
"""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import signal
import shutil
import subprocess
import threading
import time
import uuid

REPO = Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod')
VALIDATION = Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-final-system-validation')
SNAPSHOT = VALIDATION / 'source-snapshot'
PROOF = VALIDATION / 'system-allocator/allocator-proof.json'
BUILD = Path('/var/tmp/cnetmod-h3-arena-build')
BINARY = BUILD / 'testing/bench/arena_h3_server'
BINARY_SHA = '5bf46aeeee227934742969972dfa3e4705d5fb87e66289c9109f41d89f353470'
H2LOAD = Path('/var/tmp/cnetmod-h3-loadgen169/h2load')
H2LOAD_SHA = '9a35cefa52e809a32f1898d65575a2cbbf54555031604830b852e2f6b5a4484f'
HELPER = Path('/mnt/c/Users/25007/.codex/tmp/run_official_h3_isolated.py')
HELPER_SHA = '1c1c2f367903e49a3e908a72a6aeccf261046620427a9c834c3c7d0c51ff7f45'
ISOLATED = Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-isolated.sh')
OFFICIAL = Path('/var/tmp/cnetmod-http-arena-8977be27')
OFFICIAL_SHA = '8977be2721a5aba7b320345cddfcd6f6b654e698'
OLD = Path('/var/tmp/cnetmod-h3-official')
PEERS = {
    'agensio-official-h3': 'sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504',
    'genhttp-ioxide-official-h3': 'sha256:72d6de94308798c44f38e98576f132ea1daf4bba07463011eadbb2f859beb8be',
}
REMOVED_ENV = ('SSLKEYLOGFILE', 'LD_PRELOAD', 'LD_AUDIT')
EXPECTED_ORDER = [('cnetmod-official-optimized', ['baseline-h3']),
                  ('cnetmod-official-static', ['static-h3']),
                  ('agensio-official-h3', ['baseline-h3', 'static-h3']),
                  ('genhttp-ioxide-official-h3', ['baseline-h3', 'static-h3'])]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')


def clean_environment():
    env = dict(os.environ)
    for key in (*REMOVED_ENV, 'CNETMOD_QUIC_GSO', 'CNETMOD_QUIC_MAX_PATH_MTU',
                'CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE', 'H2LOAD_H3_CMD'):
        env.pop(key, None)
    return env


def exact_replace(text, before, after, count=1):
    if text.count(before) != count:
        raise ValueError(f'staged official layout drift: expected {count} copies of {before!r}')
    return text.replace(before, after)


def parse_rounds(text):
    blocks = re.split(r'\[run (\d+)/3\]', text)
    if len(blocks) != 7:
        raise ValueError('expected exactly three original official rounds')
    rows = []
    for index in range(1, 7, 2):
        if int(blocks[index]) != len(rows) + 1:
            raise ValueError('official round order changed')
        body = blocks[index + 1]
        counts = re.findall(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout', body)
        codes = re.findall(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx', body)
        timing = re.findall(r'finished in ([\d.]+)s, ([\d.]+) req/s', body)
        if len(counts) != 1 or len(codes) != 1 or len(timing) != 1:
            raise ValueError('missing/ambiguous original h2load result')
        total, started, done, succeeded, failed, errored, timeout = map(int, counts[0])
        status = list(map(int, codes[0]))
        elapsed, printed_rps = map(float, timing[0])
        # Timed h2load can report started>total (the extra in-flight window).
        # Do not impose a fixed-request total==started gate on -D measurements.
        if not (total >= done and started >= done == succeeded > 0 and
                failed == errored == timeout == 0 and status[0] >= done and
                status[1:] == [0, 0, 0] and elapsed > 0):
            raise ValueError('official timed correctness gate failed')
        rows.append(dict(round=len(rows)+1, total=total, started=started, done=done,
                         succeeded=succeeded, failed=failed, errored=errored, timeout=timeout,
                         status_2xx=status[0], header_only_delta=status[0]-done,
                         elapsed_seconds=elapsed, printed_rps=printed_rps,
                         completed_rps=done/elapsed, official_valid_2xx_rps=status[0]/elapsed))
    return rows


class Run:
    def __init__(self, args):
        if not args.execute:
            raise ValueError('prepared runner: --execute and an exclusive run window are required')
        self.args = args
        self.nonce = uuid.uuid4().hex
        self.prefix = 'cnetmod-final-' + self.nonce
        self.root = args.output or Path(f'/var/tmp/cnetmod-h3-final-system-official-mtu{args.path_mtu}-gso{args.gso}-{self.nonce}')
        if not re.fullmatch(rf'/var/tmp/cnetmod-h3-final-system-official-mtu{args.path_mtu}-gso{args.gso}-[0-9a-f]{{32}}', str(self.root)):
            raise ValueError('output must be a fresh approved nonce path under /var/tmp')
        self.root.mkdir()
        self.stage = self.root / 'arena'
        self.records = self.stage / 'records'
        self.records.mkdir(parents=True)
        self.env = clean_environment()
        self.env['CNETMOD_OFFICIAL_RUN_NONCE'] = self.nonce
        self.label = 'cnetmod.official.run=' + self.nonce
        self.summary = dict(valid=False, allocator='system', binary_sha256=BINARY_SHA,
                            path_mtu=args.path_mtu, receive_ceiling=8192, gso=args.gso,
                            framework_order=EXPECTED_ORDER, profiles=[], commands=[],
                            selection='original official best valid-2xx RPS of three; completed rates also recorded')
        self.stop = threading.Event()
        self.thread = None

    def stop_owned_group(self, group):
        def members():
            rows = []
            for path in Path('/proc').iterdir():
                if not path.name.isdigit():
                    continue
                try:
                    pid = int(path.name)
                    if os.getpgid(pid) != group:
                        continue
                    stat = (path / 'stat').read_text().rpartition(')')[2].split()
                    if stat[0] == 'Z':
                        continue
                    marker = ('CNETMOD_OFFICIAL_RUN_NONCE=' + self.nonce).encode()
                    if os.getsid(pid) != group or marker not in (path / 'environ').read_bytes().split(b'\0'):
                        raise ValueError('refuse cancellation of unowned command group')
                    rows.append(pid)
                except (FileNotFoundError, ProcessLookupError):
                    pass
            return rows
        before = members()
        if not before:
            return
        for sig, budget in ((signal.SIGTERM, 2), (signal.SIGKILL, 2)):
            if not members():
                break
            os.killpg(group, sig)
            deadline = time.monotonic() + budget
            while members() and time.monotonic() < deadline:
                time.sleep(.02)
        after = members()
        self.summary.setdefault('command_group_cleanup', []).append(dict(group=group, before=before, after=after))
        if after:
            raise RuntimeError('owned command descendants remained alive')

    def persist(self):
        save(self.root / 'summary.json', self.summary)

    def command(self, argv, *, log=None, timeout=60, cwd=None, check=True):
        self.summary['commands'].append(dict(argv=list(map(str, argv)), cwd=str(cwd) if cwd else None))
        self.persist()
        stream = Path(log).open('wb') if log else None
        process = subprocess.Popen(list(map(str, argv)), cwd=cwd, env=self.env,
                                   stdout=stream if stream else subprocess.PIPE,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            if stream:
                process.wait(timeout=timeout)
                output = None
            else:
                output, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.stop_owned_group(process.pid)
            process.wait(timeout=5)
            raise
        finally:
            if stream:
                stream.close()
        # Official validator watchdog children can outlive an already-exited
        # parent. File stdout avoids EOF waits; retire only nonce-owned members.
        self.stop_owned_group(process.pid)
        if stream:
            output = Path(log).read_bytes()
        if check and process.returncode:
            raise subprocess.CalledProcessError(process.returncode, argv)
        return output.decode('utf-8', errors='replace'), process.returncode

    def inspect_image(self, image, cnetmod=False):
        text, _ = self.command(['docker', 'image', 'inspect', image])
        row = json.loads(text)[0]
        if image.startswith('sha256:') and row['Id'] != image:
            raise ValueError('pinned image identity mismatch')
        env = dict(item.split('=', 1) for item in (row['Config'].get('Env') or []) if '=' in item)
        if any(env.get(key) for key in REMOVED_ENV):
            raise ValueError('image contains forbidden tracing/loader environment')
        if cnetmod:
            expected = {'CNETMOD_QUIC_MAX_PATH_MTU': str(self.args.path_mtu),
                        'CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE': '8192',
                        'CNETMOD_QUIC_GSO': str(self.args.gso)}
            if any(env.get(k) != v for k, v in expected.items()):
                raise ValueError('image diagnostic ENV mismatch')
        return dict(id=row['Id'], entrypoint=row['Config'].get('Entrypoint'),
                    diagnostic_env={k: v for k, v in env.items() if k.startswith('CNETMOD_QUIC_')})

    def verify_inputs(self):
        metadata = json.loads((SNAPSHOT / 'metadata.json').read_text())
        if len(metadata['sources']) != 2209:
            raise ValueError('not the approved2209-source snapshot')
        if sha(SNAPSHOT / 'source.diff') != metadata['diff_sha256']:
            raise ValueError('source snapshot diff mismatch')
        if sha(BINARY) != BINARY_SHA or sha(H2LOAD) != H2LOAD_SHA or sha(HELPER) != HELPER_SHA:
            raise ValueError('frozen binary/h2load/helper identity changed')
        proof = json.loads(PROOF.read_text())
        if not (proof['system_allocator_verified'] is True and proof['cmake_use_mimalloc_off'] is True and
                proof['binary_sha256'] == BINARY_SHA and proof['noargs_returncode'] == 2 and
                proof['ld_preload'] is None and proof['ld_audit'] is None):
            raise ValueError('system allocator proof rejected')
        cache = (BUILD / 'CMakeCache.txt').read_text()
        if 'CNETMOD_USE_MIMALLOC:BOOL=OFF' not in cache:
            raise ValueError('actual build is not system allocator OFF')
        if 'mimalloc' in (PROOF.parent / 'arena-link-command.txt').read_text().lower():
            raise ValueError('allocator override appears in recorded actual link')
        bindings = (PROOF.parent / 'allocator-bindings.txt').read_text()
        if not re.search(r'binding file .*arena_h3_server .*to /usr/lib/libc\.so\.6 .*symbol .malloc.', bindings):
            raise ValueError('missing actual arena system malloc binding proof')
        for name, digest in proof['files'].items():
            if sha(BUILD / name) != digest:
                raise ValueError('allocator proof backing input changed: ' + name)
        inputs = {REPO / name: digest for name, digest in metadata['sources'].items()}
        inputs.update({BUILD / name: digest for name, digest in proof['files'].items()})
        inputs.update({path: sha(path) for path in (SNAPSHOT / 'metadata.json', SNAPSHOT / 'source.diff',
                      BINARY, H2LOAD, HELPER, ISOLATED, Path(__file__).resolve())})
        inputs.update({path: sha(path) for path in PROOF.parent.iterdir() if path.is_file()})
        for path, digest in inputs.items():
            if sha(path) != digest:
                raise ValueError('source/input drift: ' + str(path))
        self.inputs = {str(path): digest for path, digest in inputs.items()}
        self.summary['source_count'] = len(metadata['sources'])
        self.summary['actual_head'] = metadata['head']
        self.summary['source_diff_sha256'] = metadata['diff_sha256']
        save(self.records / 'inputs-before.json', self.inputs)
        shutil.copy2(SNAPSHOT / 'metadata.json', self.records / 'source-metadata.json')
        shutil.copy2(SNAPSHOT / 'source.diff', self.records / 'source.diff')
        shutil.copytree(PROOF.parent, self.records / 'system-allocator')
        shutil.copy2(Path(__file__), self.records / 'runner.py')
        shutil.copy2(HELPER, self.records / 'original-helper.py')

    def make_image(self, name, arguments):
        context = self.root / name
        (context / 'lib').mkdir(parents=True)
        shutil.copy2(BINARY, context / 'program')
        if sha(context / 'program') != BINARY_SHA:
            raise ValueError('copied server identity mismatch')
        dependencies, _ = self.command(['ldd', str(BINARY)], log=self.records / (name + '-ldd.log'))
        if 'not found' in dependencies or 'mimalloc' in dependencies.lower():
            raise ValueError('server dependency or allocator drift')
        for path in re.findall(r'=> (/\S+)', dependencies):
            shutil.copy2(path, context / 'lib' / Path(path).name)
            self.inputs[path] = sha(path)
        shutil.copy2('/lib64/ld-linux-x86-64.so.2', context / 'loader')
        shutil.copy2(OLD / 'gai.conf', context / 'gai.conf')
        entrypoint = ['/lib64/ld-linux-x86-64.so.2', '--library-path', '/lib', '/program', *arguments]
        (context / 'Dockerfile').write_text(
            'FROM scratch\nCOPY program /program\nCOPY lib/ /lib/\n'
            'COPY loader /lib64/ld-linux-x86-64.so.2\nCOPY gai.conf /etc/gai.conf\n'
            f'LABEL cnetmod.official.run="{self.nonce}"\n'
            f'ENV CNETMOD_QUIC_MAX_PATH_MTU={self.args.path_mtu} CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE=8192 CNETMOD_QUIC_GSO={self.args.gso}\n'
            'ENTRYPOINT ' + json.dumps(entrypoint) + '\n')
        tag = self.prefix + '-' + name
        self.command(['docker', 'build', '-t', tag, str(context)],
                     log=self.records / (name + '-build.log'), timeout=180)
        identity = self.inspect_image(tag, cnetmod=True)
        self.summary.setdefault('image_proof', {})[name] = identity
        self.inputs.update({str(path): sha(path) for path in context.rglob('*') if path.is_file()})
        shutil.copy2(context / 'Dockerfile', self.records / (name + '-Dockerfile'))
        return identity['id']

    def patch_stage(self):
        patches = []
        def edit(relative, change):
            path = self.stage / relative
            before = path.read_text()
            after = change(before)
            path.write_text(after)
            patches.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                fromfile='prepared/' + relative, tofile='scoped/' + relative))
        def framework(text):
            text = exact_replace(text, 'IMAGE_NAME="httparena-${FRAMEWORK}"', f'IMAGE_NAME="{self.prefix}-${{FRAMEWORK}}"')
            text = exact_replace(text, 'CONTAINER_NAME="httparena-bench-${FRAMEWORK}"', f'CONTAINER_NAME="{self.prefix}-bench-${{FRAMEWORK}}"')
            return exact_replace(text, '-d --name "$CONTAINER_NAME" --network host',
                f'-d --name "$CONTAINER_NAME" --label {self.label} --label cnetmod.official.role=server --network host')
        edit('scripts/lib/framework.sh', framework)
        def benchmark(text):
            start = text.index('cleanup_all() {')
            end = text.index('\nif [ "${SKIP_TUNE:-}"', start)
            text = text[:start] + 'cleanup_all() {\n    framework_stop\n}\n' + text[end:]
            if re.search(r'^\s*docker (?:volume|image) prune|^docker ps .*name=httparena-', text, re.M):
                raise ValueError('global cleanup survived preparation')
            return text
        edit('scripts/benchmark.sh', benchmark)
        def validate(text):
            for before, after in (
                ('IMAGE_NAME="httparena-${FRAMEWORK}"', f'IMAGE_NAME="{self.prefix}-${{FRAMEWORK}}"'),
                ('CONTAINER_NAME="httparena-validate-${FRAMEWORK}"', f'CONTAINER_NAME="{self.prefix}-validate-${{FRAMEWORK}}"'),
                ('PG_CONTAINER="httparena-validate-postgres"', f'PG_CONTAINER="{self.prefix}-validate-postgres"'),
                ('PG_NETWORK="httparena-validate-net"', f'PG_NETWORK="{self.prefix}-validate-net"')):
                text = exact_replace(text, before, after)
            text = exact_replace(text, 'docker_args=(-d --name "$CONTAINER_NAME"',
                f'docker_args=(-d --name "$CONTAINER_NAME" --label {self.label} --label cnetmod.official.role=server', 2)
            return text.replace('docker run --rm ', f'docker run --rm --label {self.label} ')
        edit('scripts/validate.sh', validate)
        def h2load(text):
            original = '    timeout 45 taskset -c "$GCANNON_CPUS" "$@" 2>&1 || true'
            return exact_replace(text, original,
                '    printf \'%q \' timeout 45 taskset -c "$GCANNON_CPUS" "$@" >> "$CNETMOD_OFFICIAL_ARGV"\n'
                '    printf \'\\n\' >> "$CNETMOD_OFFICIAL_ARGV"\n'
                '    local code=0\n'
                '    timeout 45 taskset -c "$GCANNON_CPUS" "$@" 2>&1 || code=$?\n'
                '    printf \'%s\\n\' "$code" >> "$CNETMOD_OFFICIAL_EXITS"\n'
                '    return 0')
        edit('scripts/lib/tools/h2load-h3.sh', h2load)
        (self.records / 'task-scoped-staging.patch').write_text(''.join(patches))

    def sample(self):
        with (self.records / 'resource-samples.jsonl').open('w') as out:
            while not self.stop.is_set():
                try:
                    text = subprocess.check_output(['docker', 'ps', '--filter', 'label=' + self.label,
                        '--filter', 'label=cnetmod.official.role=server', '--format', '{{.ID}} {{.Names}}'],
                        env=self.env, text=True, timeout=5)
                    for line in text.splitlines():
                        cid, name = line.split()
                        pid = int(subprocess.check_output(['docker', 'inspect', cid, '--format', '{{.State.Pid}}'],
                                                         env=self.env, text=True, timeout=5))
                        status = Path(f'/proc/{pid}/status').read_text()
                        values = {key: int(re.search(r'^'+key+r':\s+(\d+)', status, re.M)[1])
                                  for key in ('VmRSS', 'VmSize', 'Threads')}
                        out.write(json.dumps(dict(time=time.time(), container=name, id=cid, pid=pid, **values))+'\n')
                        out.flush()
                except (OSError, ValueError, TypeError, subprocess.SubprocessError):
                    pass
                self.stop.wait(1)

    def cleanup(self):
        self.stop.set()
        if self.thread:
            self.thread.join(timeout=12)
        text, _ = self.command(['docker', 'ps', '-aq', '--filter', 'label=' + self.label])
        removed = []
        for cid in text.split():
            inspected, _ = self.command(['docker', 'inspect', cid])
            row = json.loads(inspected)[0]
            if row['Config'].get('Labels', {}).get('cnetmod.official.run') != self.nonce:
                raise ValueError('refuse cleanup of unowned container')
            self.command(['docker', 'rm', '-f', '-v', cid])
            removed.append(cid)
        remaining, _ = self.command(['docker', 'ps', '-aq', '--filter', 'label=' + self.label])
        self.summary['cleanup'] = dict(removed=removed, remaining=remaining.split())
        if remaining.strip():
            raise ValueError('owned container cleanup incomplete')

    def execute(self):
        try:
            self.verify_inputs()
            if not set(range(16, 32)).issubset(os.sched_getaffinity(0)):
                raise ValueError('original client CPU16-31 set unavailable')
            if sha('/etc/gai.conf') != sha(OLD / 'gai.conf'):
                raise ValueError('invoke via established private gai.conf mount wrapper')
            head, _ = self.command(['git', '-C', str(OFFICIAL), 'rev-parse', 'HEAD'])
            if head.strip() != OFFICIAL_SHA:
                raise ValueError('official source revision drift')
            self.command(['git', '-C', str(OFFICIAL), 'diff', '--exit-code', OFFICIAL_SHA, '--',
                          'scripts', 'requests', 'data/static', 'data/dataset.json'])
            # Check absence only; never remove another run's containers/listeners.
            active, _ = self.command(['docker', 'ps', '-a', '--filter', 'name=httparena-', '--format', '{{.Names}}'])
            listeners, _ = self.command(['ss', '-H', '-lun', 'sport = :8443'])
            _, processes = self.command(['pgrep', '-x', 'h2load|gcannon|wrk|zrk|ghz|oha'], check=False)
            if processes not in (0, 1):
                raise ValueError('load-generator process preflight failed')
            if active.strip() or listeners.strip() or processes == 0:
                raise ValueError('another benchmark/listener exists; exclusive window unavailable')
            images = json.loads((OLD / 'images.json').read_text())
            images = {name: images[name] for name, _ in EXPECTED_ORDER}
            if [(name, entry['tests']) for name, entry in images.items()] != EXPECTED_ORDER:
                raise ValueError('framework/profile order drift')
            for name, image in PEERS.items():
                if images[name]['image'] != image:
                    raise ValueError('pinned peer image ledger changed')
                self.summary.setdefault('image_proof', {})[name] = self.inspect_image(image)
            loadgen = self.inspect_image('cnetmod-h3-official-loadgen')
            if not loadgen['id'].startswith('sha256:6c9bb39e8233'):
                raise ValueError('original correctness-only loadgen image changed')
            self.summary['validation_image'] = loadgen
            images['cnetmod-official-optimized']['image'] = self.make_image('cnetmod-official-optimized',
                ['/certs/server.crt', '/certs/server.key', '8443', '32'])
            images['cnetmod-official-static']['image'] = self.make_image('cnetmod-official-static',
                ['/certs/server.crt', '/certs/server.key', '8443', '32', '/data/static'])
            save(self.root / 'images.json', images)
            shutil.copytree(OLD / 'arena/data', self.stage / 'data')
            self.command(['python', str(HELPER), '--official', str(OFFICIAL), '--stage', str(self.stage),
                          '--h2load', str(H2LOAD), '--images', str(self.root / 'images.json'), '--prepare-only'],
                         log=self.records / 'prepare-only.log')
            shutil.copytree(self.stage / 'scripts', self.root / 'prepared-scripts')
            self.patch_stage()
            for name, entry in images.items():
                build_script = self.stage / 'frameworks' / name / 'build.sh'
                build_script.write_text('#!/bin/sh\nset -eu\ndocker tag ' + shlex.quote(entry['image']) +
                                        ' ' + shlex.quote(self.prefix + '-' + name) + '\n')
                build_script.chmod(0o755)
            self.env.update(SKIP_TUNE='true', H2LOAD_H3=str(H2LOAD), H2LOAD_H3_IMAGE=loadgen['id'],
                            TMPDIR=str(self.stage / 'tmp'), DURATION='5s', RUNS='3', H3THREADS='64',
                            GCANNON_CPUS='16-31', LOADGEN_DOCKER='false')
            Path(self.env['TMPDIR']).mkdir(exist_ok=True)
            self.summary['measured_env'] = {key: self.env[key] for key in (
                'SKIP_TUNE', 'H2LOAD_H3', 'H2LOAD_H3_IMAGE', 'DURATION', 'RUNS', 'H3THREADS',
                'GCANNON_CPUS', 'LOADGEN_DOCKER')}
            self.summary['removed_environment_names'] = list(REMOVED_ENV)
            self.summary['loader_environment'] = {key: self.env[key] for key in ('LD_LIBRARY_PATH',) if key in self.env}
            fixtures = [p for folder in ('data', 'requests') for p in (self.stage / folder).rglob('*') if p.is_file()]
            self.inputs.update({str(p): sha(p) for p in fixtures})
            for p in (Path('/lib64/ld-linux-x86-64.so.2'), OLD / 'gai.conf', OLD / 'images.json'):
                self.inputs[str(p)] = sha(p)
            dependencies, _ = self.command(['/lib64/ld-linux-x86-64.so.2', '--list', str(H2LOAD)],
                                           log=self.records / 'h2load-libraries.log')
            for path in re.findall(r'=> (/\S+)', dependencies):
                self.inputs[path] = sha(path)
            save(self.records / 'inputs-before.json', self.inputs)
            save(self.records / 'dataset-sha256.json', {str(p.relative_to(self.stage)): sha(p) for p in fixtures})
            for name, argv in [('environment', ['uname', '-a']), ('hardware', ['lscpu']),
                               ('compiler', ['clang++', '--version']), ('h2load', [str(H2LOAD), '--version'])]:
                self.command(argv, log=self.records / (name + '.txt'))
            for name in images:
                print('Validating', name, flush=True)
                log = self.records / (name + '-official-validation.log')
                self.command(['bash', str(self.stage / 'scripts/validate.sh'), name], cwd=self.stage, log=log, timeout=180)
                result = re.findall(r'=== Results: (\d+) passed, (\d+) failed(?:, (\d+) skipped)? ===', log.read_text())
                if len(result) != 1 or int(result[0][0]) == 0 or int(result[0][1]) or int(result[0][2] or 0):
                    raise ValueError('official correctness missing checks/failed/skipped: ' + name)
            anchor = self.prefix + '-netns'
            self.command(['docker', 'run', '-d', '--name', anchor, '--label', self.label, '--network', 'none',
                          '--entrypoint', '/bin/sleep', PEERS['agensio-official-h3'], 'infinity'])
            pid, _ = self.command(['docker', 'inspect', anchor, '--format', '{{.State.Pid}}'])
            netpatch = []
            for relative in ('scripts/benchmark.sh', 'scripts/lib/framework.sh'):
                path = self.stage / relative
                before = path.read_text()
                after = before.replace('--network host', '--network container:' + anchor)
                path.write_text(after)
                netpatch.extend(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
                    fromfile='scoped/' + relative, tofile='isolated/' + relative))
            (self.records / 'network-isolation.patch').write_text(''.join(netpatch))
            self.summary['network'] = dict(anchor=anchor, pid=int(pid), mode='same isolated loopback for all frameworks/client')
            self.command(['nsenter', '-t', pid.strip(), '-n', 'ip', '-j', 'link', 'show', 'lo'],
                         log=self.records / 'loopback-link.json')
            self.command(['nsenter', '-t', pid.strip(), '-n', 'ip', '-j', 'address', 'show', 'lo'],
                         log=self.records / 'loopback-address.json')
            self.thread = threading.Thread(target=self.sample, daemon=True)
            self.thread.start()
            for name, entry in images.items():
                for profile in entry['tests']:
                    print('Running', name, profile, flush=True)
                    log = self.records / f'{name}-{profile}.log'
                    argv_file = self.records / f'{name}-{profile}.argv.txt'
                    exit_file = self.records / f'{name}-{profile}.exits.txt'
                    self.env.update(CNETMOD_OFFICIAL_ARGV=str(argv_file), CNETMOD_OFFICIAL_EXITS=str(exit_file))
                    self.command(['nsenter', '-t', pid.strip(), '-n', 'bash', str(self.stage / 'scripts/benchmark.sh'),
                                  name, profile, '--save'], cwd=self.stage, log=log, timeout=240)
                    exits = [int(value) for value in exit_file.read_text().splitlines()]
                    argvs = [shlex.split(value) for value in argv_file.read_text().splitlines()]
                    if exits != [0, 0, 0] or len(argvs) != 3:
                        raise ValueError('missing/failed actual h2load exits')
                    for argv in argvs:
                        if argv[:7] != ['timeout', '45', 'taskset', '-c', '16-31', str(H2LOAD), '--alpn-list=h3']:
                            raise ValueError('actual h2load binary/affinity/ALPN drift')
                        if argv[-8:] != ['-c', '64', '-m', '64', '-t', '64', '-D', '5s']:
                            raise ValueError('official measured h2load dimensions changed')
                    result_path = self.stage / 'results' / profile / '64' / (name + '.json')
                    selected = json.loads(result_path.read_text())
                    if any(selected.get(key, 0) for key in ('status_3xx', 'status_4xx', 'status_5xx')) or selected.get('status_2xx', 0) <= 0:
                        raise ValueError('official selected result invalid')
                    rounds = parse_rounds(log.read_text())
                    if (selected.get('framework') != name or selected.get('connections') != 64 or
                        selected.get('threads') != 64 or selected.get('duration') != '5s' or
                        selected.get('rps') != max(int(row['official_valid_2xx_rps']) for row in rounds)):
                        raise ValueError('official selected result/dimensions do not match original winner policy')
                    self.summary['profiles'].append(dict(framework=name, profile=profile, raw_h2load_exits=exits,
                        h2load_argv=argvs, rounds=rounds, selected=selected,
                        official_selection_unchanged=True))
                    self.persist()
            if sum(len(p['rounds']) for p in self.summary['profiles']) != 18:
                raise ValueError('not all18 official rounds completed')
        except BaseException as error:
            self.summary['failure'] = repr(error)
            raise
        finally:
            try:
                self.cleanup()
            finally:
                if hasattr(self, 'inputs'):
                    after = {path: sha(path) for path in self.inputs}
                    save(self.records / 'inputs-after.json', after)
                    self.summary['inputs_unchanged'] = after == self.inputs
                self.summary['valid'] = bool('failure' not in self.summary and
                    self.summary.get('inputs_unchanged') and len(self.summary['profiles']) == 6 and
                    self.summary.get('cleanup', {}).get('remaining') == [])
                self.persist()
                print('RESULTS', self.root, 'VALID', self.summary['valid'], flush=True)
        if not self.summary['valid']:
            raise RuntimeError('official final integrity/correctness/cleanup gate failed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--path-mtu', type=int, choices=(1472, 4096, 8192), required=True)
    parser.add_argument('--gso', type=int, choices=(0, 1), required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    Run(args).execute()


if __name__ == '__main__':
    main()
