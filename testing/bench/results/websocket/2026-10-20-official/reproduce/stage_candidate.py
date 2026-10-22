import json, os, shutil, subprocess, tarfile
from pathlib import Path
root=Path('/root/cnetmod-ws-20261020')
env=dict(os.environ,DOCKER_HOST='unix:///run/cnetmod-ws-20261020.sock')
candidate=root/'candidate'
candidate.mkdir(exist_ok=True)
shutil.copy2(root/'build/testing/bench/bench_ws_server',candidate/'bench_ws_server')
runtime=root/'candidate-rootfs'
shutil.copytree(root/'runtime-rootfs',runtime,dirs_exist_ok=True)
shutil.copy2(candidate/'bench_ws_server',runtime/'bench_ws_server')
with tarfile.open(root/'candidate-runtime.tar','w') as archive:
    archive.add(runtime,arcname='.')
entrypoint=json.dumps(['/bin/sh','-c','exec /bench_ws_server 8080 "$(nproc)"'])
subprocess.run(['docker','import','--change','ENTRYPOINT '+entrypoint,str(root/'candidate-runtime.tar'),'ws-candidate:20261020'],check=True,env=env)
subprocess.run(['docker','tag','ws-candidate:20261020','httparena-cnetmod-ws-candidate'],check=True,env=env)
target=root/'HttpArena/frameworks/cnetmod-ws-candidate'
target.mkdir(exist_ok=True)
meta=json.loads((root/'source/testing/bench/httparena-ws/meta.json').read_text())
meta['display_name']='cnetmod WS candidate'
(target/'meta.json').write_text(json.dumps(meta,indent=2)+'\n')
(target/'build.sh').write_text('#!/bin/bash\nset -euo pipefail\ndocker tag ws-candidate:20261020 httparena-cnetmod-ws-candidate\n')
(target/'build.sh').chmod(0o755)
