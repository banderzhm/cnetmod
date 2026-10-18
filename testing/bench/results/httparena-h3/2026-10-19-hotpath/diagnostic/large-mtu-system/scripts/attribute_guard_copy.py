import subprocess,collections,re,json,os
from pathlib import Path
os.nice(19)
roots=[Path('/var/tmp/cnetmod-h3-large-mtu-perf-a4018f36386a48b5a85b0146a90f03ef'),Path('/var/tmp/cnetmod-h3-large-mtu-perf-c6333e93d31b45c689c0146783514018')]
for root in roots:
 out=root/'offline-analysis-guard-mtu';binary=root/'arena_h3_server'
 nm=subprocess.check_output(['nm','-n','--defined-only',str(binary)],text=True)
 symbols=[]; names={}
 for line in nm.splitlines():
  f=line.split(maxsplit=2)
  if len(f)==3 and f[1].lower()=='t':
   a=int(f[0],16);symbols.append((a,f[2]));names[f[2]]=a
 blocks=(out/'samples.txt').read_text().strip().split('\n\n')
 parsed=[]; votes=collections.Counter();libcvotes=collections.Counter()
 libcnames={}
 for line in subprocess.check_output(['nm','-D','--defined-only','/usr/lib/libc.so.6'],text=True).splitlines():
  f=line.split(maxsplit=2)
  if len(f)==3:libcnames[f[2].split('@')[0]]=int(f[0],16)
 for block in blocks:
  frames=[]
  for line in block.splitlines():
   m=re.match(r'^\s*([0-9a-f]+) (.+?)(?:\+0x([0-9a-f]+))? \((.+)\)$',line)
   if not m:continue
   ip=int(m[1],16);name=m[2];off=int(m[3] or '0',16);dso=m[4];frames.append((ip,name,off,dso))
   if dso.endswith('/arena_h3_server') and name in names:votes[ip-names[name]-off]+=1
   if dso.endswith('/libc.so.6') and name in libcnames:libcvotes[ip-libcnames[name]-off]+=1
  if frames:parsed.append(frames)
 base,count=votes.most_common(1)[0];libcbase,libccount=libcvotes.most_common(1)[0]
 inserts=[]
 for i,(address,name) in enumerate(symbols):
  if '__insert_with_size' in name and 'vectorISt4byte' in name:inserts.append((address,symbols[i+1][0],name))
 pc_counts=collections.Counter();callers=collections.Counter();libccallers=collections.Counter();memsetcount=0
 for frames in parsed:
  first=frames[0];pc=first[0]-base
  if any(a<=pc<b for a,b,_ in inserts):
   pc_counts[pc]+=1
   caller=next((f for f in frames[1:] if f[0]!=first[0] and f[3]!='inlined'),None)
   if caller:callers[(caller[1],hex(caller[2]),hex(caller[0]-base))]+=1
  if first[0]-libcbase==0x176ed3:
   memsetcount+=1;caller=next((f for f in frames[1:] if f[0]!=first[0] and f[3]!='inlined'),None)
   if caller:libccallers[(caller[1],hex(caller[2]),hex(caller[0]-base))]+=1
 result=dict(sample_blocks=len(blocks),parsed_blocks=len(parsed),elf_base=hex(base),elf_base_votes=votes.most_common(3),libc_base=hex(libcbase),libc_base_votes=libcvotes.most_common(3),insert_ranges=[dict(start=hex(a),end=hex(b),mangled=n) for a,b,n in inserts],insert_leaf_samples=sum(pc_counts.values()),insert_pcs=[dict(pc=hex(pc),samples=n) for pc,n in pc_counts.most_common()],insert_physical_callers=[dict(mangled=k[0],offset=k[1],elf_pc=k[2],samples=n) for k,n in callers.most_common()],libc_stosb_pc='0x176ed3',libc_stosb_samples=memsetcount,libc_stosb_callers=[dict(mangled=k[0],offset=k[1],elf_pc=k[2],samples=n) for k,n in libccallers.most_common()])
 (out/'copy-callers.json').write_text(json.dumps(result,indent=2))
 with (out/'insert-loop.asm').open('w') as f:
  a,b,n=max(inserts,key=lambda r:sum(v for pc,v in pc_counts.items() if r[0]<=pc<r[1]))
  subprocess.run(['objdump','-d','-C','--start-address='+hex(a),'--stop-address='+hex(b),str(binary)],stdout=f,check=True)
 print(root);print(json.dumps({k:result[k] for k in ('insert_leaf_samples','insert_physical_callers','libc_stosb_samples','libc_stosb_callers')},indent=2),flush=True)
