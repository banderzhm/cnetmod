"""Archive completed text evidence only; raw qlogs, ELF and keys stay outside repo."""
from pathlib import Path
import csv
import hashlib
import json
import shutil
import subprocess

repo=Path('C:/Users/25007/.codex/worktrees/2c3c/cnetmod')
tmp=Path('C:/Users/25007/.codex/tmp')
external=Path('//wsl.localhost/archlinux/var/tmp')
ab=external/'cnetmod-h3-mtu-ceiling-ab-48da061b63724a608da432d269675413'
pre=external/'cnetmod-h3-mtu-preflight-d1d75d2b98744840896b101f141765ac'
dest=repo/'testing/bench/results/httparena-h3/2026-10-18-hotpath/diagnostic/mtu-ceiling-system-allocator'
assert not dest.exists()
summary=json.loads((ab/'summary.json').read_text())
preflight=json.loads((pre/'preflight-report.json').read_text())
assert summary['valid'] and preflight['valid'] and len(summary['rounds'])==18
assert summary['cleanup']['returncode']==preflight['cleanup']['returncode']==0
assert summary['binary_sha256']==preflight['binary_sha256']=='5bf46aeeee227934742969972dfa3e4705d5fb87e66289c9109f41d89f353470'
dest.mkdir(parents=True)
for directory,label in [(ab,'ab'),(pre,'preflight')]:
    for source in sorted(directory.rglob('*')):
        if not source.is_file():continue
        if source.suffix not in ('.json','.log','.stdout','.stderr','.diff'):continue
        relative=source.relative_to(directory)
        target=dest/label/relative;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
for source in [tmp/'cnetmod-h3-mtu-preflight.py',tmp/'cnetmod-h3-mtu-ceiling-ab.py',tmp/'cnetmod-h3-mtu-ceiling-analyze.py',Path(__file__),tmp/'cnetmod-h3-mtu-env-prepared/arena-mtu-env.patch',tmp/'cnetmod-h3-mtu-env-prepared/metadata.json',tmp/'cnetmod-h3-mtu-preflight-prepared/run-commands.txt']:
    target=dest/'tools'/source.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
analysis=json.loads((ab/'comparison.json').read_text())
raw=[]
for entry in preflight['measurements']:
    raw.extend(dict(ceiling=entry['ceiling'],**record) for record in entry['raw_evidence'] if record['path'].endswith('.sqlog'))
(dest/'external-qlog-manifest.json').write_text(json.dumps(dict(scope='raw qlogs retained externally; no raw qlog or key copied to repository',files=raw),indent=2)+'\n')
fields=['sequence','profile','block','ceiling','done','status_2xx','header_delta','elapsed_seconds','completed_rps','printed_rps','cpu_us_per_completed','p99_ms','rss_before_kib','rss_after_kib']
rows=[]
for sequence,r in enumerate(summary['rounds'],1):
    rows.append(dict(sequence=sequence,profile=r['profile'],block=r['block'],ceiling=r['variant'],done=r['done'],status_2xx=r['status_2xx'],header_delta=r['header_completion_delta'],elapsed_seconds=r['elapsed_seconds'],completed_rps=r['completed_per_elapsed_second'],printed_rps=r['printed_rps'],cpu_us_per_completed=r['server_cpu_microseconds_per_completed'],p99_ms=r['p99_ms'],rss_before_kib=r['before']['memory_kib']['Rss'],rss_after_kib=r['after']['memory_kib']['Rss']))
