import json
import os
from pathlib import Path
import subprocess
import sys

repo = Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod')
records = repo / 'testing/bench/results/httparena-h3/2026-10-17-static-fixes'
os.environ['TMPDIR'] = '/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-linux-tmp'
command = [sys.executable, str(repo / 'testing/http3/h3_acceptance.py'),
    '--server', '/tmp/cnetmod-h3-arena-build/bin/h3_interop_server',
    '--client', '/tmp/cnetmod-h3-arena-build/bin/h3_interop_client',
    '--wtransport-probe', '/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-wtransport-linux-target/release/webtransport_probe',
    '--nghttp3-client-command', '/tmp/cnetmod-h3-ngtcp2-peer-build/examples/osslclient --timeout=1s --no-quic-dump --exit-on-first-stream-close 127.0.0.1 {port} {url}',
    '--nghttp3-server-command', '/tmp/cnetmod-h3-ngtcp2-peer-build/examples/osslserver -q --htdocs {root} 127.0.0.1 {port} {key} {cert}',
    '--results', str(records / 'linux-all-peers-1.json')]
(records / 'linux-all-peers-command.json').write_bytes((json.dumps({'argv':command,'cwd':str(repo),'env':{'TMPDIR':os.environ['TMPDIR']}},indent=2)+'\n').encode())
completed = subprocess.run(command,cwd=repo,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(records / 'linux-all-peers-1.log').write_bytes(completed.stdout)
print(completed.stdout.decode(),flush=True)
if completed.returncode:
    raise SystemExit(completed.returncode)
assert all(case['status']=='passed' for case in json.loads((records / 'linux-all-peers-1.json').read_text()))
