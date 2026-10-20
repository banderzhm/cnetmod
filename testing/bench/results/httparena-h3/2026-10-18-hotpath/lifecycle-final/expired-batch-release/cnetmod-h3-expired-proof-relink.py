from pathlib import Path
import hashlib,json,subprocess
build=Path('/var/tmp/cnetmod-h3-arena-build');out=Path('/var/tmp/cnetmod-h3-expired-weak-proof-final2')
if Path('/proc/self/ns/mnt').readlink()==Path('/proc/1/ns/mnt').readlink():raise RuntimeError('private mount required')
recorded=Path('/tmp/cnetmod-h3-arena-build');recorded.mkdir(parents=True,exist_ok=True)
subprocess.run(['mount','--bind',str(build),str(recorded)],check=True)
subprocess.run(['mount','-o','remount,bind,ro',str(recorded)],check=True)
files=set(build.rglob('*.pcm'))|set(build.rglob('*.modmap'))|set(build.glob('*.a'))|set(build.rglob('h3_interop_server.cpp.o'))
def snap():return {str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in sorted(files)}
before=snap()
for name in ['expired-held','expired-released']:
 d=out/name
 command=json.loads((d/'link-command.json').read_text())
 destination=d/'h3_interop_server-4s'
 command[command.index('-o')+1]=str(destination)
 (d/'link-command-4s.json').write_text(json.dumps(command,indent=2))
 subprocess.run(command,cwd=recorded,check=True)
 (d/'binary-4s-sha256.txt').write_text(hashlib.sha256(destination.read_bytes()).hexdigest()+'\n')
after=snap();(out/'relink-4s-integrity.json').write_text(json.dumps({'unchanged':before==after,'before':before,'after':after},indent=2))
assert before==after
