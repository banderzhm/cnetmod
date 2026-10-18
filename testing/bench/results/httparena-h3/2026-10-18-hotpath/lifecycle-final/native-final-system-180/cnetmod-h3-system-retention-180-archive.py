"""Offline text-only archive. Run only after coordinator confirms180run exit0."""
from pathlib import Path
import csv
import hashlib
import json
import re
import shutil
import statistics

repo=Path('C:/Users/25007/.codex/worktrees/2c3c/cnetmod')
tmp=Path('C:/Users/25007/.codex/tmp')
external='/var/tmp/cnetmod-h3-system-retention-03b6c34bf7154d55ba0c935e0ba9a5d8'
run=Path('//wsl.localhost/archlinux'+external)
dest=repo/'testing/bench/results/httparena-h3/2026-10-18-hotpath/lifecycle-final/native-final-system-180'
assert not dest.exists()
summary=json.loads((run/'summary.json').read_text())
assert summary['valid'] and summary['planned_rounds']==180 and len(summary['rounds'])==180
assert summary['binary_sha256']=='5bf46aeeee227934742969972dfa3e4705d5fb87e66289c9109f41d89f353470'
assert summary['settings']==dict(gso='1',send_ceiling=4096,receive_capacity=8192) and summary['allocator']=='system'
console=tmp/'cnetmod-h3-system-retention-180-console.log'
raw_console=console.read_bytes()
console_text=raw_console.decode('utf-16' if raw_console.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig')
assert 'RESULTS '+external in console_text and 'Traceback (most recent call last)' not in console_text
dest.mkdir(parents=True)
for source in sorted(run.iterdir()):
    if source.is_file() and source.suffix in ('.json','.log'):shutil.copy2(source,dest/source.name)
for source in [tmp/'cnetmod-h3-system-retention-extended.py',console,Path(__file__)]:shutil.copy2(source,dest/source.name)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(dest/'source-metadata.json')==summary['source_metadata_sha256']
assert sha(dest/'allocator-proof.json')==summary['allocator_proof_sha256']
metadata=json.loads((dest/'source-metadata.json').read_text())
diff=tmp/'cnetmod-h3-final-system-validation/source-snapshot/source.diff'
assert sha(diff)==metadata['diff_sha256'];shutil.copy2(diff,dest/'source.diff')
validation=[]
for index in range(1,181):
    text=(dest/f'round-{index}-h2load.log').read_text()
    counts=re.search(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout',text)
    status=re.search(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx',text)
    assert counts and status
    counts=list(map(int,counts.groups()));status=list(map(int,status.groups()))
    assert counts==[200000,200000,200000,200000,0,0,0] and status==[200000,0,0,0]
    validation.append(dict(round=index,total=200000,started=200000,done=200000,succeeded=200000,failed=0,errored=0,timeout=0,status_2xx=200000,status_non2xx=0))
assert sum(r['done'] for r in validation)==36000000
(dest/'round-validation.json').write_text(json.dumps(validation,indent=2)+'\n')
samples=summary['samples'];assert len(samples)==182
rounds={int(re.fullmatch(r'round-(\d+)-idle-2s',r['name'])[1]):r for r in samples if r['name'].startswith('round-')}
assert set(rounds)==set(range(1,181))
assert rounds[120]['rss']==147673088 and rounds[150]['rss']==146640896 and rounds[180]['rss']==150642688
final=next(r for r in samples if r['name']=='final-idle-37s')
assert final['rss']==150642688 and final['private']==148131840
console_final=[json.loads(line) for line in console_text.splitlines() if line.startswith('{') and 'final-idle-37s' in line]
assert console_final==[final]
late=[dict(round=i,**rounds[i]) for i in range(120,181)]
minimum=min(late,key=lambda r:r['rss']);maximum=max(late,key=lambda r:r['rss'])
changes=[late[i]['rss']-late[i-1]['rss'] for i in range(1,len(late))]
fit=statistics.linear_regression([r['round'] for r in late],[r['rss'] for r in late])
stats=dict(scope='finite180round RSS/private observation, not proof of unlimited boundedness or absence of all leaks',
    completed_requests=36000000,successful_rounds=180,round120=rounds[120],round150=rounds[150],round180=rounds[180],final_idle=final,
    round120_to180=dict(sample_count=len(late),rss_min_bytes=minimum['rss'],rss_min_round=minimum['round'],rss_max_bytes=maximum['rss'],rss_max_round=maximum['round'],
        private_min_bytes=min(r['private'] for r in late),private_max_bytes=max(r['private'] for r in late),rss_median_bytes=statistics.median(r['rss'] for r in late),
        rss_end_minus_start_bytes=rounds[180]['rss']-rounds[120]['rss'],rss_end_change_percent=100*(rounds[180]['rss']/rounds[120]['rss']-1),
        rss_adjacent_increases=sum(d>0 for d in changes),rss_adjacent_decreases=sum(d<0 for d in changes),rss_adjacent_unchanged=sum(d==0 for d in changes),
        descriptive_ols_rss_bytes_per_round=fit.slope,ols_not_a_forecast=True),
    round120_to150_rss_change_bytes=rounds[150]['rss']-rounds[120]['rss'],round150_to180_rss_change_bytes=rounds[180]['rss']-rounds[150]['rss'],
    final_minus_round180_rss_bytes=final['rss']-rounds[180]['rss'],final_sample_gap_from_round180_seconds=final['time']-rounds[180]['time'])
(dest/'memory-analysis.json').write_text(json.dumps(stats,indent=2)+'\n')
with (dest/'memory-samples.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=['name','time','rss','private']);writer.writeheader();writer.writerows(samples)
note=dict(provenance='Coordinator /root confirmation of final exec tool session40035 exit0, plus original full console log',
    reported_exit_code=0,tool_session=40035,console_file=console.name,console_sha256=sha(console),
    results_directory=external,console_results_line_verified=True,console_final_sample_matches_summary=True,
    cleanup='Extended runner waits for its server and docker rm check=True before printing RESULTS. Reported exit0 and captured RESULTS establish that this cleanup path completed; no additional process inventory is inferred.',
    archived_runner_sha256=sha(dest/'cnetmod-h3-system-retention-extended.py'),runner_hash_scope='archive-time copy; runner did not record runtime self-hash')
(dest/'execution-evidence.json').write_text(json.dumps(note,indent=2)+'\n')
mapping=dict(provenance='Coordinator /root reports post-commit independent verification of all2209 recorded source hashes with zero differences',
    source_commit='36568ef8cb14a1ddba6680a53d5fc67c5692140e',original_run_head=metadata['head'],
    original_run_diff_sha256=metadata['diff_sha256'],binary_sha256=summary['binary_sha256'],source_input_count=len(metadata['sources']),
    historical_metadata_preserved=True)
(dest/'source-commit-mapping.json').write_text(json.dumps(mapping,indent=2)+'\n')
lines=['# Extended final system-allocator180round retention','',
    'All180 original fixed-count h2load logs independently revalidate200,000 total/started/done/succeeded/2xx each and zero failed/errored/timeout/non2xx:36,000,000 successful requests. The full original console contains final-idle-37s and RESULTS, matching summary data. The coordinator confirms tool session40035 exit0; server wait and checked Docker cleanup precede RESULTS. This is a finite correctness/memory observation, not a throughput ranking or proof of unlimited leak freedom.',
    '', 'Settings remain frozen ELF5bf46a... with system allocator, GSO1, send discovery ceiling4096, receive capacity8192,32serverworkers,64clients/streams/threads, client CPU16–31 and the existing isolated loopback/gai setup. No in-process observer, keepalive or forced allocator trim is introduced in the final idle interval.',
    '', '| Sample | RSS bytes | Private bytes |','|---|---:|---:|']
for name in ['initial','round-1-idle-2s','round-30-idle-2s','round-60-idle-2s','round-90-idle-2s','round-120-idle-2s','round-150-idle-2s','round-180-idle-2s','final-idle-37s']:
    sample=next(s for s in samples if s['name']==name);lines.append(f"| {name} | {sample['rss']:,} | {sample['private']:,} |")
lines+=['',f"The61samples at rounds120–180 range from{minimum['rss']:,}RSS bytes (round{minimum['round']}) to{maximum['rss']:,} (round{maximum['round']}). Round120→150 changes{stats['round120_to150_rss_change_bytes']:+,}bytes; round150→180 changes{stats['round150_to180_rss_change_bytes']:+,}bytes. The overall endpoint change is{stats['round120_to180']['rss_end_minus_start_bytes']:+,}bytes ({stats['round120_to180']['rss_end_change_percent']:+.2f}%). There are{stats['round120_to180']['rss_adjacent_increases']} adjacent increases,{stats['round120_to180']['rss_adjacent_decreases']} decreases and{stats['round120_to180']['rss_adjacent_unchanged']} unchanged pairs. This late finite window shows a much narrower fluctuating footprint than startup, but its bounds are observations, not a stationary-process or indefinite-boundedness theorem.",
    '',f"The final labeled37s-idle sample is RSS{final['rss']:,}/private{final['private']:,}bytes, unchanged from round180. Recorded round180→final sample time is{stats['final_sample_gap_from_round180_seconds']:.4f}s: the label reflects2s round cooldown plus35s sleep, while final source/binary verification adds time before sampling. RSS/private alone cannot identify live allocations, prove absence of all leaks or guarantee return to startup memory.",
    '', 'The earlier60round run is a separate fresh-process observation and is not concatenated into this180round series. No larger lifetime conclusion is derived from the two finite runs. Existing ownership tests and historical allocator-bin diagnosis remain separate evidence under their own allocator/snapshot identities.',
    '', 'Source commit36568ef8cb14a1ddba6680a53d5fc67c5692140e was created after these runs. The coordinator independently rechecked all2209 recorded source hashes with zero differences and the same5bf ELF. Original d0d9+dirtydiff metadata is preserved; source-commit-mapping.json records the later mapping without rewriting history.',
    '',f'Raw external directory: `{external}`. This archive contains180h2load logs, original full console, zero-byte server log, summary, source metadata/dirtydiff, allocator proof, extended runner, offline validator and all182memory samples. ELF/PCM/keys/perfdata are excluded. Descriptive OLS statistics are not forecasts. Final CPU diagnostics are under diagnostic/final-system-mtu-perf; the official comparison remains below agensio, and memory validation does not change that result.']
(dest/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
readme=repo/'testing/bench/results/httparena-h3/2026-10-18-hotpath/README.md'
before=readme.read_bytes();text=before.decode('utf-8');eol='\r\n' if '\r\n' in text else '\n'
new_retention=(f'The subsequent extended system-allocator run uses the same5bf candidate and4096/GSO-on/receive8192 settings and passes180 serial200,000-request static rounds:36,000,000 completed successes with zero errors. RSS at rounds120/150/180 is147,673,088/146,640,896/150,642,688bytes; rounds120–180 range from{minimum["rss"]:,} to{maximum["rss"]:,}bytes. The final sample labeled37seconds idle is unchanged at RSS150,642,688/private148,131,840bytes (recorded round180→final gap{stats["final_sample_gap_from_round180_seconds"]:.4f}s, including source verification). This is a finite late-window fluctuation, not proof of infinite boundedness or absence of every leak. Full console, all180logs and independent exact-count validation are in `lifecycle-final/native-final-system-180/`.')
new_perf=('The final-source system-allocator CPU profiles compare1472 and4096 with GSO enabled and receive8192 on the same5bf ELF. Both complete successfully with zero lost samples (9344/9471). CPU per completed static request is34.064→26.768microseconds; multiplying by sampled period share estimates kernel11.637→9.369 and user22.428→17.399microseconds. These are sampling estimates, not exact function timers or rankings;9.43%/11.00% of addresses remain unresolved within known DSOs. AES-GCM remains about2.257→2.238microseconds despite its larger relative share. Byte-vector insertion is a measured family requiring further callsite/ELF validation; no untested pointer-range optimization is included. See `diagnostic/final-system-mtu-perf/`. These diagnostics do not erase the official agensio gap reported above.')
anchor='The system-allocator GSO candidate `2cb22a'
assert text.count(anchor)==1 and 'The subsequent extended system-allocator run uses' not in text
index=text.index(anchor);text=text[:index]+new_retention+eol+eol+new_perf+eol+eol+text[index:]
commit_sentence='The verified production source is now commit `36568ef8cb14a1ddba6680a53d5fc67c5692140e`: the coordinator rechecked all2209 recorded source hashes with zero differences and Linux ELF remains5bf46a; historical d0d9+dirtydiff run metadata is preserved.'
branch='Branch: `fix/http3-arena-performance`. These records include diagnostic and failed runs; they are not a new official ranking.'
assert text.count(branch)==1 and commit_sentence not in text
text=text.replace(branch,branch+eol+eol+commit_sentence,1)
readme.write_bytes(text.encode('utf-8'))
(dest/'top-readme-edit.json').write_text(json.dumps(dict(before_sha256=hashlib.sha256(before).hexdigest(),after_sha256=sha(readme),scope='only two new180retention/finalperf paragraphs and explicitly authorized sourcecommit mapping sentence'),indent=2)+'\n')
manifest=[]
for path in sorted(dest.rglob('*')):
    if path.is_file():
        assert path.suffix in ('.json','.log','.py','.diff','.md','.csv')
        manifest.append(dict(path=path.relative_to(dest).as_posix(),bytes=path.stat().st_size,sha256=sha(path)))
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(dict(archive=str(dest),files=len(manifest),bytes=sum(r['bytes'] for r in manifest),manifest_sha256=sha(dest/'manifest.json'),readme_sha256=sha(readme),statistics=stats),indent=2))
