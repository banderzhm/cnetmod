import json, os, shutil, subprocess, sys
from pathlib import Path
root=Path('/root/cnetmod-ws-20261020')
name=sys.argv[1] if len(sys.argv)>1 else 'cnetmod-ws-batch'
image=name+':20261020'
context=root/'optimization/images'/name
context.mkdir(parents=True,exist_ok=True)
shutil.copyfile(root/'build/testing/bench/bench_ws_server',context/'bench_ws_server')
(context/'bench_ws_server').chmod(0o755)
(context/'Dockerfile').write_text('FROM ws-baseline:20261020\nCOPY bench_ws_server /bench_ws_server\n')
env=dict(os.environ,DOCKER_HOST='unix:///run/cnetmod-ws-20261020.sock')
subprocess.run(['docker','build','-t',image,str(context)],env=env,check=True)
entry=root/'HttpArena/frameworks'/name
entry.mkdir(exist_ok=True)
meta=json.loads((root/'HttpArena/frameworks/cnetmod-ws-baseline/meta.json').read_text())
meta['display_name']=name
(entry/'meta.json').write_text(json.dumps(meta,indent=2)+'\n')
(entry/'build.sh').write_text('#!/bin/bash\nset -euo pipefail\ndocker tag '+image+' httparena-'+name+'\n')
(entry/'build.sh').chmod(0o755)
subprocess.run(['docker','tag',image,'httparena-'+name],env=env,check=True)
