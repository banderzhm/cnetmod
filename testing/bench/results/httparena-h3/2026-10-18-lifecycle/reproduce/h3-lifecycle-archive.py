import argparse
import json
import re
import shutil
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('source', type=Path)
parser.add_argument('destination', type=Path)
args = parser.parse_args()
source = args.source / 'arena'
out = args.destination
shutil.copytree(source / 'records', out / 'records', dirs_exist_ok=True)
shutil.copytree(source / 'results', out / 'official-selected', dirs_exist_ok=True)
images = json.loads((source / 'records/images.json').read_text())
summary = {}
for name, item in images.items():
    for profile in item['tests']:
        log = (source / 'records' / f'{name}-{profile}.log').read_text(errors='replace')
        rounds = []
        for block in re.split(r'\[run \d+/\d+\]', log)[1:]:
            requests = re.search(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout', block)
            status = re.search(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx', block)
            finish = re.search(r'finished in ([\d.]+)(\w+), ([\d.]+) req/s', block)
            stats = re.search(r'\[info\] CPU ([\d.]+)% \| Mem (\S+)', block)
            row = {'round': len(rounds) + 1}
            if requests:
                row.update(zip(('total', 'started', 'done', 'succeeded', 'failed', 'errored', 'timeout'), map(int, requests.groups())))
            if status:
                row['status'] = dict(zip(('2xx', '3xx', '4xx', '5xx'), map(int, status.groups())))
            if finish:
                row['elapsed_seconds'] = float(finish[1]) * {'s': 1, 'ms': .001}[finish[2]]
                row['headline_rps'] = float(finish[3])
                if status:
                    row['valid_rps_from_printed_elapsed'] = int(status[1]) / row['elapsed_seconds']
            if stats:
                row['cpu_percent'], row['memory_peak'] = float(stats[1]), stats[2]
            rounds.append(row)
        selected = source / 'results' / profile / '64' / (name + '.json')
        summary[name + '/' + profile] = {'rounds': rounds,
            'official_selected': json.loads(selected.read_text()) if selected.exists() else None}
(out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
