"""Offline symbol groups, without rerunning perf or touching source."""
from pathlib import Path
from collections import defaultdict
import json
import re

data=json.loads(Path('/var/tmp/cnetmod-h3-system-perf-comparison.json').read_text())
groups={
    'recovery_named_only':('quic_recovery_controller@cnetmod.protocol.quic::insert_sent_packet(',
        'quic_recovery_controller@cnetmod.protocol.quic::recycle_sent_packet(',
        'loss_detector@cnetmod.protocol.quic::insert_inflight_packet(',
        'loss_detector@cnetmod.protocol.quic::recycle_inflight_packet('),
    'generic_tree_helpers':('__tree_remove','__tree_balance_after_insert'),
    'named_malloc_free':('malloc','cfree'),
}
result={}
for mode,root in (
    ('off',Path('/var/tmp/cnetmod-h3-system-perf-75d3473f92744a87ad2bf125387ea28e')),
    ('on',Path('/var/tmp/cnetmod-h3-system-perf-7fff9ece9adc48bfb0e81a0f5c9dc4fe'))):
    rows=[]
    for line in (root/'offline-analysis/self-compact.tsv').read_text().splitlines()[1:]:
        percent,samples,period,dso,flag,symbol=line.split('\t',5)
        rows.append(dict(percent=float(percent),samples=int(samples),period=int(period),dso=dso,flag=flag,symbol=symbol))
    total=sum(r['samples'] for r in rows)
    cpu=data[mode]['workload']['cpu_microseconds_per_completed']
    unknown=defaultdict(int)
    for row in rows:
        if re.fullmatch(r'(?:0x)?[0-9a-fA-F]{8,}',row['symbol']) or row['symbol']=='[unknown]':
            unknown[row['dso']]+=row['samples']
    selected={}
    for group,fragments in groups.items():
        if group=='named_malloc_free':match=lambda r:r['symbol'] in fragments
        else:match=lambda r:any(fragment in r['symbol'] for fragment in fragments)
        found=[r for r in rows if match(r)]
        samples=sum(r['samples'] for r in found)
        selected[group]=dict(samples=samples,sample_share_percent=100*samples/total,
            estimated_cpu_us_per_completed=cpu*samples/total,
            symbols=[dict(symbol=r['symbol'],dso=r['dso'],samples=r['samples']) for r in found])
    result[mode]=dict(groups=selected,raw_address_samples_by_named_dso=dict(unknown))
Path('/var/tmp/cnetmod-h3-system-perf-structure-review.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
