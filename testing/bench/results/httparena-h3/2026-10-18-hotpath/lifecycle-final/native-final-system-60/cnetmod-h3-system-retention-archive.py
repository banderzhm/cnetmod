"""Offline text-only archive of the completed system60round retention run."""
from pathlib import Path
import csv
import hashlib
import json
import re
import shutil
import statistics

repo=Path('C:/Users/25007/.codex/worktrees/2c3c/cnetmod')
tmp=Path('C:/Users/25007/.codex/tmp')
run=Path('//wsl.localhost/archlinux/var/tmp/cnetmod-h3-system-retention-6d27bba41cc64c7e8983ad48f9e6968f')
external='/var/tmp/cnetmod-h3-system-retention-6d27bba41cc64c7e8983ad48f9e6968f'
dest=repo/'testing/bench/results/httparena-h3/2026-10-18-hotpath/lifecycle-final/native-final-system-60'
assert not dest.exists()
summary=json.loads((run/'summary.json').read_text())
assert summary['valid'] and len(summary['rounds'])==60
assert summary['binary_sha256']=='5bf46aeeee227934742969972dfa3e4705d5fb87e66289c9109f41d89f353470'
assert summary['settings']==dict(gso='1',send_ceiling=4096,receive_capacity=8192)
assert summary['allocator']=='system'
dest.mkdir(parents=True)
for path in sorted(run.iterdir()):
    if path.is_file() and path.suffix in ('.json','.log'):
        shutil.copy2(path,dest/path.name)
