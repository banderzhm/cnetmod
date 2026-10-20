"""Offline completed-request analysis of an already completed18-round run."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('summary',type=Path)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args()
summary=json.loads(args.summary.read_text())
assert summary['valid'] and len(summary['rounds'])==18 and summary['cleanup']['returncode']==0
result=dict(summary_path=str(args.summary),summary_sha256=hashlib.sha256(args.summary.read_bytes()).hexdigest(),
    binary_sha256=summary['binary_sha256'],allocator=summary['allocator'],
    receive_payload_limit=summary['receive_payload_limit'],gso_environment=summary['gso_environment'],
    scope='same-binary matched-block native diagnostic; completed/elapsed, not printed RPS; not official ranking or retention',profiles={})
for profile in ('baseline-h3','static-h3'):
    rows=[r for r in summary['rounds'] if r['profile']==profile]
    assert len(rows)==9
    blocks={}
    for block in (1,2,3):
        group=[r for r in rows if r['block']==block]
        assert len(group)==3 and {r['variant'] for r in group}=={1472,4096,8192}
        for r in group:
            assert r['done']==r['succeeded']>0 and r['status_2xx']>=r['done']
            assert r['failed']==r['errored']==r['timeout']==0
        blocks[block]={r['variant']:r for r in group}
    variants={}
    for variant in (1472,4096,8192):
        measured=[blocks[b][variant] for b in blocks]
        variants[str(variant)]=dict(
            median_completed_per_elapsed_second=statistics.median(r['completed_per_elapsed_second'] for r in measured),
            median_cpu_microseconds_per_completed=statistics.median(r['server_cpu_microseconds_per_completed'] for r in measured),
            median_p99_ms=statistics.median(r['p99_ms'] for r in measured),
            rounds=[dict(block=r['block'],done=r['done'],status_2xx=r['status_2xx'],header_completion_delta=r['header_completion_delta'],
                completed_per_elapsed_second=r['completed_per_elapsed_second'],printed_rps=r['printed_rps'],elapsed_seconds=r['elapsed_seconds'],
                cpu_microseconds_per_completed=r['server_cpu_microseconds_per_completed'],p99_ms=r['p99_ms'],
                rss_before_kib=r['before']['memory_kib']['Rss'],rss_after_kib=r['after']['memory_kib']['Rss']) for r in measured])
    comparisons={}
    for variant in (4096,8192):
        pairs=[]
        for block in blocks:
            baseline=blocks[block][1472];candidate=blocks[block][variant]
            pairs.append(dict(block=block,
                completed_per_elapsed_gain_percent=100*(candidate['completed_per_elapsed_second']/baseline['completed_per_elapsed_second']-1),
                cpu_per_completed_change_percent=100*(candidate['server_cpu_microseconds_per_completed']/baseline['server_cpu_microseconds_per_completed']-1),
                p99_change_percent=100*(candidate['p99_ms']/baseline['p99_ms']-1)))
        comparisons[str(variant)]=dict(pairs=pairs,
            median_paired_completed_gain_percent=statistics.median(p['completed_per_elapsed_gain_percent'] for p in pairs),
            median_paired_cpu_change_percent=statistics.median(p['cpu_per_completed_change_percent'] for p in pairs),
            median_paired_p99_change_percent=statistics.median(p['p99_change_percent'] for p in pairs))
    result['profiles'][profile]=dict(variants=variants,comparisons_to1472=comparisons)
args.out.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
