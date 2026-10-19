"""Pinned agensio reference observation only. No source/config/build mutation."""
import argparse,hashlib,json,os,re,signal,subprocess,time,uuid,shutil,importlib.util
from pathlib import Path
IMAGE='sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
ELF_SHA='8c0da17c36f77b67357f3769be3af19e36d39ed04aec5286f64c329e6db3e986'
H2=Path('/var/tmp/cnetmod-h3-loadgen169/h2load');H2_SHA='9a35cefa52e809a32f1898d65575a2cbbf54555031604830b852e2f6b5a4484f'
ARENA=Path('/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena')
IDENTITY=Path('/var/tmp/agensio-reference-identity-20261019/identity.json')
HELPER=Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-owned-fast-execution-fbaeb04a21614732b24a7fdae850fcd0/owned-three-ab.py')
spec=importlib.util.spec_from_file_location('abhelpers',HELPER);ab=importlib.util.module_from_spec(spec);spec.loader.exec_module(ab)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,v):Path(p).write_text(json.dumps(v,indent=2)+'\n')
def run(cmd,**kw):return subprocess.run(cmd,check=True,**kw)
def output(cmd,**kw):return subprocess.check_output(cmd,**kw)
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--execute',action='store_true');args=ap.parse_args();assert args.execute
 nonce=uuid.uuid4().hex;out=Path('/var/tmp/agensio-reference-observation-'+nonce);out.mkdir();print('OUTPUT',out,flush=True)
 shutil.copy2(__file__,out/'runner.py');shutil.copy2(IDENTITY,out/'reference-identity.json')
 identity=json.loads(IDENTITY.read_text());assert identity['runtime']['image']==IMAGE and identity['runtime']['binary_sha256']==ELF_SHA and identity['same_binary'] and identity['source_matches_builder']
 assert sha(H2)==H2_SHA;assert set(range(16,32))<=os.sched_getaffinity(0)
 assert 'precedence ::ffff:0:0/96  100' in Path('/etc/gai.conf').read_text() or 'precedence ::ffff:0:0/96 100' in Path('/etc/gai.conf').read_text()
 env=os.environ.copy()
 for k in ['SSLKEYLOGFILE','LD_PRELOAD','LD_AUDIT','CNETMOD_QUIC_GSO','CNETMOD_QUIC_MAX_PATH_MTU','CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE']:env.pop(k,None)
 frozen={str(H2):H2_SHA,str(IDENTITY):sha(IDENTITY),str(HELPER):sha(HELPER),str(Path(__file__)):sha(__file__),'/etc/gai.conf':sha('/etc/gai.conf')}
 for folder in ['certs','requests','data/static']:
  for p in (ARENA/folder).rglob('*'):
   if p.is_file():frozen[str(p)]=sha(p)
 frozen[str(ARENA/'data/dataset.json')]=sha(ARENA/'data/dataset.json')
 save(out/'inputs-before.json',frozen)
 inspect=json.loads(output(['docker','image','inspect',IMAGE],text=True))[0];save(out/'image.json',inspect)
 assert inspect['Config']['Cmd']==['/etc/agensio/start.sh']
 report=dict(valid=False,diagnostic_only=True,image=IMAGE,binary_sha256=ELF_SHA,source_head=identity['source_head'],measurements=[],commands=[],limitations=['One unprofiled observation and one separately sampled observation; not three-round official acceptance.','No current fast profile exists. Historical guard sample percentages are not fast percentages.','DWARF callgraphs sampled separately at 99Hz; sampled throughput is not an acceptance result; inclusive percentages must not be summed.','No agensio MTU/GSO/env settings changed; default original image configuration retained.'])
 anchor='agensio-ref-net-'+nonce;server_names=[];created=False;perf=None
 def checkpoint():save(out/'summary.json',report)
 def checked(cmd,**kw):report['commands'].append(cmd);checkpoint();return run(cmd,**kw)
 def verify():
  for p,h in frozen.items():assert sha(p)==h,p
 try:
  checked(['docker','run','--pull=never','-d','--name',anchor,'--label','agensio.reference.run='+nonce,'--network','none','--entrypoint','/bin/sleep',IMAGE,'infinity'],stdout=subprocess.DEVNULL);created=True
  anchor_pid=output(['docker','inspect',anchor,'--format','{{.State.Pid}}'],text=True).strip();prefix=['nsenter','-t',anchor_pid,'-n']
  checked(prefix+['ip','link','set','lo','up'])
  for name,cmd in [('lo-link',['ip','-j','link','show','lo']),('lo-address',['ip','-j','address','show','lo']),('routes',['ip','-j','route','show','table','all'])]:(out/(name+'.json')).write_bytes(output(prefix+cmd))
  report['client_cpus']='16-31';report['server_cpu_limit']='--cpus=32 official cpuset-fallback on 32CPU host';report['host_affinity']=sorted(os.sched_getaffinity(0))
  assert len(os.sched_getaffinity(0))==32
  for sampled in [False,True]:
   verify();name='perf' if sampled else 'nonperf';own=out/name;own.mkdir();server='agensio-ref-'+name+'-'+nonce;server_names.append(server)
   cmd=['docker','run','--pull=never','-d','--name',server,'--label','agensio.reference.run='+nonce,'--network','container:'+anchor,'--security-opt','seccomp=unconfined','--ulimit','memlock=-1:-1','--ulimit','nofile=1048576:1048576','--cpus=32','-v',str(ARENA/'data/dataset.json')+':/data/dataset.json:ro','-v',str(ARENA/'data/static')+':/data/static:ro','-v',str(ARENA/'certs')+':/certs:ro',IMAGE]
   checked(cmd,stdout=subprocess.DEVNULL)
   state=json.loads(output(['docker','inspect',server],text=True))[0];save(own/'container.json',state);pid=state['State']['Pid'];assert pid>0
   assert state['Path']=='/etc/agensio/start.sh' and not state['Args']
   proc=Path('/proc')/str(pid);exe=proc/'exe'
   for _ in range(100):
    if sha(exe)==ELF_SHA:break
    time.sleep(.05)
   assert sha(exe)==ELF_SHA
   shutil.copy2(exe,own/'agensio-elf');frozen[str(own/'agensio-elf')]=ELF_SHA
   for n in ['etc/agensio/agensio.toml','etc/agensio/start.sh']:
    q=proc/'root'/n;dst=own/Path(n).name;shutil.copy2(q,dst);frozen[str(dst)]=sha(dst)
    official=Path('/var/tmp/cnetmod-h3-official/agensio')/Path(n).name;assert dst.read_bytes()==official.read_bytes(),n
   config=(own/'agensio.toml').read_text();assert re.search(r'^workers\s*=\s*0$',config,re.M)
   (own/'actual-cmdline.txt').write_bytes((proc/'cmdline').read_bytes());(own/'status.txt').write_bytes((proc/'status').read_bytes())
   for attempt in range(200):
    current=json.loads(output(['docker','inspect',server],text=True))[0];assert current['State']['Running']
    sockets=output(prefix+['ss','-H','-u','-l','-n','-p','sport = :8443'],text=True)
    if len(sockets.splitlines())==32:break
    time.sleep(.05)
   else:raise AssertionError('actual 32 QUIC worker sockets not observed')
   (own/'worker-sockets.txt').write_text(sockets)
   fdrows=[]
   for row in sockets.splitlines():
    match=re.search(r'pid=(\d+),fd=(\d+)',row);assert match and int(match[1])==pid,row;fdrows.append(int(match[2]))
   assert len(set(fdrows))==32
   tasks=[]
   for t in sorted((proc/'task').iterdir()):tasks.append(dict(tid=int(t.name),comm=(t/'comm').read_text().strip(),status=(t/'status').read_text()))
   save(own/'threads-ready.json',tasks)
   report.setdefault('worker_verification',[]).append(dict(measurement=name,quic_listener_sockets=32,distinct_fds=32,pid=pid,threads=len(tasks),method='Runtime ss maps 32 distinct QUIC8443 listener FDs to exact agensioPID; startup log retained.'))
   libs=[]
   (own/'maps.txt').write_bytes((proc/'maps').read_bytes())
   for n in sorted(set(re.findall(r'(/[^\n]+)$',(proc/'maps').read_text(),re.M))):
    if not n.startswith('/') or n.endswith(' (deleted)'):continue
    q=proc/'root'/n.lstrip('/')
    if q.is_file():libs.append(dict(path=n,sha256=sha(q),bytes=q.stat().st_size));dst=own/'mapped-files'/n.lstrip('/');dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(q,dst);run(['perf','buildid-cache','--add',str(dst)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
   save(own/'mapped-file-identities.json',libs)
   load=prefix+['taskset','-c','16-31',str(H2),'--alpn-list=h3','-i',str(ARENA/'requests/static-h2-uris.txt'),'-H','Accept-Encoding: br;q=1, gzip;q=0.8','-c','64','-m','64','-t','64','-D','5s']
   ready=prefix+[str(H2),'--alpn-list=h3','https://localhost:8443/baseline2?a=1&b=1','-n','1','-c','1','-m','1','-t','1']
   r=subprocess.run(ready,env=env,capture_output=True,text=True,timeout=15);(own/'ready.stdout').write_text(r.stdout);(own/'ready.stderr').write_text(r.stderr);assert r.returncode==0;readyrow=ab.parse(r.stdout+r.stderr);assert all(readyrow[k]==1 for k in ['total','started','done','succeeded','status_2xx'])
   time.sleep(2);verify();save(own/'commands.json',dict(server=cmd,readiness=ready,load=load,argv=(proc/'cmdline').read_bytes().decode().split('\0'),environment=state['Config']['Env'],image_cmd=inspect['Config']['Cmd']))
   before=ab.sample(pid);save(own/'cpu-before.json',before)
   if sampled:
    pcmd=['perf','record','-e','cpu-clock','-F','99','--call-graph','dwarf,16384','-p',str(pid),'-o',str(own/'perf.data')];save(own/'perf-command.json',pcmd);plog=(own/'perf-record.log').open('w');perf=subprocess.Popen(pcmd,stdout=plog,stderr=subprocess.STDOUT);time.sleep(.25);assert perf.poll() is None
   print('START',name,flush=True)
   r=subprocess.run(load,env=env,capture_output=True,text=True,timeout=50);after=ab.sample(pid);(own/'h2load.stdout').write_text(r.stdout);(own/'h2load.stderr').write_text(r.stderr);save(own/'cpu-after.json',after)
   if sampled:
    perf.send_signal(signal.SIGINT);prc=perf.wait(timeout=15);perf=None;plog.close();assert prc in [0,-2,130],prc
    for suffix,opts in [('self',['--no-children','-g','none']),('stacks',['-g','graph,0.5,caller'])]:
     with (own/('perf-'+suffix+'.txt')).open('w') as f:run(['perf','report','--stdio','--percent-limit','.1','--sort','symbol,dso','-i',str(own/'perf.data'),*opts],stdout=f,stderr=subprocess.STDOUT)
    for filename,command in [('perf-header.txt',['perf','report','--stdio','--header-only','-i',str(own/'perf.data')]),('perf-buildids.txt',['perf','buildid-list','-i',str(own/'perf.data')]),('perf-script.txt',['perf','script','--show-lost-events','-i',str(own/'perf.data')])]:
     with (own/filename).open('w') as f:run(command,stdout=f,stderr=subprocess.STDOUT)
   assert r.returncode==0
   row=ab.parse(r.stdout+r.stderr);row.update(name=name,sampled=sampled,server_cpu_seconds=after['cpu_seconds']-before['cpu_seconds'],before=before,after=after,thread_deltas=ab.thread_deltas(before,after),system_cpu_delta=ab.system_cpu_delta(before,after));row['cpu_us_per_complete']=row['server_cpu_seconds']*1e6/row['done'];report['measurements'].append(row);checkpoint();print('VALID',name,json.dumps({k:row[k] for k in ['done','completed_rps','printed_rps','cpu_us_per_complete','p99_ms','failed','errored','timeout']}),flush=True)
   (own/'server.log').write_bytes(output(['docker','logs',server],stderr=subprocess.STDOUT));checked(['docker','rm','-f',server],stdout=subprocess.DEVNULL);server_names.remove(server);time.sleep(2)
  verify();save(out/'inputs-after.json',{p:sha(p) for p in frozen});report['inputs_unchanged']=True;report['valid']=True
 except BaseException as e:report['error']=repr(e);raise
 finally:
  if perf is not None and perf.poll() is None:perf.send_signal(signal.SIGINT);perf.wait(timeout=15)
  for server in server_names:
   item=json.loads(output(['docker','inspect',server],text=True))[0];assert item['Config']['Labels']['agensio.reference.run']==nonce
   (out/(server+'-failure.log')).write_bytes(output(['docker','logs',server],stderr=subprocess.STDOUT));subprocess.run(['docker','rm','-f',server],capture_output=True)
  if created:
   item=json.loads(output(['docker','inspect',anchor],text=True))[0];assert item['Config']['Labels']['agensio.reference.run']==nonce
   c=subprocess.run(['docker','rm','-f',anchor],capture_output=True,text=True);remaining=output(['docker','ps','-aq','--filter','label=agensio.reference.run='+nonce],text=True).split();report['cleanup']=dict(returncode=c.returncode,stdout=c.stdout,stderr=c.stderr,remaining=remaining)
   if c.returncode or remaining:report['valid']=False
  checkpoint();print('RESULTS',out,'valid',report['valid'],flush=True)
 assert report['valid']
if __name__=='__main__':main()
