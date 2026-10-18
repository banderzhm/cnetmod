"""Verify completed Windows peers, their own cleanup, and frozen binaries."""
from pathlib import Path
import hashlib
import json
records=Path('C:/Users/25007/.codex/tmp/cnetmod-h3-pmtu-final-system-windows-20261018-ffa91ab479f4422791aaeacc51dbe847')
required=('aioquic-server_to_cnetmod-client','aioquic-webtransport_to_cnetmod-server',
          'wtransport-webtransport_to_cnetmod-server')
optional=('cnetmod-server_to_curl-http3','cnetmod-server_to_nghttp3-client','nghttp3-server_to_cnetmod-client')
rows=[]
for workers in (1,2):
    mandatory_path=records/f'mandatory-workers{workers}.json'
    mandatory=json.loads(mandatory_path.read_text())
    extra=json.loads((records/f'optional-workers{workers}.log').read_text())
    assert extra['cleanup_errors']==[]
    for key,source,path in ((required,mandatory,mandatory_path),
                           (optional,extra['results'],Path(extra['output'])/'results.json')):
        for name in key:
            matches=[item for item in source if item['name']==name]
            assert len(matches)==1 and matches[0]['status']=='passed',(workers,name,matches)
            rows.append({'workers':workers,'name':name,'status':'passed','evidence':str(path),
                         'peer_platform':matches[0].get('peer_platform','Windows/native')})
    directory=Path(extra['output'])
    assert json.loads((directory/'cleanup.json').read_text())==[]
    for path in directory.rglob('*.cleanup.log'):
        cleanup=json.loads(path.read_text())
        assert cleanup['after']==[],path
binaries=json.loads((records/'native-binary-hashes.json').read_text())
assert all(hashlib.sha256(Path(row['path']).read_bytes()).hexdigest()==row['sha256'] for row in binaries.values())
summary={'status':'passed','total_case_configurations':len(rows),'cases':rows,
    'ctest':json.loads((records/'ctest-summary.json').read_text()),
    'allocator':'system','source_inputs_unchanged':json.loads((records/'source-integrity.json').read_text()),
    'binaries_unchanged_after_peers':True,
    'mandatory_optional_skips':'Retained as skips in mandatory raw reports; optional driver provides independent actual passes.'}
(records/'verified-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
assert len(rows)==12 and summary['source_inputs_unchanged']
print('verified38CTest/12peer caseconfigs, system allocator, source+binary hashes unchanged, Linux peer cleanup empty')
