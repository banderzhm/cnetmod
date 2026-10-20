"""Bounded larger-PMTU client-qlog diagnostic, prepared only.
Fixed receive65507/GSO1/system allocator; send16384/32768/65507, fresh32workers.
Low-concurrency fixed-n1c1m1t is deliberately distinct from timed64c64m64t.
HTTP/FIN validity and observed convergence are separate results. A configured
ceiling or padded probe alone never proves ordinary application packets reached it.
"""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import time
import uuid

TOOLS=Path(__file__).resolve().parent
FILE_CAP=64*1024**2
TOTAL_CAP=1024**3


def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda:f.read(1024**2),b''):h.update(chunk)
    return h.hexdigest()


def save(path,value):
    data=json.dumps(value,indent=2)+'\n'
    assert len(data.encode())<16*1024**2,'summary exceeds16MiB'
    path.write_text(data)


def intervals_add(ranges,start,end):
    assert 0<=start<=end
    result=[]
    for lo,hi in sorted(ranges+[(start,end)]):
        if result and lo<=result[-1][1]:result[-1]=(result[-1][0],max(hi,result[-1][1]))
        else:result.append((lo,hi))
    assert len(result)<=128,'stream interval budget'
    return result


def analyze_qlog_base(path,requests):
    assert path.stat().st_size<FILE_CAP
    local=[];remote=[];streams={};packet_sizes={};memberships=set();all_received=set()
    header=False;events=0;duplicate_received=0;non_pn_packets=Counter()
    with path.open('rb') as f:
        while True:
            line=f.readline(1024**2+1)
            if not line:break
            assert len(line)<=1024**2,'record budget'
            if not line.strip():continue
            assert line.startswith(b'\x1e'),'not JSON-SEQ'
            event=json.loads(line[1:])
            assert isinstance(event,dict)
            if 'qlog_version' in event:
                assert not header
                header=(event['qlog_version']=='0.3' and event['qlog_format']=='JSON-SEQ' and
                    event['trace']['vantage_point']['type']=='client')
                assert header,'unsupported qlog schema/vantage'
                continue
            events+=1
            assert events<=1000000,'event budget'
            name=event.get('name');data=event.get('data',{})
            if name=='transport:parameters_set':
                assert data.get('owner') in ('local','remote')
                value=int(data['max_udp_payload_size'])
                (local if data['owner']=='local' else remote).append(value)
            if name not in ('transport:packet_received','transport:packet_sent'):continue
            kind=data['header']['packet_type']
            if kind in ('retry','version_negotiation'):
                # These are valid non-PN packets and have no decoded frames.
                # ngtcp2 can emit a placeholder packet_number for Retry; it
                # must not enter the application PN or STREAM accounting.
                assert not data.get('frames'), 'non-PN packet carries frames'
                non_pn_packets[kind]+=1
                continue
            assert kind in ('initial','handshake','0RTT','1RTT'),'unknown packet type'
            frames=data['frames'];assert isinstance(frames,list) and len(frames)<=4096
            received=name.endswith('received')
            pn=int(data['header']['packet_number'])
            if received:
                key=(kind,pn)
                if key in all_received:duplicate_received+=1
                all_received.add(key)
            response_sids=set()
            for frame in frames:
                if frame.get('frame_type') not in ('stream','reset_stream','stop_sending'):continue
                sid=int(frame['stream_id'])
                if sid%4:continue
                if sid not in streams:
                    assert len(streams)<requests,'stream budget'
                    streams[sid]={'sent':[],'received':[],'sent_fin':set(),'received_fin':set(),'reset':False}
                state=streams[sid]
                if frame['frame_type']!='stream':state['reset']=True;continue
                start=int(frame.get('offset',0));end=start+int(frame['length'])
                direction='received' if received else 'sent'
                state[direction]=intervals_add(state[direction],start,end)
                if frame.get('fin',False):state[direction+'_fin'].add(end)
                assert len(state[direction+'_fin'])<=1,'conflicting FIN'
                if received:
                    assert kind=='1RTT','unexpected server response packet space'
                    response_sids.add(sid)
            if response_sids:
                length=int(data['raw']['length'])
                assert 0<length<=65507
                assert pn not in packet_sizes or packet_sizes[pn]==length
                packet_sizes[pn]=length
                memberships.update((sid,pn) for sid in response_sids)
    assert header and local and remote and streams and packet_sizes
    assert min(local)>=65507 and set(remote)=={65507},'peer/server payload limit mismatch'
    complete=[];incomplete=[]
    for sid,state in sorted(streams.items()):
        ok=not state['reset']
        for direction in ('sent','received'):
            finals=state[direction+'_fin']
            ok=ok and len(finals)==1 and state[direction]==[(0,next(iter(finals)))]
        (complete if ok else incomplete).append(sid)
    assert len(complete)==requests and not incomplete,'fixed-count transport incomplete'
    histogram=Counter(packet_sizes.values())
    return dict(trace_file=str(path),events=events,peer_max_udp_payload_size=min(local),
        server_max_udp_payload_size=min(remote),request_stream_count=len(streams),
        complete_response_stream_count=len(complete),incomplete_stream_ids=incomplete,
        response_stream_packet_count=len(packet_sizes),
        response_stream_packet_memberships=len(memberships),
        received_1rtt_packet_count=sum(kind=='1RTT' for kind,pn in all_received),
        duplicate_received_packet_events=duplicate_received,
        non_pn_packet_counts=dict(non_pn_packets),
        response_stream_packet_size_histogram={str(k):v for k,v in sorted(histogram.items())},
        max_response_stream_quic_packet_bytes=max(packet_sizes.values()),
        response_stream_packets_per_completed=len(packet_sizes)/requests,
        response_stream_packet_memberships_per_completed=len(memberships)/requests,
        fin_complete_stream_ids=complete,
        raw_length_scope='qlog raw.length: received1RTT response STREAM QUIC packet bytes; short-header packet is final in UDP datagram; no independent wire capture')


