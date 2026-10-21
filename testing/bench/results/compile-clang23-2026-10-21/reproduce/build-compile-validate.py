from pathlib import Path
import shutil,subprocess,json,time,sys,re
r=Path('/root/cnetmod-compile-20261021');w=Path('/mnt/c/Users/25007/.codex/worktrees/c315/cnetmod');s=r/'candidate-source';b=r/'candidate'
for name in ['testing/tests/CMakeLists.txt','testing/tests/test_converter.cpp']:(s/name).write_bytes((w/name).read_bytes().replace(b'\r\n',b'\n'))
targets=['test_converter','test_charconv','test_task','test_task_group','test_stream_write','test_channel','test_mutex','test_shared_mutex','test_wait_group','test_semaphore','test_http_parser','test_http2_hpack','test_http2_stream','test_http_compress','test_grpc','test_grpc_proto','test_redis','test_redis_template','test_redis_tls','test_mysql_pool','test_postgresql_transport','test_application_integrations','test_json_template']
commands=[['cmake','--build',str(b),'--target',*targets,'-j4'],['ctest','--test-dir',str(b),'--output-on-failure','--timeout','90','-j1','-R','^('+'|'.join(map(re.escape,targets))+')$','--output-junit',str(r/'validation-junit.xml')]]
records=[]
for stage,cmd in zip(['validation-build','validation-tests'],commands):
 start=time.monotonic()
 with (r/(stage+'.log')).open('w') as log: p=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 records.append({'stage':stage,'command':cmd,'seconds':time.monotonic()-start,'returncode':p.returncode});(r/'validation.json').write_text(json.dumps({'targets':targets,'stages':records},indent=2));print(stage,p.returncode,flush=True)
 if p.returncode:sys.exit(p.returncode)
