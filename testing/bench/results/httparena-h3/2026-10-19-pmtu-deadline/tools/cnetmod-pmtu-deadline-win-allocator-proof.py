"""Read built PE imports/link records after serial validation. No target runs."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys

build=Path('C:/Users/25007/.codex/tmp/cnetmod-h3-windows')
records=Path(sys.argv[1])
dumpbin=Path('D:/runtime/vs/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/dumpbin.exe')
names='test_udp_sendto_on_cancel quic_retire_stream_server quic_pmtu_cc_server quic_pmtu_deadline_server quic_datagram_cc_server test_concurrent_containers test_deadline test_task_group test_wait_group test_task test_udp_batch test_quic_varint test_quic_frame test_quic_packet test_quic_stream test_quic_loss_detection test_quic_congestion test_quic_early_data test_http3_qpack test_http3_frame h3_interop_server h3_interop_client http3_e2e_server http_client_http3_e2e http3_migration_e2e arena_h3_server'.split()
proof=records/'allocator-proof'
proof.mkdir(exist_ok=True)
native=records/'native'
native.mkdir(exist_ok=True)
cache=(build/'CMakeCache.txt').read_text()
assert 'CNETMOD_USE_MIMALLOC:BOOL=OFF' in cache
assert 'CNETMOD_USING_MIMALLOC:BOOL=FALSE' in cache
shutil.copy2(build/'CMakeCache.txt',proof/'CMakeCache.txt')
report={'allocator':'system','executables':{},'link_records':{},
 'scope':'PE imports and actual MSBuild link records; existing unused mimalloc DLLs are not loaded merely by being present.'}
for name in names:
    matches=[p for p in build.rglob(name+'.exe') if 'Release' in p.parts]
    assert len(matches)==1,(name,matches)
    path=matches[0]
    output=subprocess.check_output([str(dumpbin),'/imports',str(path)])
    (proof/(name+'.imports.txt')).write_bytes(output)
    assert b'mimalloc' not in output.lower() and b'mi_version' not in output.lower(),name
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    report['executables'][name]={'path':str(path),'sha256':digest}
    shutil.copy2(path,native/path.name)
for path in build.rglob('link.command.1.tlog'):
    target=next((name for name in names if any(part.startswith(name+'.') for part in path.parts)),None)
    if not target:continue
    raw=path.read_bytes()
    text=raw.decode('utf-16') if raw.startswith((b'\xff\xfe',b'\xfe\xff')) else raw.decode('utf-8')
    (proof/(target+'.link.command.txt')).write_text(text)
    assert 'mimalloc' not in text.lower(),str(path)
    report['link_records'][target]={'path':str(path),'raw_sha256':hashlib.sha256(raw).hexdigest()}
assert set(report['link_records'])==set(names),set(names)-set(report['link_records'])
(proof/'proof.json').write_text(json.dumps(report,indent=2)+'\n')
(records/'native-binary-hashes.json').write_text(json.dumps(report['executables'],indent=2)+'\n')
print('system allocator proof',len(report['executables']),'PE imports and link commands; arena',report['executables']['arena_h3_server']['sha256'])

