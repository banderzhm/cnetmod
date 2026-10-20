from pathlib import Path
import os,subprocess,hashlib,json
os.nice(19)
out=Path('/var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0/owned-asm');out.mkdir()
variants=json.loads(Path('/var/tmp/cnetmod-h3-owned-fast-candidate-fbaeb04a21614732b24a7fdae850fcd0/three-variants.json').read_text())
inputs={label:(value['binary'],value['binary_sha256']) for label,value in variants.items()}
manifest={}
for label,(path,digest) in inputs.items():
 binary=Path(path);assert hashlib.sha256(binary.read_bytes()).hexdigest()==digest
 nm=subprocess.check_output(['nm','-n','-C','--defined-only',str(binary)],text=True);rows=[]
 for line in nm.splitlines():
  fs=line.split(maxsplit=2)
  if len(fs)==3 and fs[1].lower()=='t':rows.append((int(fs[0],16),fs[2]))
 methods={}
 for method in ('size()','append_to(','is_connection_close()','owned_bytes()','release_owned_bytes()'):
  for i,(address,name) in enumerate(rows):
   if 'quic_send_frame@cnetmod.protocol.quic.send_pipeline::'+method in name:
    end=rows[i+1][0];command=['objdump','-d','-C','--start-address='+hex(address),'--stop-address='+hex(end),str(binary)];filename=method.split('(')[0]+'.asm';dest=out/label/filename;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(subprocess.check_output(command));methods[method]=dict(start=hex(address),end=hex(end),bytes=end-address,command=command,sha256=hashlib.sha256(dest.read_bytes()).hexdigest())
 manifest[label]=dict(binary=str(binary),binary_sha256=digest,methods=methods)
(out/'manifest.json').write_text(json.dumps(manifest,indent=2));print(out)
