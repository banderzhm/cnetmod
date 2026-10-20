from pathlib import Path
import subprocess,json,hashlib,uuid
out=Path('/var/tmp/agensio-reference-identity-20261019');out.mkdir(exist_ok=True)
images={'runtime':'sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504','builder':'sha256:e544b11758fa3db89f33a56e6676db5b29506cc5478ad3c84f0e5b37676dae2b'}
report={}
for label,img in images.items():
    inspect=subprocess.check_output(['docker','image','inspect',img]);(out/(label+'-image.json')).write_bytes(inspect)
    name='cnetmod-reference-'+label+'-'+uuid.uuid4().hex
    cid=subprocess.check_output(['docker','create','--name',name,'--network','none','--entrypoint','/bin/true',img],text=True).strip()
    try:
        src='/usr/local/bin/agensio' if label=='runtime' else '/src/build/agensio'
        dest=out/(label+'-agensio')
        subprocess.run(['docker','cp',cid+':'+src,str(dest)],check=True)
        report[label]={'image':img,'binary_sha256':hashlib.sha256(dest.read_bytes()).hexdigest()}
        if label=='builder':
            subprocess.run(['docker','cp',cid+':/src/.git',str(out/'builder-git')],check=True)
            for part in ['src','CMakeLists.txt']:
                subprocess.run(['docker','cp',cid+':/src/'+part,str(out/('builder-'+part))],check=True)
    finally:subprocess.run(['docker','rm',cid],check=True,stdout=subprocess.DEVNULL)
report['same_binary']=report['runtime']['binary_sha256']==report['builder']['binary_sha256']
(out/'identity.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
