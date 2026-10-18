import json,re,statistics,csv,hashlib
from pathlib import Path
root=Path('/var/tmp/cnetmod-h3-large-mtu-ab-d411da495070401985b365d4e8e26e52')
d=json.loads((root/'summary.json').read_text());assert d['valid'] and len(d['rounds'])==9
rows=[]
for row in d['rounds']:
 r=dict(row);name=f"static-h3-{r['block']}-{r['variant']}";text=(root/(name+'-h2load.stdout')).read_text()
 traffic=re.search(r'traffic: .*?\((\d+)\) total, .*?\((\d+)\) headers .*?, .*?\((\d+)\) data',text);assert traffic
 r['traffic_total_bytes'],r['traffic_header_bytes'],r['traffic_data_bytes']=map(int,traffic.groups())
 assert r['done']==r['succeeded'] and r['status_2xx']>=r['done'] and all(r[k]==0 for k in ('failed','errored','timeout'))
 r['received_udp_per_completed']=r['udp_received']/r['done'];r['data_bytes_per_received_udp']=r['traffic_data_bytes']/r['udp_received'];r['data_bytes_per_completed']=r['traffic_data_bytes']/r['done'];r['rss_after_bytes']=r['after']['memory_kib']['Rss']*1024;r['private_after_bytes']=(r['after']['memory_kib']['Private_Clean']+r['after']['memory_kib']['Private_Dirty'])*1024
 r['h2load_stdout_sha256']=hashlib.sha256((root/(name+'-h2load.stdout')).read_bytes()).hexdigest();rows.append(r)
medians=[]
for v in (16384,32768,65507):
 rr=[r for r in rows if r['variant']==v];assert len(rr)==3
 medians.append(dict(variant=v,**{k:statistics.median(r[k] for r in rr) for k in ('completed_per_elapsed_second','printed_rps','server_cpu_microseconds_per_completed','p99_ms','rss_after_bytes','received_udp_per_completed','data_bytes_per_received_udp','data_bytes_per_completed')}))
paired=[]
for block in (1,2,3):
 base=next(r for r in rows if r['block']==block and r['variant']==16384)
 for v in (32768,65507):
  rr=next(r for r in rows if r['block']==block and r['variant']==v)
  paired.append(dict(block=block,variant=v,completed_rate_change=rr['completed_per_elapsed_second']/base['completed_per_elapsed_second']-1,cpu_per_completed_change=rr['server_cpu_microseconds_per_completed']/base['server_cpu_microseconds_per_completed']-1))
out=root.parent/(root.name+'-analysis');out.mkdir(exist_ok=True)
(out/'analysis.json').write_text(json.dumps(dict(valid=True,source_summary_sha256=hashlib.sha256((root/'summary.json').read_bytes()).hexdigest(),scope='Nine configuration diagnostic rounds on frozen fc6e50; known unpatched PMTU scheduling interval overwrite. Not official ranking or final 1.1M acceptance. h2load received UDP includes probes and control and expands UDP_GRO segmentation; not syscall or pure STREAM packet counts.',medians=medians,paired_vs_16384=paired,rounds=rows),indent=2))
fields=['block','variant','done','printed_rps','elapsed_seconds','completed_per_elapsed_second','header_completion_delta','server_cpu_microseconds_per_completed','p99_ms','rss_after_bytes','private_after_bytes','udp_received','received_udp_per_completed','traffic_data_bytes','data_bytes_per_received_udp','data_bytes_per_completed']
with (out/'all-rounds.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=fields,extrasaction='ignore');w.writeheader();w.writerows(rows)
print(json.dumps(dict(medians=medians,paired=paired),indent=2));print(out)
