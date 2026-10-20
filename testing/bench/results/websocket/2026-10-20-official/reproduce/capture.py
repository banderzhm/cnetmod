import datetime, hashlib, json, os, platform, subprocess
from pathlib import Path
root=Path('/root/cnetmod-ws-20261020')
def output(args):
    done=subprocess.run(args,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    return dict(command=args,returncode=done.returncode,output=done.stdout)
data={'captured_at': datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).isoformat(),
      'baseline_core_revision':'5c969d4ac3b772963df80ff66c93fc8165901dc1',
      'harness_revision':'cd973a1864cba50fe234ca50f57b8a50a3da7dd9',
      'official_revision':'8977be2721a5aba7b320345cddfcd6f6b654e698',
      'network_namespace':'cnetmod-ws-20261020',
      'host_network_restored':json.loads((root/'host-network-before.json').read_text()),
      'measurements':[], 'sha256':{},
      'dependencies': {'boringssl':'1a5078d343ba5d9501062caf11e3173fc364dc59',
                       'mimalloc':'d4881d338125e1cb7c47ba4cfb398d6f7c0c8d45',
                       'stdexec':'633c87370b986735b0c8667993b5fc18bf1b3129',
                       'json':'74aefc3486c6dcd03a5a039d86f1ac8d146c0f74'}}
for args in [['uname','-a'],['lscpu'],['free','-b'],['clang++','--version'],['cmake','--version'],
             ['gcc','--version'],['pacman','-Q','libc++','libc++abi','liburing'],
             ['docker','-H','unix:///run/cnetmod-ws-20261020.sock','info'],
             ['git','-C',str(root/'gcannon-src'),'rev-parse','HEAD'],
             ['nsenter','--net=/run/netns/cnetmod-ws-20261020','ip','route','get','127.0.0.1'],
             ['nsenter','--net=/run/netns/cnetmod-ws-20261020','sysctl','net.ipv4.ip_local_port_range','net.ipv4.tcp_tw_reuse','net.core.somaxconn'],
             ['nsenter','--net=/run/netns/cnetmod-ws-20261020','ip','link','show','lo']]:
    data['measurements'].append(output(args))
for name in ['baseline/bench_ws_server','baseline/bench_ws_codec','gcannon-src/gcannon','build/CMakeCache.txt']:
    data['sha256'][name]=hashlib.sha256((root/name).read_bytes()).hexdigest()
(root/'environment.json').write_text(json.dumps(data,indent=2)+'\n')
