"""System-allocator link/binding audit; no service or traffic."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys

if '--namespace' not in sys.argv:
    raise SystemExit(subprocess.run(['unshare','--mount','--propagation','private',sys.executable,str(Path(__file__).resolve()),'--namespace']).returncode)
build=Path('/tmp/cnetmod-h3-arena-build')
build.mkdir(parents=True,exist_ok=True)
subprocess.run(['mount','--bind','/var/tmp/cnetmod-h3-arena-build',str(build)],check=True)
root=Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-final-system-validation')
out=root/'system-allocator'
out.mkdir(parents=True, exist_ok=False)
assert not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT')
binary=build/'testing/bench/arena_h3_server'
commands=subprocess.check_output(['/var/tmp/cnetmod-ninja-1.11/bin/ninja','-t','commands','arena_h3_server'],cwd=build,text=True).splitlines()
link=next(command for command in reversed(commands) if ' -o testing/bench/arena_h3_server ' in command)
(out/'arena-link-command.txt').write_text(link+'\n')
assert 'mimalloc' not in link.lower(),link
outputs={}
for name,command in (
    ('nm-dynamic',['nm','-D',str(binary)]),
    ('ldd',['ldd',str(binary)]),
    ('readelf-dynamic',['readelf','-d',str(binary)]),
    ('runtime-archive-members',['llvm-ar','t',str(build/'libcnetmod_runtime.a')])):
    result=subprocess.run(command,capture_output=True,text=True)
    (out/(name+'.stdout')).write_text(result.stdout)
    (out/(name+'.stderr')).write_text(result.stderr)
    assert result.returncode==0,name
    outputs[name]=result.stdout
assert 'mimalloc' not in outputs['ldd'].lower()
assert not re.search(r'\b[TW]\s+(malloc|calloc|realloc|free)(@|\s|$)',outputs['nm-dynamic'],re.M)
assert re.search(r'\bU\s+malloc@',outputs['nm-dynamic'])
env=os.environ.copy()
env.update(LD_DEBUG='bindings,libs',LD_BIND_NOW='1')
with (out/'arena-noargs.stdout').open('w') as stdout,(out/'arena-bindings.stderr').open('w') as stderr:
    # No arguments exits at usage validation; dynamic loader still resolves
    # all imports due LD_BIND_NOW. No listener/server is started.
    result=subprocess.run([str(binary)],env=env,stdout=stdout,stderr=stderr,timeout=15)
assert result.returncode==2,result.returncode
bindings=(out/'arena-bindings.stderr').read_text()
selected=[line for line in bindings.splitlines() if re.search(r"symbol [`'](malloc|calloc|realloc|free|_Znwm|_Znam|_ZdlPv|_ZdaPv)'",line)]
(out/'allocator-bindings.txt').write_text('\n'.join(selected)+'\n')
assert any("symbol `malloc'" in line and 'libc.so.6' in line and 'binding file '+str(binary) in line for line in selected)
assert any("symbol `_Znwm'" in line and ('libc++.so' in line or 'libc++abi.so' in line) and 'binding file '+str(binary) in line for line in selected)
assert not any('mimalloc' in line.lower() for line in selected)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
manifest=dict(system_allocator_verified=True,binary=str(binary),binary_sha256=sha(binary),
    noargs_returncode=result.returncode,ld_preload=os.environ.get('LD_PRELOAD'),ld_audit=os.environ.get('LD_AUDIT'),
    cmake_use_mimalloc_off='CNETMOD_USE_MIMALLOC:BOOL=OFF' in (build/'CMakeCache.txt').read_text(),
    files={str(path.relative_to(build)):sha(path) for path in (build/'CMakeCache.txt',build/'build.ninja',build/'libcnetmod_runtime.a',build/'libcnetmod_quic.a',build/'libcnetmod_http.a',binary,build/'testing/tests/test_udp_batch')})
(out/'allocator-proof.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2),flush=True)
