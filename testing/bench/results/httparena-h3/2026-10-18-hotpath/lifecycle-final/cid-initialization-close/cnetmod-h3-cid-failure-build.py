"""Prepare same-layout CID-failure controls; never modify the normal build.

Only run in the root-assigned exclusive build window, after a normal rebuild
of quic_retire_stream_server with the exact current sources and PCMs.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import shlex
import shutil
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod'))
    parser.add_argument('--build', type=Path, default=Path('/var/tmp/cnetmod-h3-arena-build'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--current-build-verified', action='store_true', required=True)
    parser.add_argument('--bind-recorded-build', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    repo, build, out = args.repo.resolve(), args.build.resolve(), args.out.resolve()
    if out.exists() or repo in out.parents or build in out.parents:
        parser.error('--out must be new and outside the source/build trees')
    entries = json.loads((build / 'compile_commands.json').read_text())
    recorded = Path(entries[0]['directory'])
    if args.bind_recorded_build:
        if args.bind_recorded_build != recorded:
            raise RuntimeError('recorded build path changed')
        recorded.mkdir(parents=True, exist_ok=True)
        subprocess.run(['mount', '--bind', str(build), str(recorded)], check=True)
        subprocess.run(['mount', '-o', 'remount,bind,ro', str(recorded)], check=True)
    else:
        raise SystemExit(subprocess.run([
            'unshare', '--mount', '--propagation', 'private', sys.executable,
            str(Path(__file__).resolve()), *sys.argv[1:],
            '--bind-recorded-build', str(recorded)]).returncode)

    spec = importlib.util.spec_from_file_location(
        'cold_helpers', Path(__file__).with_name('cnetmod-h3-close-diagnostic-latest-build.py'))
    helpers = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helpers)
    paths = set(build.rglob('*.pcm')) | set(build.rglob('*.modmap')) | set(build.glob('*.a'))
    paths |= set((repo / 'src').rglob('*.cpp')) | set((repo / 'src').rglob('*.cppm'))
    paths |= {repo / 'testing/http3/quic_retire_stream_server.cpp',
              build / 'compile_commands.json', build / 'build.ninja'}

    def snapshot():
        return {str(path): digest(path) for path in sorted(paths) if path.is_file()}

    before = snapshot()
    out.mkdir(parents=True)
    (out / 'inputs-before.json').write_text(json.dumps(before, indent=2))
    try:
        fixture = (repo / 'testing/http3/quic_retire_stream_server.cpp').read_text()
        anchor = '        config.active_connection_id_limit = 2U;'
        if fixture.count(anchor) != 1:
            raise RuntimeError('fixture injection anchor changed')
        # This is a local configuration field, not a wire transport parameter.
        # Existing registered Retry CID and authenticated server CID stay 8B.
        # It deterministically reaches issue_parallel_local_connection_ids'
        # existing zero-length check after TLS completion/parameter application.
        fixture = fixture.replace(anchor, anchor + '\n        config.cid_length = 0U; // external failure injection')
        # The general-purpose raw fixture stops its observer timer on any
        # parse error. Match the real shared listener's error contract here:
        # publish the deadline and keep driving; never flush manually.
        error_branch = 'co_await fail(state, "process: " + processed.error().message());\n            co_return;'
        if fixture.count(error_branch) != 1:
            raise RuntimeError('fixture observer error anchor changed')
        fixture = fixture.replace(error_branch,
            'schedule(state, state.connection->next_timer_deadline());\n            continue;')
        connection = (repo / 'src/protocol/quic/quic_connection.cpp').read_text()
        failure_call = '                impl_->queue_protocol_close(quic_errc::internal_error, frame_type::crypto);\n'
        if connection.count(failure_call) != 2:
            raise RuntimeError('expected exactly two approved CID failure-close calls')
        negative = connection.replace(failure_call, '')
        fixture_entry = next(entry for entry in entries
                             if Path(entry['file']).name == 'quic_retire_stream_server.cpp')
        connection_entry = next(entry for entry in entries
                                if Path(entry['file']).name == 'quic_connection.cpp')
        commands = subprocess.check_output([
            '/var/tmp/cnetmod-ninja-1.11/bin/ninja', '-t', 'commands',
            'quic_retire_stream_server'], cwd=recorded, text=True).splitlines()
        link = shlex.split(commands[-1])
        start = next(index for index, arg in enumerate(link) if arg.endswith('/clang++'))
        link = link[start:link.index('&&', start)]
        fixture_object = str((recorded / 'testing/http3/CMakeFiles/quic_retire_stream_server.dir/quic_retire_stream_server.cpp.o'))
        # Ninja's link normally spells the target object relative to its cwd.
        candidates = [arg for arg in link if arg.endswith('/quic_retire_stream_server.cpp.o')]
        if len(candidates) != 1:
            raise RuntimeError(('ambiguous fixture object', candidates))
        fixture_object = candidates[0]
        manifest = {'scope': 'same-layout CID failure mechanism control, not an old release binary',
                    'injection': 'only external raw fixture config.cid_length=0; wire Retry/server CIDs remain valid',
                    'control_difference': 'negative removes only two approved internal-error queue calls',
                    'variants': []}
        for name in ('without-cid-close', 'with-cid-close'):
            folder = out / name
            folder.mkdir()
            (folder / 'quic_retire_stream_server.cpp').write_text(fixture)
            command, cwd = helpers.compile_command(fixture_entry, 'quic_retire_stream_server.cpp', folder, [])
            (folder / 'fixture-command.json').write_text(json.dumps(command, indent=2))
            subprocess.run(command, cwd=cwd, check=True)
            archive = folder / 'libcnetmod_quic.a'
            shutil.copyfile(build / 'libcnetmod_quic.a', archive)
            if name == 'without-cid-close':
                (folder / 'quic_connection.cpp').write_text(negative)
                command, cwd = helpers.compile_command(connection_entry, 'quic_connection.cpp', folder, [])
                (folder / 'connection-command.json').write_text(json.dumps(command, indent=2))
                subprocess.run(command, cwd=cwd, check=True)
                members = subprocess.check_output(['ar', 't', str(archive)], text=True).splitlines()
                if members.count('quic_connection.cpp.o') != 1:
                    raise RuntimeError('ambiguous connection archive member')
                subprocess.run(['ar', 'r', str(archive), str(folder / 'quic_connection.cpp.o')], check=True)
            command = list(link)
            command[command.index('-o') + 1] = str(folder / 'quic_retire_stream_server')
            command[command.index(fixture_object)] = str(folder / 'quic_retire_stream_server.cpp.o')
            for index, arg in enumerate(command):
                if arg.endswith('.a') and (recorded / arg).samefile(build / 'libcnetmod_quic.a'):
                    command[index] = str(archive)
                if arg.startswith('--dependency-file='):
                    command[index] = '--dependency-file=' + str(folder / 'link.d')
            (folder / 'link-command.json').write_text(json.dumps(command, indent=2))
            subprocess.run(command, cwd=recorded, check=True)
            manifest['variants'].append({'name': name,
                'binary_sha256': digest(folder / 'quic_retire_stream_server'),
                'fixture_sha256': digest(folder / 'quic_retire_stream_server.cpp'),
                'quic_archive_sha256': digest(archive)})
        (out / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    finally:
        after = snapshot()
        (out / 'inputs-after.json').write_text(json.dumps(after, indent=2))
        changed = [path for path in before.keys() | after.keys() if before.get(path) != after.get(path)]
        (out / 'integrity.json').write_text(json.dumps({'unchanged': not changed, 'changed': changed}, indent=2))
        if changed:
            raise RuntimeError(('production inputs changed', changed))


if __name__ == '__main__':
    main()
