from pathlib import Path
import json,shutil,gzip,hashlib,subprocess,os
os.nice(19)
repo=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod')
out=repo/'testing/bench/results/httparena-h3/2026-10-19-hotpath/diagnostic/large-mtu-system'
assert not (out/'manifest.json').exists();out.mkdir(parents=True,exist_ok=True)
pre=Path('/var/tmp/cnetmod-h3-large-mtu-preflight-feeddffd59fd4befaaf905b17ced73f7')
ab=Path('/var/tmp/cnetmod-h3-large-mtu-ab-d411da495070401985b365d4e8e26e52')
profiles={'4096':Path('/var/tmp/cnetmod-h3-large-mtu-perf-a4018f36386a48b5a85b0146a90f03ef'),'65507':Path('/var/tmp/cnetmod-h3-large-mtu-perf-c6333e93d31b45c689c0146783514018')}
external=[]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def copy(src,relative):
 data=src.read_bytes();assert not data.startswith(b'\x7fELF') and not data.startswith(b'-----BEGIN PRIVATE KEY-----') and not data.startswith(b'-----BEGIN CERTIFICATE-----')
 dest=out/relative;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
def record(p):external.append(dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)))
for p in sorted(ab.iterdir()):
 if p.is_file() and p.name!='arena_h3_server':copy(p,Path('old-interval-overwrite/timed')/p.name)
record(ab/'arena_h3_server')
for p in sorted(ab.with_name(ab.name+'-analysis').iterdir()):copy(p,Path('old-interval-overwrite/analysis')/p.name)
for name in ('preflight-report.json','runner.py','large_mtu_qlog.py','large_mtu_ab.py','source-metadata.json','source.diff','allocator-proof.json'):
 copy(pre/name,Path('old-interval-overwrite/preflight')/name)
for v in (16384,32768,65507):
 for p in sorted((pre/str(v)).iterdir()):
  if p.is_file():
   if p.name.endswith('.sqlog') or 'keylog' in p.name:record(p)
   elif p.name in ('analysis.json','commands.json','h2load.stdout','h2load.stderr','server.log'):copy(p,Path('old-interval-overwrite/preflight')/str(v)/p.name)
for p in sorted(pre.with_name(pre.name+'-timeline-audit').iterdir()):copy(p,Path('old-interval-overwrite/timeline')/p.name)
copy(Path('/var/tmp/cnetmod-h3-large-mtu-guard-perf-comparison.json'),'guard-perf/comparison.json')
for mode,root in profiles.items():
 dest=Path('guard-perf')/mode
 for name in ('summary.json','runner.py','supplied-source-metadata.json','actual-dirty-source.patch','allocator-proof.json','server.log','perf-record.log','readiness.log','h2load.log','static-perf-header.txt'):copy(root/name,dest/name)
 for name in ('static-perf-self.txt','static-perf-stacks.txt'):
  target=out/dest/(name+'.gz')
  with target.open('wb') as f:
   with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as z:z.write((root/name).read_bytes())
 for p in sorted((root/'offline-analysis-guard-mtu').iterdir()):
  if p.name not in ('samples.txt','self-counts.txt'):copy(p,dest/'offline-analysis'/p.name)
 for name in ('arena_h3_server','static-perf.data'):record(root/name)
 record(root/'offline-analysis-guard-mtu/samples.txt')
commands={};root=profiles['65507'];binary=root/'arena_h3_server'
for label,start,end in [('packer-append-call',0x30bfaf,0x30c020),('packer-zero-fill-call',0x30c710,0x30c7a0),('apply-boundary-copy',0x312460,0x312540)]:
 command=['objdump','-d','-C','--start-address='+hex(start),'--stop-address='+hex(end),str(binary)]
 target=out/'guard-perf/instructions'/f'{label}.asm';target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(subprocess.check_output(command));commands[label]=command
command=['addr2line','-f','-C','-i','-e',str(binary),'0x30bff7','0x30c75e','0x3124c2','0x346740']
(out/'guard-perf/instructions/caller-source-lines.txt').write_bytes(subprocess.check_output(command));commands['caller_source']=command
buildids=subprocess.check_output(['/usr/sbin/perf','buildid-list','-i',str(root/'static-perf.data')],text=True)
libcnotes=subprocess.check_output(['readelf','-n','/usr/lib/libc.so.6'],text=True)
(out/'guard-perf/instructions/libc-identity.txt').write_text('Perf recorded libc build ID:\n'+''.join(line+'\n' for line in buildids.splitlines() if '/usr/lib/libc.so.6' in line)+'\nDisassembled libc notes:\n'+libcnotes+'\nSHA256 '+sha(Path('/usr/lib/libc.so.6'))+'\n')
(out/'guard-perf/instructions/commands.json').write_text(json.dumps(commands,indent=2))
prepared=Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-large-mtu-perf-prepared')
for name in ('large_mtu_perf.py','analyze_guard_profiles.py','collect_guard_stacks.py','attribute_guard_copy.py','copy_secondary_callers.py','archive_diagnostics.py','README.md','preparation.json'):copy(prepared/name,Path('scripts')/name)
previous=Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-large-mtu-prepared')
for name in ('fixture-check-repair.json','large_mtu_ab.before-fixture-fix.py','analyze_completed.py','probe-timeline-audit.py'):copy(previous/name,Path('scripts/old-diagnostic')/name)
(out/'external-input-manifest.json').write_text(json.dumps(dict(retained_external_only=external,scope='No ELF, raw qlog, raw perf, TLS keys or certificates are archived.'),indent=2))
(out/'coordinator-execution-note.md').write_text('''# Execution tool evidence

Preflight exec session53439 returned exit0 and all3 converged. Initial timed exec session27076 failed its fixture symlink assertion before creating an output directory or starting a server/load. All fixture hashes independently matched. The narrow lexical-path correction rejects absolute/parent traversal while permitting existing certs/requests symlinks; original parser/report hashes and driver are retained. Corrected session58734 returned exit0, RESULTS validTrue after9 rounds and finally cleanup/source checks.

Guard profiles sessions69880 and94282 each returned exit0 with PROFILE LOAD DONE0 and PROFILE RESULTS validTrue. Both summaries record own SIGINT exit-2, lost0 and Docker cleanup0. Server exit-15 is requested cleanup. These are coordinator tool observations; no separate full console was saved or fabricated. Historical metadata/head/diff remain unchanged despite later commits. No additional load was run for this archive.
''')
print(out,flush=True)
