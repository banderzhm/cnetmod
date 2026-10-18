from pathlib import Path
import ast
base=Path(r'C:/Users/25007/.codex/tmp')
s=(base/'cnetmod-h3-linux-rx-yield-ab.py').read_text()
s=s.replace('RX scheduling-only AB','deferred bulk plaintext-only AB').replace('RX scheduling-only','deferred bulk plaintext-only').replace('Only http3_server.cpp may differ in filtered source metadata.','Only quic_connection.cpp differs in production. An explicitly verified parser trailing-blank-line change is allowed in historical baseline metadata.')
s=s.replace("RX_SOURCE='src/protocol/http/v3/http3_server.cpp'", "RX_SOURCE='src/protocol/quic/quic_connection.cpp'")
s=s.replace("env.update(CNETMOD_QUIC_GSO='1',CNETMOD_QUIC_MAX_PATH_MTU='4096',CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE='8192')", "env.update(CNETMOD_QUIC_GSO='1')")
s=s.replace("cnetmod-h3-linux-rx-yield-ab-", "cnetmod-h3-deferred-bulk-ab-").replace("cnetmod-rx-yield-", "cnetmod-deferred-bulk-").replace("cnetmod.rx-yield.run", "cnetmod.deferred-bulk.run")
s=s.replace("    assert differences==[RX_SOURCE] or set(differences)=={RX_SOURCE},differences", "    verify_source_difference(variants, source_sets, differences)")
s=s.replace("        path_mtu=4096,receive_ceiling=8192,gso=1,configured_server_workers=32,rounds=[],order='per profile AB/BA/AB, baseline then static; 12 serial rounds',", "        configurations=[dict(path_mtu=4096,receive_ceiling=8192),dict(path_mtu=65507,receive_ceiling=65507)],gso=1,configured_server_workers=32,rounds=[],order='per static configuration AB/BA/AB; 12 serial rounds',source_differences_exact=differences,configuration_limit='Ceilings are not proof of every packet reaching the ceiling.',")
s=s.replace("for profile in ('baseline-h3','static-h3'):", "for profile,path_mtu,receive_ceiling in (('static-h3',4096,8192),('static-h3',65507,65507)):")
s=s.replace("        for profile,path_mtu,receive_ceiling in (('static-h3',4096,8192),('static-h3',65507,65507)):\n            for block,order", "        for profile,path_mtu,receive_ceiling in (('static-h3',4096,8192),('static-h3',65507,65507)):\n            env.update(CNETMOD_QUIC_MAX_PATH_MTU=str(path_mtu),CNETMOD_QUIC_MAX_UDP_PAYLOAD_SIZE=str(receive_ceiling))\n            for block,order")
s=s.replace("name=f'{profile}-block{block}-{label}'", "name=f'{profile}-mtu{path_mtu}-recv{receive_ceiling}-block{block}-{label}'")
s=s.replace("row.update(profile=profile,block=block,variant=label,", "row.update(profile=profile,path_mtu=path_mtu,receive_ceiling=receive_ceiling,block=block,variant=label,")
s=s.replace("r['profile']==profile and r['block']==block", "r['profile']==profile and r['path_mtu']==path_mtu and r['block']==block")
s=s.replace("dict(profile=profile,block=block,\n                    completion_gain_percent", "dict(profile=profile,path_mtu=path_mtu,receive_ceiling=receive_ceiling,block=block,\n                    completion_gain_percent")
s=s.replace("print('ROUND VALID',json.dumps(row),flush=True)", "print('ROUND VALID',json.dumps({k:row[k] for k in ('profile','path_mtu','receive_ceiling','block','variant','done','completed_rps','cpu_us_per_complete','p99_ms','failed','errored','timeout')}),flush=True)")
helper='''
def verify_source_difference(variants, source_sets, differences):
    parser='testing/http3/deadline_frame_parser.py'
    assert set(differences)=={RX_SOURCE,parser},differences
    archived=Path(variants['baseline']['source_snapshot'])/'deadline_frame_parser.py'
    current=Path(variants['candidate']['source_snapshot'])/'deadline_frame_parser.py'
    assert sha(archived)==source_sets['baseline'][parser]
    assert sha(current)==source_sets['candidate'][parser]
    assert archived.read_bytes().rstrip(b'\\r\\n')==current.read_bytes().rstrip(b'\\r\\n'), 'historical parser difference is not only trailing line endings'

'''
s=s.replace('\ndef main():', helper+'\ndef main():')
ast.parse(s)
(base/'cnetmod-h3-deferred-bulk-ab.py').write_text(s,encoding='utf-8',newline='\n')
# Reuse the reviewed freeze implementation with one production-difference policy.
f=(base/'cnetmod-h3-linux-rx-freeze-candidate.py').read_text()
f=f.replace('RX-only','deferred-bulk-only').replace("HERE/'cnetmod-h3-linux-rx-yield-ab.py'", "HERE/'cnetmod-h3-deferred-bulk-ab.py'")
f=f.replace('cnetmod-h3-linux-rx-candidate-', 'cnetmod-h3-deferred-bulk-candidate-')
f=f.replace("    assert {n for n,h in sources.items() if baseline_sources[n]!=h}=={ab.RX_SOURCE}", "    differences={n for n,h in sources.items() if baseline_sources[n]!=h}\n    assert differences=={ab.RX_SOURCE,'testing/http3/deadline_frame_parser.py'},differences")
f=f.replace("    shutil.copy2(args.prepared_source_snapshot/'metadata.json',snapshot/'metadata.json')", "    shutil.copy2(args.prepared_source_snapshot/'metadata.json',snapshot/'metadata.json')\n    shutil.copy2(args.prepared_source_snapshot/'deadline_frame_parser.py',snapshot/'deadline_frame_parser.py')")
f=f.replace("    save(args.output/'candidate-variant.json',candidate)", "    ab.verify_source_difference(dict(baseline=baseline,candidate=candidate),dict(baseline=baseline_sources,candidate=sources),differences)\n    save(args.output/'candidate-variant.json',candidate)")
f=f.replace("source_difference=[ab.RX_SOURCE]", "source_difference=sorted(differences)")
f=f.replace("HERE/'cnetmod-h3-linux-rx-quantum.patch'", "HERE/'cnetmod-h3-deferred-plaintext-review-5afc/deferred-plaintext.patch'")
ast.parse(f)
(base/'cnetmod-h3-deferred-bulk-freeze.py').write_text(f,encoding='utf-8',newline='\n')
print('PREPARED AST PASS')