import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess

repo=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod')
build=Path('/tmp/cnetmod-h3-arena-build')
out=Path('/var/tmp/cnetmod-h3-close-diagnostic-old-layout')
out.mkdir(exist_ok=True)
source=(repo/'src/protocol/quic/quic_connection.cpp').read_text()
source=source.replace('import std;','import std;\nimport cnetmod.core.log;',1)
needle='        queued_protocol_error = make_error_code(error);'
assert source.count(needle)==1
source=source.replace(needle,'''        logger::warn{"cold_close protocol cid={} error={} frame={} streams={}",
            local_connection_id ? local_connection_id->to_string() : std::string{},
            static_cast<unsigned>(error), static_cast<unsigned>(triggering_frame), streams.size()};
'''+needle)
needle='    impl_->connection_state = state;\n    impl_->terminal_send_error = error;'
assert source.count(needle)==1
extra='''    const auto diagnostic_now = std::chrono::steady_clock::now();
    for (const auto& [diagnostic_id, diagnostic_path] : impl_->application_paths)
    {
        const auto diagnostic_pto = diagnostic_path->recovery.next_pto_deadline();
        const auto diagnostic_cwnd = diagnostic_path->recovery.congestion_window();
        std::uint64_t diagnostic_low{}, diagnostic_high = diagnostic_cwnd;
        while (diagnostic_low < diagnostic_high)
        {
            const auto middle = diagnostic_low + (diagnostic_high - diagnostic_low + 1U) / 2U;
            if (diagnostic_path->recovery.can_send_datagram(middle))
                diagnostic_low = middle;
            else
                diagnostic_high = middle - 1U;
        }
        logger::warn{"cold_close flight cid={} path={} pto_remaining_us={} pto_duration_us={} cwnd={} available={} writes={} flush={} encoded={} prioritized={} unreliable={} early={} next_timer_us={}",
            impl_->local_connection_id ? impl_->local_connection_id->to_string() : std::string{},
            diagnostic_id, diagnostic_pto ? std::chrono::duration_cast<std::chrono::microseconds>(diagnostic_pto->first - diagnostic_now).count() : -1,
            std::chrono::duration_cast<std::chrono::microseconds>(diagnostic_path->recovery.pto_duration()).count(),
            diagnostic_cwnd, diagnostic_low, impl_->write_commands.approximate_size(),
            impl_->send_flush_requested.load(std::memory_order_acquire), impl_->encoded_send_frames.size(),
            impl_->prioritized_stream_frame_count(), impl_->unreliable_send_frames.size(), impl_->early_data_send_frames.size(),
            next_timer_deadline() ? std::chrono::duration_cast<std::chrono::microseconds>(*next_timer_deadline() - diagnostic_now).count() : -1};
    }
'''
source=source.replace(needle,'''    logger::warn{"cold_close transport cid={} peer={} category={} error={} message={} from={} to={} streams={} idle_remaining_us={}",
        impl_->local_connection_id ? impl_->local_connection_id->to_string() : std::string{},
        impl_->peer.to_string(), error.category().name(), error.value(), error.message(),
        static_cast<unsigned>(impl_->connection_state), static_cast<unsigned>(state), impl_->streams.size(),
        impl_->idle_deadline ? std::chrono::duration_cast<std::chrono::microseconds>(
            *impl_->idle_deadline - std::chrono::steady_clock::now()).count() : -1};
'''+extra+needle)
needle='auto quic_connection::process_connection_close_frame(const connection_close_frame&) -> task<void>\n{'
assert source.count(needle)==1
source=source.replace(needle,'''auto quic_connection::process_connection_close_frame(const connection_close_frame& frame) -> task<void>
{
    logger::warn{"cold_close peer cid={} error={} application={} frame={} reason={}",
        impl_->local_connection_id ? impl_->local_connection_id->to_string() : std::string{},
        frame.error_code, frame.is_application_error, frame.frame_type_value, frame.reason};''')
(out/'quic_connection.cpp').write_text(source)
entries=json.loads((build/'compile_commands.json').read_text())
entry=next(c for c in entries if c['file'].endswith('/quic_connection.cpp'))
command=shlex.split(entry['command'])
logging_flags=shlex.split((build/'CMakeFiles/cnetmod_http.dir/src/protocol/http/v3/http3_server.cpp.o.modmap').read_text())
command.extend(arg for arg in logging_flags if arg.startswith('-fmodule-file=cnetmod.core.log'))
modmap=next(c[1:] for c in command if c.startswith('@'))
modules=[]
for arg in shlex.split((build/modmap).read_text()):
    if arg.startswith('-fmodule-file='):
        module=arg.split('=',2)[-1]
        path=Path(module)
        if not path.is_absolute(): path=build/path
        if path.is_file(): modules.append(path)
def snapshots():
    files=modules+[build/'libcnetmod_quic.a',Path('/var/tmp/cnetmod-h3-stage2-before-gso/libcnetmod_runtime.a')]
    return {str(p):dict(sha256=hashlib.sha256(p.read_bytes()).hexdigest(),mtime=p.stat().st_mtime) for p in files}
before=snapshots()
(out/'old-inputs-before.json').write_text(json.dumps(before,indent=2))
command[command.index('-o')+1]=str(out/'quic_connection.cpp.o')
command[command.index('-c')+1]=str(out/'quic_connection.cpp')
subprocess.run(command,cwd=build,check=True)
shutil.copyfile(build/'libcnetmod_quic.a',out/'libcnetmod_quic.a')
subprocess.run(['ar','r',str(out/'libcnetmod_quic.a'),str(out/'quic_connection.cpp.o')],check=True)
commands=subprocess.check_output(['/var/tmp/cnetmod-ninja-1.11/bin/ninja','-t','commands','arena_h3_server'],cwd=build,text=True).splitlines()
link=next(c for c in reversed(commands) if ' -o testing/bench/arena_h3_server ' in c)
link=link.replace(' -o testing/bench/arena_h3_server ',' -o '+str(out/'arena_h3_server')+' ')
link=link.replace(' libcnetmod_quic.a ',' '+str(out/'libcnetmod_quic.a')+' ')
link=link.replace(' libcnetmod_runtime.a ',' /var/tmp/cnetmod-h3-stage2-before-gso/libcnetmod_runtime.a ')
(out/'link-command.txt').write_text(link)
subprocess.run(link,cwd=build,shell=True,check=True)
after=snapshots()
(out/'old-inputs-after.json').write_text(json.dumps(after,indent=2))
assert before==after,'old-layout inputs changed during isolated compilation'
print(out,flush=True)
