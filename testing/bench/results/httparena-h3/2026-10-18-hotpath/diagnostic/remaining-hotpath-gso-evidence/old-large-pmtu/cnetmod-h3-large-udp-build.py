import json
from pathlib import Path
import shlex
import subprocess

repo=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod')
build=Path('/tmp/cnetmod-h3-arena-build')
out=Path('/var/tmp/cnetmod-h3-large-udp-nogso')
out.mkdir(exist_ok=True)
source=(repo/'testing/bench/arena_h3_server.cpp').read_text()
assert source.count('set_path_mtu_discovery(1472,')==1
assert source.count('set_max_udp_payload_size(1472)')==1
source=source.replace('set_path_mtu_discovery(1472,','set_path_mtu_discovery(65527,')
source=source.replace('set_max_udp_payload_size(1472)','set_max_udp_payload_size(65527)')
(out/'arena_h3_server.cpp').write_text(source)
entries=json.loads((build/'compile_commands.json').read_text())
entry=next(c for c in entries if c['file'].endswith('/arena_h3_server.cpp'))
command=shlex.split(entry['command'])
old_object=command[command.index('-o')+1]
command[command.index('-o')+1]=str(out/'arena_h3_server.cpp.o')
command[command.index('-c')+1]=str(out/'arena_h3_server.cpp')
subprocess.run(command,cwd=build,check=True)
commands=subprocess.check_output(['/var/tmp/cnetmod-ninja-1.11/bin/ninja','-t','commands',
    'arena_h3_server'],cwd=build,text=True).splitlines()
link=next(c for c in reversed(commands) if ' -o testing/bench/arena_h3_server ' in c)
link=link.replace(' '+old_object+' ',' '+str(out/'arena_h3_server.cpp.o')+' ')
link=link.replace(' -o testing/bench/arena_h3_server ',' -o '+str(out/'arena_h3_server')+' ')
link=link.replace(' libcnetmod_runtime.a ',' /var/tmp/cnetmod-h3-stage2-before-gso/libcnetmod_runtime.a ')
(out/'link-command.txt').write_text(link)
subprocess.run(link,cwd=build,shell=True,check=True)
print(out,flush=True)
