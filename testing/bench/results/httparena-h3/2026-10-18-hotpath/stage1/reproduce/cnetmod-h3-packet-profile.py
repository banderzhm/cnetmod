import collections
import json
from pathlib import Path
import re
import struct
import subprocess
import time

root = Path('/var/tmp/cnetmod-h3-packets-' + str(int(time.time())))
root.mkdir()
arena = Path('/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena')
anchor = 'cnetmod-h3-packet-netns'
image = 'sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504'
h2load = '/var/tmp/cnetmod-h3-loadgen169/h2load'
subprocess.run(['docker', 'run', '-d', '--name', anchor, '--network', 'none',
    '--entrypoint', '/bin/sleep', image, 'infinity'], check=True, stdout=subprocess.DEVNULL)
pid = subprocess.check_output(['docker', 'inspect', anchor, '--format', '{{.State.Pid}}'], text=True).strip()
prefix = ['nsenter', '-t', pid, '-n']
results = []

def capture_summary(path):
    counts = collections.Counter()
    inconsistent = 0
    with path.open('rb') as f:
        header = f.read(24)
        endian = '<' if header[:4] == b'\xd4\xc3\xb2\xa1' else '>'
        link = struct.unpack(endian+'I', header[20:24])[0]
        assert link == 1, link
        while record := f.read(16):
            _, _, size, original = struct.unpack(endian+'IIII', record)
            data = f.read(size)
            kind = struct.unpack('!H', data[12:14])[0]
            if kind == 0x0800:
                start = 14+(data[14]&15)*4
                ip_bytes = struct.unpack('!H', data[16:18])[0]
                ip_overhead = start-14
            elif kind == 0x86dd:
                start = 54
                ip_bytes = 40+struct.unpack('!H', data[18:20])[0]
                ip_overhead = 40
            else:
                continue
            udp_bytes = struct.unpack('!H', data[start+4:start+6])[0]
            counts[udp_bytes-8] += 1
            inconsistent += ip_bytes != ip_overhead+udp_bytes
    values = sorted(counts.elements())
    return dict(datagrams=len(values), udp_payload_mean=sum(values)/len(values),
        minimum=values[0], median=values[len(values)//2], p95=values[int(len(values)*.95)],
        maximum=values[-1], over_1472=sum(v for k,v in counts.items() if k>1472),
        ip_udp_length_mismatch=inconsistent, most_common=counts.most_common(12))

try:
    for framework in ('agensio',):
        for profile in ('baseline', 'static'):
            name = framework+'-'+profile
            server_log = (root/f'{name}-server.log').open('w')
            container = None
            if framework == 'cnetmod':
                command = prefix+['/var/tmp/cnetmod-h3-arena-build/testing/bench/arena_h3_server',
                    str(arena/'certs/server.crt'), str(arena/'certs/server.key'), '8443', '32']
                if profile == 'static': command.append(str(arena/'data/static'))
                server = subprocess.Popen(command, stdout=server_log, stderr=subprocess.STDOUT)
            else:
                container = 'cnetmod-h3-profile-agensio'
                subprocess.run(['docker', 'run', '-d', '--name', container,
                    '--network', 'container:'+anchor, '--cpus', '32',
                    '--security-opt', 'seccomp=unconfined', '--ulimit', 'memlock=-1:-1',
                    '--ulimit', 'nofile=1048576:1048576', '-v', str(arena/'certs')+':/certs:ro',
                    '-v', str(arena/'data/static')+':/data/static:ro',
                    '-v', str(arena/'data/dataset.json')+':/data/dataset.json:ro', image],
                    check=True, stdout=server_log, stderr=subprocess.STDOUT)
            try:
                for _ in range(100):
                    ready = subprocess.run(prefix+[h2load, '--alpn-list=h3',
                        'https://localhost:8443/baseline2?a=1&b=1', '-n', '1', '-c', '1',
                        '-m', '1', '-t', '1'], capture_output=True, text=True, timeout=15)
                    if re.search(r'\b1 2xx', ready.stdout): break
                    time.sleep(.05)
                else: raise RuntimeError('not ready '+name)
                workload = prefix+['taskset', '-c', '16-31', h2load, '--alpn-list=h3']
                workload += (['https://localhost:8443/baseline2?a=1&b=1'] if profile=='baseline'
                    else ['-i', str(arena/'requests/static-h2-uris.txt'), '-H',
                          'Accept-Encoding: br;q=1, gzip;q=0.8'])
                workload += ['-c', '64', '-m', '64', '-t', '64', '-D', '5s']
                load_log = (root/f'{name}-h2load.log').open('w')
                load = subprocess.Popen(workload, stdout=load_log, stderr=subprocess.STDOUT)
                time.sleep(1)
                with (root/f'{name}-capture.log').open('w') as log:
                    subprocess.run(prefix+['tcpdump', '-i', 'lo', '-n', '-s', '128',
                        '-c', '50000', '-w', str(root/f'{name}.pcap'), 'udp src port 8443'],
                        stdout=log, stderr=subprocess.STDOUT, check=True, timeout=15)
                assert load.wait(timeout=30) == 0
                load_log.close()
                result = capture_summary(root/f'{name}.pcap')
                result['name'] = name
                results.append(result)
                (root/'summary.json').write_text(json.dumps(results, indent=2))
                print(name, json.dumps(result), flush=True)
            finally:
                if 'load' in locals() and load.poll() is None:
                    load.terminate()
                    load.wait(timeout=10)
                if container:
                    subprocess.run(['docker', 'logs', container], stdout=server_log, stderr=subprocess.STDOUT)
                    subprocess.run(['docker', 'rm', '-f', container], check=True,
                                   stdout=server_log, stderr=subprocess.STDOUT)
                else:
                    server.terminate()
                    server.wait(timeout=10)
                server_log.close()
finally:
    subprocess.run(['docker', 'rm', '-f', anchor], check=True, stdout=subprocess.DEVNULL)
print('RESULTS', root, flush=True)
