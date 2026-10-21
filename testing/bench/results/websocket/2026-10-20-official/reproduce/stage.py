import json, os, re, shutil, subprocess, tarfile
from pathlib import Path
root = Path('/root/cnetmod-ws-20261020')
env = dict(os.environ, DOCKER_HOST='unix:///run/cnetmod-ws-20261020.sock')
def run(args, **kw):
    return subprocess.run(args, check=True, env=env, **kw)
run(['make', '-C', str(root/'gcannon-src'), '-j4'], stdout=(root/'gcannon-build.log').open('w'), stderr=subprocess.STDOUT)
rootfs = root/'runtime-rootfs'
rootfs.mkdir(exist_ok=True)
binary = root/'baseline/bench_ws_server'
paths = {str(binary): '/bench_ws_server', '/bin/sh': '/bin/sh', '/usr/bin/nproc': '/usr/bin/nproc'}
for source in list(paths):
    output = subprocess.check_output(['ldd', source], text=True)
    for path in re.findall(r'(/[^\s()]+)', output):
        paths[path] = path
for source, target in paths.items():
    destination = rootfs/target.lstrip('/')
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    destination.chmod(0o755)
with tarfile.open(root/'baseline-runtime.tar', 'w') as archive:
    archive.add(rootfs, arcname='.')
entrypoint = json.dumps(['/bin/sh', '-c', 'exec /bench_ws_server 8080 "$(nproc)"'])
run(['docker', 'import', '--change', 'ENTRYPOINT '+entrypoint, str(root/'baseline-runtime.tar'), 'ws-baseline:20261020'])
arena = root/'HttpArena'
target = arena/'frameworks/cnetmod-ws-baseline'
target.mkdir(exist_ok=True)
meta = json.loads((root/'source/testing/bench/httparena-ws/meta.json').read_text())
meta['display_name'] = 'cnetmod WS baseline'
(target/'meta.json').write_text(json.dumps(meta, indent=2)+'\n')
(target/'build.sh').write_text('#!/bin/bash\nset -euo pipefail\ndocker tag ws-baseline:20261020 httparena-cnetmod-ws-baseline\n')
(target/'build.sh').chmod(0o755)
run(['docker', 'tag', 'ws-baseline:20261020', 'httparena-cnetmod-ws-baseline'])
print('Baseline image and official registration ready', flush=True)
