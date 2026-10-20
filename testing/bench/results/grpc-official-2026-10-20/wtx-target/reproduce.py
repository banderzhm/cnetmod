from pathlib import Path
import datetime
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess

root = Path.cwd()
native = Path('/root/cnetmod-grpc-20261020')
workspace = Path('/mnt/c/Users/25007/.codex/worktrees/c315/cnetmod')
if native.exists():
    root = workspace
else:
    raise SystemExit('Run inside the native WSL environment after both runs complete')
out = root / 'build-grpc-wtx-final'
out.mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location('analyzer', root / 'testing/bench/analyze_official_grpc.py')
analyzer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyzer)
rows = []
for stage in ['wtx-target-recheck', 'wtx-control-recheck']:
    records = native / stage / 'records'
    assert json.loads((records/'quiet-host.json').read_text())['valid']
    invocation = json.loads((records/'invocation.json').read_text())
    assert invocation['environment']['DURATION'] == '5s'
    assert invocation['environment']['RUNS'] == '3'
    assert invocation['environment']['H2THREADS'] == '64'
    for file in records.glob('*-unary-grpc*.log'):
        rows.extend(analyzer.parse_log(file))
    shutil.copytree(records, out / stage, dirs_exist_ok=True)
    shutil.copyfile(native / (stage+'.log'), out / (stage+'.log'))
assert len(rows) == 24
assert all(r['valid'] and r['progressing_beyond_initial_batches'] for r in rows)
audit = json.loads((root / 'build-grpc-leaderboard-audit/audit.json').read_text())
table = []
candidate = 'cnetmod-grpc-accepted2'
control = 'cnetmod-grpc-control-native'
for profile, conns in analyzer.CASES:
    values = {}
    for entry in [candidate, control]:
        selected = [r for r in rows if (r['entry'],r['profile'],r['connections'])==(entry,profile,conns)]
        assert len(selected)==3 and {r['round'] for r in selected}=={1,2,3}
        values[entry] = max(r['rps'] for r in selected)
    ref = next(r for r in audit['comparison'] if r['profile']==f'{profile}-{conns}')
    c = values[candidate]
    table.append(dict(profile=profile,connections=conns,candidate=c,control=values[control],
        local_control_gain_percent=(c/values[control]-1)*100, published_wtx=ref['wtx'],
        target=ref['wtx_target'], published_number_gain_percent=(c/ref['wtx']-1)*100,
        passed=c>=ref['wtx_target']))
assert all(r['passed'] for r in table)
images = json.loads((native/'images-accepted2.json').read_text())
images.update(json.loads((native/'images-control-native.json').read_text()))
for name, file in [(candidate,'accepted2-server'),(control,'control-native-server')]:
    actual=hashlib.sha256((native/file).read_bytes()).hexdigest()
    assert images[name]['binary_sha256']==actual
assert not subprocess.check_output(['docker','ps','--filter','name=httparena-bench-cnetmod-grpc-','--format','{{.Names}}'],text=True).strip()
result=dict(acceptance_scope='User-selected default framework leader WTX; published RPS thresholds',
    user_authorized_token_reacquisition=True, runtime_source=images[candidate]['source_sha'],
    control_source=images[control]['source_sha'], official_workload_sha='8977be2721a5aba7b320345cddfcd6f6b654e698',
    leaderboard_snapshot_sha=audit['upstream_sha'], comparison=table, all_passed=True,
    rounds=rows, images=images,
    hardware_caveat='Published WTX values use Threadripper 3995WX; local cnetmod/control use i9-14900K WSL2. This is not an official ranking or a same-host cnetmod/WTX measurement.',
    validation_reuse='Same frozen images previously passed official 9/0 validation, 20 Release suites and 20 ASan/UBSan suites. No rebuild or source changes in this run.',
    token_released_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
(out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
manifest={file.relative_to(out).as_posix():hashlib.sha256(file.read_bytes()).hexdigest() for file in out.rglob('*') if file.is_file() and file.name!='manifest.json'}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('rounds','images')},indent=2))
