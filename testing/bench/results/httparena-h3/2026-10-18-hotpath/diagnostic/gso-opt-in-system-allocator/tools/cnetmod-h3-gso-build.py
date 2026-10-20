from pathlib import Path
import json
import subprocess
import sys

if '--namespace' not in sys.argv:
    raise SystemExit(subprocess.run(['unshare','--mount','--propagation','private',sys.executable,str(Path(__file__).resolve()),'--namespace']).returncode)
build=Path('/tmp/cnetmod-h3-arena-build')
build.mkdir(parents=True,exist_ok=True)
subprocess.run(['mount','--bind','/var/tmp/cnetmod-h3-arena-build',str(build)],check=True)
out=Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-gso-validation-1fdab0ae660649bf83f09840e8459d4f')
targets=['arena_h3_server','test_udp_batch','test_quic_stream','test_quic_loss_detection',
         'test_quic_congestion','test_quic_early_data','test_quic_frame','test_quic_packet',
         'test_quic_varint','h3_interop_server','h3_interop_client','http3_e2e_server',
         'http_client_http3_e2e','http3_migration_e2e','quic_retire_stream_server']
command=['/var/tmp/cnetmod-ninja-1.11/bin/ninja','-j2']+targets
(out/'build-command.json').write_text(json.dumps(command,indent=2)+'\n')
with (out/'build.log').open('w') as log:
    result=subprocess.run(command,cwd=build,stdout=log,stderr=subprocess.STDOUT)
(out/'build-result.json').write_text(json.dumps({'returncode':result.returncode})+'\n')
raise SystemExit(result.returncode)
