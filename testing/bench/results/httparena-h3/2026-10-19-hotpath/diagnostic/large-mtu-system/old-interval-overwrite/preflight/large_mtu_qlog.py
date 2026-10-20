"""Bounded second pass over a complete client qlog; no processes or network."""
from collections import Counter
import json
import math
import hashlib
from pathlib import Path
from urllib.parse import urlsplit


def encoded_fixture_capability(arena):
    uri_file = arena/'requests/static-h2-uris.txt'
    assert uri_file.stat().st_size < 1024**2
    uris = uri_file.read_text().splitlines()
    assert 0 < len(uris) <= 1024
    assets = (arena/'data/static').resolve()
    rows = []
    for uri in uris:
        path = urlsplit(uri).path
        assert path.startswith('/static/'), 'non-static URI cannot prove this diagnostic body capability'
        identity = (assets/path.removeprefix('/static/')).resolve()
        assert identity.is_relative_to(assets) and identity.is_file()
        brotli = Path(str(identity)+'.br')
        chosen = brotli if brotli.is_file() else identity
        rows.append(dict(uri=uri,body_file=str(chosen),encoding='br' if chosen==brotli else 'identity',
            body_bytes=chosen.stat().st_size,sha256=hashlib.sha256(chosen.read_bytes()).hexdigest()))
    largest = max(rows,key=lambda row:row['body_bytes'])
    return dict(original_uri_list_sha256=hashlib.sha256(uri_file.read_bytes()).hexdigest(),
        selected_encoding='same br;q=1,gzip;q=0.8 adapter negotiation; br if present else identity',
        largest_selected_body=largest,selected_bodies=rows,
        suitable_for_65507_single_stream_application_packet=largest['body_bytes']>65507)


def add_timeline(path, ceiling, base):
    assert ceiling in (16384, 32768, 65507)
    assert base['peer_max_udp_payload_size'] >= ceiling
    assert base['server_max_udp_payload_size'] == 65507
    probes = {}
    acked = {}
    app = {}
    events = 0
    first_1rtt = last_1rtt = None
    first_after_ack = None
    target_ack = None
    with path.open('rb') as f:
        for line in f:
            assert len(line) <= 1024**2
            if not line.strip():
                continue
            assert line.startswith(b'\x1e')
            event = json.loads(line[1:])
            if 'qlog_version' in event:
                fields = event['trace']['common_fields']
                assert fields['time_format'] == 'relative'
                continue
            events += 1
            assert events <= 1000000
            name = event.get('name')
            if name not in ('transport:packet_received', 'transport:packet_sent'):
                continue
            data = event['data']
            if data['header']['packet_type'] != '1RTT':
                continue
            timestamp = float(event['time'])
            assert math.isfinite(timestamp) and timestamp >= 0
            frames = data['frames']
            if name == 'transport:packet_sent':
                for frame in frames:
                    if frame.get('frame_type') != 'ack':
                        continue
                    ranges = frame['acked_ranges']
                    assert len(ranges) <= 4096
                    normalized = []
                    for pair in ranges:
                        assert len(pair) in (1, 2)
                        low, high = int(pair[0]), int(pair[-1])
                        assert 0 <= low <= high < (1 << 62)
                        normalized.append((low, high))
                    for pn, probe in probes.items():
                        if pn not in acked and any(low <= pn <= high for low, high in normalized):
                            acked[pn] = dict(**probe, client_ack_time_ms=timestamp, client_ack_event=events)
                            if probe['bytes'] == ceiling and target_ack is None:
                                target_ack = acked[pn]
                continue
            pn = int(data['header']['packet_number'])
            length = int(data['raw']['length'])
            assert 0 < length <= ceiling, 'received packet exceeds configured ceiling'
            if first_1rtt is None:
                first_1rtt = timestamp
            last_1rtt = timestamp
            kinds = {frame.get('frame_type') for frame in frames}
            # Exclude every PING-bearing packet from application sizing so a
            # padded probe/PTO carrying STREAM bytes cannot prove convergence.
            if 'ping' in kinds and 'padding' in kinds:
                probe = dict(packet_number=pn, bytes=length, received_time_ms=timestamp, received_event=events)
                assert pn not in probes or probes[pn]['bytes'] == length
                probes[pn] = probe
                assert len(probes) <= 4096
            response = any(frame.get('frame_type') == 'stream' and int(frame['stream_id']) % 4 == 0 for frame in frames)
            if response and 'ping' not in kinds:
                if pn not in app:
                    assert len(app) < 200000
                    app[pn] = (timestamp, length, events)
                if target_ack is not None and events > target_ack['client_ack_event'] and length > ceiling - 128:
                    if first_after_ack is None:
                        first_after_ack = dict(packet_number=pn, bytes=length, time_ms=timestamp, event=events)
    assert app, 'no non-probe application response packet'
    histogram = Counter(length for timestamp, length, event in app.values())
    bins = {}
    for timestamp, length, event in app.values():
        bucket = int(timestamp // 100) * 100
        entry = bins.setdefault(bucket, dict(packets=0, min_bytes=length, max_bytes=length, bytes=0))
        entry['packets'] += 1
        entry['bytes'] += length
        entry['min_bytes'] = min(entry['min_bytes'], length)
        entry['max_bytes'] = max(entry['max_bytes'], length)
    thresholds = {}
    for threshold in (4096, 8192, 16384, 32768):
        hits = [timestamp for timestamp, length, event in app.values() if length > threshold]
        thresholds[str(threshold)] = min(hits) if hits else None
    converged = target_ack is not None and first_after_ack is not None
    return dict(
        status='converged_by_client_evidence' if converged else 'not_converged_in_diagnostic',
        converged=converged, configured_send_ceiling=ceiling,
        first_received_1rtt_time_ms=first_1rtt, last_received_1rtt_time_ms=last_1rtt,
        observed_1rtt_connection_span_ms=last_1rtt-first_1rtt,
        observed_1rtt_span_at_least_3_seconds=(last_1rtt-first_1rtt)>=3000,
        non_probe_response_packet_count=len(app),
        non_probe_response_packets_per_completed=len(app)/base['complete_response_stream_count'],
        max_non_probe_response_packet_bytes=max(histogram),
        application_packet_size_histogram={str(k):v for k,v in sorted(histogram.items())},
        first_application_larger_than_threshold_time_ms=thresholds,
        time_bins_100ms=[dict(start_time_ms=k, **v) for k,v in sorted(bins.items())],
        received_probe_candidates=list(probes.values()),
        client_acknowledged_probe_candidates=list(acked.values()),
        target_sized_probe_client_ack=target_ack,
        first_near_target_application_after_ack=first_after_ack,
        convergence_time_ms=first_after_ack['time_ms'] if first_after_ack else None,
        near_target_rule='non-PING response STREAM packet >ceiling-128, after client ACK of a ceiling-sized PING+PADDING packet',
        proof_limit='Client observations only. ACK transmission is observed, not server reception/internal discovered PMTU state. Source retains ACK-validated discovery. qlog raw.length is QUIC packet bytes, not IP fragment/syscall count.',
        time_unit='milliseconds, qlog0.3 relative time from trace reference; event order breaks equal timestamps')
