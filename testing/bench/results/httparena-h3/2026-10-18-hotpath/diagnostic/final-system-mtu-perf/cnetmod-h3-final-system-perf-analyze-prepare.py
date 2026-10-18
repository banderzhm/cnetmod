"""Derive an offline-only1472/4096 analyzer; do not execute it here."""
from pathlib import Path
import ast
import hashlib
import json

tmp=Path('C:/Users/25007/.codex/tmp')
source=tmp/'cnetmod-h3-system-perf-analyze.py'
text=source.read_text(encoding='utf-8')
old="""os.nice(19)
roots={
    'off':Path('/var/tmp/cnetmod-h3-system-perf-75d3473f92744a87ad2bf125387ea28e'),
    'on':Path('/var/tmp/cnetmod-h3-system-perf-7fff9ece9adc48bfb0e81a0f5c9dc4fe')}
expected_binary='2cb22a32818646fbf63575370adf6a0c48b3ba5df4bbbc3d3002ab2838003e0a'
"""
new="""import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-root',type=Path,required=True)
parser.add_argument('--candidate-root',type=Path,required=True)
parser.add_argument('--binary-sha',required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args()
os.nice(19)
roots={'1472':args.baseline_root,'4096':args.candidate_root}
expected_binary=args.binary_sha.lower()
assert re.fullmatch(r'[0-9a-f]{64}',expected_binary)
assert args.baseline_root.resolve()!=args.candidate_root.resolve()
"""
assert text.count(old)==1
text=text.replace(old,new)
text=text.replace("assert original['valid'] and original['allocator']=='system' and original['gso']==mode", "assert original['valid'] and original['allocator']=='system' and original['gso']=='on'\n    assert original['max_path_mtu']==int(mode) and original['max_udp_payload_size']==8192")
text=text.replace("assert original['perf_record_exit_code']==-2 and", "assert original['perf_record_exit_code'] in (0,-2,130) and")
text=text.replace("    assert lost==0\n    assert 'Total Lost Samples: 0' in (root/'static-perf-self.txt').read_text()", "    assert lost>=0\n    prior_lost=int(re.search(r'Total Lost Samples:\\s+(\\d+)',(root/'static-perf-self.txt').read_text())[1])\n    assert prior_lost==lost")
text=text.replace("out=root/'offline-analysis'", "out=root/'offline-analysis-mtu'\n    assert not out.exists(), 'keep prior analysis intact; choose fresh input copies if retry is necessary'")
text=text.replace("('aes_gcm_enc_update_vaes_avx2','sendto_segmented_batch_now'", "('aes_gcm_enc_update_vaes_avx2','aead_context_for','quic_crypto','on_ack_received','valid_ack_ranges','sendto_segmented_batch_now'")
text=text.replace("lost_samples=lost,total_period=total_period,", "lost_samples=lost,zero_lost_samples=lost==0,total_period=total_period,\n        max_path_mtu=int(mode),receive_payload_limit=8192,gso='on',")
text=text.replace("'Old 05fe mimalloc profile uses another binary and allocator; do not mix it into this matched system off/on comparison.'", "'Older mimalloc and2cb system off/on profiles use other binaries; do not mix them into this same-binary GSO-on MTU comparison.',\n            'Configured ceiling is not the size of every packet; low-concurrency preflight does not measure the timed/profile packet distribution.',\n            'Zero self samples for a named helper can reflect inlining or sampling, not zero cost.'")
start=text.index("assert results['off']['binary_sha256']")
text=text[:start]+"""assert results['1472']['binary_sha256']==results['4096']['binary_sha256']
assert results['1472']['source_snapshot_sha256']==results['4096']['source_snapshot_sha256']
assert results['1472']['source_diff_sha256']==results['4096']['source_diff_sha256']
assert results['1472']['allocator_proof_sha256']==results['4096']['allocator_proof_sha256']
comparison=dict(binary_sha256=expected_binary,allocator='system',gso='on',receive_payload_limit=8192,
    baseline=results['1472'],candidate=results['4096'],
    completed_rate_change_percent=100*(results['4096']['workload']['completed_per_elapsed_second']/results['1472']['workload']['completed_per_elapsed_second']-1),
    cpu_per_completed_change_percent=100*(results['4096']['workload']['cpu_microseconds_per_completed']/results['1472']['workload']['cpu_microseconds_per_completed']-1),
    zero_lost_samples_both=all(r['zero_lost_samples'] for r in results.values()))
assert not args.out.exists(), 'preserve prior comparison'
args.out.write_text(json.dumps(comparison,indent=2)+'\\n')
print('OFFLINE COMPARISON',args.out,flush=True)
"""
ast.parse(text)
target=tmp/'cnetmod-h3-final-system-perf-analyze.py'
target.write_text(text,encoding='utf-8',newline='\n')
result=dict(source=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    prepared=str(target),prepared_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
    ast_parse=True,executed=False)
(tmp/'cnetmod-h3-final-system-perf-analysis-preparation.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
