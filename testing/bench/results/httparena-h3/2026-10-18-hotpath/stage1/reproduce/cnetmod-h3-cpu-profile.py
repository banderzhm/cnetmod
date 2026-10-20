import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

root = Path('/var/tmp/cnetmod-h3-profile-' + str(int(time.time())))
root.mkdir()
binary = Path('/var/tmp/cnetmod-h3-arena-build/testing/bench/arena_h3_server')
arena = Path('/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena')
h2load = '/var/tmp/cnetmod-h3-loadgen169/h2load'
anchor = 'cnetmod-h3-profile-netns'
image = 'sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
subprocess.run(['docker', 'run', '-d', '--name', anchor, '--network', 'none',
    '--entrypoint', '/bin/sleep', image, 'infinity'], check=True, stdout=subprocess.DEVNULL)
pid = subprocess.check_output(['docker', 'inspect', anchor, '--format', '{{.State.Pid}}'], text=True).strip()
cores = int(subprocess.check_output(['nproc'], text=True))
client_cpus = '32-63,96-127' if cores >= 128 else f'{cores//2}-{cores-1}'
record = dict(binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
              cores=cores, workers=32, client_cpus=client_cpus,
              mode='diagnostic, native identical binary in same isolated loopback; not official ranking',
              measurements=[])

def memory(process):
    text = Path(f'/proc/{process}/smaps_rollup').read_text()
    values = {key: int(value)*1024 for key, value in re.findall(r'^(\w+):\s+(\d+) kB', text, re.M)}
    return dict(rss=values.get('Rss'), pss=values.get('Pss'),
                private=values.get('Private_Clean', 0)+values.get('Private_Dirty', 0))

def cpu(process):
    fields = Path(f'/proc/{process}/stat').read_text().split()
    return (int(fields[13])+int(fields[14]))/os.sysconf('SC_CLK_TCK')

try:
    for profile in ('baseline', 'static'):
        log = (root / f'{profile}-server.log').open('w')
        command = ['nsenter', '-t', pid, '-n', str(binary), str(arena/'certs/server.crt'),
                   str(arena/'certs/server.key'), '8443', '32']
        if profile == 'static':
            command.append(str(arena/'data/static'))
        server = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            prefix = ['nsenter', '-t', pid, '-n']
            ready = prefix+[h2load, '--alpn-list=h3', 'https://localhost:8443/baseline2?a=1&b=1',
                            '-n', '1', '-c', '1', '-m', '1', '-t', '1']
            for _ in range(100):
                if server.poll() is not None:
                    raise RuntimeError('server exited')
                probe = subprocess.run(ready, capture_output=True, text=True, timeout=3)
                if re.search(r'\b1 2xx', probe.stdout):
                    break
                time.sleep(.05)
            else:
                raise RuntimeError('readiness failed')
            workload = prefix+['taskset', '-c', client_cpus, h2load, '--alpn-list=h3']
            if profile == 'baseline':
                workload += ['https://localhost:8443/baseline2?a=1&b=1']
            else:
                workload += ['-i', str(arena/'requests/static-h2-uris.txt'), '-H',
                             'Accept-Encoding: br;q=1, gzip;q=0.8']
            workload += ['-c', '64', '-m', '64', '-t', '64', '-D', '5s']
            for sampled in (False, True):
                name = profile+('-perf' if sampled else '-reference')
                before_cpu = cpu(server.pid)
                before = memory(server.pid)
                perf = None
                if sampled:
                    perf_log = (root/f'{name}-record.log').open('w')
                    perf = subprocess.Popen(['perf', 'record', '-e', 'cpu-clock', '-F', '99',
                        '--call-graph', 'fp', '-p', str(server.pid), '-o', str(root/f'{name}.data'),
                        '--', 'sleep', '6'], stdout=perf_log, stderr=subprocess.STDOUT)
                    time.sleep(.25)
                result = subprocess.run(workload, capture_output=True, text=True, timeout=45)
                (root/f'{name}-h2load.log').write_text(result.stdout+result.stderr)
                duration = float(re.search(r'finished in ([\d.]+)s', result.stdout)[1])
                success = int(re.search(r'(\d+) 2xx', result.stdout)[1])
                measurement = dict(name=name, command=workload, requests_2xx=success,
                    elapsed=duration, valid_rps=success/duration,
                    server_cpu_seconds=cpu(server.pid)-before_cpu, before=before,
                    after_load=memory(server.pid))
                if perf:
                    if perf.wait(timeout=10):
                        raise RuntimeError('perf record failed')
                    perf_log.close()
                    for suffix, options in [('self', ['--no-children', '-g', 'none']),
                                            ('stacks', ['-g', 'graph,0.5,caller'])]:
                        with (root/f'{name}-{suffix}.txt').open('w') as output:
                            subprocess.run(['perf', 'report', '--stdio', '--percent-limit', '.5',
                                '--sort', 'symbol,dso', '-i', str(root/f'{name}.data'), *options],
                                stdout=output, stderr=subprocess.STDOUT, check=True)
                time.sleep(2)
                measurement['after_idle_2s'] = memory(server.pid)
                time.sleep(8)
                measurement['after_idle_10s'] = memory(server.pid)
                record['measurements'].append(measurement)
                (root/'summary.json').write_text(json.dumps(record, indent=2))
                print(name, json.dumps(measurement), flush=True)
        finally:
            server.terminate()
            server.wait(timeout=10)
            log.close()
finally:
    subprocess.run(['docker', 'rm', '-f', anchor], check=True, stdout=subprocess.DEVNULL)
print('RESULTS', root, flush=True)
