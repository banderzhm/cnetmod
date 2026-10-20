"""External new ownership tests against the frozen old map module/runtime.

Do not execute until root releases its exclusive benchmark window. First build
the unmodified normal test_concurrent_containers target so its exact modmap and
link inputs exist. This helper never applies the candidate module patch.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import shlex
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod'))
    parser.add_argument('--build', type=Path, default=Path('/var/tmp/cnetmod-h3-arena-build'))
    parser.add_argument('--prepared', type=Path, default=Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-atomic-map-owner-prepared'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--current-old-map-build-verified', action='store_true', required=True)
    parser.add_argument('--bind-recorded-build', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    repo, build, out = args.repo.resolve(), args.build.resolve(), args.out.resolve()
    if out.exists() or repo in out.parents or build in out.parents:
        parser.error('--out must be new and outside source/build')
    old_interface = (repo / 'src/utils/concurrentContainers/atomic_hash_map.cppm').read_text()
    if '        Value value;\n        bool erased{};' not in old_interface:
        raise RuntimeError('not the old Value+erased map; do not mix new PCMs into the negative')
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
    files = set(build.rglob('*.pcm')) | set(build.rglob('*.modmap')) | set(build.glob('*.a'))
    files |= set((repo / 'src').rglob('*.cpp')) | set((repo / 'src').rglob('*.cppm'))
    files |= {repo / 'src/utils/concurrentContainers/atomic_hash_map.inl',
              repo / 'testing/tests/test_concurrent_containers.cpp'}
    def snapshot():
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(files) if path.is_file()}
    before = snapshot()
    out.mkdir(parents=True)
    (out / 'inputs-before.json').write_text(json.dumps(before, indent=2))
    try:
        name = 'test_concurrent_containers.cpp'
        shutil.copyfile(args.prepared / 'testing/tests' / name, out / name)
        entry = next(entry for entry in entries if Path(entry['file']).name == name)
        command, cwd = helpers.compile_command(entry, name, out, [])
        (out / 'compile-command.json').write_text(json.dumps(command, indent=2))
        subprocess.run(command, cwd=cwd, check=True)
        lines = subprocess.check_output([
            '/var/tmp/cnetmod-ninja-1.11/bin/ninja', '-t', 'commands',
            'test_concurrent_containers'], cwd=recorded, text=True).splitlines()
        link = shlex.split(lines[-1])
        start = next(index for index, arg in enumerate(link) if arg.endswith('/clang++'))
        link = link[start:link.index('&&', start)]
        objects = [arg for arg in link if arg.endswith('/' + name + '.o')]
        if len(objects) != 1:
            raise RuntimeError(('ambiguous test object', objects))
        link[link.index(objects[0])] = str(out / (name + '.o'))
        binary = out / 'test_atomic_map_old_ownership'
        link[link.index('-o') + 1] = str(binary)
        for index, arg in enumerate(link):
            if arg.startswith('--dependency-file='):
                link[index] = '--dependency-file=' + str(out / 'link.d')
        (out / 'link-command.json').write_text(json.dumps(link, indent=2))
        subprocess.run(link, cwd=recorded, check=True)
        env = dict(os.environ)
        env['CNETMOD_TEST_FILTER'] = 'atomic_hash_map'
        result = subprocess.run([str(binary)], cwd=out, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=30)
        (out / 'negative.log').write_text(result.stdout)
        expected = [
            'atomic_hash_map_each_erase_releases_owned_value',
            'atomic_hash_map_erase_preserves_concurrent_reader_snapshot']
        for name in expected:
            assert '[  FAILED  ] ' + name + ' (3 failure(s))' in result.stdout, result.stdout
        assert result.returncode == 1 and '[==========] 5 test(s) ran.' in result.stdout, result.stdout
        assert '[  PASSED  ] 3 test(s).' in result.stdout, result.stdout
        (out / 'result.json').write_text(json.dumps({
            'negative_observed': True, 'exit_code': result.returncode,
            'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
            'scope': 'new ownership assertions instantiated against old map PCMs with unchanged normal runtime',
            'expected_failed_cases': expected, 'expected_total_failed_assertions': 6,
            'existing_atomic_map_cases_and_nondefault_collision_case_passed': True}, indent=2))
    finally:
        after = snapshot()
        (out / 'inputs-after.json').write_text(json.dumps(after, indent=2))
        changed = [path for path in before.keys() | after.keys() if before.get(path) != after.get(path)]
        (out / 'integrity.json').write_text(json.dumps({'unchanged': not changed, 'changed': changed}, indent=2))
        if changed:
            raise RuntimeError(('source/module/archive changed during old control', changed))


if __name__ == '__main__':
    main()
