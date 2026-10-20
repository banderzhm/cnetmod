import json,csv,hashlib
from pathlib import Path
root=Path('/var/tmp/cnetmod-h3-large-mtu-preflight-feeddffd59fd4befaaf905b17ced73f7')
report=json.loads((root/'preflight-report.json').read_text())
out=root.parent/(root.name+'-timeline-audit');out.mkdir(exist_ok=True)
rows=[]
for measurement in report['measurements']:
 c=measurement['convergence']; probes=c['client_acknowledged_probe_candidates']; previous=None
 with (out/(str(measurement['ceiling'])+'-all-probes.csv')).open('w',newline='') as f:
  fields=['index','packet_number','bytes','received_time_ms','received_event','client_ack_time_ms','client_ack_event','size_delta','received_delta_ms','ack_delta_ms']
  writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader()
  for i,p in enumerate(probes):
   row=dict(p,index=i,size_delta=None if previous is None else p['bytes']-previous['bytes'], received_delta_ms=None if previous is None else p['received_time_ms']-previous['received_time_ms'], ack_delta_ms=None if previous is None else p['client_ack_time_ms']-previous['client_ack_time_ms']);writer.writerow(row);previous=p
 rows.append(dict(ceiling=measurement['ceiling'],received_candidates=len(c['received_probe_candidates']),acknowledged_candidates=len(probes),first=probes[0],last=probes[-1],zero_received_deltas=sum(a['received_time_ms']==b['received_time_ms'] for a,b in zip(probes,probes[1:])),size_deltas=sorted(set(b['bytes']-a['bytes'] for a,b in zip(probes,probes[1:]))),time_unit=c['time_unit']))
(out/'summary.json').write_text(json.dumps(dict(source_report_sha256=hashlib.sha256((root/'preflight-report.json').read_bytes()).hexdigest(),measurements=rows,scope='Offline extraction of all target candidates and client ACK observations, not internal validated path state. Current arm_path_mtu_discovery overwrites next-probe interval; timing is not a final acceptance result.'),indent=2))
print(json.dumps(rows));print(out)
