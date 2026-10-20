"""Serial real receiver/QUIC tests in private mount/network namespaces."""
from pathlib import Path
import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

system='--system' in sys.argv
if '--namespace' not in sys.argv:
    raise SystemExit(subprocess.run(['unshare','--mount','--net','--propagation','private',sys.executable,str(Path(__file__).resolve()),'--namespace']+(['--system'] if system else [])).returncode)
build=Path('/tmp/cnetmod-h3-arena-build')
build.mkdir(parents=True,exist_ok=True)
subprocess.run(['mount','--bind','/var/tmp/cnetmod-h3-arena-build',str(build)],check=True)
subprocess.run(['mount','--bind','/var/tmp/cnetmod-h3-official/gai.conf','/etc/gai.conf'],check=True)
subprocess.run(['ip','link','set','lo','up'],check=True)
out=Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-gso-validation-1fdab0ae660649bf83f09840e8459d4f')
pattern=r'^(test_udp_batch|test_quic_.*|http3_lifecycle|http3_cold_listener_cleanup|http3_outbound_pto_close_restart_[12]|quic_retire_stream_credit_wakeup|quic_retire_stream_notification_lifetime|quic_private_outbound_pto|quic_handshake_confirmation|http3_early_data_recovery|http3_streaming_cancellation|http3_idle_connection_cleanup|http_client_http3_end_to_end|http3_multipath_path_mtu.*)$'
if system:
    out=out/'system-allocator'
    pattern=r'^(test_udp_batch|test_quic_.*|quic_retire_stream_credit_wakeup|quic_retire_stream_notification_lifetime|quic_private_outbound_pto|quic_handshake_confirmation)$'
listing=subprocess.check_output(['ctest','--show-only=json-v1','-R',pattern],cwd=build,text=True)
(out/'directed-tests-list.json').write_text(listing)
cases=json.loads(listing)['tests']
assert len(cases)>=(12 if system else 20)
command=['ctest','--output-on-failure','-j1','--timeout','75','-R',pattern,'--output-junit',str(out/'directed-tests.xml')]
(out/'directed-tests-command.json').write_text(json.dumps(command,indent=2)+'\n')
with (out/'directed-tests.log').open('w') as log:
    result=subprocess.run(command,cwd=build,stdout=log,stderr=subprocess.STDOUT)
tree=ET.parse(out/'directed-tests.xml')
tests=tree.findall('.//testcase')
bad=[test.attrib.get('name') for test in tests if test.find('failure') is not None or test.find('skipped') is not None or test.find('error') is not None]
(out/'directed-tests-result.json').write_text(json.dumps(dict(returncode=result.returncode,selected=len(cases),executed=len(tests),bad=bad),indent=2)+'\n')
assert result.returncode==0 and len(tests)==len(cases) and not bad
print('DIRECTED PASS',len(tests),flush=True)
