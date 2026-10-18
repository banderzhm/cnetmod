"""Capture verification inputs without running a target or modifying sources."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

repo=Path('C:/Users/25007/.codex/worktrees/2c3c/cnetmod')
records=Path('C:/Users/25007/.codex/tmp/cnetmod-h3-pmtu-final-system-windows-20261018-ffa91ab479f4422791aaeacc51dbe847')
records.mkdir(exist_ok=True)
phase=sys.argv[1]
suffixes={'.cpp','.cppm','.inl','.h','.hpp','.cmake','.py','.txt','.ps1','.sh'}
paths=[repo/'CMakeLists.txt']
for directory in ('src','include','cmake','testing/http3','testing/tests','testing/bench'):
    paths += [p for p in (repo/directory).rglob('*') if p.is_file() and p.suffix in suffixes
              and 'results' not in p.relative_to(repo).parts and '__pycache__' not in p.parts]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
ledger={p.relative_to(repo).as_posix():sha(p) for p in sorted(set(paths))}
protected=repo/'testing/http3/h3-interop-results.json'
result={'source_sha256':ledger,'protected_json_sha256':sha(protected)}
(records/f'source-{phase}.json').write_text(json.dumps(result,indent=2)+'\n')
if phase=='before':
    for args,name in ((['git','rev-parse','HEAD'],'actual-head.txt'),
                      (['git','status','--short'],'dirty-status-before.txt'),
                      (['git','diff','--binary'],'dirty-source-before.patch')):
        (records/name).write_bytes(subprocess.check_output(args,cwd=repo))
    for name in ('cnetmod-h3-pmtu-final-windows-final.ps1',):
        (records/name).write_bytes((Path('C:/Users/25007/.codex/tmp')/name).read_bytes())
else:
    before=json.loads((records/'source-before.json').read_text())
    result['unchanged']=result==before
    (records/'source-integrity.json').write_text(json.dumps(result['unchanged'])+'\n')
    assert result['unchanged'],'source/protected JSON changed'
print(phase,'source files',len(ledger),'protected JSON hash',result['protected_json_sha256'])
