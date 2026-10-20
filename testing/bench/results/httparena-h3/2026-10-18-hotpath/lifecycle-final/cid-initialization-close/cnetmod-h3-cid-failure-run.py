"""Exercise a prebuilt external CID-failure fixture; exclusive window only."""
from pathlib import Path
import argparse
import asyncio
import json
import ssl
import subprocess
import sys
import tempfile
import time


async def exercise(port, expect_close, result):
    from aioquic.asyncio import QuicConnectionProtocol, connect
    from aioquic.quic.configuration import QuicConfiguration
    from aioquic.quic.events import ConnectionTerminated, HandshakeCompleted

    recording = [True]
    class Peer(QuicConnectionProtocol):
        def quic_event_received(self, event):
            if not recording[0]:
                return
            if isinstance(event, HandshakeCompleted):
                result['tls_completed'] = True
            if isinstance(event, ConnectionTerminated):
                result['peer_close'] = {'error_code': event.error_code,
                    'frame_type': event.frame_type, 'reason': event.reason_phrase}

    config = QuicConfiguration(is_client=True, alpn_protocols=['quic-retire-test'])
    config.verify_mode = ssl.CERT_NONE
    config.idle_timeout = 6
    # No application packet is needed to reach server TLS completion. The
    # negative has no handshake confirmation, so do not wait on connected().
    async with connect('127.0.0.1', port, configuration=config,
                       create_protocol=Peer, wait_connected=False) as peer:
        # aioquic wait_connected=False deliberately leaves the Initial
        # queued; transmit it explicitly before observing TLS completion.
        peer.transmit()
        result['initial_transmitted'] = True
        try:
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and 'peer_close' not in result:
                await asyncio.sleep(0.005)
            result['client_handshake_confirmed'] = peer._quic._handshake_confirmed
            result['observation_seconds'] = 3
            assert result.get('tls_completed'), 'failure injection did not reach TLS completion'
            if expect_close:
                assert result.get('peer_close') is not None, 'no explicit server CLOSE before 6s idle'
                event = result['peer_close']
                assert event['error_code'] == 1 and event['frame_type'] == 6, event
            else:
                assert 'peer_close' not in result, 'negative unexpectedly emitted a server CLOSE'
                assert not result['client_handshake_confirmed'], 'negative skipped the injected failure'
            result['injected_failure_observed'] = True
            # Copy the outcome before context-manager teardown emits a local CLOSE.
            result['observed_outcome'] = 'explicit_internal_error_close' if expect_close else 'silent_failure_no_close'
            recording[0] = False
        finally:
            recording[0] = False



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('server', type=Path)
    parser.add_argument('--expect-close', choices=('yes', 'no'), required=True)
    parser.add_argument('--mode', choices=('observer',), default='observer')
    parser.add_argument('--repo', type=Path, default=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.repo / 'testing/http3'))
    from http_client_e2e import make_certificate, reserve_port, stop_process
    result = {'case': 'cid_issue_failure', 'mode': args.mode,
              'expected_close': args.expect_close, 'status': 'failed'}
    with tempfile.TemporaryDirectory(prefix='cnetmod-cid-failure-') as directory:
        root = Path(directory)
        cert, key = make_certificate(root)
        port = reserve_port()
        command = [str(args.server.resolve()), '--port', str(port), '--mode', args.mode,
                   '--cert', str(cert), '--key', str(key),
                   '--listening-report', str(root / 'listening'),
                   '--ready-report', str(root / 'ready'),
                   '--retire-trigger', str(root / 'trigger'),
                   '--result-report', str(root / 'result')]
        server = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 5
            while not (root / 'listening').exists() or not (root / 'listening').read_text().strip():
                assert server.poll() is None and time.monotonic() < deadline, 'fixture not ready'
                time.sleep(0.01)
            asyncio.run(exercise(port, args.expect_close == 'yes', result))
            result['status'] = 'passed'
        except Exception as error:
            result['error'] = repr(error)
        finally:
            result['server_stdout'], result['server_stderr'] = stop_process(server)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, sort_keys=True), flush=True)
    return 0 if result['status'] == 'passed' else 1


if __name__ == '__main__':
    sys.exit(main())
