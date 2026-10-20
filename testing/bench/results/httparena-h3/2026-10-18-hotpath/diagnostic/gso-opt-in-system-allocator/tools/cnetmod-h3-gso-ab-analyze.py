"""Offline completed-run analysis only; refuses partial or invalid A/B."""
from pathlib import Path
import hashlib
import json
import statistics
import sys

root=Path(sys.argv[1])
source=root/'summary.json'
data=json.loads(source.read_text())
assert data['valid'] and len(data['rounds'])==12 and data['allocator']=='system'
assert data['cleanup']['returncode']==0
analysis=dict(scope='Same-binary native short A/B, system allocator; not official ranking or long retention.',
    summary_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),binary_sha256=data['binary_sha256'],profiles={})
for profile in ('baseline-h3','static-h3'):
    pairs=[]
    for number in (1,2,3):
        rows={r['variant']:r for r in data['rounds'] if r['profile']==profile and r['pair']==number}
        assert set(rows)=={'on','off'}
        off,on=rows['off'],rows['on']
        for row in rows.values():
            assert row['done']==row['succeeded']>0 and row['status_2xx']>=row['done']
            assert row['failed']==row['errored']==row['timeout']==0
        pairs.append(dict(pair=number,off_completed_rps=off['completed_per_elapsed_second'],on_completed_rps=on['completed_per_elapsed_second'],
            completed_rate_change_percent=100*(on['completed_per_elapsed_second']/off['completed_per_elapsed_second']-1),
            off_cpu_us_per_completed=off['server_cpu_microseconds_per_completed'],on_cpu_us_per_completed=on['server_cpu_microseconds_per_completed'],
            cpu_per_completed_change_percent=100*(on['server_cpu_microseconds_per_completed']/off['server_cpu_microseconds_per_completed']-1),
            off_p99_ms=off['p99_ms'],on_p99_ms=on['p99_ms'],
            off_live_rss_kib=off['after']['memory_kib']['Rss'],on_live_rss_kib=on['after']['memory_kib']['Rss'],
            off_printed_rps=off['printed_rps'],on_printed_rps=on['printed_rps'],
            off_header_delta=off['header_completion_delta'],on_header_delta=on['header_completion_delta']))
    analysis['profiles'][profile]=dict(pairs=pairs,
        median_paired_completed_rate_change_percent=statistics.median(p['completed_rate_change_percent'] for p in pairs),
        median_paired_cpu_per_completed_change_percent=statistics.median(p['cpu_per_completed_change_percent'] for p in pairs),
        off_median_completed_rps=statistics.median(p['off_completed_rps'] for p in pairs),
        on_median_completed_rps=statistics.median(p['on_completed_rps'] for p in pairs))
(root/'analysis.json').write_text(json.dumps(analysis,indent=2)+'\n')
print(json.dumps(analysis,indent=2))