def analyze_qlog(path, requests, ceiling):
    from large_mtu_qlog import add_timeline
    base = analyze_qlog_base(path, requests)
    base['convergence'] = add_timeline(path, ceiling, base)
    return base


def stop_owned(process):
    if process is not None and process.poll() is None:
        os.killpg(process.pid,signal.SIGTERM)
        try:process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGKILL);process.wait(timeout=10)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--binary-sha',required=True)
    parser.add_argument('--source-snapshot',type=Path,required=True)
    parser.add_argument('--allocator-proof',type=Path,required=True)
    parser.add_argument('--requests',type=int,default=4096)
    parser.add_argument('--diagnostic-rps',type=int,default=1000,help='h2load per-client request budget; diagnostic only, never timed A/B')
    parser.add_argument('--source-root',type=Path,default=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod'))
    parser.add_argument('--arena',type=Path,default=Path('/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena'))
    args=parser.parse_args()
    from large_mtu_qlog import encoded_fixture_capability
    fixture_capability=encoded_fixture_capability(args.arena)
    assert fixture_capability['suitable_for_65507_single_stream_application_packet'], 'diagnostic unsuitable: selected encoded bodies are too small; this is not a PMTU/protocol failure'
    h2load=Path('/var/tmp/cnetmod-h3-loadgen169/h2load')
    loadgen_sha=sha(h2load);proof_sha=sha(args.allocator_proof)
    assert 512<=args.requests<=8192
    assert 100<=args.diagnostic_rps<=2000
    assert 3<=args.requests/args.diagnostic_rps<=60, 'choose fixed-n/rps for nominal3..60s diagnostic, within90s wall cap'
    assert not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT') and not os.environ.get('SSLKEYLOGFILE')
    assert sha(args.binary)==args.binary_sha.lower() and args.binary.read_bytes()[:4]==b'\x7fELF'
    proof=json.loads(args.allocator_proof.read_text())
    assert proof['system_allocator_verified'] and proof['cmake_use_mimalloc_off'] and proof['binary_sha256']==sha(args.binary)
    metadata=args.source_snapshot/'metadata.json';diff=args.source_snapshot/'source.diff'
    snapshot=json.loads(metadata.read_text(encoding='utf-8-sig'));metadata_sha=sha(metadata)
    assert sha(diff)==snapshot['diff_sha256'].lower()
    def verify():
        assert sha(metadata)==metadata_sha and sha(diff)==snapshot['diff_sha256'].lower()
        for name,digest in snapshot['sources'].items():
            source=(args.source_root/name).resolve()
            assert source.is_relative_to(args.source_root.resolve()) and sha(source)==digest.lower(),name
        assert sha(args.binary)==args.binary_sha.lower()
        assert sha(h2load)==loadgen_sha and sha(args.allocator_proof)==proof_sha
    verify()
    assert set(range(16,32))<=os.sched_getaffinity(0)
    image='sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
    out=Path('/var/tmp/cnetmod-h3-large-mtu-preflight-'+uuid.uuid4().hex);out.mkdir()
    assert shutil.disk_usage(out).free>2*TOTAL_CAP
    binary=out/'arena_h3_server';shutil.copy2(args.binary,binary)
    shutil.copy2(TOOLS/'large_mtu_qlog.py',out/'large_mtu_qlog.py')
    shutil.copy2(TOOLS/'large_mtu_ab.py',out/'large_mtu_ab.py')
    for src,name in [(metadata,'source-metadata.json'),(diff,'source.diff'),(args.allocator_proof,'allocator-proof.json'),(Path(__file__),'runner.py')]:shutil.copy2(src,out/name)
    h2load=Path('/var/tmp/cnetmod-h3-loadgen169/h2load')
    spec=importlib.util.spec_from_file_location('mtu_ab',TOOLS/'large_mtu_ab.py')
    ab=importlib.util.module_from_spec(spec);spec.loader.exec_module(ab)
    report=dict(valid=False,scope=__doc__,binary_sha256=sha(binary),source_metadata_sha256=metadata_sha,
        source_diff_sha256=sha(diff),loadgen_sha256=sha(h2load),receive_payload_limit=65507,gso_environment='1',
        peer_max_udp_payload_size=None,measurements=[],allocator='system',allocator_proof_sha256=sha(args.allocator_proof),
        gai_conf_sha256=sha('/etc/gai.conf'),image=image,
        encoded_fixture_capability=fixture_capability,diagnostic_requests=args.requests,diagnostic_request_rps=args.diagnostic_rps,diagnostic_nominal_duration_seconds=args.requests/args.diagnostic_rps,diagnostic_clients=1,diagnostic_streams=1,diagnostic_threads=1,
        server_workers=32,file_cap_bytes=FILE_CAP,total_cap_bytes=TOTAL_CAP,
        actual_validated_pmtu_internal_state_observed=False,runner_sha256=sha(Path(__file__)),
        ab_parser_sha256=sha(TOOLS/'large_mtu_ab.py'),timeline_parser_sha256=sha(TOOLS/'large_mtu_qlog.py'),
        fixture_sha256={str(p.relative_to(args.arena)):sha(p) for directory in ('certs','requests','data/static') for p in sorted((args.arena/directory).rglob('*')) if p.is_file()})
    anchor='cnetmod-large-mtu-preflight-'+uuid.uuid4().hex;created=False;server=client=None
    def persist():save(out/'preflight-report.json',report)
    def size():return sum(p.stat().st_size for p in out.rglob('*') if p.is_file())
    def limits():resource.setrlimit(resource.RLIMIT_FSIZE,(FILE_CAP,FILE_CAP))
    persist()
    try:
        subprocess.run(['docker','run','--pull=never','-d','--name',anchor,'--network','none','--entrypoint','/bin/sleep',image,'infinity'],check=True,stdout=subprocess.DEVNULL);created=True
        pid=subprocess.check_output(['docker','inspect',anchor,'--format','{{.State.Pid}}'],text=True).strip()
        prefix=['nsenter','-t',pid,'-n'];subprocess.run(prefix+['ip','link','set','lo','up'],check=True)
        for ceiling in (16384,32768,65507):
            verify();assert sha(binary)==args.binary_sha.lower()
            rd=out/str(ceiling);rd.mkdir()
            print('PREFLIGHT START',ceiling,flush=True)
            env=os.environ.copy();env.pop('SSLKEYLOGFILE',None)
            env.update(CNETMOD_QUIC_GSO='1',CNETMOD_QUIC_MAX_PATH_MTU=str(ceiling),CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE='65507')
            command=prefix+[str(binary),str(args.arena/'certs/server.crt'),str(args.arena/'certs/server.key'),'8443','32',str(args.arena/'data/static')]
            save(rd/'server-command.json',dict(command=command,environment={k:env[k] for k in ('CNETMOD_QUIC_GSO','CNETMOD_QUIC_MAX_PATH_MTU','CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE')}))
            with (rd/'server.log').open('wb') as log:
                server=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True,preexec_fn=limits)
                report['owned_server_pid']=server.pid;persist()
                for _ in range(200):
                    sockets=subprocess.check_output(prefix+['ss','-H','-u','-l','-n','sport = :8443'],text=True)
                    if len(sockets.splitlines())==32:break
                    assert server.poll() is None,'server exited before ready'
                    time.sleep(.05)
                else:raise AssertionError('32socket readiness timeout')
                ready=subprocess.run(prefix+[str(h2load),'--alpn-list=h3','https://localhost:8443/baseline2?a=1&b=1','-n','1','-c','1','-m','1','-t','1'],capture_output=True,text=True,timeout=15)
                (rd/'ready.stdout').write_text(ready.stdout);(rd/'ready.stderr').write_text(ready.stderr)
                ready_counts=ab.parse(ready.stdout+ready.stderr)
                assert ready.returncode==0 and all(ready_counts[k]==1 for k in ('total','started','done','succeeded','status_2xx'))
                time.sleep(2)
                load=prefix+['taskset','-c','16-31',str(h2load),'--alpn-list=h3','-i',str(args.arena/'requests/static-h2-uris.txt'),'-H','Accept-Encoding: br;q=1, gzip;q=0.8','-c','1','-m','1','-t','1','-n',str(args.requests),'--rps='+str(args.diagnostic_rps),'--qlog-file-base='+str(rd/'client')]
                save(rd/'client-command.json',load)
                client_env=os.environ.copy();client_env.pop('SSLKEYLOGFILE',None)
                with (rd/'h2load.stdout').open('wb') as stdout,(rd/'h2load.stderr').open('wb') as stderr:
                    client=subprocess.Popen(load,env=client_env,stdout=stdout,stderr=stderr,start_new_session=True,preexec_fn=limits)
                    start=time.monotonic()
                    while client.poll() is None:
                        assert time.monotonic()-start<90,'diagnostic wall-time cap'
                        assert server.poll() is None,'server exited during diagnostic'
                        assert size()<TOTAL_CAP,'total diagnostic disk cap'
                        assert len(list(rd.glob('client.*.sqlog')))<=1,'unexpected multiple client qlogs'
                        assert all(p.stat().st_size<FILE_CAP for p in rd.iterdir() if p.is_file()),'per-file diagnostic cap'
                        time.sleep(.1)
                    code=client.wait();client=None
                assert code==0 and server.poll() is None
                stop_owned(server);server_exit=server.wait();server=None
            print('PREFLIGHT LOAD DONE',ceiling,flush=True)
            counts=ab.parse((rd/'h2load.stdout').read_text()+(rd/'h2load.stderr').read_text())
            assert all(counts[k]==args.requests for k in ('total','started','done','succeeded','status_2xx')),'fixed-count HTTP mismatch'
            qlogs=list(rd.glob('client.*.sqlog'));assert len(qlogs)==1
            transport=analyze_qlog(qlogs[0],args.requests,ceiling)
            maximum=transport['convergence']['max_non_probe_response_packet_bytes']
            assert 0<maximum<=ceiling, 'invalid application packet size'
            save(rd/'analysis.json',dict(http=counts,transport=transport,client_exit=code,server_stop_exit=server_exit))
            raw=[dict(path=str(p.resolve()),sha256=sha(p),bytes=p.stat().st_size) for p in sorted(rd.iterdir()) if p.is_file()]
            row=dict(ceiling=ceiling,completed_requests=counts['done'],response_stream_packet_count=transport['response_stream_packet_count'],
                max_response_stream_quic_packet_bytes=maximum,
                actual_application_packet_sizes_verified=True,response_packet_counts_verified=True,
                convergence=transport['convergence'],
                response_stream_packets_per_completed=transport['response_stream_packets_per_completed'],
                peer_max_udp_payload_size=transport['peer_max_udp_payload_size'],
                max_udp_payload_size=65507,gso_environment='1',raw_evidence=raw,
                packet_size_scope=transport['raw_length_scope'])
            report['measurements'].append(row)
            peers=[r['peer_max_udp_payload_size'] for r in report['measurements']]
            report['peer_max_udp_payload_size']=min(peers);persist()
            print('PREFLIGHT DIAGNOSTIC VALID',ceiling,'convergence',row['convergence']['status'],'max_non_probe_application_quic_packet',maximum,'response_packets',row['response_stream_packet_count'],flush=True)
            time.sleep(2)
        verify();assert len(report['measurements'])==3;report['valid']=True
        report['all_converged']=all(r['convergence']['converged'] for r in report['measurements'])
    except BaseException as error:
        report['failure']=repr(error);raise
    finally:
        for owned in (client,server):
            try:stop_owned(owned)
            except BaseException as cleanup_error:
                report['valid']=False
                report.setdefault('process_cleanup_errors',[]).append(repr(cleanup_error))
        if created:
            cleaned=subprocess.run(['docker','rm','-f',anchor],capture_output=True,text=True)
            report['cleanup']=dict(returncode=cleaned.returncode,stdout=cleaned.stdout,stderr=cleaned.stderr)
            if cleaned.returncode:report['valid']=False
        report['retained_bytes']=size();persist()
        print('PREFLIGHT RESULTS',out,'valid',report['valid'],'all_converged',report.get('all_converged',False),flush=True)
    assert report['valid']


if __name__=='__main__':main()