with (dest/'rounds.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(rows)
lines=['# All18 timed rounds','', 'Every round has zero failed/errored/timeout/non2xx; done==succeeded>0 and2xx>=done. RSS is a fresh-server live endpoint, not retention.','', '| # | Profile | Block | Ceiling | Done | 2xx-done | Completed/s | Printed/s | CPU us/completed | p99 ms | RSS KiB before→after |','|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---|']
for r in rows:
    lines.append(f"| {r['sequence']} | {r['profile']} | {r['block']} | {r['ceiling']} | {r['done']} | {r['header_delta']} | {r['completed_rps']:.0f} | {r['printed_rps']:.1f} | {r['cpu_us_per_completed']:.3f} | {r['p99_ms']:.2f} | {r['rss_before_kib']}→{r['rss_after_kib']} |")
(dest/'rounds.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
lines=['# MTU ceilings with system allocator — diagnostic comparison','', 'All three low-concurrency qlog preflights and all18 uninstrumented timed rounds pass. The same frozen ELF5bf46a... and source snapshot are used throughout; receive limit8192, GSO1 and system allocator stay fixed. Only send discovery ceiling1472/4096/8192 varies. Default adapter limits remain1472. This is a native matched-block diagnostic, not an official framework ranking, general PMTU reliability proof or long-retention test.','', 'The low-concurrency preflight uses1client/1stream/1thread,4096 fixed requests and32serverworkers. It verifies peer receive limit65527 and server receive limit8192, complete request/response STREAM byte ranges and FINs, and received application1RTT packet lengths. Retry/VN are recognized as non-PN packets; normal packets still require frames and PN. The preflight excludes control/probe-only packets from response counts.','', '| Ceiling | Max response QUIC packet bytes | Distinct response packets | Packets/completed |','|---:|---:|---:|---:|']
for r in preflight['measurements']:lines.append(f"| {r['ceiling']} | {r['max_response_stream_quic_packet_bytes']} | {r['response_stream_packet_count']} | {r['response_stream_packets_per_completed']:.5f} |")
lines+=['', 'These are qlog raw.length QUIC packet bytes, not an independent wire capture. A short-header1RTT packet is final in a UDP datagram; the ordinary application packetizer sends one per original UDP segment. GRO/GSO message sizes, syscalls and wire packet counts are different denominators. Per-stream packet memberships are stored separately because a multi-stream packet can otherwise be counted twice. No syscall count is invented. Configured ceiling is not the size of every packet, and this low-concurrency evidence does not measure the full timed-run size distribution.','', 'The timed runs keep the original static URI/encoding and32workers,64clients,64streams,64threads,5seconds, client CPU16–31, private Docker-anchor loopback and IPv4-first gai wrapper. Three blocks/profile balance1472/4096/8192 ordering. Full source/ELF hashes are verified outside load. No qlog, shim, LD_PRELOAD or tracing is enabled during timing. Header-only2xx are not counted as completed throughput.','', 'Median matched changes versus1472 (each percentage is computed within its own block, then the three values are summarized):','', '| Profile | Ceiling | Completion gain per block % | Median gain % | CPU change per block % | Median CPU change % | p99 change per block % |','|---|---:|---|---:|---|---:|---|']
for profile,p in analysis['profiles'].items():
    for ceiling,c in p['comparisons_to1472'].items():
        gains=', '.join(f"{v['completed_per_elapsed_gain_percent']:+.2f}" for v in c['pairs'])
        cpu=', '.join(f"{v['cpu_per_completed_change_percent']:+.2f}" for v in c['pairs'])
        tail=', '.join(f"{v['p99_change_percent']:+.2f}" for v in c['pairs'])
        lines.append(f"| {profile} | {ceiling} | {gains} | {c['median_paired_completed_gain_percent']:+.2f} | {cpu} | {c['median_paired_cpu_change_percent']:+.2f} | {tail} |")
lines+=['', 'See [all18 rounds](rounds.md), CSV and ab/comparison.json for completed/elapsed versus printed RPS, every CPU/p99/RSS measurement and all paired changes. Fresh-server RSS is not evidence that memory returns after idle. Static improvement versus1472 does not imply the largest ceiling wins; compare4096 and8192 across all blocks. Baseline results must be judged separately. The chosen controlled-path setting still needs the coordinator\'s long retention and official/framework validation.','', 'All raw qlogs and candidate ELF remain in the external directories named by the manifests; no ELF, PCM, raw qlog or key is archived here. The observer C draft was not used or compiled. JSON-SEQ byte/record, file64MiB, total1GiB and90second limits fail closed. No prior preflight failure occurred in this run; larger application sizes were established at the original4096 requests without altering probe step/interval. Repository protected interoperability JSON was not touched.','', 'Loopback supports these controlled datagram sizes. This does not establish unfragmented PMTU reliability on external routes; existing DF/fragmentation and black-hole behavior require separate review. Earlier mimalloc performance records cannot be combined with this system-allocator comparison.']
(dest/'README.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
manifest=[]
for path in sorted(dest.rglob('*')):
    if path.is_file():
        assert path.suffix not in ('.sqlog','.pcm','.o','.a','.so','.pem','.key')
        manifest.append(dict(path=path.relative_to(dest).as_posix(),bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
(dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(dict(archive=str(dest),files=len(manifest),bytes=sum(r['bytes'] for r in manifest),manifest_sha256=hashlib.sha256((dest/'manifest.json').read_bytes()).hexdigest())))
