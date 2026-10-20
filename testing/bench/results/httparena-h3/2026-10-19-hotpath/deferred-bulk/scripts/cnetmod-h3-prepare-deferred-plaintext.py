from pathlib import Path
import hashlib, json, difflib, ast
repo=Path(r'C:/Users/25007/.codex/worktrees/2c3c/cnetmod')
out=Path(r'C:/Users/25007/.codex/tmp/cnetmod-h3-deferred-plaintext-review-5afc')
out.mkdir(exist_ok=True)
rel='src/protocol/quic/quic_connection.cpp'
raw=(repo/rel).read_bytes()
assert hashlib.sha256(raw).hexdigest()=='50cd8c3ba2cc226c45e83a483ba4c04665e229b76aae69ebf4523170abb7f1c2'
assert b'\n' not in raw.replace(b'\r\n', b'')
source=raw.decode().replace('\r\n', '\n')
start=source.index('auto quic_connection::pack_one_rtt_packet(')
end=source.index('auto quic_connection::pack_path_validation_packet(', start)
old=source[start:end]
new=old

def change(before, after, count=1):
    global new
    before='\n'+before
    after='\n'+after
    assert new.count(before)==count, (before, new.count(before), count)
    new=new.replace(before, after)

change('    if (impl_->connection_role == quic_role::server)\n', '''    // ACK and the optional probe PING stay at the front. Select the remaining
    // complete frames by exact length, then materialize their bytes only after
    // the existing whole-packet amplification and congestion checks admit them.
    auto planned_payload_size = payload.size();
    if (impl_->connection_role == quic_role::server)
''')
change('            payload.insert(payload.end(), frame.begin(), frame.end());\n', '            planned_payload_size += frame.size();\n')
change('                payload.insert(payload.end(), frame.begin(), frame.end());\n', '                planned_payload_size += frame.size();\n')
change('        payload.size() < payload_budget)', '        planned_payload_size < payload_budget)')
change('        if (frame.size() > payload_budget - payload.size())', '        if (frame.size() > payload_budget - planned_payload_size)')
change('        frame.append_to(payload);', '        planned_payload_size += frame.size();')
change('''    bool contains_unreliable_datagram = false;
    while (!impl_->unreliable_send_frames.empty())
    {
        const auto& frame = impl_->unreliable_send_frames.front();
        if (payload.size() + frame.size() > payload_budget)
            break;
        payload.insert(payload.end(), frame.begin(), frame.end());
        impl_->unreliable_send_frames.pop_front();
        contains_unreliable_datagram = true;
    }
''', '''    std::size_t selected_unreliable_datagrams{};
    for (const auto& frame : impl_->unreliable_send_frames)
    {
        if (planned_payload_size + frame.size() > payload_budget)
            break;
        planned_payload_size += frame.size();
        ++selected_unreliable_datagrams;
    }
    const bool contains_unreliable_datagram = selected_unreliable_datagrams != 0U;
    const auto discard_selected_datagrams = [&]()
    {
        for (std::size_t index{}; index < selected_unreliable_datagrams; ++index)
            impl_->unreliable_send_frames.pop_front();
    };
''')
change('''            : payload.size();
        if (payload.size() < required_payload)
            payload.resize(required_payload, std::byte{0});
''', '''            : planned_payload_size;
        planned_payload_size = std::max(planned_payload_size, required_payload);
''')
change('''    if (payload.empty())
        return packet_storage;''', '''    if (planned_payload_size == 0U)
    {
        discard_selected_datagrams();
        return packet_storage;
    }''')
change('        payload.size() + keys->tag_len;', '        planned_payload_size + keys->tag_len;')
change('''    if (!impl_->can_send_on_path(path, estimated_packet_size))
    {
''', '''    if (!impl_->can_send_on_path(path, estimated_packet_size))
    {
        // Selected DATAGRAMs are unreliable: drop this prefix when the path
        // cannot transmit, without copying their bytes into the payload.
        discard_selected_datagrams();
''')
change('''        // DATAGRAM frames were consumed above. Drop them under congestion
        // instead of transmitting beyond cwnd (RFC 9221 section 5.4).

''', '''        // Drop the same selected DATAGRAM prefix under congestion instead
        // of transmitting beyond cwnd (RFC 9221 section 5.4).
        discard_selected_datagrams();

''')
change('''    constexpr std::size_t pn_length = 4;
''', '''    retransmittable_frames.for_each([&payload](const quic_send_frame& frame)
    {
        frame.append_to(payload);
    });
    for (std::size_t index{}; index < selected_unreliable_datagrams; ++index)
    {
        const auto& frame = impl_->unreliable_send_frames.front();
        payload.insert(payload.end(), frame.begin(), frame.end());
        impl_->unreliable_send_frames.pop_front();
    }
    // This is the same final probe padding length used for admission above.
    if (payload.size() < planned_payload_size)
        payload.resize(planned_payload_size, std::byte{0});
    constexpr std::size_t pn_length = 4;
''')
# No new byte/frame container; old reserve and post-admission state tail intact.
assert new.count('std::vector<')==old.count('std::vector<')
assert new.count('retransmittable_frame_storage ')==old.count('retransmittable_frame_storage ')
assert new.count('payload.reserve(payload_budget);')==1
assert new[new.index('    constexpr std::size_t pn_length = 4;'):]==old[old.index('    constexpr std::size_t pn_length = 4;'):]
assert source[:start]+old+source[end:]==source
candidate=source[:start]+new+source[end:]
candidate_raw=candidate.replace('\n', '\r\n').encode()
for name,data in [('baseline.cpp',raw),('candidate.cpp',candidate_raw)]:
    (out/name).write_bytes(data)
patch=''.join(difflib.unified_diff(source.splitlines(True),candidate.splitlines(True),fromfile='a/'+rel,tofile='b/'+rel))
(out/'deferred-plaintext.patch').write_text(patch,encoding='utf-8',newline='\n')
metadata={'head':'5afc506dfefe021ca17caadaa1870572255dea1f','base_source_sha256':hashlib.sha256(raw).hexdigest(),'candidate_source_sha256':hashlib.sha256(candidate_raw).hexdigest(),'patch_sha256':hashlib.sha256(patch.encode()).hexdigest(),'scope':rel,'applied':False,'built':False,'runtime_tested':False,'limits':'ACK/PING prefix and cold reserves unchanged; only bulk materialization deferred. No in-place seal.'}
(out/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
assert (repo/rel).read_bytes()==raw
print(json.dumps(metadata))