for path in [tmp/'cnetmod-h3-system-retention.py',Path(__file__)]:shutil.copy2(path,dest/path.name)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(dest/'allocator-proof.json')==summary['allocator_proof_sha256']
assert sha(dest/'source-metadata.json')==summary['source_metadata_sha256']
metadata=json.loads((dest/'source-metadata.json').read_text())
source_diff=tmp/'cnetmod-h3-final-system-validation/source-snapshot/source.diff'
assert sha(source_diff)==metadata['diff_sha256']
shutil.copy2(source_diff,dest/'source.diff')
validation=[]
for index in range(1,61):
    text=(dest/f'round-{index}-h2load.log').read_text()
    counts=re.search(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout',text)
    statuses=re.search(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx',text)
    assert counts and statuses
    numbers=list(map(int,counts.groups()));status=list(map(int,statuses.groups()))
    assert numbers[:4]==[200000]*4 and numbers[4:]==[0]*3
    assert status==[200000,0,0,0]
    validation.append(dict(round=index,total=numbers[0],started=numbers[1],done=numbers[2],succeeded=numbers[3],failed=0,errored=0,timeout=0,status_2xx=status[0],status_non2xx=0))
assert sum(r['done'] for r in validation)==12000000
(dest/'round-validation.json').write_text(json.dumps(validation,indent=2)+'\n')
samples=summary['samples'];assert len(samples)==62
rounds={int(re.fullmatch(r'round-(\d+)-idle-2s',s['name'])[1]):s for s in samples if s['name'].startswith('round-')}
assert set(rounds)==set(range(1,61))
late=[dict(round=i,**rounds[i]) for i in range(30,61)]
xs=[r['round'] for r in late];ys=[r['rss'] for r in late]
fit=statistics.linear_regression(xs,ys)
changes=[ys[i]-ys[i-1] for i in range(1,len(ys))]
minimum=min(late,key=lambda s:s['rss']);maximum=max(late,key=lambda s:s['rss'])
final=next(s for s in samples if s['name']=='final-idle-37s')
assert final['rss']==138039296 and final['private']==135520256
stats=dict(scope='descriptive finite-run RSS/private samples, not live-owned allocation or unlimited boundedness proof',
    successful_rounds=60,completed_requests=12000000,round30=rounds[30],round40=rounds[40],round50=rounds[50],round60=rounds[60],final_idle=final,
    round30_to60=dict(sample_count=len(late),rss_min_bytes=minimum['rss'],rss_min_round=minimum['round'],rss_max_bytes=maximum['rss'],rss_max_round=maximum['round'],
        private_min_bytes=min(s['private'] for s in late),private_max_bytes=max(s['private'] for s in late),
        rss_median_bytes=statistics.median(ys),rss_end_minus_start_bytes=ys[-1]-ys[0],
        rss_end_change_percent=100*(ys[-1]/ys[0]-1),rss_adjacent_increases=sum(x>0 for x in changes),rss_adjacent_decreases=sum(x<0 for x in changes),
        descriptive_ols_rss_bytes_per_round=fit.slope,ols_not_a_forecast=True),
    final_minus_round30_rss_bytes=final['rss']-rounds[30]['rss'],
    final_minus_round60_rss_bytes=final['rss']-rounds[60]['rss'],
    final_sample_gap_from_round60_seconds=final['time']-rounds[60]['time'],
    timing_note='The final-idle-37s label denotes2s round cooldown plus35s explicit sleep. Final source/binary verification precedes sampling and adds time; recorded sample gap is retained, not rounded to exactly35s.')
(dest/'memory-analysis.json').write_text(json.dumps(stats,indent=2)+'\n')
with (dest/'memory-samples.csv').open('w',newline='') as file:
    writer=csv.DictWriter(file,fieldnames=['name','time','rss','private']);writer.writeheader();writer.writerows(samples)
note=dict(provenance='Coordinator /root direct message; no standalone raw console file exists',
    reported_tool_session=45922,reported_final_tool='write_stdin',reported_exit_code=0,
    reported_final_stdout=dict(name='final-idle-37s',time=1792332886.3463662,rss=138039296,private=135520256,results_directory=external),
    runner_argument_values=dict(expected_sha=summary['binary_sha256'],path_mtu=4096,gso='1',source_snapshot='/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-final-system-validation/source-snapshot',allocator_proof='/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-final-system-validation/system-allocator/allocator-proof.json'),
    cleanup_inference='Runner executes server.terminate/server.wait and docker rm check=True before printing RESULTS. Reported final exit0 therefore establishes that this cleanup path completed successfully.',
    limitations='This is a coordinator execution note, not fabricated raw tool stdout. Summary.valid is set before finally; it alone does not record cleanup. No additional current-process snapshot was supplied.',
    archived_runner_sha256=sha(dest/'cnetmod-h3-system-retention.py'),runner_hash_scope='archive-time copy; runner did not record its own runtime hash')
(dest/'coordinator-execution-note.json').write_text(json.dumps(note,indent=2)+'\n')
lines=['# Final system-allocator60round static retention','',
    'The frozen5bf46a... candidate completes all60 serial200,000-request static rounds:12,000,000 done/succeeded/2xx and zero failed, errored, timed-out or non2xx requests. Independent offline parsing rechecks every original h2load log. This is correctness/retention evidence, not a throughput ranking.',
    '', 'Settings: system allocator (`CNETMOD_USE_MIMALLOC=OFF` proof), GSO1, ACK-validated send discovery ceiling4096, fixed receive capacity8192,32serverworkers and64clients/streams/threads, client CPU16–31, isolated Docker-anchor loopback with the existing gai wrapper. Production defaults are not changed by this run. Source metadata/dirtydiff and allocator proof are retained and match their recorded hashes.',
    '', '| Sample | RSS bytes | Private bytes |','|---|---:|---:|']
for name in ['initial','round-1-idle-2s','round-30-idle-2s','round-40-idle-2s','round-50-idle-2s','round-60-idle-2s','final-idle-37s']:
    s=next(s for s in samples if s['name']==name);lines.append(f"| {name} | {s['rss']:,} | {s['private']:,} |")
lines+=['',f"Rounds30–60 (31samples) have RSS minimum{minimum['rss']:,}bytes at round{minimum['round']} and maximum{maximum['rss']:,}bytes at round{maximum['round']}. RSS changes by{ys[-1]-ys[0]:,}bytes ({100*(ys[-1]/ys[0]-1):.2f}%) from round30 to round60, with{sum(x>0 for x in changes)} adjacent increases and{sum(x<0 for x in changes)} decreases. Later endpoints are higher: round40→50 rises{rounds[50]['rss']-rounds[40]['rss']:,}bytes and round50→60 rises{rounds[60]['rss']-rounds[50]['rss']:,}bytes. The finite trajectory fluctuates and retains an upward tendency; it must not be described as proven saturation or infinite long-run boundedness.",
    '',f"The final sample labeled final-idle-37s is RSS{final['rss']:,}/private{final['private']:,}bytes, down{rounds[60]['rss']-final['rss']:,}RSS bytes from the round60 sample but still above round30. The configured2s cooldown+35s final sleep is followed by source/binary hashing before sampling; recorded sample timestamps are{final['time']-rounds[60]['time']:.3f}seconds apart. No forced allocator trim, inbound keepalive or in-process observer is added in that final interval.",
    '', 'RSS/private pages alone do not identify live ownership, distinguish allocator page retention from a leak, or show that memory returns to its startup level. Historical mimalloc ownership/bin results belong to another allocator/snapshot and are not interchangeable with this system-allocator series. A60round observation provides evidence over this workload/window only.',
    '', 'The coordinator reports tool session45922 ended with exit0 and RESULTS after the final sample. The runner\'s finally path waits for its server and removes its own Docker anchor before RESULTS; this establishes that cleanup path completed. No separate raw console file or additional process inventory exists. `coordinator-execution-note.json` records that provenance rather than inventing raw console output or summary cleanup fields.',
    '',f'External raw directory: `{external}`. All60h2load logs, zero-byte server log, summary, source metadata/dirtydiff, allocator proof, runner and offline archive/analyzer are copied. The39MB ELF, any keys and unrelated payloads are excluded. Full62point samples and descriptive statistics are in memory-samples.csv and memory-analysis.json. The OLS slope is descriptive and must not be extrapolated as a future leak rate.']
(dest/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
# Restrict top-level README edits to the supplied pending-retention statements,
# the completed MTU archive marker and a single new factual retention paragraph.
readme=repo/'testing/bench/results/httparena-h3/2026-10-18-hotpath/README.md'
before=readme.read_bytes();text=before.decode('utf-8');eol='\r\n' if '\r\n' in text else '\n'
text=text.replace(' (archive being finalized)','')
text=text.replace('Final performance and system-allocator retention results for this snapshot are still pending.','Final official framework comparison results for this snapshot are still pending; completed system-allocator retention is reported below.')
text=text.replace('Final retention and official framework comparison are still pending.','The completed system-allocator retention is reported below; final official framework comparison remains pending.')
text=text.replace('final system-allocator retention and framework comparisons remain pending.','completed final system-allocator retention is reported above; framework comparisons remain pending.')
paragraph=(f'The final system-allocator retention run for `5bf46aeeee227934742969972dfa3e4705d5fb87e66289c9109f41d89f353470`, with GSO enabled, send ceiling4096 and receive capacity8192, passes all60 serial200,000-request static rounds:12,000,000 completed successes and zero errors. Round30 RSS is128,802,816bytes; rounds30–60 range from{minimum["rss"]:,} to{maximum["rss"]:,}bytes and end at138,674,176bytes, a7.66% increase from round30. The final sample labeled37seconds idle is RSS138,039,296/private135,520,256bytes; source verification adds time beyond the configured idle interval. The later samples fluctuate but retain an upward tendency, so this finite run does not prove indefinite boundedness or identify live allocation ownership. The coordinator reports exit0 after server/Docker cleanup; no separate raw console or extra process inventory was recorded. See `lifecycle-final/native-final-system-60/`.')
anchor='The system-allocator GSO candidate `2cb22a'
assert text.count(anchor)==1 and 'The final system-allocator retention run for' not in text
position=text.index(anchor);text=text[:position]+paragraph+eol+eol+text[position:]
readme.write_bytes(text.encode('utf-8'))
(dest/'top-readme-edit.json').write_text(json.dumps(dict(before_sha256=hashlib.sha256(before).hexdigest(),after_sha256=sha(readme),scope='completed MTU archive marker and current pending-retention facts only'),indent=2)+'\n')
manifest=[]
for path in sorted(dest.rglob('*')):
    if path.is_file():
        assert path.suffix in ('.json','.log','.py','.diff','.md','.csv')
        manifest.append(dict(path=path.relative_to(dest).as_posix(),bytes=path.stat().st_size,sha256=sha(path)))
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(dict(archive=str(dest),files=len(manifest),bytes=sum(v['bytes'] for v in manifest),manifest_sha256=sha(dest/'manifest.json'),readme_sha256=sha(readme),statistics=stats),indent=2))
