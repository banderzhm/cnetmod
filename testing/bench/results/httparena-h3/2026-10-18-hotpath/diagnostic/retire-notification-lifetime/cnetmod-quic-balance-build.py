"""External current-library allocation-balance build; no production writes."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

home=Path(__file__).resolve().parent
helper=home/'cnetmod-quic-packet-failure-proposal/build_fault.py'
spec=importlib.util.spec_from_file_location('fault_helpers',helper)
f=importlib.util.module_from_spec(spec)
spec.loader.exec_module(f)
build=Path('/var/tmp/cnetmod-h3-arena-build')
recorded=Path('/tmp/cnetmod-h3-arena-build')
out=Path('/var/tmp/cnetmod-h3-quic-node-balance-05fe')
if '--namespace' not in sys.argv:
    assert not out.exists()
    raise SystemExit(subprocess.run(['unshare','--mount','--propagation','private',sys.executable,str(Path(__file__).resolve()),'--namespace']).returncode)
recorded.mkdir(parents=True,exist_ok=True)
subprocess.run(['mount','--bind',str(build),str(recorded)],check=True)
subprocess.run(['mount','-o','remount,bind,ro',str(recorded)],check=True)
entries=json.loads((recorded/'compile_commands.json').read_text())
entry=next(item for item in entries if Path(item['file']).name=='quic_retire_stream_server.cpp')
inputs=set(recorded.rglob('*.pcm'))|set(recorded.rglob('*.a'))|set(recorded.rglob('static.c.o'))
inputs|={path for path in Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod/src').rglob('*') if path.is_file()}
inputs|={home/'cnetmod-quic-balance.cpp',helper,Path(__file__).resolve(),recorded/'compile_commands.json',recorded/'build.ninja'}
before=f.snapshot(inputs)
out.mkdir()
(out/'inputs-before.json').write_text(json.dumps(before,indent=2)+'\n')
try:
    source=out/'quic_balance.cpp'
    shutil.copy2(home/'cnetmod-quic-balance.cpp',source)
    obj=out/'quic_balance.cpp.o'
    command=f.compilation(entry,source,obj,recorded)
    f.run(command,recorded,out,'compile')
    archive=out/'libcnetmod_quic.a'
    shutil.copy2(recorded/archive.name,archive)
    binary=out/'quic_balance'
    link=f.link_command(recorded,command[0],binary,obj,archive)
    names=['_Znwm','_Znam','_ZnwmSt11align_val_t','_ZnamSt11align_val_t','_ZdlPv','_ZdaPv','_ZdlPvm','_ZdaPvm','_ZdlPvSt11align_val_t','_ZdaPvSt11align_val_t','_ZdlPvmSt11align_val_t','_ZdaPvmSt11align_val_t']
    link.extend('-Wl,--wrap='+name for name in names)
    f.run(link,recorded,out,'link')
    assert str(archive) in (out/'link.log').read_text()
    print(str(binary),f.digest(binary),flush=True)
finally:
    after=f.snapshot(inputs)
    changed=[name for name in before.keys()|after.keys() if before.get(name)!=after.get(name)]
    (out/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n')
    (out/'integrity.json').write_text(json.dumps({'unchanged':not changed,'changed':changed},indent=2)+'\n')
    assert not changed,changed
