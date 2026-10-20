import json
import os
from pathlib import Path
import re
import subprocess
import time

out=Path('/var/tmp/cnetmod-h3-coldclose-retention-'+str(int(time.time())))
out.mkdir()
arena=Path('/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena')
binary='/var/tmp/cnetmod-h3-close-diagnostic-old-layout/arena_h3_server'
h2load='/var/tmp/cnetmod-h3-loadgen169/h2load'
anchor='cnetmod-h3-coldclose-netns'
image='sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
subprocess.run(['docker','run','-d','--name',anchor,'--network','none','--entrypoint',
    '/bin/sleep',image,'infinity'],check=True,stdout=subprocess.DEVNULL)
pid=subprocess.check_output(['docker','inspect',anchor,'--format','{{.State.Pid}}'],text=True).strip()
prefix=['nsenter','-t',pid,'-n']
report=dict(scope='native frozen conditional binary, no counter instrumentation, no gdb attachment; independent retention test',samples=[],rounds=[])
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
            '8443','32',str(arena/'data/static')],stdout=log,stderr=subprocess.STDOUT)
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
        for index in range(12):
            response=subprocess.run(prefix+['taskset','-c','16-31',h2load,'--alpn-list=h3',
                '-i',str(arena/'requests/static-h2-uris.txt'),'-H','Accept-Encoding: br;q=1, gzip;q=0.8',
                '-c','64','-m','64','-t','64','-n','200000'],capture_output=True,text=True,timeout=50)
            text=response.stdout+response.stderr
            (out/f'round-{index+1}-h2load.log').write_text(text)
            assert response.returncode==0 and re.search(r'\b0 failed',text),text[-1500:]
            successful=int(re.search(r'(\d+) 2xx',text)[1])
            assert successful==200000,successful
            report['rounds'].append(dict(index=index+1,requests=successful))
            time.sleep(2)
            sample(f'round-{index+1}-idle-2s')
        # No inbound packet, allocator API call or in-process observer runs
        # during this final retention interval. gdb only reads raw atomics.
        time.sleep(35)
        sample('final-idle-37s')
except BaseException:
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
