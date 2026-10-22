import json, os, subprocess
from pathlib import Path
root=Path('/root/cnetmod-ws-20261020')
name='cnetmod-ws-final24-b64'
context=root/'optimization/images'/name
context.mkdir(parents=True,exist_ok=True)
(context/'Dockerfile').write_text('FROM cnetmod-ws-final:20261020\nENV CNETMOD_IOURING_SUBMIT_BATCH=64 CNETMOD_IOURING_CQE_TICK_LIMIT=1024\nENTRYPOINT ["/bench_ws_server","8080","24"]\n')
env=dict(os.environ,DOCKER_HOST='unix:///run/cnetmod-ws-20261020.sock')
subprocess.run(['docker','build','-t',name+':20261020',str(context)],env=env,check=True)
entry=root/'HttpArena/frameworks'/name
entry.mkdir(exist_ok=True)
meta=json.loads((root/'HttpArena/frameworks/cnetmod-ws-baseline/meta.json').read_text())
meta['display_name']=name
meta['description']='24 workers and submit batch 64, CQ tick 1024; same 32-CPU quota; runtime tuning explicitly recorded'
(entry/'meta.json').write_text(json.dumps(meta,indent=2)+'\n')
(entry/'build.sh').write_text('#!/bin/bash\nset -euo pipefail\ndocker tag '+name+':20261020 httparena-'+name+'\n')
(entry/'build.sh').chmod(0o755)
subprocess.run(['docker','tag',name+':20261020','httparena-'+name],env=env,check=True)
