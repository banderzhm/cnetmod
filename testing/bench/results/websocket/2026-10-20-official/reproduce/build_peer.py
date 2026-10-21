import difflib, os, shutil, subprocess, sys
from pathlib import Path
root=Path('/root/cnetmod-ws-20261020')
name=sys.argv[1]
original=root/'HttpArena/frameworks'/name
context=root/'peer-build'/name
shutil.copytree(original,context,dirs_exist_ok=True)
source=(original/'Dockerfile').read_text()
modified=source
if '--https-apt' in sys.argv:
    shutil.copyfile('/etc/ssl/certs/ca-certificates.crt',context/'ws-build-ca-bundle.crt')
    modified=modified.replace('FROM ubuntu:24.04 AS build',
        'FROM ubuntu:24.04 AS build\nCOPY ws-build-ca-bundle.crt /etc/ssl/certs/ca-certificates.crt')
    modified=modified.replace('FROM ubuntu:24.04\n',
        'FROM ubuntu:24.04\nCOPY ws-build-ca-bundle.crt /etc/ssl/certs/ca-certificates.crt\n')
    modified=modified.replace('RUN apt-get update',
        'RUN sed -i "s|http://|https://|g" /etc/apt/sources.list.d/ubuntu.sources && apt-get update')
if name=='genhttp-ioxide':
    modified=modified.replace('dotnet restore','dotnet restore --disable-parallel').replace('dotnet publish genhttp.csproj','dotnet publish genhttp.csproj -m:4')
elif name=='wtx-ws':
    modified=modified.replace('cargo build --release','cargo build --release -j4')
elif name=='zix-ws':
    modified=modified.replace('zig build \\', 'zig build -j4 \\')
elif name=='vanilla-epoll':
    modified=modified.replace('make local=1','make -j4 local=1').replace('-j"$(nproc)"','-j4')
elif name=='fulmine':
    modified=modified.replace('RUN npm install --omit=dev','ENV npm_config_jobs=4\nRUN npm install --omit=dev')
(context/'Dockerfile').write_text(modified)
(context/'build-parallelism.patch').write_text(''.join(difflib.unified_diff(source.splitlines(True),modified.splitlines(True),fromfile='official/Dockerfile',tofile='local-build/Dockerfile')))
image='cnetmod-ws-peer-'+name+':20261020'
with (root/('build-peer-'+name+'.log')).open('w') as log:
    args=['docker','build','--network','host','-t',image]
    if '--direct' in sys.argv:
        for key in ['http_proxy','HTTP_PROXY','https_proxy','HTTPS_PROXY','all_proxy','ALL_PROXY']:
            args.extend(['--build-arg',key+'='])
    args.append(str(context))
    subprocess.run(args,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=600)
env=dict(os.environ, DOCKER_HOST='unix:///run/cnetmod-ws-20261020.sock')
with (root/('image-'+name+'.tar')).open('wb') as archive:
    subprocess.run(['docker','save',image],stdout=archive,check=True)
subprocess.run(['docker','load','-i',str(root/('image-'+name+'.tar'))],env=env,check=True)
subprocess.run(['docker','tag',image,'httparena-'+name],env=env,check=True)
(original/'build.sh').write_text('#!/bin/bash\nset -euo pipefail\ndocker tag '+image+' httparena-'+name+'\n')
(original/'build.sh').chmod(0o755)
print('READY',name,flush=True)
