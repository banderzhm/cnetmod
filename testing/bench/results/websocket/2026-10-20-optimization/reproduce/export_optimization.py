import csv, hashlib, json, os, re, shutil, statistics, subprocess, sys
from pathlib import Path
root=Path('/root/cnetmod-ws-20261020')
repo=Path('/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod')
out=repo/'testing/bench/results/websocket/2026-10-20-optimization'
out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(repo/'testing/bench'))
from analyze_ws_official import parse_log
from compare_ws_target import load_entry, compare
rows=[]
manifest=[]
for entry in sorted((root/'optimization/results').iterdir()):
    if not entry.is_dir(): continue
    for path in sorted(entry.glob('*.log')):
        name=re.sub(r'[\x00-\x1f]',lambda m:f'_0x{ord(m[0]):02x}_',path.name)
        target=out/'raw'/entry.name/name
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(path,target)
        record=dict(source=str(path),destination=str(target.relative_to(out)))
        try:
            parsed=parse_log(target)
            for row in parsed: row['source_log']=str(target.relative_to(out))
            rows.extend(parsed)
            record.update(rounds=len(parsed),parse_status='parsed')
        except ValueError as error:
            record.update(parse_status='invalid_or_partial',reason=str(error))
        manifest.append(record)
(out/'rounds.json').write_text(json.dumps(rows,indent=2)+'\n')
(out/'raw-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
groups={}
for row in rows:
    groups.setdefault((row['framework'],row['profile'],row['connections']),[]).append(row)
summary=[]
for key,samples in sorted(groups.items()):
    if sorted(x['round'] for x in samples) != [1,2,3]:continue
    best=max(samples,key=lambda x:x['frames_per_second'])
    summary.append(dict(framework=key[0],profile=key[1],connections=key[2],
        best_frames_per_second=best['frames_per_second'],best_round=best['round'],
        min_frames_per_second=min(x['frames_per_second'] for x in samples),
        median_frames_per_second=statistics.median(x['frames_per_second'] for x in samples),
        best_p99_us=best['latency_us']['p99'],best_cpu_percent=best['cpu_percent'],best_memory=best['memory'],
        total_error_events=sum(sum(x['errors'].values()) for x in samples)))
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
with (out/'summary.csv').open('w',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=list(summary[0]));writer.writeheader();writer.writerows(summary)

original=repo/'testing/bench/results/websocket/2026-10-20-official/raw'
peers={name:load_entry(original/name) for name in ['fulmine','genhttp-ioxide','vanilla-epoll']}
for entry in (out/'raw').iterdir():
    if not entry.name.startswith('cnetmod-'):continue
    try:
        result=compare(load_entry(entry),peers)
        (out/(entry.name+'-target.json')).write_text(json.dumps(result,indent=2)+'\n')
    except ValueError:
        pass
for path in (root/'optimization').glob('*.log'):
    shutil.copyfile(path,out/path.name)
for path in (root/'optimization').glob('*.txt'):
    if path.name.endswith('.compact.txt') or path.name.endswith('-callers.txt'):
        shutil.copyfile(path,out/path.name)
for directory in ['gates','asan','verification']:
    source=root/'optimization'/directory
    if source.exists():
        for path in source.rglob('*'):
            if not path.is_file() or path.suffix not in {'.log','.json'}:continue
            target=out/directory/path.relative_to(source)
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(path,target)
old_images={x['name']:x for x in json.loads((out/'images.json').read_text())} if (out/'images.json').exists() else {}
images=[]
env=dict(os.environ,DOCKER_HOST='unix:///run/cnetmod-ws-20261020.sock')
for path in sorted((root/'optimization/images').iterdir()):
    if not path.is_dir():continue
    binary=path/'bench_ws_server'
    dockerfile=path/'Dockerfile'
    record=dict(old_images.get(path.name,{}),name=path.name)
    if binary.exists(): record['executable_sha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
    if dockerfile.exists(): record['Dockerfile']=dockerfile.read_text()
    result=subprocess.run(['docker','inspect','--type','image',path.name+':20261020'],env=env,capture_output=True,text=True)
    if result.returncode==0:
        data=json.loads(result.stdout)[0]
        record.update(image_id=data['Id'],config=data['Config'])
    images.append(record)
(out/'images.json').write_text(json.dumps(images,indent=2)+'\n')
reproduce=out/'reproduce';reproduce.mkdir(exist_ok=True)
for name in ['build_batch.sh','build_gate_run.sh','stage_batch.py','stage_workers.py','stage_final24.py','official_batch.sh','run_profiles.sh','gate_batch.sh','validate_one.sh','asan.sh','profile_final.sh','perf_final_report.py','export_optimization.py','stage_reviewed16.py','run_reviewed16.sh','resume.sh','cleanup.py','install_rt.sh','finish_verify.sh']:
    source=repo/'build-ws-evidence'/name
    if source.exists():shutil.copyfile(source,reproduce/name)
for name in ['duplex-awaiter-rejected.patch','batch-nodelay.patch']:
    source=repo/'build-ws-evidence'/name
    if source.exists():shutil.copyfile(source,out/name)
(out/'.gitignore').write_text('!*.log\n!raw/**\n!gates/**\n!asan/**\n!reproduce/**\n')
(out/'.gitattributes').write_text('* -text -whitespace\n')
checksums={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file() and p.name!='sha256.json'}
(out/'sha256.json').write_text(json.dumps(checksums,indent=2)+'\n')
print(f'Exported {len(rows)} rounds in {len(manifest)} logs to {out}')
