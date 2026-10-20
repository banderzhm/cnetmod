"""Reconfigure this coordinated build only; keep repository default unchanged."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys

if '--namespace' not in sys.argv:
    raise SystemExit(subprocess.run(['unshare','--mount','--propagation','private',sys.executable,str(Path(__file__).resolve()),'--namespace']).returncode)
assert not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT')
build=Path('/tmp/cnetmod-h3-arena-build')
build.mkdir(parents=True,exist_ok=True)
subprocess.run(['mount','--bind','/var/tmp/cnetmod-h3-arena-build',str(build)],check=True)
out=Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-gso-validation-1fdab0ae660649bf83f09840e8459d4f')/'system-allocator'
out.mkdir()
shutil.copy2(build/'CMakeCache.txt',out/'CMakeCache.before.txt')
commands=[['cmake','-S','/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod','-B',str(build),'-DCNETMOD_USE_MIMALLOC=OFF'],
    ['/var/tmp/cnetmod-ninja-1.11/bin/ninja','-j2','arena_h3_server','test_udp_batch','test_quic_stream',
     'test_quic_loss_detection','test_quic_congestion','test_quic_early_data','test_quic_frame',
     'test_quic_packet','test_quic_varint','quic_retire_stream_server']]
for label,command in zip(('configure','build'),commands):
    (out/(label+'-command.json')).write_text(json.dumps(command,indent=2)+'\n')
    with (out/(label+'.log')).open('w') as log:
        result=subprocess.run(command,cwd=build,stdout=log,stderr=subprocess.STDOUT)
    (out/(label+'-result.json')).write_text(json.dumps({'returncode':result.returncode})+'\n')
    assert result.returncode==0,label
shutil.copy2(build/'CMakeCache.txt',out/'CMakeCache.after.txt')
assert 'CNETMOD_USE_MIMALLOC:BOOL=OFF' in (build/'CMakeCache.txt').read_text()
print('SYSTEM BUILD PASS',flush=True)
