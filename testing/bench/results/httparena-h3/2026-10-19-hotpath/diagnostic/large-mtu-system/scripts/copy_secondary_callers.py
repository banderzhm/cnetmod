from pathlib import Path
import re,collections,json,os
os.nice(19)
for mode,root in [('4096',Path('/var/tmp/cnetmod-h3-large-mtu-perf-a4018f36386a48b5a85b0146a90f03ef')),('65507',Path('/var/tmp/cnetmod-h3-large-mtu-perf-c6333e93d31b45c689c0146783514018'))]:
 out=root/'offline-analysis-guard-mtu'; audit=json.loads((out/'copy-callers.json').read_text());base=int(audit['elf_base'],16); ranges=[(int(r['start'],16),int(r['end'],16)) for r in audit['insert_ranges']]; chains=collections.Counter()
 for b in (out/'samples.txt').read_text().strip().split('\n\n'):
  fs=[]
  for line in b.splitlines():
   m=re.match(r'^\s*([0-9a-f]+) (.+?)(?:\+0x([0-9a-f]+))? \((.+)\)$',line)
   if m and m[4]!='inlined':fs.append((int(m[1],16),m[2],int(m[3] or '0',16)))
  if not fs or not any(a<=fs[0][0]-base<b for a,b in ranges):continue
  physical=[f for f in fs[1:] if f[0]!=fs[0][0]]
  if physical and ('12encode_frame' in physical[0][1] or '18apply_stream_write' in physical[0][1]):
   chains[tuple((n,hex(o),hex(ip-base)) for ip,n,o in physical[:3])]+=1
 result=[dict(frames=[dict(mangled=n,offset=o,elf_pc=pc) for n,o,pc in chain],samples=count) for chain,count in chains.most_common()]
 (out/'secondary-copy-callers.json').write_text(json.dumps(result,indent=2));print(mode,json.dumps(result[:5],indent=2))
