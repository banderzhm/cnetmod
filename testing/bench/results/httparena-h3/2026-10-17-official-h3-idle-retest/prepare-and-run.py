import hashlib,json,os,re,shutil,subprocess,threading,time,sys
from pathlib import Path

repo=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod')
revision=sys.argv[1]
root=Path('/var/tmp/cnetmod-h3-retest-idle-'+revision[:8])
root.mkdir(exist_ok=False)
stage=root/'arena'
stage.mkdir(exist_ok=True)
records=stage/'records'
records.mkdir(exist_ok=True)
old=Path('/var/tmp/cnetmod-h3-official')
data=json.loads((old/'images.json').read_text())
data={k:v for k,v in data.items() if k in ('cnetmod-official-optimized','cnetmod-official-static')}
binary=Path('/tmp/cnetmod-h3-arena-build/testing/bench/arena_h3_server')
expected=hashlib.sha256(binary.read_bytes()).hexdigest()
assert expected == json.loads((repo/'testing/bench/results/httparena-h3/2026-10-17-official-h3-retest/manifest.json').read_text())['latest_binaries_sha256']['linux-arena_h3_server']
# Native Git verified the Windows-managed worktree before launch.
def image(name,arguments):
    context=root/name
    (context/'lib').mkdir(parents=True)
    shutil.copy2(binary,context/'program')
    dependencies=subprocess.check_output(['ldd',str(binary)],text=True)
    for path in re.findall(r'=> (/\S+)',dependencies):
        shutil.copy2(path,context/'lib'/Path(path).name)
    shutil.copy2('/lib64/ld-linux-x86-64.so.2',context/'loader')
    shutil.copy2(old/'gai.conf',context/'gai.conf')
    (context/'Dockerfile').write_text('FROM scratch\nCOPY program /program\nCOPY lib/ /lib/\nCOPY loader /lib64/ld-linux-x86-64.so.2\nCOPY gai.conf /etc/gai.conf\nENTRYPOINT '+json.dumps(['/lib64/ld-linux-x86-64.so.2','--library-path','/lib','/program']+arguments)+'\n')
    with (records/(name+'-build.log')).open('wb') as output:
        subprocess.run(['docker','build','-t',name,str(context)],check=True,stdout=output,stderr=subprocess.STDOUT)
    return subprocess.check_output(['docker','inspect',name,'--format','{{.Id}}'],text=True).strip()
data['cnetmod-official-optimized']['image']=image('cnetmod-h3-idle-retest-baseline',['/certs/server.crt','/certs/server.key','8443','32'])
data['cnetmod-official-static']['image']=image('cnetmod-h3-idle-retest-static',['/certs/server.crt','/certs/server.key','8443','32','/data/static'])
(root/'images.json').write_text(json.dumps(data,indent=2))
shutil.copytree(old/'arena/data',stage/'data')
for name,args in [('environment',['uname','-a']),('hardware',['lscpu']),('compiler',['clang++','--version']),('h2load',['/tmp/cnetmod-h3-loadgen169/src/h2load','--version'])]:
    p=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (records/(name+'.txt')).write_bytes(p.stdout)
(records/'revision.json').write_text(json.dumps({'commit':revision,'binary_sha256':expected,'test_token':'explicitly handed over by user for serial official retest','profiles':'two cnetmod profiles after idle-route cleanup fix, three rounds each; all previous 27 rounds retained','resource_sampling':'supplemental /proc RSS once per second; official Docker peaks unchanged'},indent=2))
stop=threading.Event()
def sample():
    with (records/'resource-samples.jsonl').open('w') as output:
        while not stop.is_set():
            p=subprocess.run(['docker','ps','--filter','name=httparena-','--format','{{.ID}} {{.Names}}'],capture_output=True,text=True)
            for line in p.stdout.splitlines():
                cid,name=line.split()
                try:
                    pid=int(subprocess.check_output(['docker','inspect',cid,'--format','{{.State.Pid}}'],text=True))
                    status=Path(f'/proc/{pid}/status').read_text()
                    values={key:int(re.search(r'^'+key+r':\s+(\d+)',status,re.M)[1]) for key in ['VmRSS','VmSize','Threads']}
                    output.write(json.dumps(dict(time=time.time(),container=name,id=cid,pid=pid,**values))+'\n')
                    output.flush()
                except (OSError,subprocess.CalledProcessError,TypeError):
                    pass
            stop.wait(1)
thread=threading.Thread(target=sample,daemon=True)
thread.start()
try:
    command=['unshare','--mount','bash','/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-isolated.sh',
             '/tmp/cnetmod-h3-peer/bin/python',str(repo/'testing/bench/run_official_h3.py'),
             '--official','/tmp/cnetmod-http-arena','--stage',str(stage),
             '--h2load','/tmp/cnetmod-h3-loadgen169/src/h2load','--images',str(root/'images.json')]
    (records/'command.json').write_text(json.dumps(command,indent=2))
    completed=subprocess.run(command)
finally:
    stop.set()
    thread.join(timeout=5)
raise SystemExit(completed.returncode)
