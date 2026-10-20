"""Two external backend negative controls; production/build inputs read-only."""
from pathlib import Path
import hashlib
import importlib.util
import json
import shlex
import shutil
import subprocess
import sys

tmp=Path('/mnt/c/Users/25007/.codex/tmp')
helper=tmp/'cnetmod-quic-packet-failure-proposal/build_fault.py'
spec=importlib.util.spec_from_file_location('build_helpers',helper)
helpers=importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
if '--namespace' not in sys.argv:
    raise SystemExit(subprocess.run(['unshare','--mount','--propagation','private',sys.executable,str(Path(__file__).resolve()),'--namespace']).returncode)
build=Path('/tmp/cnetmod-h3-arena-build')
build.mkdir(parents=True,exist_ok=True)
subprocess.run(['mount','--bind','/var/tmp/cnetmod-h3-arena-build',str(build)],check=True)
subprocess.run(['mount','-o','remount,bind,ro',str(build)],check=True)
root=tmp/'cnetmod-h3-gso-validation-1fdab0ae660649bf83f09840e8459d4f'
backend=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod/src/executor/platform/io_uring_async_op.cpp')
entries=json.loads((build/'compile_commands.json').read_text())
entry=next(item for item in entries if item['file'].endswith('/io_uring_async_op.cpp'))
commands=subprocess.check_output(['/var/tmp/cnetmod-ninja-1.11/bin/ninja','-t','commands','test_udp_batch'],cwd=build,text=True).splitlines()
link=next(line for line in reversed(commands) if ' -o testing/tests/test_udp_batch ' in line)
assert link.count(' libcnetmod_runtime.a ')==1
original=backend.read_text()
assert original.count('return original_prefix[static_cast<std::size_t>(sent) - 1U];')==1
assert original.count('|| cork != 0)')==1 and original.count('native_segment != 0)')==1
inputs=set(build.rglob('*.pcm'))|set(build.glob('*.a'))|{backend,helper,build/'compile_commands.json',build/'build.ninja'}
before=helpers.snapshot(inputs)
(root/'negative-inputs-before.json').write_text(json.dumps(before,indent=2)+'\n')
try:
    for name,source in (
        ('raw-kernel-prefix',original.replace('return original_prefix[static_cast<std::size_t>(sent) - 1U];','return static_cast<std::size_t>(sent);')),
        ('ignore-native-overrides',original.replace('|| cork != 0)','|| false)').replace('native_segment != 0)','false)'))):
        out=root/('negative-'+name)
        out.mkdir(exist_ok=True)
        copy=out/'io_uring_async_op.cpp'
        copy.write_text(source)
        obj=out/'io_uring_async_op.cpp.o'
        command=helpers.compilation(entry,copy,obj,build)
        helpers.run(command,build,out,'compile')
        archive=out/'libcnetmod_runtime.a'
        shutil.copy2(build/'libcnetmod_runtime.a',archive)
        helpers.run(['llvm-ar','rs',str(archive),str(obj)],build,out,'archive')
        binary=out/'test_udp_batch'
        candidate=link.replace(' libcnetmod_runtime.a ',' '+shlex.quote(str(archive))+' ')
        candidate=candidate.replace(' -o testing/tests/test_udp_batch ',' -o '+shlex.quote(str(binary))+' ')
        candidate=candidate.replace('--dependency-file=testing/tests/CMakeFiles/test_udp_batch.dir/link.d',
            '--dependency-file='+shlex.quote(str(out/'link.d')))
        if (out/'link.log').exists() and not (out/'link.initial-failure.log').exists():
            shutil.copy2(out/'link.log',out/'link.initial-failure.log')
            shutil.copy2(out/'link-command.txt',out/'link.initial-failure-command.txt')
        (out/'link-command.txt').write_text(candidate+'\n')
        with (out/'link.log').open('w') as log:
            result=subprocess.run(candidate,cwd=build,shell=True,stdout=log,stderr=subprocess.STDOUT)
        assert result.returncode==0,(name,'link failed')
        (out/'manifest.json').write_text(json.dumps(dict(variant=name,binary_sha256=helpers.digest(binary),archive_sha256=helpers.digest(archive),source_sha256=helpers.digest(copy)),indent=2)+'\n')
        print('BUILT NEGATIVE',name,helpers.digest(binary),flush=True)
finally:
    after=helpers.snapshot(inputs)
    changed=[name for name in before.keys()|after.keys() if before.get(name)!=after.get(name)]
    (root/'negative-inputs-after.json').write_text(json.dumps(after,indent=2)+'\n')
    (root/'negative-input-integrity.json').write_text(json.dumps(dict(unchanged=not changed,changed=changed),indent=2)+'\n')
    assert not changed,changed
