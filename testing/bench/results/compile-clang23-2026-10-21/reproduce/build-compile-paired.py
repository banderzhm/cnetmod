from pathlib import Path
import subprocess,time,json,resource,threading,os,shutil,sys
r=Path('/root/cnetmod-compile-20261021');w=Path('/mnt/c/Users/25007/.codex/worktrees/c315/cnetmod')
# A script invocation starts only after the diagnostic build has completed.
records=[]
def run(name,cmd):
 start=time.monotonic(); before=resource.getrusage(resource.RUSAGE_CHILDREN); samples=[]; done=threading.Event()
 def monitor():
  while not done.wait(1):
   heavy=[]
   for p in Path('/proc').iterdir():
    if not p.name.isdigit():continue
    try:
     comm=(p/'comm').read_text().strip()
     if comm in {'h2load','server','server-h3','cc1plus','clang++','ninja'}:heavy.append({'pid':int(p.name),'comm':comm})
    except OSError:pass
   samples.append({'elapsed':time.monotonic()-start,'processes':heavy})
 t=threading.Thread(target=monitor);t.start()
 with (r/(name+'.log')).open('w') as out: result=subprocess.run(cmd,stdout=out,stderr=subprocess.STDOUT)
 done.set();t.join();after=resource.getrusage(resource.RUSAGE_CHILDREN)
 rec={'name':name,'command':cmd,'seconds':time.monotonic()-start,'user_seconds':after.ru_utime-before.ru_utime,'system_seconds':after.ru_stime-before.ru_stime,'returncode':result.returncode}
 (r/(name+'-monitor.json')).write_text(json.dumps(samples,indent=2));records.append(rec);(r/'untraced-times.json').write_text(json.dumps(records,indent=2));print(json.dumps(rec),flush=True)
 if result.returncode:sys.exit(result.returncode)
for variant in ['baseline','candidate']:
 args=json.loads((r/(variant+'-configure.json')).read_text());args=[a.replace('-O3 -DNDEBUG -ftime-trace','-O3 -DNDEBUG') for a in args]
 run(variant+'-untraced-configure',args)
 run(variant+'-untraced-full',['cmake','--build',str(r/variant),'--target','cnetmod_core','-j4'])
 run(variant+'-noop',['cmake','--build',str(r/variant),'--target','cnetmod_core','-j4'])
for round_index,order in enumerate([['baseline','candidate'],['candidate','baseline'],['baseline','candidate']],1):
 for variant in order:
  build=(r/variant).resolve()
  assert build.parent==r.resolve() and build.name in {'baseline','candidate'}
  # Remove this experiment's cnetmod outputs. Clearing the Ninja command log
  # below also forces third-party commands to rerun; verify the retained logs.
  for directory in (build/'CMakeFiles').glob('cnetmod*.dir'):
   for p in directory.rglob('*'):
    if p.is_file() and (p.name.endswith(('.o','.pcm','.ddi'))):
     assert p.resolve().is_relative_to(build);p.unlink()
  log=build/'.ninja_log'
  if log.exists(): log.unlink()
  run(f'{variant}-round{round_index}',['cmake','--build',str(build),'--target','cnetmod_core','-j4'])
  shutil.copy2(log,r/f'{variant}-round{round_index}.ninja_log')
print('PAIRED COMPLETE',flush=True)
