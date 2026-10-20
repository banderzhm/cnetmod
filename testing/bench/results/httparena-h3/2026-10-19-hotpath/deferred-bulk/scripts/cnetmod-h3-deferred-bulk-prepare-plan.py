from pathlib import Path
import json,uuid,ast
base=Path(r'C:/Users/25007/.codex/tmp')
nonce=uuid.uuid4().hex
out='/var/tmp/cnetmod-h3-deferred-bulk-candidate-'+nonce
baseline=dict(binary='/var/tmp/cnetmod-pmtu-deadline-5afc506d/baseline-guard/arena_h3_server',binary_sha256='2fa51db8652cf8a0d166d79ab4d36241c9a5c789aa8ed4c0cb5e57eb56bb7d28',source_snapshot='/mnt/c/Users/25007/.codex/tmp/pmtu-adaptive-review-5afc506d/source-guard',allocator_proof='/var/tmp/cnetmod-pmtu-deadline-5afc506d/baseline-guard/allocator-proof.json',proof_build_root='/var/tmp/cnetmod-pmtu-deadline-5afc506d/baseline-guard/allocator-backing')
(base/'cnetmod-h3-deferred-bulk-baseline-variant.json').write_text(json.dumps(baseline,indent=2)+'\n')
plan=dict(candidate_root=out,source_snapshot='/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-deferred-bulk-source-ac465be6',arena='/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena',fault_output='/var/tmp/cnetmod-h3-deferred-bulk-fault-'+nonce,fault_results='/var/tmp/cnetmod-h3-deferred-bulk-fault-results-'+nonce)
(base/'cnetmod-h3-deferred-bulk-execution-plan.json').write_text(json.dumps(plan,indent=2)+'\n')
for filename in ['cnetmod-h3-deferred-bulk-ab.py','cnetmod-h3-deferred-bulk-freeze.py']:
    ast.parse((base/filename).read_text())
print(json.dumps(plan))