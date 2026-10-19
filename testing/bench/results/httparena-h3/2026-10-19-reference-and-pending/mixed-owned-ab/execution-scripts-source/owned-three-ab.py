"""Prepared only: frozen guard/mixed/owned-fast H3 comparison. Requires exclusive window.
Input variants JSON keys guard/mixed/fast; each requires binary, binary_sha256,
source_snapshot, allocator_proof, proof_build_root. No build or source mutation.
Run through cnetmod-h3-isolated.sh. Source and allocator proof gates are mandatory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time
import uuid

H2LOAD=Path('/var/tmp/cnetmod-h3-loadgen169/h2load')
H2LOAD_SHA='9a35cefa52e809a32f1898d65575a2cbbf54555031604830b852e2f6b5a4484f'
IMAGE='sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
RX_SOURCE='src/protocol/quic/quic_connection.cpp'
MIXED_PRODUCTION={RX_SOURCE,'src/protocol/quic/quic_send_pipeline.cpp','src/protocol/quic/quic_send_pipeline.cppm'}
MIXED_CHANGED=MIXED_PRODUCTION|{'testing/tests/test_quic_frame.cpp','testing/tests/test_quic_loss_detection.cpp'}

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,obj): Path(path).write_text(json.dumps(obj,indent=2)+'\n')

def source_name(name):
    p=Path(name)
    if p.is_absolute() or '..' in p.parts: return False
    if name=='CMakeLists.txt': return True
    if not name.startswith(('src/','include/','cmake/','testing/')): return False
    if 'results' in p.parts or '__pycache__' in p.parts: return False
    return name!='testing/http3/h3-interop-results.json'

def parse(text):
    counts=re.findall(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout',text)
    status=re.findall(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx',text)
    timing=re.findall(r'finished in ([\d.]+)(ms|s|m),\s*([\d.]+) req/s',text)
    assert len(counts)==len(status)==len(timing)==1,'ambiguous/missing h2load result'
    total,started,done,succeeded,failed,errored,timeout=map(int,counts[0])
    ok,redirection,client_error,server_error=map(int,status[0])
    elapsed=float(timing[0][0])*{'ms':.001,'s':1,'m':60}[timing[0][1]]
    assert total>=done and started>=done==succeeded>0 and ok>=done and elapsed>0
    assert failed==errored==timeout==redirection==client_error==server_error==0,'h2load errors'
    latency=next(l.strip() for l in text.splitlines() if l.startswith('request '))
    values=re.findall(r'([\d.]+)(us|ms|s|m)',latency)
    assert len(values)>=7 and 'median' in text and 'p99' in text
    ms=[float(v)*{'us':.001,'ms':1,'s':1000,'m':60000}[u] for v,u in values]
    udp=re.search(r'UDP datagram: (\d+) sent, (\d+) received',text)
    assert udp
    return dict(total=total,started=started,done=done,succeeded=succeeded,status_2xx=ok,
        header_completion_delta=ok-done,failed=failed,errored=errored,timeout=timeout,
        elapsed_seconds=elapsed,printed_rps=float(timing[0][2]),completed_rps=done/elapsed,
        valid_2xx_rps=ok/elapsed,p99_ms=ms[4],median_ms=ms[2],latency_line=latency,
        udp_sent=int(udp[1]),udp_received=int(udp[2]))

def sample(pid):
    monotonic_ns=time.monotonic_ns()
    cpu_line=Path('/proc/stat').read_text().splitlines()[0].split()
    assert cpu_line[0]=='cpu' and len(cpu_line)>=9
    system_ticks=dict(zip(('user','nice','system','idle','iowait','irq','softirq','steal','guest','guest_nice'),map(int,cpu_line[1:])))
    fields=Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()
    smaps=Path(f'/proc/{pid}/smaps_rollup').read_text()
    status=Path(f'/proc/{pid}/status').read_text()
    affinity=re.search(r'^Cpus_allowed_list:\s*(.+)$',status,re.M)
    threads={}
    for task in sorted(Path(f'/proc/{pid}/task').iterdir(),key=lambda p:int(p.name)):
        try:
            raw=(task/'stat').read_text()
            values=raw.rsplit(')',1)[1].split()
            task_status=(task/'status').read_text()
            allowed=re.search(r'^Cpus_allowed_list:\s*(.+)$',task_status,re.M)
            threads[task.name]=dict(comm=raw.split('(',1)[1].rsplit(')',1)[0],
                cpu_seconds=(int(values[11])+int(values[12]))/os.sysconf('SC_CLK_TCK'),
                cpus_allowed_list=allowed[1].strip() if allowed else None)
        except FileNotFoundError:
            threads[task.name]=dict(unavailable='thread exited during snapshot')
    return dict(cpu_seconds=(int(fields[11])+int(fields[12]))/os.sysconf('SC_CLK_TCK'),
        monotonic_ns=monotonic_ns,system_cpu_ticks=system_ticks,clock_ticks_per_second=os.sysconf('SC_CLK_TCK'),
        cpus_allowed_list=affinity[1].strip() if affinity else None,threads=threads,
        memory_kib={k:int(v) for k,v in re.findall(r'^(Rss|Pss|Private_Clean|Private_Dirty):\s+(\d+) kB$',smaps,re.M)})

def system_cpu_delta(before,after):
    fields=('user','nice','system','idle','iowait','irq','softirq','steal')
    ticks={k:after['system_cpu_ticks'][k]-before['system_cpu_ticks'][k] for k in fields}
    elapsed=(after['monotonic_ns']-before['monotonic_ns'])/1e9
    hz=before['clock_ticks_per_second']
    total=sum(ticks.values()) # guest/guest_nice already included in user/nice.
    busy=sum(ticks[k] for k in ('user','nice','system','irq','softirq','steal'))
    row=dict(ticks=ticks,total_ticks_excluding_guest=total,busy_ticks=busy,idle_ticks=ticks['idle'],
        iowait_ticks=ticks['iowait'],monotonic_elapsed_seconds=elapsed,clock_ticks_per_second=hz,
        scope='whole host CPU aggregate, including load generator and unrelated work; no affinity changes')
    if elapsed>0 and total>0 and all(v>=0 for v in ticks.values()):
        row.update(busy_fraction=busy/total,idle_fraction=ticks['idle']/total,iowait_fraction=ticks['iowait']/total,
            average_busy_cpu_equivalents=busy/hz/elapsed,average_idle_cpu_equivalents=ticks['idle']/hz/elapsed)
    else:row['derived_metrics_unavailable']='nonpositive interval/total or regressing host counter (possible CPU topology/accounting change)'
    return row

def thread_deltas(before,after):
    rows=[]
    for tid in sorted(set(before['threads'])|set(after['threads']),key=int):
        a,b=before['threads'].get(tid),after['threads'].get(tid)
        row=dict(tid=int(tid),before=a,after=b)
        if a and b and 'cpu_seconds' in a and 'cpu_seconds' in b:
            row['cpu_seconds_delta']=b['cpu_seconds']-a['cpu_seconds']
        else:row['delta_unavailable']='not present with complete data at both checkpoints'
        rows.append(row)
    return rows

def stop_server(server,report):
    if server is None: return
    # This exact group is created solely for our nsenter->server command.
    if server.poll() is None:
        os.killpg(server.pid,signal.SIGTERM)
        try: server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(server.pid,signal.SIGKILL);server.wait(timeout=5)
    report.setdefault('server_exits',[]).append(dict(pid=server.pid,returncode=server.returncode))

def verify_source_difference(variants, source_sets, differences):
    parser='testing/http3/deadline_frame_parser.py'
    assert set(differences)==MIXED_CHANGED|{parser},differences
    assert source_sets['guard'].keys()==source_sets['mixed'].keys()==source_sets['fast'].keys()
    fast_changes={n for n,h in source_sets['mixed'].items() if source_sets['fast'][n]!=h}
    assert fast_changes=={'src/protocol/quic/quic_send_pipeline.cpp'},fast_changes
    assert source_sets['mixed']['src/protocol/quic/quic_send_pipeline.cpp']=='bf798ea5600b39002037752eae44d2baef658de88e5fe1860894d1c6d70a51ee'
    assert source_sets['fast']['src/protocol/quic/quic_send_pipeline.cpp']=='497ce762561a78851c54103e72ba0ed8f27c7449afb8dce11a01313a75487147'
    archived=Path(variants['guard']['source_snapshot'])/'deadline_frame_parser.py'
    current=Path(variants['mixed']['source_snapshot'])/'deadline_frame_parser.py'
    assert sha(archived)==source_sets['guard'][parser]
    assert sha(current)==source_sets['mixed'][parser]
    assert archived.read_bytes().rstrip(b'\r\n')==current.read_bytes().rstrip(b'\r\n')

PROFILES=(('baseline-h3',4096,8192),('static-h3',4096,8192),('static-h3',65507,65507))
# All six permutations balance every identity in every position and pair order.
ORDERS=(('guard','mixed','fast'),('fast','mixed','guard'),
        ('mixed','fast','guard'),('guard','fast','mixed'),
        ('fast','guard','mixed'),('mixed','guard','fast'))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--execute',action='store_true')
    ap.add_argument('--variants',type=Path,required=True)
    ap.add_argument('--arena',type=Path,required=True)
    ap.add_argument('--source-root',type=Path,default=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod'))
    args=ap.parse_args()
    assert args.execute,'prepared only; explicit --execute and exclusive benchmark window required'
    variants=json.loads(args.variants.read_text())
    assert set(variants)=={'guard','mixed','fast'}
    assert variants['guard']['binary_sha256']=='2fa51db8652cf8a0d166d79ab4d36241c9a5c789aa8ed4c0cb5e57eb56bb7d28'
    assert variants['mixed']['binary_sha256']=='4e81085bb948f91a1ef6a98e4e0dc670d8fd843776b5ad17009ce1b9dc81a32a'
    assert variants['fast']['binary_sha256'] not in {variants['guard']['binary_sha256'],variants['mixed']['binary_sha256']}
    env=dict(os.environ)
    for k in ('SSLKEYLOGFILE','LD_PRELOAD','LD_AUDIT'):env.pop(k,None)
    env.update(CNETMOD_QUIC_GSO='1')
    assert sha(H2LOAD)==H2LOAD_SHA and set(range(16,32))<=os.sched_getaffinity(0)
    assert shutil.disk_usage('/var/tmp').free>=1024**3
    nonce=uuid.uuid4().hex
    out=Path('/var/tmp/cnetmod-h3-owned-three-ab-'+nonce);out.mkdir()
    shutil.copy2(__file__,out/'runner.py')
    shutil.copy2(args.variants,out/'variants.json')
    frozen={str(args.variants):sha(args.variants),str(Path(__file__)):sha(__file__),str(H2LOAD):H2LOAD_SHA}
    source_sets={}
    proof_records={}
    for label,value in variants.items():
        assert re.fullmatch(r'[a-fA-F0-9]{64}',value['binary_sha256'])
        binary=Path(value['binary']);assert sha(binary)==value['binary_sha256'].lower()
        assert binary.read_bytes()[:4]==b'\x7fELF'
        proof_path=Path(value['allocator_proof']);proof=json.loads(proof_path.read_text())
        assert proof['system_allocator_verified'] is True and proof['cmake_use_mimalloc_off'] is True
        assert proof['binary_sha256']==value['binary_sha256'].lower() and proof['noargs_returncode']==2
        assert proof['ld_preload'] is None and proof['ld_audit'] is None
        root=Path(value['proof_build_root'])
        for name,digest in proof['files'].items():
            p=root/name;assert p.resolve().is_relative_to(root.resolve()) and sha(p)==digest.lower(),str(p)
            frozen[str(p)]=digest.lower()
        snapshot=Path(value['source_snapshot'])
        metadata_path=snapshot/'metadata.json';diff_path=snapshot/'source.diff'
        metadata=json.loads(metadata_path.read_text(encoding='utf-8-sig'))
        assert sha(diff_path)==metadata['diff_sha256'].lower()
        sources={n:h.lower() for n,h in metadata['sources'].items() if source_name(n)}
        assert {RX_SOURCE,'src/protocol/quic/quic_connection.cpp','src/executor/platform/io_uring_async_op.cpp','testing/bench/arena_h3_server.cpp'}<=sources.keys()
        source_sets[label]=sources
        own=out/label;own.mkdir()
        shutil.copy2(binary,own/'arena_h3_server')
        shutil.copy2(proof_path,own/'allocator-proof.json')
        shutil.copy2(diff_path,own/'source.diff')
        save(own/'source-manifest.json',dict(scope='filtered src/include/cmake/CMakeLists/testing source only; results excluded',head=metadata['head'],sources=sources,original_metadata_sha256=sha(metadata_path)))
        proof_records[label]=dict(binary=str(binary),binary_sha256=sha(binary),allocator_proof_sha256=sha(proof_path),source_manifest_sha256=sha(own/'source-manifest.json'),source_count=len(sources))
        for p in (binary,proof_path,metadata_path,diff_path,own/'arena_h3_server',own/'source-manifest.json'):frozen[str(p)]=sha(p)
    assert source_sets['guard'].keys()==source_sets['mixed'].keys()==source_sets['fast'].keys()
    differences=[n for n,h in source_sets['guard'].items() if source_sets['mixed'][n]!=h]
    verify_source_difference(variants, source_sets, differences)
    # Completeness against current relevant source tree; no benchmark history scan.
    actual_names=set()
    for directory in ('src','include','cmake','testing'):
        for current,dirs,names in os.walk(args.source_root/directory):
            dirs[:]=[d for d in dirs if d not in ('results','__pycache__','.git')]
            for file in names:
                p=Path(current)/file
                name=p.relative_to(args.source_root).as_posix()
                if p.is_file() and source_name(name):actual_names.add(name)
    actual_names.add('CMakeLists.txt')
    assert actual_names==set(source_sets['fast']),dict(unrecorded=sorted(actual_names-set(source_sets['fast'])),missing=sorted(set(source_sets['fast'])-actual_names))
    for directory in ('certs','requests','data/static'):
        assert (args.arena/directory).is_dir()
        for p in (args.arena/directory).rglob('*'):
            if p.is_file():frozen[str(p)]=sha(p)
    # Runtime loader dependencies must remain identical; inspect without running the server.
    for label in variants:
        binary=out/label/'arena_h3_server'
        result=subprocess.run(['/lib64/ld-linux-x86-64.so.2','--list',str(binary)],env=env,capture_output=True,text=True,check=True)
        (out/label/'loader-dependencies.txt').write_text(result.stdout+result.stderr)
        paths=re.findall(r'(?:=>\s*)?(/[^\s()]+)',result.stdout)
        assert paths
        for name in paths:
            if Path(name).is_file():frozen[name]=sha(name)
    def verify():
        for name,digest in frozen.items():assert sha(name)==digest,name
        for name,digest in source_sets['fast'].items():assert sha(args.source_root/name)==digest,name
    verify()
    save(out/'inputs-before.json',frozen)
    report=dict(valid=False,scope=__doc__,allocator='system',variants=proof_records,source_difference=sorted(MIXED_PRODUCTION),
        configurations=[dict(profile=p,path_mtu=m,receive_ceiling=r) for p,m,r in PROFILES],gso=1,configured_server_workers=32,rounds=[],order='all six permutations per profile; 54 serial rounds; fresh server each round',source_differences_exact=differences,configuration_limit='Ceilings are not proof of every packet reaching the ceiling.',
        loadgen_sha256=H2LOAD_SHA,removed_environment=['SSLKEYLOGFILE','LD_PRELOAD','LD_AUDIT'],
        fixture_hashes={str(p):h for p,h in frozen.items() if str(p).startswith(str(args.arena))},
        gai_conf_sha256=sha('/etc/gai.conf'),anchor_image=IMAGE)
    def checkpoint():save(out/'summary.json',report)
    anchor='cnetmod-mixed-prefix-base-'+nonce;anchor_created=False;server=None
    checkpoint()
    try:
        subprocess.run(['docker','image','inspect',IMAGE],check=True,stdout=subprocess.DEVNULL)
        subprocess.run(['docker','run','--pull=never','-d','--name',anchor,'--label','cnetmod.mixed-prefix-base.run='+nonce,'--network','none','--entrypoint','/bin/sleep',IMAGE,'infinity'],check=True,stdout=subprocess.DEVNULL)
        anchor_created=True
        pid=subprocess.check_output(['docker','inspect',anchor,'--format','{{.State.Pid}}'],text=True).strip()
        prefix=['nsenter','-t',pid,'-n']
        subprocess.run(prefix+['ip','link','set','lo','up'],check=True)
        for kind,cmd in [('link',['ip','-j','link','show','lo']),('address',['ip','-j','address','show','lo'])]:
            (out/('loopback-'+kind+'.json')).write_bytes(subprocess.check_output(prefix+cmd))
        for profile,path_mtu,receive_ceiling in PROFILES:
            env.update(CNETMOD_QUIC_MAX_PATH_MTU=str(path_mtu),CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE=str(receive_ceiling))
            for block,order in enumerate(ORDERS,1):
                for label in order:
                    verify()
                    name=f'{profile}-mtu{path_mtu}-recv{receive_ceiling}-block{block}-{label}';print('ROUND START',name,flush=True)
                    command=prefix+[str(out/label/'arena_h3_server'),str(args.arena/'certs/server.crt'),str(args.arena/'certs/server.key'),'8443','32']
                    if profile=='static-h3':command.append(str(args.arena/'data/static'))
                    load=prefix+['taskset','-c','16-31',str(H2LOAD),'--alpn-list=h3']
                    if profile=='static-h3':load+=['-i',str(args.arena/'requests/static-h2-uris.txt'),'-H','Accept-Encoding: br;q=1, gzip;q=0.8']
                    else:load+=['https://localhost:8443/baseline2?a=1&b=1']
                    load+=['-c','64','-m','64','-t','64','-D','5s']
                    save(out/(name+'-commands.json'),dict(server=command,h2load=load,environment={k:env[k] for k in ('CNETMOD_QUIC_GSO','CNETMOD_QUIC_MAX_PATH_MTU','CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE')}))
                    with (out/(name+'-server.log')).open('w') as log:
                        server=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
                        for _ in range(200):
                            assert server.poll() is None,'server exited before ready'
                            sockets=subprocess.check_output(prefix+['ss','-H','-u','-l','-n','sport = :8443'],text=True)
                            if len(sockets.splitlines())==32:break
                            time.sleep(.05)
                        else:raise AssertionError('32 worker sockets not ready')
                        ready_cmd=prefix+[str(H2LOAD),'--alpn-list=h3','https://localhost:8443/baseline2?a=1&b=1','-n','1','-c','1','-m','1','-t','1']
                        save(out/(name+'-ready-command.json'),ready_cmd)
                        with (out/(name+'-ready.stdout')).open('w') as stdout,(out/(name+'-ready.stderr')).open('w') as stderr:
                            ready=subprocess.run(ready_cmd,env=env,stdout=stdout,stderr=stderr,timeout=15)
                        r=parse((out/(name+'-ready.stdout')).read_text()+(out/(name+'-ready.stderr')).read_text())
                        assert ready.returncode==0 and all(r[k]==1 for k in ('total','started','done','succeeded','status_2xx'))
                        time.sleep(2)
                        before=sample(server.pid)
                        save(out/(name+'-cpu-before.json'),before)
                        with (out/(name+'-h2load.stdout')).open('w') as stdout,(out/(name+'-h2load.stderr')).open('w') as stderr:
                            result=subprocess.run(load,env=env,stdout=stdout,stderr=stderr,timeout=50)
                        after=sample(server.pid)
                        save(out/(name+'-cpu-after.json'),after)
                        save(out/(name+'-exit.json'),dict(h2load=result.returncode,server=server.poll()))
                        assert result.returncode==0 and server.poll() is None
                        row=parse((out/(name+'-h2load.stdout')).read_text()+(out/(name+'-h2load.stderr')).read_text())
                        row.update(profile=profile,path_mtu=path_mtu,receive_ceiling=receive_ceiling,block=block,variant=label,before=before,after=after,thread_cpu_deltas=thread_deltas(before,after),system_cpu_delta=system_cpu_delta(before,after),h2load_exit=result.returncode,
                            cpu_seconds=after['cpu_seconds']-before['cpu_seconds'])
                        row['cpu_us_per_complete']=row['cpu_seconds']*1e6/row['done']
                        report['rounds'].append(row);checkpoint();print('ROUND VALID',json.dumps({k:row[k] for k in ('profile','path_mtu','receive_ceiling','block','variant','done','completed_rps','cpu_us_per_complete','p99_ms','failed','errored','timeout')}),flush=True)
                        stop_server(server,report);server=None
                    time.sleep(2)
        verify();save(out/'inputs-after.json',{n:sha(n) for n in frozen})
        assert len(report['rounds'])==54
        report['matched_blocks']=[]
        for profile,path_mtu,receive_ceiling in PROFILES:
            for block in range(1,7):
                for reference,target in (('guard','mixed'),('guard','fast'),('mixed','fast')):
                    a=next(r for r in report['rounds'] if r['profile']==profile and r['path_mtu']==path_mtu and r['block']==block and r['variant']==reference)
                    b=next(r for r in report['rounds'] if r['profile']==profile and r['path_mtu']==path_mtu and r['block']==block and r['variant']==target)
                    report['matched_blocks'].append(dict(profile=profile,path_mtu=path_mtu,receive_ceiling=receive_ceiling,block=block,reference=reference,target=target,
                        completion_gain_percent=(b['completed_rps']/a['completed_rps']-1)*100,
                        cpu_per_complete_change_percent=(b['cpu_us_per_complete']/a['cpu_us_per_complete']-1)*100,
                        p99_change_percent=(b['p99_ms']/a['p99_ms']-1)*100))
        report['inputs_unchanged']=True
        report['valid']=True
    except BaseException as error:
        report['error']=repr(error);raise
    finally:
        stop_server(server,report)
        if anchor_created:
            identity=json.loads(subprocess.check_output(['docker','inspect',anchor],text=True))[0]
            assert identity['Config']['Labels']['cnetmod.mixed-prefix-base.run']==nonce
            cleanup=subprocess.run(['docker','rm','-f',anchor],capture_output=True,text=True)
            remaining=subprocess.check_output(['docker','ps','-aq','--filter','label=cnetmod.mixed-prefix-base.run='+nonce],text=True).split()
            report['cleanup']=dict(returncode=cleanup.returncode,stdout=cleanup.stdout,stderr=cleanup.stderr,remaining=remaining)
            if cleanup.returncode or remaining:report['valid']=False
        checkpoint();print('RESULTS',out,'valid',report['valid'],flush=True)
    assert report['valid']

if __name__=='__main__': main()
