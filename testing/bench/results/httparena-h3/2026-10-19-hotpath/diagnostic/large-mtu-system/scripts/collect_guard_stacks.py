from pathlib import Path
import subprocess,os,json,hashlib
os.nice(19)
roots=[Path('/var/tmp/cnetmod-h3-large-mtu-perf-a4018f36386a48b5a85b0146a90f03ef'),Path('/var/tmp/cnetmod-h3-large-mtu-perf-c6333e93d31b45c689c0146783514018')]
for root in roots:
 out=root/'offline-analysis-guard-mtu'; command=['/usr/sbin/perf','script','-i',str(root/'static-perf.data'),'-F','ip,sym,symoff,dso','--no-demangle']
 (out/'samples-command.json').write_text(json.dumps(command))
 with (out/'samples.txt').open('w') as f:subprocess.run(command,stdout=f,stderr=subprocess.PIPE,check=True,timeout=60)
 with (out/'libc-copy-region.asm').open('w') as f:subprocess.run(['objdump','-d','--start-address=0x176d00','--stop-address=0x176fa0','/usr/lib/libc.so.6'],stdout=f,check=True)
 print(root,'samples_bytes',(out/'samples.txt').stat().st_size,flush=True)
