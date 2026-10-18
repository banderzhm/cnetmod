"""Build current permanent retire fixture externally with pinned old/new archive."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

home=Path(__file__).resolve().parent
version=sys.argv[1]
assert version in ('old','new')
helper=home/'cnetmod-quic-packet-failure-proposal/build_fault.py'
spec=importlib.util.spec_from_file_location('helpers',helper)
f=importlib.util.module_from_spec(spec)
spec.loader.exec_module(f)
original=Path('/var/tmp/cnetmod-h3-arena-build')
build=Path('/tmp/cnetmod-h3-arena-build')
out=Path('/var/tmp/cnetmod-h3-notification-'+version+'-proof-build')
if '--namespace' not in sys.argv:
    assert not out.exists()
    raise SystemExit(subprocess.run(['unshare','--mount','--propagation','private',sys.executable,str(Path(__file__).resolve()),version,'--namespace']).returncode)
build.mkdir(parents=True,exist_ok=True)
subprocess.run(['mount','--bind',str(original),str(build)],check=True)
subprocess.run(['mount','-o','remount,bind,ro',str(build)],check=True)
source=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod/testing/http3/quic_retire_stream_server.cpp')
archive=(Path('/var/tmp/cnetmod-h3-quic-node-balance-05fe/libcnetmod_quic.a')
         if version=='old' else build/'libcnetmod_quic.a')
if version=='old':
    frozen=json.loads(Path('/var/tmp/cnetmod-h3-quic-node-balance-05fe/inputs-before.json').read_text())
    assert f.digest(archive)==frozen['/tmp/cnetmod-h3-arena-build/libcnetmod_quic.a']['sha256']
entries=json.loads((build/'compile_commands.json').read_text())
entry=next(item for item in entries if Path(item['file']).name=='quic_retire_stream_server.cpp')
inputs=set(build.rglob('*.pcm'))|set(build.rglob('*.a'))|set(build.rglob('static.c.o'))
inputs|={source,archive,helper,Path(__file__).resolve(),build/'compile_commands.json',build/'build.ninja'}
before=f.snapshot(inputs)
out.mkdir()
(out/'inputs-before.json').write_text(json.dumps(before,indent=2)+'\n')
try:
    copy=out/source.name
    shutil.copy2(source,copy)
    obj=out/(copy.name+'.o')
    command=f.compilation(entry,copy,obj,build)
    f.run(command,build,out,'compile')
    linked=out/'libcnetmod_quic.a'
    shutil.copy2(archive,linked)
    binary=out/'quic_retire_stream_server'
    link=f.link_command(build,command[0],binary,obj,linked)
    f.run(link,build,out,'link')
    trace=(out/'link.log').read_text()
    assert str(linked)+'(quic_connection.cpp.o)' in trace
    member=subprocess.check_output(['llvm-ar','p',str(linked),'quic_connection.cpp.o'])
    import hashlib
    (out/'link-proof.json').write_text(json.dumps({'version':version,'archive_input':str(archive),
       'archive_sha256':f.digest(archive),'connection_member_sha256':hashlib.sha256(member).hexdigest(),
       'binary_sha256':f.digest(binary),'external_member_seen_in_trace':True},indent=2)+'\n')
    print(str(binary),f.digest(binary),flush=True)
finally:
    after=f.snapshot(inputs)
    changed=[name for name in before.keys()|after.keys() if before.get(name)!=after.get(name)]
    (out/'inputs-after.json').write_text(json.dumps(after,indent=2)+'\n')
    (out/'integrity.json').write_text(json.dumps({'unchanged':not changed,'changed':changed},indent=2)+'\n')
    assert not changed,changed
