from pathlib import Path
import hashlib
import json
import shutil
base = Path('C:/Users/25007/.codex/worktrees/2c3c/cnetmod')
target = base / 'testing/bench/results/httparena-h3/2026-10-18-hotpath/diagnostic/remaining-hotpath-gso-evidence'
target.mkdir(exist_ok=True)
rows = []
for number in (1, 30, 60):
    source = base / f'testing/bench/results/httparena-h3/2026-10-18-hotpath/lifecycle-final/native-final-60-rounds/round-{number}-h2load.log'
    shutil.copy2(source, target / source.name)
    import re
    text = source.read_text()
    udp = re.search(r'UDP datagram: (\d+) sent, (\d+) received', text)
    done = int(re.search(r'requests: \d+ total, \d+ started, (\d+) done', text)[1])
    status = int(re.search(r'status codes: (\d+) 2xx', text)[1])
    body = int(re.search(r'\((\d+)\) data', text)[1])
    assert done == status == 200000
    rows.append(dict(round=number, done=done, status_2xx=status, udp_sent=int(udp[1]), udp_received=int(udp[2]),
        received_per_2xx=int(udp[2])/status, sent_per_2xx=int(udp[1])/status, body_bytes=body, body_bytes_per_2xx=body/status))
(target / 'counts.json').write_text(json.dumps(rows, indent=2) + '\n')
manifest = {p.name:dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in target.iterdir() if p.is_file() and p.name != 'sha256.json'}
(target / 'sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
