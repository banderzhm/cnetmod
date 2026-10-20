"""Offline AST/pure-parser/staging checks. Does not execute runner or subprocess."""
import ast
from pathlib import Path
import tempfile
import types

script=Path('C:/Users/25007/.codex/tmp/cnetmod-h3-final-system-official.py')
tree=ast.parse(script.read_text(encoding='utf-8'))
namespace={'__name__':'offline_runner_check','__file__':str(script)}
exec(compile(tree,str(script),'exec'),namespace)
parse=namespace['parse_rounds']
one='\nfinished in 5.50s, 18.00 req/s\nrequests: 100 total, 100 started, 99 done, 99 succeeded, 0 failed, 0 errored, 0 timeout\nstatus codes: 100 2xx, 0 3xx, 0 4xx, 0 5xx\n'
valid=''.join(f'[run {i}/3]'+one for i in range(1,4))
assert len(parse(valid))==3 and parse(valid)[0]['header_only_delta']==1
for invalid in (valid.replace('0 failed','1 failed'),valid.replace('0 3xx','1 3xx'),
                valid.replace('99 succeeded','98 succeeded'),valid.replace('[run 3/3]',''),
                valid.replace('finished in 5.50s','finished in error'),valid.replace('99 done','0 done')):
    try:parse(invalid)
    except ValueError:pass
    else:raise AssertionError('invalid round data accepted')
historical=Path('C:/Users/25007/.codex/worktrees/2c3c/cnetmod/testing/bench/results/httparena-h3/2026-10-18-hotpath/lifecycle-final/official-final-05fe/records')
count=0
for name,profiles in namespace['EXPECTED_ORDER']:
    for profile in profiles:
        text=(historical/f'{name}-{profile}.log').read_text(encoding='utf-8')
        count+=len(parse(text))
assert count==18
original=Path('//wsl.localhost/archlinux/var/tmp/cnetmod-http-arena-8977be27/scripts')
with tempfile.TemporaryDirectory(prefix='cnetmod-official-offline-') as directory:
    root=Path(directory)
    for relative in ('benchmark.sh','lib/framework.sh','validate.sh','lib/tools/h2load-h3.sh'):
        path=root/'scripts'/relative
        path.parent.mkdir(parents=True,exist_ok=True)
        text=(original/relative).read_text(encoding='utf-8')
        if relative=='benchmark.sh':
            excluded=('docker volume prune','docker image prune','docker image prune ',
                'docker ps -q  --filter "name=httparena-"','docker ps -aq --filter "name=httparena-"')
            text=''.join(line for line in text.splitlines(keepends=True) if not any(token in line for token in excluded))
        elif relative=='lib/framework.sh':
            before='h3|static-h3)\n            probe_url="https://localhost:$H2PORT/baseline2?a=1&b=1"'
            text=text.replace(before,before+'\n            probe_extra+=(--http3-only)',1)
        elif relative=='validate.sh':
            for before,after in (('PORT=8080\n','PORT=18080\n'),('H2PORT=8443\n','H2PORT=48443\n'),
                ('H1TLS_PORT=8081\n','H1TLS_PORT=18081\n'),('H2C_PORT=8082\n','H2C_PORT=18082\n')):
                text=text.replace(before,after,1)
            text=text.replace("| grep -q ' 1 2xx'; then","| grep ' 1 2xx' >/dev/null; then")
        path.write_text(text)
    runner=namespace['Run'].__new__(namespace['Run'])
    runner.stage=root
    runner.records=root/'records'
    runner.records.mkdir()
    runner.nonce='f'*32
    runner.prefix='cnetmod-final-'+runner.nonce
    runner.label='cnetmod.official.run='+runner.nonce
    runner.patch_stage()
    benchmark=(root/'scripts/benchmark.sh').read_text(encoding='utf-8')
    assert 'cleanup_all() {\n    framework_stop\n}' in benchmark
    assert '    postgres_stop\n' not in benchmark and '    redis_stop\n' not in benchmark
    tool=(root/'scripts/lib/tools/h2load-h3.sh').read_text(encoding='utf-8')
    assert 'timeout 45 taskset -c "$GCANNON_CPUS" "$@" 2>&1 || code=$?' in tool
    assert 'local code=0' in tool and 'CNETMOD_OFFICIAL_ARGV' in tool
    validate=(root/'scripts/validate.sh').read_text(encoding='utf-8')
    assert 'PG_CONTAINER="httparena-' not in validate and 'PG_NETWORK="httparena-' not in validate
    assert validate.count('--label '+runner.label)>=4
print('PASS: Python AST;1valid+6invalid synthetic parser cases;18historical official rounds;exact scoped staging against pinned official source. No subprocess/build/load.')
