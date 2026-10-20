"""Read completed native sync A/B evidence; never launch a build or workload."""
import argparse
import hashlib
import json
import re
import statistics
from pathlib import Path


def analyse(directory):
    summary_path = directory / 'summary.json'
    report = json.loads(summary_path.read_text())
    rows = report['rounds']
    assert report.get('valid') is True, 'A/B runner has not validated the complete run'
    expected = {(p, n, v) for p in ('baseline-h3', 'static-h3')
                for n in (1, 2, 3) for v in ('before', 'after')}
    assert len(rows) == 12
    assert {(r['profile'], r['pair'], r['variant']) for r in rows} == expected
    enriched = {}
    for row in rows:
        key = (row['profile'], row['pair'], row['variant'])
        assert row['done'] == row['succeeded'] and row['done'] > 0
        assert row['status_2xx'] >= row['done']
        assert row['elapsed_seconds'] > 0
        name = f'{key[0]}-{key[1]}-{key[2]}'
        log = (directory / (name + '-h2load.log')).read_text()
        counts = re.search(r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout', log)
        codes = re.search(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx', log)
        assert counts and codes, name
        assert int(counts[3]) == row['done'] and int(counts[4]) == row['succeeded'], name
        assert all(int(counts[i]) == 0 for i in (5, 6, 7)), name
        assert int(codes[1]) == row['status_2xx'] and all(int(codes[i]) == 0 for i in (2, 3, 4)), name
        assert abs(row['completed_per_elapsed_second'] - row['done'] / row['elapsed_seconds']) < .001
        smaps = (directory / (name + '-smaps.txt')).read_text()
        memory = {m[1]: int(m[2]) for m in re.finditer(r'^(Rss|Pss|Private_Clean|Private_Dirty):\s+(\d+) kB$', smaps, re.M)}
        assert 'Rss' in memory
        header = re.search(r'^\s*min\s+max\s+median\s+p95\s+p99\s+mean\s+sd\s+\+/-\s*sd\s*$', log, re.M)
        latency = re.search(r'^request\s*:\s+(.+)$', log, re.M)
        assert header and latency, (name, 'expected pinned h2load percentile table')
        columns = latency[1].split()
        assert len(columns) == 8
        p99 = columns[4]
        p99_match = re.fullmatch(r'([\d.]+)(us|ms|s)', p99)
        assert p99_match, (name, p99)
        p99_seconds = float(p99_match[1]) * {'us': .000001, 'ms': .001, 's': 1}[p99_match[2]]
        enriched[key] = dict(row, memory_kib=memory,
            request_latency_summary=latency[1] if latency else None,
            p99=p99, p99_seconds=p99_seconds)
    output = {'scope': 'Native paired A/B only; not an official arena ranking.',
              'valid': True, 'source_summary_sha256': hashlib.sha256(summary_path.read_bytes()).hexdigest(),
              'binary_sha256': report.get('binary_sha256'), 'profiles': {}}
    rate_fields = ('completed_per_elapsed_second', 'printed_rps', 'status_2xx_per_elapsed_second')
    for profile in ('baseline-h3', 'static-h3'):
        pairs = []
        for number in (1, 2, 3):
            before = enriched[(profile, number, 'before')]
            after = enriched[(profile, number, 'after')]
            pairs.append({'pair': number, 'order': 'AB' if number != 2 else 'BA',
                'before': before, 'after': after,
                'gain_percent': {f: (after[f] / before[f] - 1) * 100 for f in rate_fields},
                'p99_delta_percent': (after['p99_seconds'] / before['p99_seconds'] - 1) * 100,
                'rss_delta_kib': after['memory_kib']['Rss'] - before['memory_kib']['Rss'],
                'rss_delta_percent': (after['memory_kib']['Rss'] / before['memory_kib']['Rss'] - 1) * 100})
        output['profiles'][profile] = {'pairs': pairs,
            'median_paired_gain_percent': {f: statistics.median(p['gain_percent'][f] for p in pairs) for f in rate_fields},
            'variant_median': {v: {f: statistics.median(p[v][f] for p in pairs) for f in rate_fields} for v in ('before', 'after')},
            'median_rss_kib': {v: statistics.median(p[v]['memory_kib']['Rss'] for p in pairs) for v in ('before', 'after')},
            'p99_available_all_rounds': all(p[v]['p99'] is not None for p in pairs for v in ('before', 'after'))}
    output['limitations'] = ['Rates use rounded elapsed time printed by h2load.',
        'Timed runs may have more 2xx headers than complete responses; header_completion_delta is retained.',
        'RSS is one live snapshot after load, not an idle retention or leak result.',
        'p99 is read from the pinned h2load percentile table, not inferred from mean or max.',
        'No wire packet, syscall, allocation, or official cross-server ranking is inferred.']
    return output

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(analyse(args.directory), indent=2)
    if args.output:
        args.output.write_text(result + '\n')
    print(result)

