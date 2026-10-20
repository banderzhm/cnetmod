import argparse,csv,json,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args();root=a.root
s=json.loads((root/'summary.json').read_text());assert s['valid'] and s['inputs_unchanged'] and len(s['rounds'])==12 and s['cleanup']['remaining']==[] and s['cleanup']['returncode']==0
fields=['path_mtu','receive_ceiling','block','variant','done','status_2xx','header_completion_delta','elapsed_seconds','completed_rps','valid_2xx_rps','printed_rps','cpu_us_per_complete','p99_ms','rss_after_mib','host_busy_cpu','host_idle_cpu','positive_cpu_tids','failed','errored','timeout']
rows=[]
for r in s['rounds']:
    row={k:r[k] for k in fields if k in r}
    row.update(rss_after_mib=r['after']['memory_kib']['Rss']/1024,host_busy_cpu=r['system_cpu_delta'].get('average_busy_cpu_equivalents'),host_idle_cpu=r['system_cpu_delta'].get('average_idle_cpu_equivalents'),positive_cpu_tids=sum(x.get('cpu_seconds_delta',0)>0 for x in r['thread_cpu_deltas']))
    rows.append(row)
with (root/'all-rounds.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
medians=[]
for mtu in [4096,65507]:
    entry={'path_mtu':mtu,'receive_ceiling':8192 if mtu==4096 else 65507,'variants':{}}
    for label in ['baseline','candidate']:
        subset=[r for r in rows if r['path_mtu']==mtu and r['variant']==label]
        entry['variants'][label]={k:statistics.median(r[k] for r in subset) for k in ['completed_rps','valid_2xx_rps','cpu_us_per_complete','p99_ms','rss_after_mib']}
    entry['paired_changes']=[r for r in s['matched_blocks'] if r['path_mtu']==mtu]
    medians.append(entry)
result={'valid':True,'scope':'12 static-h3 rounds; per ceiling three matched AB/BA/AB blocks. Nonofficial paired experiment. Header rate differs from completed rate.','configurations':medians,'rounds':rows,'source_difference':s['source_differences_exact'],'proof':s['variants'],'limits':['Ceilings do not prove every packet reaches that size.','Host busy includes load generator; thread skew does not prove available spare capacity.','No packet rejection counter was added, so performance change cannot identify rejected versus successful copy counts.']}
(root/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))