"""Low-priority offline analysis of two completed system-allocator profiles.
Never builds, records perf, or starts a server/client. Existing summaries stay intact.
"""
from collections import defaultdict
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

os.nice(19)
roots={
    'off':Path('/var/tmp/cnetmod-h3-system-perf-75d3473f92744a87ad2bf125387ea28e'),
    'on':Path('/var/tmp/cnetmod-h3-system-perf-7fff9ece9adc48bfb0e81a0f5c9dc4fe')}
expected_binary='2cb22a32818646fbf63575370adf6a0c48b3ba5df4bbbc3d3002ab2838003e0a'
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
results={}
for mode,root in roots.items():
    out=root/'offline-analysis'
    out.mkdir(exist_ok=True)
    summary_sha=sha(root/'summary.json')
    original=json.loads((root/'summary.json').read_text())
    assert original['valid'] and original['allocator']=='system' and original['gso']==mode
    assert original['perf_record_exit_code']==-2 and original['perf_stop_requested']=='SIGINT after workload completion'
    assert original['namespace_cleanup']['returncode']==0
    assert sha(root/'arena_h3_server')==expected_binary==original['binary_sha256']
    assert sha(root/'supplied-source-metadata.json')==original['source_snapshot_sha256']
    assert sha(root/'actual-dirty-source.patch')==original['source_diff_sha256']
    assert sha(root/'allocator-proof.json')==original['allocator_proof_sha256']
    metadata=json.loads((root/'supplied-source-metadata.json').read_text(encoding='utf-8-sig'))
    proof=json.loads((root/'allocator-proof.json').read_text())
    assert proof['system_allocator_verified'] and proof['cmake_use_mimalloc_off']
    assert proof['binary_sha256']==expected_binary and not proof['ld_preload'] and not proof['ld_audit']
    assert metadata['head']==original['actual_head'] and metadata['diff_sha256']==original['source_diff_sha256']
    assert {item['path']:item['sha256'] for item in original['source_inputs']}==metadata['sources']
    command=['/usr/sbin/perf','report','--stdio','--no-children','--percent-limit','0',
        '--show-nr-samples','--show-total-period','--sort','symbol,dso','-g','none',
        '-i',str(root/'static-perf.data')]
    (out/'self-counts-command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (out/'self-counts.txt').open('w') as output:
        code=subprocess.run(command,stdout=output,stderr=subprocess.STDOUT,timeout=60).returncode
    assert code==0
    report=(out/'self-counts.txt').read_text()
    lost=int(re.search(r'Total Lost Samples:\s+(\d+)',report)[1])
    assert lost==0
    assert 'Total Lost Samples: 0' in (root/'static-perf-self.txt').read_text()
    header=(root/'static-perf-header.txt').read_text()
    assert 'cpu-clock' in header
    event_period=int(re.search(r'Event count \(approx.\):\s+(\d+)',report)[1])
    record=(root/'perf-record.log').read_text()
    captured=int(re.search(r'\((\d+) samples\)',record)[1])
    rows=[]
    for line in report.splitlines():
        match=re.match(r'^\s*([\d.]+)%\s+(\d+)\s+(\d+)\s+(.*)$',line)
        if not match:continue
        rest=match[4].rsplit(None,3)
        assert len(rest)==4 and rest[2:]==['-','-'],line
        flag_symbol,dso=rest[:2]
        flag,symbol=flag_symbol.split(None,1)
        assert flag in ('[.]','[k]','[?]','[g]','[H]','[u]'),flag
        raw_address=bool(re.fullmatch(r'(?:0x)?[0-9a-fA-F]{8,}',symbol))
        rows.append(dict(percent_rounded=float(match[1]),samples=int(match[2]),period=int(match[3]),
            flag=flag,symbol=symbol,dso=dso,kernel=flag=='[k]',
            unresolved=raw_address or symbol=='[unknown]' or dso=='[unknown]',
            raw_address=raw_address,unknown_dso=dso=='[unknown]'))
    total_samples=sum(r['samples'] for r in rows)
    total_period=sum(r['period'] for r in rows)
    assert rows and total_samples==captured and total_period==event_period,(len(rows),total_samples,captured,total_period,event_period)
    cpu=original['after_load']['cpu_seconds']-original['before_load']['cpu_seconds']
    assert abs(cpu-original['server_cpu_seconds'])<1e-7
    cpu_per_request=cpu*1e6/original['done']
    by_dso=defaultdict(lambda:dict(samples=0,period=0))
    for row in rows:
        by_dso[row['dso']]['samples']+=row['samples']
        by_dso[row['dso']]['period']+=row['period']
        row.update(sample_share_percent=100*row['samples']/total_samples,
            period_share_percent=100*row['period']/total_period,
            estimated_cpu_us_per_completed=cpu_per_request*row['period']/total_period)
    dsos=[]
    for name,count in by_dso.items():
        dsos.append(dict(dso=name,**count,sample_share_percent=100*count['samples']/total_samples,
            period_share_percent=100*count['period']/total_period,
            estimated_cpu_us_per_completed=cpu_per_request*count['period']/total_period))
    dsos.sort(key=lambda row:row['period'],reverse=True)
    categories={}
    for name,predicate in (('kernel',lambda r:r['kernel']),('userspace',lambda r:not r['kernel']),
        ('unresolved_union',lambda r:r['unresolved']),('raw_addresses',lambda r:r['raw_address']),
        ('unknown_dso',lambda r:r['unknown_dso'])):
        samples=sum(r['samples'] for r in rows if predicate(r))
        period=sum(r['period'] for r in rows if predicate(r))
        categories[name]=dict(samples=samples,period=period,sample_share_percent=100*samples/total_samples,
            period_share_percent=100*period/total_period,estimated_cpu_us_per_completed=cpu_per_request*period/total_period)
    workload_text=(root/'h2load.log').read_text()
    counts=re.search(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout',workload_text)
    statuses=re.search(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx',workload_text)
    assert counts and statuses and int(counts[3])==int(counts[4])==original['done']>0
    assert int(statuses[1])>=original['done'] and all(int(counts[i])==0 for i in (5,6,7)) and all(int(statuses[i])==0 for i in (2,3,4))
    selected=[]
    for fragment in ('aes_gcm_enc_update_vaes_avx2','sendto_segmented_batch_now','async_sendto_segmented_batch','sendmmsg','getsockopt','__libc_malloc','__libc_free','__insert_with_size','pack_one_rtt_packet','apply_stream_write','tree_remove','quic_stream_scheduler'):
        matches=[r for r in rows if fragment in r['symbol']]
        selected.append(dict(fragment=fragment,samples=sum(r['samples'] for r in matches),period=sum(r['period'] for r in matches),
            period_share_percent=100*sum(r['period'] for r in matches)/total_period,
            estimated_cpu_us_per_completed=cpu_per_request*sum(r['period'] for r in matches)/total_period,
            symbols=[dict(symbol=r['symbol'],dso=r['dso']) for r in matches]))
    result=dict(mode=mode,valid_original=True,original_summary_sha256=summary_sha,binary_sha256=expected_binary,
        source_snapshot_sha256=original['source_snapshot_sha256'],source_diff_sha256=original['source_diff_sha256'],
        source_input_count=len(metadata['sources']),source_check_scope='Frozen recorded metadata and diff verified; current production is not rehashed while another agent builds.',
        allocator='system',allocator_proof_sha256=original['allocator_proof_sha256'],
        perf_record_exit_code=original['perf_record_exit_code'],perf_stop_requested=original['perf_stop_requested'],
        data_sha256=sha(root/'static-perf.data'),data_bytes=(root/'static-perf.data').stat().st_size,
        captured_samples=captured,total_samples=total_samples,lost_samples=lost,total_period=total_period,
        symbols=len(rows),dso=dsos,categories=categories,top_self=sorted(rows,key=lambda r:r['period'],reverse=True)[:35],
        selected_symbols=selected,workload={key:original[key] for key in ('done','status_2xx','elapsed_seconds','printed_rps','completed_per_elapsed_second','server_cpu_seconds','cpu_microseconds_per_completed')},
        limitations=['Two single perturbed profiles, not an RPS ranking or repeated CPU benchmark.',
            'Sample period weighted CPU attribution per completed request is an estimate (process CPU delta multiplied by sampled share), not directly measured leaf timing.',
            'fp stacks can truncate optimized frames; inclusive call chains must not be summed.',
            'Sampling is not a syscall/packet/allocation count. Small leaf changes have sampling/run variation.',
            'Old 05fe mimalloc profile uses another binary and allocator; do not mix it into this matched system off/on comparison.'])
    assert sha(root/'summary.json')==summary_sha
    (out/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    (out/'self-compact.tsv').write_text('percent\tsamples\tperiod\tdso\tflag\tsymbol\n'+''.join(f"{r['period_share_percent']:.8f}\t{r['samples']}\t{r['period']}\t{r['dso']}\t{r['flag']}\t{r['symbol']}\n" for r in sorted(rows,key=lambda r:r['period'],reverse=True)))
    results[mode]=result
    print(json.dumps({key:result[key] for key in ('mode','total_samples','lost_samples','dso','categories','workload')},indent=2),flush=True)

assert results['off']['binary_sha256']==results['on']['binary_sha256']
assert results['off']['source_snapshot_sha256']==results['on']['source_snapshot_sha256']
comparison=dict(binary_sha256=expected_binary,allocator='system',off=results['off'],on=results['on'],
    completed_rate_change_percent=100*(results['on']['workload']['completed_per_elapsed_second']/results['off']['workload']['completed_per_elapsed_second']-1),
    cpu_per_completed_change_percent=100*(results['on']['workload']['cpu_microseconds_per_completed']/results['off']['workload']['cpu_microseconds_per_completed']-1))
Path('/var/tmp/cnetmod-h3-system-perf-comparison.json').write_text(json.dumps(comparison,indent=2)+'\n')
