import argparse,json,statistics
from pathlib import Path
ap=argparse.ArgumentParser();ap.add_argument('summary',type=Path);args=ap.parse_args()
s=json.loads(args.summary.read_text());assert s['valid'] and s['inputs_unchanged'] and s['cleanup']['returncode']==0 and not s['cleanup']['remaining'];assert len(s['rounds'])==54
result=dict(valid=True,summary=str(args.summary),medians=[],matched_effects=[])
for profile,mtu,recv in (('baseline-h3',4096,8192),('static-h3',4096,8192),('static-h3',65507,65507)):
 for label in ('guard','mixed','fast'):
  rows=[r for r in s['rounds'] if (r['profile'],r['path_mtu'],r['receive_ceiling'],r['variant'])==(profile,mtu,recv,label)];assert len(rows)==6
  assert all(r['done']==r['succeeded']>0 and r['status_2xx']>=r['done'] and r['failed']==r['errored']==r['timeout']==0 for r in rows)
  result['medians'].append(dict(profile=profile,path_mtu=mtu,receive_ceiling=recv,variant=label,**{k:statistics.median(r[k] for r in rows) for k in ('completed_rps','printed_rps','cpu_us_per_complete','p99_ms')},rss_mib=statistics.median(r['after']['memory_kib']['Rss']/1024 for r in rows)))
 for reference,target in (('guard','mixed'),('guard','fast'),('mixed','fast')):
  rows=[r for r in s['matched_blocks'] if (r['profile'],r['path_mtu'],r['reference'],r['target'])==(profile,mtu,reference,target)];assert len(rows)==6
  result['matched_effects'].append(dict(profile=profile,path_mtu=mtu,receive_ceiling=recv,reference=reference,target=target,**{k:dict(median=statistics.median(r[k] for r in rows),individual=[r[k] for r in rows]) for k in ('completion_gain_percent','cpu_per_complete_change_percent','p99_change_percent')}))
print(json.dumps(result,indent=2))
