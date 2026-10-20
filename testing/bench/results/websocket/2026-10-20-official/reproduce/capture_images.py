import hashlib, json, subprocess
from pathlib import Path
root=Path('/root/cnetmod-ws-20261020')
out=root/'verification'
out.mkdir(exist_ok=True)
images=['ws-baseline:20261020','ws-candidate:20261020',
        'cnetmod-ws-peer-genhttp-ioxide:20261020','cnetmod-ws-peer-fulmine:20261020',
        'cnetmod-ws-peer-vanilla-epoll:20261020']
metadata=[]
for image in images:
    result=subprocess.run(['docker','-H','unix:///run/cnetmod-ws-20261020.sock','image','inspect',image],capture_output=True,text=True)
    if result.returncode: continue
    record=json.loads(result.stdout)[0]
    metadata.append({key:record.get(key) for key in ['Id','RepoTags','RepoDigests','Created','Architecture','Os']})
(out/'images.json').write_text(json.dumps(metadata,indent=2)+'\n')
hashes={name:hashlib.sha256((root/name).read_bytes()).hexdigest()
        for name in ['baseline/bench_ws_server','candidate/bench_ws_server',
                     'build/testing/bench/bench_ws_server']}
(out/'server-binaries.json').write_text(json.dumps(hashes,indent=2)+'\n')
image='cnetmod-ws-peer-fulmine:20261020'
container=subprocess.check_output(['docker','create',image],text=True).strip()
try:
    subprocess.run(['docker','cp',container+':/app/node_modules/.package-lock.json',str(out/'fulmine-resolved-packages.json')],check=True)
finally:
    subprocess.run(['docker','rm',container],check=True)
