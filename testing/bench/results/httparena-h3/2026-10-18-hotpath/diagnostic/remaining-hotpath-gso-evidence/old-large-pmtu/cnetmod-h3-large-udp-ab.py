"""Serial 1472-byte writer scheduling A/B; no production configuration changes."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

out = Path('/var/tmp/cnetmod-h3-large-udp-ab-'+str(int(time.time())))
out.mkdir()
arena = Path('/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena')
h2load = '/var/tmp/cnetmod-h3-loadgen169/h2load'
binaries = {
    'stage1': Path('/var/tmp/cnetmod-h3-conditional-nogso/arena_h3_server'),
    'stage2': Path('/var/tmp/cnetmod-h3-large-udp-nogso/arena_h3_server')}
anchor = 'cnetmod-h3-flush-ab-netns'
image = 'sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
subprocess.run(['docker','run','-d','--name',anchor,'--network','none','--entrypoint',
                '/bin/sleep',image,'infinity'],check=True,stdout=subprocess.DEVNULL)
pid = subprocess.check_output(['docker','inspect',anchor,'--format','{{.State.Pid}}'],text=True).strip()
prefix = ['nsenter','-t',pid,'-n']
results = dict(mode='diagnostic counterbalanced A/B; all workloads serial',
               binaries={n:dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                         for n,p in binaries.items()}, measurements=[])

def cpu(pid):
    s=Path(f'/proc/{pid}/stat').read_text().split()
    return (int(s[13])+int(s[14]))/os.sysconf('SC_CLK_TCK')

try:
    for profile in ('baseline','static'):
        for round, stage in enumerate(('stage1','stage2','stage2','stage1','stage1','stage2')):
            name=f'{profile}-{round+1}-{stage}'
            with (out/f'{name}-server.log').open('w') as log:
                command=prefix+[str(binaries[stage]),str(arena/'certs/server.crt'),
                    str(arena/'certs/server.key'),'8443','32']
                if profile=='static': command.append(str(arena/'data/static'))
                server=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
                try:
                    for _ in range(100):
                        sockets=subprocess.check_output(prefix+['ss','-H','-u','-l','-n','sport = :8443'],text=True)
                        if len(sockets.splitlines())==32: break
                        assert server.poll() is None,'server exited before binding'
                        time.sleep(.05)
                    else: raise AssertionError('not all worker sockets bound '+name)
                    for attempt in range(1):
                        try:
                            ready=subprocess.run(prefix+[h2load,'--alpn-list=h3',
                                'https://localhost:8443/baseline2?a=1&b=1','-n','1','-c','1','-m','1','-t','1'],
                                capture_output=True,text=True,timeout=5)
                        except subprocess.TimeoutExpired as error:
                            (out/f'{name}-readiness-timeout-{attempt}.log').write_text(str(error))
                            raise
                            continue
                        if re.search(r'\b1 2xx',ready.stdout): break
                        time.sleep(.05)
                    else: raise AssertionError('readiness failed '+name)
                    workload=prefix+['taskset','-c','16-31',h2load,'--alpn-list=h3']
                    if profile=='static':
                        workload+=['-i',str(arena/'requests/static-h2-uris.txt'),'-H',
                                   'Accept-Encoding: br;q=1, gzip;q=0.8']
                    else: workload+=['https://localhost:8443/baseline2?a=1&b=1']
                    low=profile.endswith('1c1m')
                    count='1' if low else '64'
                    workload+=['-c',count,'-m',count,'-t',count,'-D','2s' if low else '5s']
                    before=cpu(server.pid)
                    response=subprocess.run(workload,capture_output=True,text=True,timeout=40)
                    text=response.stdout+response.stderr
                    (out/f'{name}-h2load.log').write_text(text)
                    assert response.returncode==0,(name,response.returncode)
                    duration=float(re.search(r'finished in ([\d.]+)s',text)[1])
                    success=int(re.search(r'(\d+) 2xx',text)[1])
                    assert re.search(r'\b0 failed',text),(name,'failed requests')
                    udp=re.search(r'UDP datagram: (\d+) sent, (\d+) received',text)
                    result=dict(name=name,stage=stage,profile=profile,command=workload,
                        requests_2xx=success,elapsed=duration,valid_rps=success/duration,
                        server_cpu_seconds=cpu(server.pid)-before,
                        udp_sent=int(udp[1]),udp_received=int(udp[2]),
                        udp_received_per_request=int(udp[2])/success,
                        latency=next(s.strip() for s in text.splitlines() if s.startswith('request ')))
                    results['measurements'].append(result)
                    (out/'summary.json').write_text(json.dumps(results,indent=2))
                    print(json.dumps(result),flush=True)
                finally:
                    server.terminate()
                    server.wait(timeout=10)
            time.sleep(2)
finally:
    subprocess.run(['docker','rm','-f',anchor],check=True,stdout=subprocess.DEVNULL)
print('RESULTS',out,flush=True)
