import concurrent.futures
import datetime
import hashlib
import json
import pathlib
import urllib.request

root = pathlib.Path.cwd()
out = root / 'build-grpc-leaderboard-audit'
out.mkdir(exist_ok=True)
sha = '81963a7f99ef2eb0fe1b142de1f80232f88d3dab'
base = f'https://raw.githubusercontent.com/MDA2AV/HttpArena/{sha}/'

def fetch(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'cnetmod-leaderboard-audit'})
    with urllib.request.urlopen(req, timeout=45) as response:
        return response.read()

tree = json.loads(fetch(f'https://api.github.com/repos/MDA2AV/HttpArena/git/trees/{sha}?recursive=1'))
paths = [x['path'] for x in tree['tree'] if x['path'].startswith('site/data/results/') and x['path'].endswith('.json')]

def inspect(path):
    raw = fetch(base + path)
    data = json.loads(raw)
    rows = {k: v for k, v in data['results'].items() if k.startswith('unary-grpc-')}
    return path, raw, rows

entries = {}
manifest = {}
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    for path, raw, rows in pool.map(inspect, paths):
        if rows:
            name = pathlib.PurePosixPath(path).name
            (out / name).write_bytes(raw)
            manifest[name] = hashlib.sha256(raw).hexdigest()
            entries[name[:-5]] = rows

summary = json.loads((root / 'testing/bench/results/grpc-official-2026-10-20/final-summary.json').read_text())
comparison = []
for row in summary['comparison']:
    key = f"{row['profile']}-{row['connections']}"
    leaders = sorted(((data[key]['rps'], name) for name, data in entries.items() if key in data), reverse=True)
    wtx = entries['wtx'][key]['rps']
    fastest, name = leaders[0]
    candidate = row['candidate']
    comparison.append(dict(profile=key, candidate=candidate, wtx=wtx,
        wtx_target=(wtx * 11 + 9)//10, wtx_gain_percent=(candidate/wtx-1)*100,
        fastest_entry=name, fastest=fastest, all_categories_target=(fastest*11+9)//10,
        all_categories_gain_percent=(candidate/fastest-1)*100,
        ranked_entries=[dict(entry=n,rps=r) for r,n in leaders]))
result = dict(checked_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
    upstream_sha=sha, scanned_result_files=len(paths), grpc_entries=len(entries),
    runtime_sha=summary['source_sha'], comparison=comparison,
    interpretation='Cross-host published-number comparison only; no same-host or official-run acceptance claim.',
    source_sha256=manifest)
(out / 'audit.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))
