from pathlib import Path
import json,subprocess,hashlib
root=Path('/var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0/owned-asm')
v=json.loads(Path('/var/tmp/cnetmod-h3-owned-fast-candidate-fbaeb04a21614732b24a7fdae850fcd0/three-variants.json').read_text())
result={}
for label,value in v.items():
 b=Path(value['binary']);assert hashlib.sha256(b.read_bytes()).hexdigest()==value['binary_sha256']
 rows=[]
 for line in subprocess.check_output(['nm','-n','-C','--defined-only',str(b)],text=True).splitlines():
  fields=line.split(maxsplit=2)
  if len(fields)==3 and fields[1].lower()=='t':rows.append((int(fields[0],16),fields[2]))
 methods=[]
 for i,(address,name) in enumerate(rows):
  if 'pack_one_rtt_packet' in name and 'clone' not in name:
   end=rows[i+1][0];cmd=['objdump','-d','-C','--start-address='+hex(address),'--stop-address='+hex(end),str(b)];asm=subprocess.check_output(cmd,text=True)
   dest=root/label/('packer-'+str(len(methods))+'.asm');dest.write_text(asm)
   relevant=[l for l in asm.splitlines() if ('call' in l or 'jmp' in l) and any(x in l for x in ('append_to','__insert_with_size','is_connection_close'))]
   methods.append(dict(name=name,start=hex(address),end=hex(end),bytes=end-address,relevant_calls=relevant,asm_sha256=hashlib.sha256(dest.read_bytes()).hexdigest()))
 result[label]=methods
(root/'packer-manifest.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
