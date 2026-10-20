#!/usr/bin/env python3
"""Prepared, not executed: isolated-netns static failure qlog diagnostic, never ranking."""
import argparse
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import subprocess
import time
import uuid

TOOLS=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('qlog_summary',TOOLS/'cnetmod-h3-qlog-summary.py')
qlog=importlib.util.module_from_spec(spec);spec.loader.exec_module(qlog)

def sha(path):
    with path.open('rb') as f:
        h=hashlib.sha256()
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()

def save(path,value):
    # Bound each generated JSON artifact independently; a partial file is failure evidence.
    used=0
    with path.open('w') as f:
        for chunk in json.JSONEncoder(indent=2).iterencode(value):
            used+=len(chunk.encode())
            if used>1024**3:raise RuntimeError('generated JSON exceeded1GiB diagnostic cap')
            f.write(chunk)

def stop_owned(process):
    if process is None: return
    # Every owned Popen starts a new session and its PID is its process-group ID.
    if process.poll() is None:
        os.killpg(process.pid,signal.SIGTERM)
        try:process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGKILL);process.wait(timeout=10)

def usage(directory): return sum(p.stat().st_size for p in directory.rglob('*') if p.is_file())

def client_file_cap_reached(directory,cap):
    # Generated qlog-summary.json has its separate1GiB bound, not the child RLIMIT.
    paths=list(directory.glob('client.*.sqlog'))
    paths.extend(directory/name for name in ('requests.tsv','h2load.stdout','h2load.stderr','tls.keys'))
    return any(path.is_file() and path.stat().st_size>=cap for path in paths)


