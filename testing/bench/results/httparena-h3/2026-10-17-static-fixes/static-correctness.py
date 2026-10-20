import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time
from urllib.parse import urlsplit

repo = Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod')
records = repo / 'testing/bench/results/httparena-h3/2026-10-17-static-fixes'
stage = Path('/var/tmp/cnetmod-h3-official/arena')
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
command = ['/tmp/cnetmod-h3-arena-build/testing/bench/arena_h3_server',
           str(stage/'certs/server.crt'), str(stage/'certs/server.key'),
           str(port), '1', str(stage/'data/static')]
with (records/'static-correctness-server.log').open('wb') as log:
    server = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    try:
        for attempt in range(30):
            ready = subprocess.run(['curl', '--noproxy', '*', '--http3-only', '-skf',
                                    '--max-time', '1', f'https://127.0.0.1:{port}/baseline2?a=1&b=1'],
                                   capture_output=True)
            if ready.returncode == 0:
                assert ready.stdout == b'2'
                break
            time.sleep(.1)
        else:
            raise RuntimeError('server readiness failed')
        names = [Path(urlsplit(url).path).name for url in
                 (stage/'requests/static-h2-uris.txt').read_text().splitlines()]
        checks = []
        with tempfile.TemporaryDirectory(dir='/var/tmp') as directory:
            root = Path(directory)
            curl = ['curl']
            for name in names:
                for encoding in ['identity', 'br;q=1, gzip;q=0.8']:
                    if len(curl) > 1:
                        curl += ['--next']
                    index = len(checks)
                    source = stage/'data/static'/name
                    compressed = source.with_name(name+'.br')
                    expected_br = encoding != 'identity' and compressed.is_file()
                    curl += ['--noproxy', '*', '--http3-only', '-skf', '--max-time', '10',
                             '-H', 'Accept-Encoding: '+encoding, '-D', str(root/f'{index}.headers'),
                             '-o', str(root/f'{index}.body'), '-w', '%{num_connects}\n',
                             f'https://127.0.0.1:{port}/static/{name}']
                    checks.append({'path':name, 'encoding':encoding,
                                   'expected_br':expected_br,
                                   'source':str(compressed if expected_br else source)})
            result = subprocess.run(curl, capture_output=True, timeout=120)
            (records/'static-correctness-curl.log').write_bytes(result.stdout+result.stderr)
            result.check_returncode()
            connections = [int(n) for n in result.stdout.splitlines()]
            assert connections == [1] + [0]*(len(checks)-1), connections
            for index, check in enumerate(checks):
                body = (root/f'{index}.body').read_bytes()
                headers = (root/f'{index}.headers').read_text().lower()
                assert body == Path(check['source']).read_bytes(), check
                assert 'http/3 200' in headers, headers
                assert ('content-encoding: br' in headers) == check['expected_br'], headers
                check['bytes'] = len(body)
                check['passed'] = True
        (records/'static-correctness.json').write_bytes((json.dumps({
            'kind':'serial correctness only; no throughput or memory measurement',
            'server_argv':command, 'connections':connections, 'checks':checks}, indent=2)+'\n').encode())
        print(f'{len(checks)} exact-byte official fixture checks passed on one reused HTTP/3 connection')
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
