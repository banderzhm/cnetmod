import hashlib
import json
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

repo = Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod')
revision = sys.argv[1]
root = Path('/var/tmp/cnetmod-h3-lifecycle-' + revision[:8] + '-' + str(int(time.time())))
root.mkdir(exist_ok=False)
stage = root / 'arena'
stage.mkdir()
records = stage / 'records'
records.mkdir()
old = Path('/var/tmp/cnetmod-h3-official')
images = json.loads((old / 'images.json').read_text())
images = {key: value for key, value in images.items()
          if key in ('cnetmod-official-optimized', 'cnetmod-official-static', 'agensio-official-h3', 'genhttp-ioxide-official-h3')}
binary = Path('/var/tmp/cnetmod-h3-arena-build/testing/bench/arena_h3_server')
binary_sha256 = hashlib.sha256(binary.read_bytes()).hexdigest()


def image(tag, arguments):
    context = root / tag
    (context / 'lib').mkdir(parents=True)
    shutil.copy2(binary, context / 'program')
    dependencies = subprocess.check_output(['ldd', str(binary)], text=True)
    for path in re.findall(r'=> (/\S+)', dependencies):
        shutil.copy2(path, context / 'lib' / Path(path).name)
    shutil.copy2('/lib64/ld-linux-x86-64.so.2', context / 'loader')
    shutil.copy2(old / 'gai.conf', context / 'gai.conf')
    entrypoint = ['/lib64/ld-linux-x86-64.so.2', '--library-path', '/lib',
                  '/program'] + arguments
    (context / 'Dockerfile').write_text(
        'FROM scratch\nCOPY program /program\nCOPY lib/ /lib/\n'
        'COPY loader /lib64/ld-linux-x86-64.so.2\n'
        'COPY gai.conf /etc/gai.conf\nENTRYPOINT ' + json.dumps(entrypoint) + '\n')
    with (records / (tag + '-build.log')).open('wb') as output:
        subprocess.run(['docker', 'build', '-t', tag, str(context)], check=True,
                       stdout=output, stderr=subprocess.STDOUT)
    return subprocess.check_output(
        ['docker', 'inspect', tag, '--format', '{{.Id}}'], text=True).strip()


images['cnetmod-official-optimized']['image'] = image(
    'cnetmod-h3-lifecycle-baseline',
    ['/certs/server.crt', '/certs/server.key', '8443', '32'])
images['cnetmod-official-static']['image'] = image(
    'cnetmod-h3-lifecycle-static',
    ['/certs/server.crt', '/certs/server.key', '8443', '32', '/data/static'])
(root / 'images.json').write_text(json.dumps(images, indent=2) + '\n')
shutil.copytree(old / 'arena/data', stage / 'data')
for name, args in [
        ('environment', ['uname', '-a']),
        ('hardware', ['lscpu']),
        ('compiler', ['clang++', '--version']),
        ('h2load', ['/var/tmp/cnetmod-h3-loadgen169/h2load', '--version'])]:
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (records / (name + '.txt')).write_bytes(result.stdout)
(records / 'revision.json').write_text(json.dumps({
    'commit': revision,
    'binary_sha256': binary_sha256,
    'test_token': 'explicitly handed over by user for this serial official retest',
    'profiles': 'cnetmod, agensio and genhttp-ioxide: baseline-h3 and static-h3, three serial official rounds each',
    'fix': 'QUIC stream state and credit accounting, cancellation ownership, bounded HTTP/3 buffering, scheduler and recovery teardown; immutable response headers retained',
    'resource_sampling': 'supplemental /proc RSS once per second; official Docker peaks unchanged'
}, indent=2) + '\n')

stop = threading.Event()


def sample():
    with (records / 'resource-samples.jsonl').open('w') as output:
        while not stop.is_set():
            containers = subprocess.run(
                ['docker', 'ps', '--filter', 'name=httparena-',
                 '--format', '{{.ID}} {{.Names}}'], capture_output=True, text=True)
            for line in containers.stdout.splitlines():
                cid, name = line.split()
                try:
                    pid = int(subprocess.check_output(
                        ['docker', 'inspect', cid, '--format', '{{.State.Pid}}'],
                        text=True))
                    status = Path(f'/proc/{pid}/status').read_text()
                    values = {key: int(re.search(
                        r'^' + key + r':\s+(\d+)', status, re.M)[1])
                        for key in ['VmRSS', 'VmSize', 'Threads']}
                    output.write(json.dumps(dict(
                        time=time.time(), container=name, id=cid, pid=pid,
                        **values)) + '\n')
                    output.flush()
                except (OSError, subprocess.CalledProcessError, TypeError):
                    pass
            stop.wait(1)


thread = threading.Thread(target=sample, daemon=True)
thread.start()
try:
    command = [
        'unshare', '--mount', 'bash',
        '/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-isolated.sh',
        '/usr/bin/python',
        str(repo / 'testing/bench/run_official_h3.py'),
        '--official', '/var/tmp/cnetmod-http-arena-8977be27', '--stage', str(stage),
        '--h2load', '/var/tmp/cnetmod-h3-loadgen169/h2load',
        '--images', str(root / 'images.json')]
    (records / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    completed = subprocess.run(command)
finally:
    stop.set()
    thread.join(timeout=5)
raise SystemExit(completed.returncode)


