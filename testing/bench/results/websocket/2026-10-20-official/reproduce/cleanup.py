import json, os, signal, subprocess
from pathlib import Path
root=Path('/root/cnetmod-ws-20261020')
out=root/'verification'
out.mkdir(exist_ok=True)
records=[]
def run(args):
    result=subprocess.run(args,capture_output=True,text=True)
    records.append(dict(command=args,exit_code=result.returncode,stdout=result.stdout,stderr=result.stderr))
    return result
docker=['docker','-H','unix:///run/cnetmod-ws-20261020.sock']
containers=run(docker+['ps','-aq']).stdout.split()
if containers:
    run(docker+['rm','-f','-v',*containers])
assert not run(docker+['ps','-aq']).stdout.strip(), 'task containers still exist'
run(['systemctl','stop','cnetmod-ws-docker-net','cnetmod-ws-containerd-net'])
run(['ip','netns','delete','cnetmod-ws-20261020'])
host_before=json.loads((root/'host-network-before.json').read_text())
host_after={}
for key in host_before:
    if key=='mtu':
        host_after[key]=Path('/sys/class/net/lo/mtu').read_text().strip()
    else:
        result=run(['sysctl','-n',key])
        host_after[key]=result.stdout.strip()
unchanged=host_before==host_after
records.append(dict(host_network_before=host_before,host_network_after=host_after,host_network_unchanged=unchanged))
run(['systemctl','is-active','cnetmod-ws-docker-net','cnetmod-ws-containerd-net'])
run(['ip','netns','list'])
run(['docker','ps','--format','{{.Names}} {{.Status}}'])
(out/'cleanup.json').write_text(json.dumps(records,indent=2)+'\n')
assert unchanged, 'host network changed; inspect cleanup.json before proceeding'
cmdline=Path('/proc/547/cmdline')
if cmdline.exists() and cmdline.read_bytes()==b'sleep\x0014400\x00':
    os.kill(547,signal.SIGTERM)
print('Task containers/services/namespace stopped; host network unchanged; test token released.')
