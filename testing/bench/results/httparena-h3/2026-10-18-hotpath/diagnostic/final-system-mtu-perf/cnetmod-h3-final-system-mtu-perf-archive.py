"""Offline archive of completed1472/4096 profiles; no record/build/load."""
from pathlib import Path
import gzip
import hashlib
import json
import re
import shutil

repo=Path('C:/Users/25007/.codex/worktrees/2c3c/cnetmod')
tmp=Path('C:/Users/25007/.codex/tmp')
external=Path('//wsl.localhost/archlinux/var/tmp')
roots={'1472':external/'cnetmod-h3-system-perf-91b5cc8bd99c410585a9f598f920b767',
       '4096':external/'cnetmod-h3-system-perf-7b4e3e7fa14c452091b92573e61e9855'}
comparison=external/'cnetmod-h3-final-system-mtu-perf-comparison.json'
data=json.loads(comparison.read_text())
assert data['zero_lost_samples_both'] and data['binary_sha256']=='5bf46aeeee227934742969972dfa3e4705d5fb87e66289c9109f41d89f353470'
dest=repo/'testing/bench/results/httparena-h3/2026-10-18-hotpath/diagnostic/final-system-mtu-perf'
assert not dest.exists();dest.mkdir(parents=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
shutil.copy2(comparison,dest/'comparison.json')
inputs=[];groups={}
for mode,root in roots.items():
    target=dest/mode;target.mkdir()
    result=data['baseline' if mode=='1472' else 'candidate']
    original=json.loads((root/'summary.json').read_text())
    assert result['lost_samples']==0 and original['namespace_cleanup']['returncode']==0
    for path in sorted(root.iterdir()):
        if path.is_file() and path.suffix in ('.json','.txt','.log','.patch'):
            if path.name in ('static-perf-self.txt','static-perf-stacks.txt'):
                with path.open('rb') as source,gzip.open(target/(path.name+'.gz'),'wb',compresslevel=6) as output:shutil.copyfileobj(source,output)
            else:shutil.copy2(path,target/path.name)
    for path in sorted((root/'offline-analysis-mtu').iterdir()):
        if not path.is_file():continue
        if path.suffix=='.txt':
            with path.open('rb') as source,gzip.open(target/(path.name+'.gz'),'wb',compresslevel=6) as output:shutil.copyfileobj(source,output)
        elif path.suffix in ('.json','.tsv'):shutil.copy2(path,target/path.name)
    inputs.append(dict(ceiling=int(mode),external_directory='/var/tmp/'+root.name,
        binary_sha256=result['binary_sha256'],perf_data_sha256=result['data_sha256'],perf_data_bytes=result['data_bytes'],
        summary_sha256=result['original_summary_sha256'],source_metadata_sha256=result['source_snapshot_sha256'],
        source_diff_sha256=result['source_diff_sha256'],allocator_proof_sha256=result['allocator_proof_sha256'],
        perf_record_exit_code=result['perf_record_exit_code'],namespace_cleanup=original['namespace_cleanup']))
    rows=[]
    for line in (root/'offline-analysis-mtu/self-compact.tsv').read_text().splitlines()[1:]:
        percent,samples,period,dso,flag,symbol=line.split('\t',5)
        rows.append(dict(percent=float(percent),samples=int(samples),period=int(period),dso=dso,flag=flag,symbol=symbol))
    predicates={
        'recovery_named_insert_recycle':lambda r:any(fragment in r['symbol'] for fragment in ('quic_recovery_controller@cnetmod.protocol.quic::insert_sent_packet(', 'quic_recovery_controller@cnetmod.protocol.quic::recycle_sent_packet(', 'loss_detector@cnetmod.protocol.quic::insert_inflight_packet(', 'loss_detector@cnetmod.protocol.quic::recycle_inflight_packet(')),
        'generic_tree_helpers':lambda r:'__tree_remove' in r['symbol'] or '__tree_balance_after_insert' in r['symbol'],
        'named_malloc_cfree_only':lambda r:r['symbol'] in ('malloc','cfree'),
        'byte_vector_insert_family':lambda r:'__insert_with_size' in r['symbol'] and 'vector<std::byte' in r['symbol'],
    }
    totals={}
    for name,predicate in predicates.items():
        found=[r for r in rows if predicate(r)];period=sum(r['period'] for r in found)
        totals[name]=dict(samples=sum(r['samples'] for r in found),period=period,
            percent=100*period/result['total_period'],estimated_cpu_us_per_completed=result['workload']['cpu_microseconds_per_completed']*period/result['total_period'],symbols=found)
    unknown={}
    for r in rows:
        if re.fullmatch(r'(?:0x)?[0-9a-fA-F]{8,}',r['symbol']) or r['symbol']=='[unknown]':unknown[r['dso']]=unknown.get(r['dso'],0)+r['samples']
    groups[mode]=dict(groups=totals,unresolved_samples_by_dso=unknown)
(dest/'external-input-manifest.json').write_text(json.dumps(inputs,indent=2)+'\n')
(dest/'symbol-groups.json').write_text(json.dumps(groups,indent=2)+'\n')
for name in ['cnetmod-h3-final-system-perf.py','cnetmod-h3-final-system-perf-analyze.py','cnetmod-h3-final-system-perf-analyze-prepare.py','cnetmod-h3-final-system-perf-commands.txt',Path(__file__).name]:shutil.copy2(tmp/name,dest/name)
source_info=[];metadata=json.loads((roots['4096']/'supplied-source-metadata.json').read_text(encoding='utf-8-sig'))
for name,lo,hi in [('src/protocol/quic/quic_send_pipeline.cpp',129,141),('src/protocol/quic/quic_connection.cpp',4335,4344),('src/protocol/quic/quic_frame.cpp',25,29),('src/protocol/quic/quic_frame.cpp',720,740)]:
    source=repo/name;assert sha(source)==metadata['sources'][name]
    numbered='\n'.join(f'{index}: {line}' for index,line in enumerate(source.read_text(encoding='utf-8').splitlines(),1) if lo<=index<=hi)+'\n'
    filename=f'{source.stem}-{lo}-{hi}.txt';(dest/filename).write_text(numbered,encoding='utf-8')
    source_info.append(dict(path=name,first_line=lo,last_line=hi,sha256=sha(source),excerpt=filename))
(dest/'source-locations.json').write_text(json.dumps(source_info,indent=2)+'\n')
base=data['baseline'];candidate=data['candidate']
lines=['# Final system-allocator MTU CPU profiles','',
    'Both serial profiles completed successfully, with original summary.valid=true, explicit own SIGINT exit-2 and Docker cleanup0. They use the same frozen ELF `'+data['binary_sha256']+'`, matching frozen source/dirtydiff and verified system allocator. GSO is on, receive capacity8192,32workers/64clients/64streams/64threads, original static URI/encoding, client CPU16–31 and5second workload. Only send discovery ceiling1472/4096 differs. These are two instrumented diagnostics, not a repeated ranking or standalone performance acceptance.',
    '', '| Measure | 1472 | 4096 |','|---|---:|---:|']
for name,getter,fmt in [
    ('Completed requests',lambda r:r['workload']['done'],',.0f'),
    ('2xx headers',lambda r:r['workload']['status_2xx'],',.0f'),
    ('Completed / elapsed seconds',lambda r:r['workload']['completed_per_elapsed_second'],',.1f'),
    ('h2load printed RPS',lambda r:r['workload']['printed_rps'],',.1f'),
    ('CPU microseconds / completed',lambda r:r['workload']['cpu_microseconds_per_completed'],'.3f'),
    ('Kernel sample share %',lambda r:r['categories']['kernel']['period_share_percent'],'.3f'),
    ('Estimated kernel microseconds / completed',lambda r:r['categories']['kernel']['estimated_cpu_us_per_completed'],'.3f'),
    ('Estimated user microseconds / completed',lambda r:r['categories']['userspace']['estimated_cpu_us_per_completed'],'.3f'),
    ('Samples',lambda r:r['total_samples'],',.0f'),
    ('Lost samples',lambda r:r['lost_samples'],',.0f'),
    ('Known-DSO unresolved raw addresses %',lambda r:r['categories']['raw_addresses']['period_share_percent'],'.3f'),
    ('Unknown DSO samples',lambda r:r['categories']['unknown_dso']['samples'],',.0f')]:lines.append(f'| {name} | {format(getter(base),fmt)} | {format(getter(candidate),fmt)} |')
lines+=['',f"CPU/completed changes by{data['cpu_per_completed_change_percent']:+.2f}%. Kernel and user both decrease in the sample-based absolute attribution, although kernel relative share rises. CPU delta multiplied by sampled period share is an estimate, not a measured leaf timer. Samples sum exactly to captured counts and total periods; zero lost samples does not eliminate sampling variance, optimized-frame truncation or symbol limitations.",
    '', '## Full DSO distribution','', '| DSO | 1472 share % | 4096 share % | 1472 estimated us/completed | 4096 estimated us/completed |','|---|---:|---:|---:|---:|']
dsos=sorted({r['dso'] for result in (base,candidate) for r in result['dso']})
for dso in dsos:
    a=next(r for r in base['dso'] if r['dso']==dso);b=next(r for r in candidate['dso'] if r['dso']==dso)
    lines.append(f"| {dso} | {a['period_share_percent']:.3f} | {b['period_share_percent']:.3f} | {a['estimated_cpu_us_per_completed']:.3f} | {b['estimated_cpu_us_per_completed']:.3f} |")
lines+=['', '## Selected self families','', '| Symbol/family | 1472 self % | 4096 self % | 1472 estimated us/completed | 4096 estimated us/completed |','|---|---:|---:|---:|---:|']
for fragment in ['aes_gcm_enc_update_vaes_avx2','pack_one_rtt_packet','__insert_with_size','quic_stream_scheduler','apply_stream_write','on_ack_received','tree_remove','getsockopt','aead_context_for']:
    a=next(r for r in base['selected_symbols'] if r['fragment']==fragment);b=next(r for r in candidate['selected_symbols'] if r['fragment']==fragment)
    lines.append(f"| {fragment} | {a['period_share_percent']:.3f} | {b['period_share_percent']:.3f} | {a['estimated_cpu_us_per_completed']:.3f} | {b['estimated_cpu_us_per_completed']:.3f} |")
lines+=['', 'AES relative share increases6.625%→8.362%, while its CPU/completed estimate is approximately2.257→2.238microseconds. This is not evidence encryption slowed down. Named aead-context lookup is only about0.095→0.062microseconds in self samples; this does not capture all inlined refcount/locking cost or prove it is zero. Named malloc/cfree totals and generic tree helpers are recorded separately in symbol-groups.json; unresolved libc addresses cannot all be assigned to allocation, and generic tree costs cannot all be assigned to recovery.',
    '', '## Byte-vector insertion: measured family, unimplemented candidate','',
    'The combined byte-vector `__insert_with_size` family rises from3.36%/about1.145microseconds per completion at1472 to6.77%/about1.812microseconds at4096. This is an absolute estimate increase as well as a relative share increase. The profile does **not** establish that one callsite owns the family, that libc++ implements a pathological loop, or that replacing it will improve throughput. Packet size/call mix, inlining/debug attribution and sampling/run variance remain relevant. Caller stacks contain insertion under packet assembly and STREAM encoding; optimized inline labels are not a reliable unique source-PC attribution.',
    '', 'Exact current source candidates are `src/protocol/quic/quic_send_pipeline.cpp:129` (`quic_send_frame::append_to`), owned-byte insertion at133, shared span/body insertion at140, and its packet-builder call at `src/protocol/quic/quic_connection.cpp:4340`. STREAM encoding also uses `src/protocol/quic/quic_frame.cpp:720` and shared `append_bytes` insertion at28. Numbered excerpts and per-source SHA256 are retained in source-locations.json and verified against the same frozen metadata. The ELF SHA above binds these observations; no different binary is substituted.',
    '', 'A future minimal experiment could compare a checked contiguous data()/pointer range with the current iterator range, keeping bytes, reserve policy, empty-input handling, errors, ownership, cancellation, packetization and scheduling identical. It first needs actual ELF/callsite examination and an independent measured control. Do not add an untested pointer-range patch to the current repair, replace append with resize+memcpy without accounting for zero-fill, or infer an allocation count from self samples. No such candidate was implemented, compiled or benchmarked here. The more invasive visitor/range-merge/single-ledger ideas remain deferred under their separate lifetime review.',
    '', '## Evidence limits','',
    'The earlier2cb off/on system profiles and older mimalloc profiles use other binaries/configurations and are not part of this same-binary comparison. Neither sampled shares nor these two profiles provide syscall/packet/allocation counts or a complete explanation of the agensio gap. Configured ceiling is not every actual packet size; the independent low-concurrency qlog preflight proves application sizes are reachable but does not measure the full timed/profile distribution. Current unresolved raw-address samples account for about9.43%/11.00%, despite all samples having known DSOs. Inclusive stacks must not be summed as disjoint costs.',
    '', 'Both raw perf.data and frozen ELF stay in the unique external directories in external-input-manifest.json. This archive contains original summaries, metadata/dirtydiff/proof, workload/server/perf logs, headers, compressed self/caller reports, all-row compact self counts, analysis, source excerpts and scripts. No perf.data, ELF, PCM or keys are copied. Prepared command/runner files are archive-time copies; runtime metadata and data/summary hashes bind the recorded profile.']
(dest/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
manifest=[]
for path in sorted(dest.rglob('*')):
    if path.is_file():
        assert path.suffix in ('.json','.txt','.log','.patch','.py','.gz','.tsv','.md')
        manifest.append(dict(path=path.relative_to(dest).as_posix(),bytes=path.stat().st_size,sha256=sha(path)))
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(dict(archive=str(dest),files=len(manifest),bytes=sum(r['bytes'] for r in manifest),manifest_sha256=sha(dest/'manifest.json')),indent=2))
