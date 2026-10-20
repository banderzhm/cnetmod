import json, os, subprocess, sys
from pathlib import Path
root=Path('/root/cnetmod-ws-20261020')
workers=int(sys.argv[1])
name=f'cnetmod-ws-workers{workers}'
image=name+':20261020'
context=root/'optimization/images'/name
context.mkdir(parents=True,exist_ok=True)
entrypoint=json.dumps(['/bench_ws_server','8080',str(workers)])
(context/'Dockerfile').write_text('FROM cnetmod-ws-batch-small:20261020\nENTRYPOINT '+entrypoint+'\n')
env=dict(os.environ,DOCKER_HOST='unix:///run/cnetmod-ws-20261020.sock')
subprocess.run(['docker','build','-t',image,str(context)],env=env,check=True)
entry=root/'HttpArena/frameworks'/name
entry.mkdir(exist_ok=True)
meta=json.loads((root/'HttpArena/frameworks/cnetmod-ws-baseline/meta.json').read_text())
meta['display_name']=name
meta['description']=f'WS message loop; explicit {workers} workers within unchanged official CPU budget'
(entry/'meta.json').write_text(json.dumps(meta,indent=2)+'\n')
(entry/'build.sh').write_text('#!/bin/bash\nset -euo pipefail\ndocker tag '+image+' httparena-'+name+'\n')
(entry/'build.sh').chmod(0o755)
subprocess.run(['docker','tag',image,'httparena-'+name],env=env,check=True)
