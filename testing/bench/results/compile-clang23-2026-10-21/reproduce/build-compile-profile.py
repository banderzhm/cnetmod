from pathlib import Path
import subprocess, tarfile, io, json, time, sys, os
root=Path('/root/cnetmod-compile-20261021');root.mkdir(exist_ok=True)
win=Path('/mnt/c/Users/25007/.codex/worktrees/c315/cnetmod')
src=root/'source';src.mkdir(exist_ok=True)
if not (src/'CMakeLists.txt').exists():
    archive=(win/'build-compile-source.tar').read_bytes()
    with tarfile.open(fileobj=io.BytesIO(archive)) as f:f.extractall(src,filter='data')
    (src/'3rdparty').symlink_to('/root/cnetmod-grpc-20261020/verified-source/3rdparty',target_is_directory=True)
    (root/'baseline-sha.txt').write_text((win/'build-compile-baseline-sha.txt').read_text(encoding='utf-8-sig'))
mode=sys.argv[1] if len(sys.argv)>1 else 'baseline'
if mode!='baseline': src=root/'candidate-source'
if mode!='baseline': src=root/'candidate-source'
build=root/mode
args=['cmake','-S',str(src),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_CXX_COMPILER=/usr/bin/clang++','-DCMAKE_C_COMPILER=/usr/bin/clang','-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -ftime-trace','-DCNETMOD_USE_MOLD=OFF','-DCNETMOD_USE_MIMALLOC=ON','-DCNETMOD_ENABLE_HTTP=ON','-DCNETMOD_ENABLE_GRPC=ON','-DCNETMOD_ENABLE_POSTGRESQL=ON','-DCNETMOD_ENABLE_MYSQL=ON','-DCNETMOD_ENABLE_REDIS=ON','-DCNETMOD_ENABLE_ORM=OFF','-DCNETMOD_BUILD_TESTS=ON','-DCNETMOD_BUILD_EXAMPLES=OFF','-DCNETMOD_ENABLE_YAML_CONFIGURATION=OFF','-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -Wl,--threads=4','-DCMAKE_JOB_POOLS=compile_link=1','-DCMAKE_JOB_POOL_LINK=compile_link','-DPython3_EXECUTABLE=/opt/cnetmod-ci-venv/bin/python']
(root/(mode+'-configure.json')).write_text(json.dumps(args,indent=2))
with (root/(mode+'-configure.log')).open('w') as log:subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True)
start=time.monotonic()
with (root/(mode+'-build.log')).open('w') as log:r=subprocess.run(['cmake','--build',str(build),'--target','cnetmod_core','-j4'],stdout=log,stderr=subprocess.STDOUT)
(root/(mode+'-time.json')).write_text(json.dumps({'seconds':time.monotonic()-start,'returncode':r.returncode,'target':'cnetmod_core','jobs':4},indent=2))
print(mode,r.returncode,time.monotonic()-start,flush=True)
sys.exit(r.returncode)
