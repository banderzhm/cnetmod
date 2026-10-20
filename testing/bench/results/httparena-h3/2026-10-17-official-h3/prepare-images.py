import json, re, shutil, subprocess
from pathlib import Path

root=Path('/var/tmp/cnetmod-h3-official')
def image(name,binary,arguments):
    context=root/name
    (context/'lib').mkdir(parents=True,exist_ok=True)
    shutil.copy2(binary,context/'program')
    dependencies=subprocess.check_output(['ldd',str(binary)],text=True)
    for path in re.findall(r'=> (/\S+)',dependencies):
        shutil.copy2(path,context/'lib'/Path(path).name)
    shutil.copy2('/lib64/ld-linux-x86-64.so.2',context/'loader')
    shutil.copy2(root/'gai.conf',context/'gai.conf')
    (context/'Dockerfile').write_text('FROM scratch\nCOPY program /program\nCOPY lib/ /lib/\nCOPY loader /lib64/ld-linux-x86-64.so.2\nCOPY gai.conf /etc/gai.conf\nENTRYPOINT '+json.dumps(['/lib64/ld-linux-x86-64.so.2','--library-path','/lib','/program']+arguments)+'\n')
    subprocess.run(['docker','build','-t',name,str(context)],check=True,stdout=(root/(name+'-build.log')).open('wb'),stderr=subprocess.STDOUT)
    return subprocess.check_output(['docker','inspect',name,'--format','{{.Id}}'],text=True).strip()
base=image('cnetmod-h3-official-baseline','/tmp/cnetmod-h3-arena-baseline',['/certs/server.crt','/certs/server.key','8443','32'])
opt=image('cnetmod-h3-official-optimized','/tmp/cnetmod-h3-arena-build/testing/bench/arena_h3_server',['/certs/server.crt','/certs/server.key','8443','32'])
static=image('cnetmod-h3-official-static','/tmp/cnetmod-h3-arena-build/testing/bench/arena_h3_server',['/certs/server.crt','/certs/server.key','8443','32','/data/static'])
loadgen=image('cnetmod-h3-official-loadgen','/tmp/cnetmod-h3-loadgen169/src/h2load',[])
agensio=subprocess.check_output(['docker','inspect','cnetmod-h3-official-agensio','--format','{{.Id}}'],text=True).strip()
genhttp=subprocess.check_output(['docker','inspect','httparena-genhttp-ioxide-local','--format','{{.Id}}'],text=True).strip()
data={
 'cnetmod-official-baseline':{'image':base,'language':'C++','tests':['baseline-h3']},
 'cnetmod-official-optimized':{'image':opt,'language':'C++','tests':['baseline-h3']},
 'cnetmod-official-static':{'image':static,'language':'C++','tests':['static-h3']},
 'agensio-official-h3':{'image':agensio,'language':'C++','tests':['baseline-h3','static-h3']},
 'genhttp-ioxide-official-h3':{'image':genhttp,'language':'C#','tests':['baseline-h3','static-h3']}}
(root/'images.json').write_text(json.dumps(data,indent=2))
stage=root/'arena'
stage.mkdir(exist_ok=True)
shutil.copytree('/tmp/cnetmod-http-arena/data',stage/'data',dirs_exist_ok=True)
print('Prepared images and isolated dataset',flush=True)