def run(args):
    if not 1<=args.rounds<=12: raise ValueError('rounds must be1..12')
    if not 1<=args.file_mib<=64 or not 8<=args.total_gib<=64:raise ValueError('file cap1..64MiB,total8..64GiB')
    if sha(args.binary)!=args.sha256.lower():raise ValueError('candidate hash mismatch')
    nonce=uuid.uuid4().hex
    out=Path('/var/tmp')/('cnetmod-h3-cold-qlog-'+nonce);out.mkdir()
    binary=out/'arena_h3_server';shutil.copy2(args.binary,binary)
    cap=args.total_gib*1024**3;filecap=args.file_mib*1024**2
    if shutil.disk_usage(out).free<cap+2*1024**3:raise RuntimeError('insufficient free disk for bounded diagnostic')
    report=dict(scope='qlog diagnostic; instrumentation perturbs timing; no RPS ranking',candidate=str(args.binary),sha256=sha(binary),
                limits=dict(total_bytes=cap,each_client_file_bytes=filecap,rounds=args.rounds,timeout_seconds=70),rounds=[],valid=False)
    save(out/'summary.json',report)
    anchor='cnetmod-h3-qlog-'+nonce;created=False;server=None;client=None
    image='sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
    try:
        subprocess.run(['docker','run','-d','--name',anchor,'--network','none','--entrypoint','/bin/sleep',image,'infinity'],check=True,stdout=subprocess.DEVNULL);created=True
        pid=subprocess.check_output(['docker','inspect',anchor,'--format','{{.State.Pid}}'],text=True).strip()
        prefix=['nsenter','-t',pid,'-n'];h2load='/var/tmp/cnetmod-h3-loadgen169/h2load'
        report['loadgen_sha256']=sha(Path(h2load));report['netns_anchor']=anchor
        def server_limits():resource.setrlimit(resource.RLIMIT_FSIZE,(filecap,filecap))
        with (out/'server.log').open('wb') as log:
            server=subprocess.Popen(prefix+[str(binary),str(args.arena/'certs/server.crt'),str(args.arena/'certs/server.key'),'8443','32',str(args.arena/'data/static')],stdout=log,stderr=subprocess.STDOUT,start_new_session=True,preexec_fn=server_limits)
            report['owned_server_pid']=server.pid;save(out/'summary.json',report)
            for _ in range(100):
                sockets=subprocess.check_output(prefix+['ss','-H','-u','-l','-n','sport = :8443'],text=True)
                if len(sockets.splitlines())==32:break
                if server.poll() is not None:raise RuntimeError('server exited before ready')
                time.sleep(.05)
            else:raise RuntimeError('32 worker sockets not bound')
            ready=subprocess.run(prefix+[h2load,'--alpn-list=h3','https://localhost:8443/baseline2?a=1&b=1','-n','1','-c','1','-m','1','-t','1'],capture_output=True,text=True,timeout=10)
            (out/'ready.log').write_text(ready.stdout+ready.stderr)
            if ready.returncode or not re.search(r'\b1 2xx',ready.stdout):raise RuntimeError('readiness request failed')
            time.sleep(2)
            for index in range(1,args.rounds+1):
                # Reserve worst-case qlogs64 + client output/request files3 at RLIMIT,
                # plus1GiB generated summary and compression/metadata margin.
                reserved=69*filecap+1024**3+256*1024**2
                if usage(out)+reserved>cap:raise RuntimeError('insufficient reserved disk budget for another complete round')
                print(f'ROUND {index} START',flush=True)
                rd=out/f'round-{index}';rd.mkdir();command=prefix+['taskset','-c','16-31',h2load,'--alpn-list=h3',
                    '-i',str(args.arena/'requests/static-h2-uris.txt'),'-H','Accept-Encoding: br;q=1, gzip;q=0.8',
                    '-c','64','-m','64','-t','64','-n','200000','--qlog-file-base='+str(rd/'client'),'--log-file='+str(rd/'requests.tsv')]
                save(rd/'command.json',command)
                def limits():resource.setrlimit(resource.RLIMIT_FSIZE,(filecap,filecap))
                reason=None
                env=os.environ.copy();env.pop('SSLKEYLOGFILE',None)
                if args.keylog:env['SSLKEYLOGFILE']=str(rd/'tls.keys')
                with (rd/'h2load.stdout').open('wb') as stdout,(rd/'h2load.stderr').open('wb') as stderr:
                    client=subprocess.Popen(command,stdout=stdout,stderr=stderr,start_new_session=True,preexec_fn=limits,env=env)
                    start=time.monotonic()
                    while client.poll() is None:
                        if time.monotonic()-start>70:reason='wall-time cap';break
                        if usage(out)>=cap:reason='total diagnostic disk cap';break
                        if len(list(rd.glob('client.*.sqlog')))>64:reason='unexpected connection file count';break
                        if client_file_cap_reached(rd,filecap):reason='per-file diagnostic cap';break
                        time.sleep(.2)
                    if reason:stop_owned(client)
                    code=client.wait();client=None
                print(f'ROUND {index} LOAD DONE exit={code} stop={reason}',flush=True)
                text=(rd/'h2load.stdout').read_text(errors='replace')+(rd/'h2load.stderr').read_text(errors='replace')
                summary=qlog.summarize(rd);save(rd/'qlog-summary.json',summary)
                print(f'ROUND {index} PARSE DONE complete={summary["diagnostic_complete"]} connections={len(summary["connections"])}',flush=True)
                files=list(rd.glob('client.*.sqlog'))
                diagnostic_ok=reason is None and code==0 and summary['diagnostic_complete'] and len(files)==64 and not client_file_cap_reached(rd,filecap)
                count=re.search(r'(\d+) 2xx',text)
                http_ok=bool(code==0 and count and int(count[1])==200000 and all(re.search(r'\b0 '+key,text) for key in ('failed','errored','timeout')))
                # Never turn parse unknown/truncation into PASS, even if h2load reports success.
                request_streams=sum(conn['request_streams'] for conn in summary['connections'])
                completed_streams=sum(conn['transport_complete'] for conn in summary['connections'])
                transport_ok=request_streams==200000 and completed_streams==200000 and sum(conn['requests_with_fin'] for conn in summary['connections'])==200000 and all(not conn['incomplete'] for conn in summary['connections'])
                result=dict(index=index,exit_code=code,diagnostic_complete=diagnostic_ok,http_complete=http_ok,
                            transport_complete=transport_ok,request_streams=request_streams,completed_streams=completed_streams,stop_reason=reason,passed=diagnostic_ok and http_ok and transport_ok)
                report['rounds'].append(result);save(out/'summary.json',report)
                if not result['passed']:
                    # Keep every raw qlog, request log and shared server close log on failure.
                    time.sleep(2);raise RuntimeError(f'round{index} failed; preserve raw diagnostics')
                manifest=[]
                for path in files:
                    manifest.append(dict(file=path.name,bytes=path.stat().st_size,sha256=sha(path)))
                    compressed=Path(str(path)+'.gz')
                    with path.open('rb') as source,gzip.open(compressed,'wb',compresslevel=1) as dest:shutil.copyfileobj(source,dest)
                    # Remove only this newly created round's exact original after successful compression.
                    path.unlink()
                save(rd/'qlog-manifest.json',manifest)
                if usage(out)>=cap:raise RuntimeError('archive reached total disk cap; no later round started')
                time.sleep(2)
            report['valid']=True
    except BaseException as error:
        report['failure']=repr(error);raise
    finally:
        stop_owned(client);stop_owned(server)
        if created:subprocess.run(['docker','rm','-f',anchor],check=True,stdout=subprocess.DEVNULL)
        report['retained_bytes']=usage(out);save(out/'summary.json',report)
        print('RESULTS',out,flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--binary',type=Path,default=Path('/var/tmp/cnetmod-h3-close-diagnostic-latest-native-r9c/arena_h3_server'))
    p.add_argument('--keylog',action='store_true',help='optional bounded per-round SSLKEYLOGFILE; not required for decoded qlog ranges')
    p.add_argument('--sha256',required=True);p.add_argument('--arena',type=Path,default=Path('/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena'))
    p.add_argument('--rounds',type=int,default=12);p.add_argument('--total-gib',type=int,default=8);p.add_argument('--file-mib',type=int,default=64)
    run(p.parse_args())
