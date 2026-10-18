"""Nine serial static-only larger validated-PMTU ceiling rounds, prepared only.
Fixed receive65507, GSO1, system allocator; balanced16384/32768/65507 order.
Not an official ranking. Requires matching bounded qlog preflight and binary,
source snapshot, allocator proof. By default unobserved convergence blocks load;
--allow-unconverged-ceilings runs explicitly labelled configuration trials only.
Timed done=succeeded>0,2xx>=done, all errors/non2xx0; headers are not completions.
Run only in the coordinator's serial window via the isolated mount/gai wrapper.
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


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def parse(text):
    counts = re.search(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout', text)
    status = re.search(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx', text)
    rate = re.search(r'finished in ([\d.]+)(ms|s|m),\s*([\d.]+) req/s', text)
    assert counts and status and rate, 'missing h2load count/status/rate'
    total, started, done, succeeded, failed, errored, timeout = map(int, counts.groups())
    ok, redirection, client_error, server_error = map(int, status.groups())
    assert done == succeeded and done > 0 and ok >= done, 'incomplete/failed completed request counts'
    assert failed == errored == timeout == redirection == client_error == server_error == 0, 'nonzero errors'
    elapsed = float(rate[1]) * {'ms': .001, 's': 1, 'm': 60}[rate[2]]
    assert elapsed > 0
    latency = next(line.strip() for line in text.splitlines() if line.startswith('request '))
    # loadgen1.69 table: min max median p95 p99 mean sd +/-sd.
    values = re.findall(r'([\d.]+)(us|ms|s|m)', latency)
    assert len(values) >= 7 and 'median' in text and 'p99' in text
    milliseconds = [float(value) * {'us': .001, 'ms': 1, 's': 1000, 'm': 60000}[unit] for value, unit in values]
    udp = re.search(r'UDP datagram: (\d+) sent, (\d+) received', text)
    assert udp
    return dict(total=total, started=started, done=done, succeeded=succeeded,
        status_2xx=ok, header_completion_delta=ok-done, failed=failed, errored=errored,
        timeout=timeout, printed_rps=float(rate[3]), elapsed_seconds=elapsed,
        completed_per_elapsed_second=done/elapsed, status_2xx_per_elapsed_second=ok/elapsed,
        p99_ms=milliseconds[4], median_ms=milliseconds[2], latency_line=latency,
        udp_sent=int(udp[1]), udp_received=int(udp[2]))


def sample(pid):
    fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    smaps = Path(f'/proc/{pid}/smaps_rollup').read_text()
    return dict(cpu_seconds=(int(fields[11])+int(fields[12]))/os.sysconf('SC_CLK_TCK'),
        memory_kib={key:int(value) for key,value in re.findall(r'^(Rss|Pss|Private_Clean|Private_Dirty):\s+(\d+) kB$',smaps,re.M)})


def stop(process):
    if process is not None and process.poll() is None:
        os.killpg(process.pid,signal.SIGTERM)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid,signal.SIGKILL)
            process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--binary-sha', required=True)
    parser.add_argument('--source-snapshot', type=Path, required=True)
    parser.add_argument('--allocator-proof',type=Path,required=True)
    parser.add_argument('--source-root',type=Path,default=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod'))
    parser.add_argument('--arena',type=Path,default=Path('/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena'))
    parser.add_argument('--preflight-report',type=Path,required=True)
    parser.add_argument('--allow-unconverged-ceilings',action='store_true',help='diagnostic configuration timing; never claims actual target MTU was reached')
    args=parser.parse_args()
    proof_sha=sha(args.allocator_proof)
    loadgen_sha=sha('/var/tmp/cnetmod-h3-loadgen169/h2load')
    preflight_sha=sha(args.preflight_report)
    assert sha(args.binary)==args.binary_sha.lower() and args.binary.read_bytes()[:4]==b'\x7fELF'
    assert not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT') and not os.environ.get('SSLKEYLOGFILE')
    allocator=json.loads(args.allocator_proof.read_text())
    assert allocator['system_allocator_verified'] and allocator['cmake_use_mimalloc_off']
    assert allocator['binary_sha256']==args.binary_sha.lower()
    metadata=args.source_snapshot/'metadata.json'
    diff=args.source_snapshot/'source.diff'
    snapshot=json.loads(metadata.read_text(encoding='utf-8-sig'))
    assert re.fullmatch(r'[0-9a-f]{40}',snapshot['head'])
    assert sha(diff)==snapshot['diff_sha256'].lower()
    inputs=snapshot['sources']
    assert {'src/protocol/quic/quic_connection.cpp','src/executor/platform/io_uring_async_op.cpp','testing/bench/arena_h3_server.cpp'} <= inputs.keys()

    def verify_sources():
        assert sha(metadata)==metadata_sha and sha(diff)==snapshot['diff_sha256'].lower()
        assert sha(args.allocator_proof)==proof_sha
        assert sha('/var/tmp/cnetmod-h3-loadgen169/h2load')==loadgen_sha
        assert sha(args.preflight_report)==preflight_sha
        for name,digest in inputs.items():
            source=(args.source_root/name).resolve()
            assert source.is_relative_to(args.source_root.resolve()) and sha(source)==digest.lower(),name

    metadata_sha=sha(metadata)
    verify_sources()
    # Independent diagnostic evidence is required before untraced timing.
    # Its producer must distinguish application data from PMTU-only probes
    # and UDP_SEGMENT stride from a grouped syscall message's total length.
    preflight=json.loads(args.preflight_report.read_text())
    assert preflight['valid'] and preflight['binary_sha256']==args.binary_sha.lower()
    # Completed preflight keeps its exact original parser. This historical
    # version differs only in the corrected fixture-symlink gate below.
    preflight_ab_source=args.preflight_report.parent/'large_mtu_ab.py'
    assert preflight['ab_parser_sha256']==sha(preflight_ab_source)
    assert preflight['ab_parser_sha256'] in (sha(Path(__file__)),
        '8c6a04b7107c59d650ac658da8f1e30e28b8a7c45fcd6e64d5c309c70a74cbfd')
    assert preflight['source_metadata_sha256']==metadata_sha
    assert preflight['source_diff_sha256']==sha(diff)
    assert preflight['allocator_proof_sha256']==sha(args.allocator_proof)
    assert preflight['loadgen_sha256']==sha('/var/tmp/cnetmod-h3-loadgen169/h2load')
    assert preflight['gai_conf_sha256']==sha('/etc/gai.conf')
    for name,digest in preflight['fixture_sha256'].items():
        relative=Path(name)
        assert not relative.is_absolute() and '..' not in relative.parts
        source=args.arena/relative
        assert sha(source)==digest, name
    assert preflight['receive_payload_limit']==65507 and preflight['gso_environment']=='1'
    assert preflight['peer_max_udp_payload_size']>=65507
    measurements=preflight['measurements']
    assert len(measurements)==3 and {r['ceiling'] for r in measurements}=={16384,32768,65507}
    for measurement in measurements:
        ceiling=measurement['ceiling']
        assert measurement['actual_application_packet_sizes_verified'] and measurement['response_packet_counts_verified']
        assert measurement['completed_requests']>0 and measurement['response_stream_packet_count']>0
        maximum=measurement['max_response_stream_quic_packet_bytes']
        assert 0<maximum<=ceiling
        convergence=measurement['convergence']
        assert convergence['configured_send_ceiling']==ceiling
        assert convergence['max_non_probe_response_packet_bytes']==maximum
        assert convergence['status'] in ('converged_by_client_evidence','not_converged_in_diagnostic')
        assert convergence['converged']==(convergence['status']=='converged_by_client_evidence')
        assert args.allow_unconverged_ceilings or convergence['converged'], 'preflight target not observed; preserve this evidence before a larger fixed-n diagnostic or explicit configuration-only timing'
        evidence=measurement['raw_evidence']
        assert evidence
        for record in evidence:
            assert sha(record['path'])==record['sha256']
    assert set(range(16,32))<=os.sched_getaffinity(0)
    assert shutil.disk_usage('/var/tmp').free>=2*1024**3
    image='sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
    subprocess.run(['docker','image','inspect',image],check=True,stdout=subprocess.DEVNULL)
    out=Path('/var/tmp/cnetmod-h3-large-mtu-ab-'+uuid.uuid4().hex)
    out.mkdir()
    binary=out/'arena_h3_server'
    shutil.copy2(args.binary,binary)
    shutil.copy2(metadata,out/'source-metadata.json')
    shutil.copy2(diff,out/'source.diff')
    shutil.copy2(args.allocator_proof,out/'allocator-proof.json')
    shutil.copy2(args.preflight_report,out/'preflight-report.json')
    shutil.copy2(Path(__file__),out/'runner.py')
    shutil.copy2(preflight_ab_source,out/'preflight-ab-original.py')
    (out/'fixture-check-repair.json').write_text(json.dumps(dict(original_preflight_report_sha256=preflight_sha, original_ab_sha256=preflight['ab_parser_sha256'], actual_ab_sha256=sha(Path(__file__)), scope='Accept existing fixture symlinks after rejecting absolute or parent traversal names; retain exact content hashes. Prior attempt failed before output directory or any load.'),indent=2))
    h2load=Path('/var/tmp/cnetmod-h3-loadgen169/h2load')
    report=dict(valid=False,scope=__doc__,binary_sha256=sha(binary),binary_source=str(args.binary),
        actual_head=snapshot['head'],source_metadata_sha256=metadata_sha,source_diff_sha256=sha(diff),
        preflight_report_sha256=sha(args.preflight_report),
        loadgen_sha256=sha(h2load),gai_conf_sha256=sha('/etc/gai.conf'),image=image,
        order='16384/32768/65507,65507/16384/32768,32768/65507/16384 static only; same binary',
        receive_payload_limit=65507,gso_environment='1',send_discovery_ceilings=[16384,32768,65507],
        application_quic_packet_sizes_verified_by_preflight=True,
        all_preflight_ceilings_converged=all(r['convergence']['converged'] for r in measurements),
        allow_unconverged_ceilings=args.allow_unconverged_ceilings,
        convergence_by_ceiling={str(r['ceiling']):r['convergence'] for r in measurements},
        runner_sha256=sha(Path(__file__)),
        actual_validated_pmtu_internal_state_observed=False,probe_interval_milliseconds=1,
        rounds=[],allocator='system',
        allocator_proof_sha256=sha(args.allocator_proof),ld_preload=None,ld_audit=None,
        fixture_sha256={str(f.relative_to(args.arena)):sha(f) for directory in ('certs','requests','data/static') for f in sorted((args.arena/directory).rglob('*')) if f.is_file()})
    anchor='cnetmod-large-mtu-ab-'+uuid.uuid4().hex
    server=None
    anchor_created=False

    def save():
        (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')

    save()
    try:
        subprocess.run(['docker','run','--pull=never','-d','--name',anchor,'--network','none','--entrypoint','/bin/sleep',image,'infinity'],check=True,stdout=subprocess.DEVNULL)
        anchor_created=True
        pid=subprocess.check_output(['docker','inspect',anchor,'--format','{{.State.Pid}}'],text=True).strip()
        prefix=['nsenter','-t',pid,'-n']
        subprocess.run(prefix+['ip','link','set','lo','up'],check=True)
        for profile in ('static-h3',):
            for pair,order in enumerate(((16384,32768,65507),(65507,16384,32768),(32768,65507,16384)),1):
                for variant in order:
                    verify_sources()
                    assert sha(binary)==args.binary_sha.lower() and sha(args.binary)==args.binary_sha.lower()
                    name=f'{profile}-{pair}-{variant}'
                    print('ROUND START',name,flush=True)
                    env=os.environ.copy()
                    env['CNETMOD_QUIC_GSO']='1'
                    env['CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE']='65507'
                    env['CNETMOD_QUIC_MAX_PATH_MTU']=str(variant)
                    command=prefix+[str(binary),str(args.arena/'certs/server.crt'),str(args.arena/'certs/server.key'),'8443','32']
                    if profile=='static-h3':command.append(str(args.arena/'data/static'))
                    with (out/(name+'-server.log')).open('w') as log:
                        server=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
                        (out/(name+'-commands.json')).write_text(json.dumps(dict(server=command,gso_environment=env['CNETMOD_QUIC_GSO'],max_path_mtu=variant,max_udp_payload_size=65507),indent=2))
                        for _ in range(200):
                            sockets=subprocess.check_output(prefix+['ss','-H','-u','-l','-n','sport = :8443'],text=True)
                            if len(sockets.splitlines())==32:break
                            assert server.poll() is None,'server exited'
                            time.sleep(.05)
                        else:raise AssertionError('32 workers not ready')
                        with (out/(name+'-ready.stdout')).open('w') as stdout,(out/(name+'-ready.stderr')).open('w') as stderr:
                            ready=subprocess.run(prefix+[str(h2load),'--alpn-list=h3','https://localhost:8443/baseline2?a=1&b=1','-n','1','-c','1','-m','1','-t','1'],stdout=stdout,stderr=stderr,text=True,timeout=15)
                        ready_counts=parse((out/(name+'-ready.stdout')).read_text()+(out/(name+'-ready.stderr')).read_text())
                        assert ready.returncode==0 and all(ready_counts[field]==1 for field in ('total','started','done','succeeded','status_2xx'))
                        time.sleep(2)
                        load=prefix+['taskset','-c','16-31',str(h2load),'--alpn-list=h3']
                        if profile=='static-h3':load+=['-i',str(args.arena/'requests/static-h2-uris.txt'),'-H','Accept-Encoding: br;q=1, gzip;q=0.8']
                        else:load+=['https://localhost:8443/baseline2?a=1&b=1']
                        load+=['-c','64','-m','64','-t','64','-D','5s']
                        (out/(name+'-load-command.json')).write_text(json.dumps(load,indent=2))
                        before=sample(server.pid)
                        with (out/(name+'-h2load.stdout')).open('w') as stdout,(out/(name+'-h2load.stderr')).open('w') as stderr:
                            result=subprocess.run(load,stdout=stdout,stderr=stderr,text=True,timeout=50)
                        after=sample(server.pid)
                        print('LOAD DONE',name,'exit',result.returncode,flush=True)
                        assert result.returncode==0 and server.poll() is None
                        text=(out/(name+'-h2load.stdout')).read_text()+(out/(name+'-h2load.stderr')).read_text()
                        row=parse(text)
                        row.update(profile=profile,block=pair,variant=variant,max_path_mtu=variant,
                            max_udp_payload_size=65507,gso_environment='1',before=before,after=after,
                            server_cpu_seconds=after['cpu_seconds']-before['cpu_seconds'],command=load,server_command=command)
                        row['server_cpu_microseconds_per_completed']=row['server_cpu_seconds']*1e6/row['done']
                        row['preflight_convergence_status']=report['convergence_by_ceiling'][str(variant)]['status']
                        report['rounds'].append(row)
                        save()
                        print('ROUND VALID',json.dumps(row),flush=True)
                        stop(server)
                        row['server_stop_exit']=server.wait()
                        server=None
                        save()
                    time.sleep(2)
        verify_sources()
        assert len(report['rounds'])==9 and sha(binary)==sha(args.binary)==args.binary_sha.lower()
        report['valid']=True
    except BaseException as error:
        report['error']=repr(error)
        raise
    finally:
        try:stop(server)
        except BaseException as cleanup_error:
            report['valid']=False
            report['process_cleanup_error']=repr(cleanup_error)
        if anchor_created:
            cleanup=subprocess.run(['docker','rm','-f',anchor],capture_output=True,text=True)
            report['cleanup']=dict(returncode=cleanup.returncode,stdout=cleanup.stdout,stderr=cleanup.stderr)
            if cleanup.returncode!=0:report['valid']=False
        save()
        print('RESULTS',out,'valid',report['valid'],flush=True)
    assert report['valid'],'cleanup or validation failed; see retained summary'


if __name__=='__main__':
    main()
