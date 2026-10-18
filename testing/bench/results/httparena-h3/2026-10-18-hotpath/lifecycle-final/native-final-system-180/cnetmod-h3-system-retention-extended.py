import json
import argparse
import hashlib
import shutil
import os
import sys
import uuid
from pathlib import Path
import re
import subprocess
import time

parser=argparse.ArgumentParser()
parser.add_argument('expected_sha')
parser.add_argument('--rounds',type=int,choices=(60,180),default=180)
parser.add_argument('--path-mtu',type=int,choices=(1472,4096,8192),required=True)
parser.add_argument('--gso',choices=('0','1'),required=True)
parser.add_argument('--source-snapshot',type=Path,required=True)
parser.add_argument('--allocator-proof',type=Path,required=True)
args=parser.parse_args()
assert not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT')
proof=json.loads(args.allocator_proof.read_text())
assert proof['system_allocator_verified'] and proof['cmake_use_mimalloc_off']
assert proof['binary_sha256']==args.expected_sha
metadata=json.loads((args.source_snapshot/'metadata.json').read_text())
repo=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod')
def verify_sources():
    assert all(hashlib.sha256((repo/name).read_bytes()).hexdigest()==digest for name,digest in metadata['sources'].items()), 'source drift'
verify_sources()
server_env=os.environ.copy()
server_env.pop('SSLKEYLOGFILE',None)
server_env.update(CNETMOD_QUIC_GSO=args.gso,CNETMOD_QUIC_MAX_PATH_MTU=str(args.path_mtu),CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE='8192')
out=Path('/var/tmp/cnetmod-h3-system-retention-'+uuid.uuid4().hex)
out.mkdir()
arena=Path('/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena')
candidate=Path('/var/tmp/cnetmod-h3-arena-build/testing/bench/arena_h3_server')
shutil.copy2(candidate,out/'arena_h3_server')
binary=str(out/'arena_h3_server')
binary_sha256=hashlib.sha256(Path(binary).read_bytes()).hexdigest()
assert binary_sha256==args.expected_sha.lower(), 'candidate hash mismatch'
h2load='/var/tmp/cnetmod-h3-loadgen169/h2load'
loadgen_sha=hashlib.sha256(Path(h2load).read_bytes()).hexdigest()
anchor='cnetmod-h3-final-retention-'+uuid.uuid4().hex
image='sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
subprocess.run(['docker','run','-d','--name',anchor,'--network','none','--entrypoint',
    '/bin/sleep',image,'infinity'],check=True,stdout=subprocess.DEVNULL)
pid=subprocess.check_output(['docker','inspect',anchor,'--format','{{.State.Pid}}'],text=True).strip()
prefix=['nsenter','-t',pid,'-n']
report=dict(scope='Frozen system-allocator ownership, ACK and PMTU candidate; serial 200k static rounds, not official ranking',binary_sha256=binary_sha256,samples=[],rounds=[])
report['loadgen_sha256']=loadgen_sha
report['planned_rounds']=args.rounds
report['workers']=32
report['connections']=64
report['max_streams']=64
report['client_threads']=64
report['client_cpus']='16-31'
report.update(settings=dict(gso=args.gso,send_ceiling=args.path_mtu,receive_capacity=8192),allocator_proof_sha256=hashlib.sha256(args.allocator_proof.read_bytes()).hexdigest(),source_metadata_sha256=hashlib.sha256((args.source_snapshot/'metadata.json').read_bytes()).hexdigest(),allocator='system')
shutil.copy2(args.allocator_proof,out/'allocator-proof.json')
shutil.copy2(args.source_snapshot/'metadata.json',out/'source-metadata.json')
server=None

def sample(name):
    smaps=Path(f'/proc/{server.pid}/smaps_rollup').read_text()
    memory={k:int(v)*1024 for k,v in re.findall(r'^(\w+):\s+(\d+) kB',smaps,re.M)}
    result=dict(name=name,time=time.time(),rss=memory['Rss'],
        private=memory.get('Private_Clean',0)+memory.get('Private_Dirty',0))
    report['samples'].append(result)
    (out/'summary.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(result),flush=True)

try:
    with (out/'server.log').open('w') as log:
        server=subprocess.Popen(prefix+[binary,str(arena/'certs/server.crt'),str(arena/'certs/server.key'),
            '8443','32',str(arena/'data/static')],env=server_env,stdout=log,stderr=subprocess.STDOUT)
        for _ in range(100):
            sockets=subprocess.check_output(prefix+['ss','-H','-u','-l','-n','sport = :8443'],text=True)
            if len(sockets.splitlines())==32: break
            assert server.poll() is None
            time.sleep(.05)
        else: raise AssertionError('worker sockets not bound')
        ready=subprocess.run(prefix+[h2load,'--alpn-list=h3','https://localhost:8443/baseline2?a=1&b=1',
            '-n','1','-c','1','-m','1','-t','1'],capture_output=True,text=True,timeout=10)
        assert re.search(r'\b1 2xx',ready.stdout),ready.stdout+ready.stderr
        time.sleep(2)
        sample('initial')
        for index in range(args.rounds):
            response=subprocess.run(prefix+['taskset','-c','16-31',h2load,'--alpn-list=h3',
                '-i',str(arena/'requests/static-h2-uris.txt'),'-H','Accept-Encoding: br;q=1, gzip;q=0.8',
                '-c','64','-m','64','-t','64','-n','200000'],capture_output=True,text=True,timeout=50)
            text=response.stdout+response.stderr
            (out/f'round-{index+1}-h2load.log').write_text(text)
            assert response.returncode==0 and re.search(r'\b0 failed',text) and re.search(r'\b0 errored',text) and re.search(r'\b0 timeout',text),text[-1500:]
            successful=int(re.search(r'(\d+) 2xx',text)[1])
            assert successful==200000,successful
            counts=re.search(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded',text)
            assert counts and all(int(x)==200000 for x in counts.groups()),text[-1500:]
            report['rounds'].append(dict(index=index+1,requests=successful))
            time.sleep(2)
            sample(f'round-{index+1}-idle-2s')
        # No inbound packet, allocator API call or in-process observer runs
        # during this final retention interval; only /proc memory samples are read.
        time.sleep(35)
        verify_sources()
        assert hashlib.sha256(candidate.read_bytes()).hexdigest()==binary_sha256
        assert hashlib.sha256(Path(h2load).read_bytes()).hexdigest()==loadgen_sha
        report['valid'] = True
        sample('final-idle-37s')
except BaseException as error:
    report['failure'] = repr(error)
    report['valid'] = False
    (out/'summary.json').write_text(json.dumps(report,indent=2))
    if server is not None and server.poll() is None:
        sample('failure-immediate')
        time.sleep(35)
        sample('failure-idle-35s')
    raise
finally:
    if server is not None:
        server.terminate()
        server.wait(timeout=10)
    subprocess.run(['docker','rm','-f',anchor],check=True,stdout=subprocess.DEVNULL)
print('RESULTS',out,flush=True)



