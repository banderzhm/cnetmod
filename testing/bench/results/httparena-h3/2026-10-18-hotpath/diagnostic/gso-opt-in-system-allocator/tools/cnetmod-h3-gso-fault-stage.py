"""Serialized isolated fault/negative execution; preserve all logs."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

if '--namespace' not in sys.argv:
    raise SystemExit(subprocess.run(['unshare','--mount','--net','--propagation','private',sys.executable,str(Path(__file__).resolve()),'--namespace']).returncode)
subprocess.run(['ip','link','set','lo','up'],check=True)
subprocess.run(['mount','--bind','/var/tmp/cnetmod-h3-official/gai.conf','/etc/gai.conf'],check=True)
tmp=Path('/mnt/c/Users/25007/.codex/tmp')
out=tmp/'cnetmod-h3-gso-validation-1fdab0ae660649bf83f09840e8459d4f'
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
shim=out/'system-allocator/gso_fault.so'
command=['cc','-shared','-fPIC','-O2','-o',str(shim),str(tmp/'cnetmod-h3-gso-prepared/gso_fault.c'),'-ldl','-pthread']
(out/'system-allocator/shim-compile-command.json').write_text(json.dumps(command,indent=2)+'\n')
with (out/'system-allocator/shim-compile.log').open('w') as log:
    result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0
test=Path('/var/tmp/cnetmod-h3-arena-build/testing/tests/test_udp_batch')
command=[sys.executable,str(tmp/'cnetmod-h3-gso-fault-check.py'),'--test-binary',str(test),'--test-sha',sha(test),'--shim',str(shim),'--shim-sha',sha(shim)]
with (out/'system-allocator/fault-stage.log').open('w') as log:
    result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,'fault stage; see retained log'
rows=[]
for name,test_filter in (
    ('raw-kernel-prefix','udp_segmented_batch_reports_original_prefix_and_preserves_early_cancel'),
    ('ignore-native-overrides','udp_segmented_batch_retains_native_cork_and_segment_options')):
    binary=out/('negative-'+name)/'test_udp_batch'
    env=os.environ.copy()
    env.pop('LD_PRELOAD',None)
    env.pop('CNETMOD_GSO_FAULT',None)
    env['CNETMOD_TEST_FILTER']=test_filter
    with (out/('negative-'+name)/'test.stdout').open('w') as stdout,(out/('negative-'+name)/'test.stderr').open('w') as stderr:
        result=subprocess.run([str(binary)],env=env,stdout=stdout,stderr=stderr,timeout=30)
    text=(out/('negative-'+name)/'test.stdout').read_text()+(out/('negative-'+name)/'test.stderr').read_text()
    assert result.returncode==1 and '[  FAILED  ] '+test_filter in text and 'ASSERT FAILED' in text,(name,result.returncode)
    rows.append(dict(variant=name,binary_sha256=sha(binary),filter=test_filter,actual_returncode=result.returncode,expected_negative_failure=True))
    print('NEGATIVE EXPECTED FAIL',name,flush=True)
(out/'system-allocator/negative-results.json').write_text(json.dumps(dict(all_expected_fail=True,results=rows),indent=2)+'\n')
print('FAULT AND NEGATIVE STAGE PASS',flush=True)
