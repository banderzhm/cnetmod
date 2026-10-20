"""Prepare two isolated destructor/weak-reference diagnostic binaries.

Run only after a normal current directed-freeze h3_interop_server build, in the exclusive build
window. Never builds or modifies original archives, PCMs, objects or sources.
The two variants differ only in expired.clear(); all diagnostics are identical.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,shlex,shutil,subprocess,sys

HELPER=r'''
    // Diagnostic-only: owns a weak reference and an identifier, never the peer.
    auto weak_release_probe(io_context& context,
        std::weak_ptr<quic::quic_connection> connection, std::string cid) -> task<void>
    {
        (void)co_await async_timer_wait(context, std::chrono::milliseconds{100});
        logger::warn{"weak_release cid={} checkpoint_ms=100 expired={} strong_count={}",
            cid, connection.expired(), connection.use_count()};
        (void)co_await async_timer_wait(context, std::chrono::milliseconds{400});
        logger::warn{"weak_release cid={} checkpoint_ms=500 expired={} strong_count={}",
            cid, connection.expired(), connection.use_count()};
    }
'''

def once(s,a,b):
    if s.count(a)!=1:raise RuntimeError(('source anchor changed',s.count(a),a[:100]))
    return s.replace(a,b,1)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def snap(files):return {str(p):sha(p)for p in sorted(files)if p.is_file()}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,default=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod'))
    parser.add_argument('--build',type=Path,default=Path('/var/tmp/cnetmod-h3-arena-build'))
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--bind-recorded-build',type=Path,help=argparse.SUPPRESS)
    parser.add_argument('--current-build-verified',action='store_true',required=True,
        help='Caller has completed normal h3_interop_server build with these exact sources')
    args=parser.parse_args();repo=args.repo.resolve();build=args.build.resolve();out=args.out.resolve()
    if out.exists() or build in out.parents or repo in out.parents:parser.error('--out must be new and outside source/build')
    spec=importlib.util.spec_from_file_location('cold_helpers',Path(__file__).with_name('cnetmod-h3-close-diagnostic-latest-build.py'))
    helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
    entries=json.loads((build/'compile_commands.json').read_text())
    recorded=Path(entries[0]['directory'])
    if args.bind_recorded_build:
        if args.bind_recorded_build != recorded:
            raise RuntimeError('recorded build path mismatch')
        recorded.mkdir(parents=True,exist_ok=True)
        subprocess.run(['mount','--bind',str(build),str(recorded)],check=True)
        subprocess.run(['mount','-o','remount,bind,ro',str(recorded)],check=True)
    else:
        command=['unshare','--mount','--propagation','private',sys.executable,
                 str(Path(__file__).resolve()),*sys.argv[1:],
                 '--bind-recorded-build',str(recorded)]
        raise SystemExit(subprocess.run(command).returncode)
    paths=set(build.rglob('*.pcm'))|set(build.rglob('*.modmap'))|set(build.glob('*.a'))
    paths|=set((repo/'src').rglob('*.cpp'))|set((repo/'src').rglob('*.cppm'))
    before=snap(paths);out.mkdir(parents=True)
    (out/'inputs-before.json').write_text(json.dumps(before,indent=2))
    try:
        conn=(repo/'src/protocol/quic/quic_connection.cpp').read_text()
        conn=once(conn,'import :connection;','import :connection;\nimport cnetmod.core.log;')
        conn=once(conn,'quic_connection::~quic_connection() = default;',r'''quic_connection::~quic_connection()
{
    logger::warn{"weak_connection_destroy cid={}",
        impl_->local_connection_id ? impl_->local_connection_id->to_string() : std::string{}};
}''')
        server=(repo/'src/protocol/http/v3/http3_server.cpp').read_text()
        server=once(server,'namespace detail {','namespace detail {\n'+HELPER)
        anchor='''                refresh_connection_routes(connection);
            }

            // Release the last expired batch'''
        server=once(server,anchor,'''                refresh_connection_routes(connection);
                if (connection->is_closed())
                    spawn(context_, detail::weak_release_probe(context_,
                        std::weak_ptr<quic::quic_connection>{connection},
                        connection->local_cid() ? connection->local_cid()->to_string() : std::string{}));
            }

            // Release the last expired batch''')
        lines=subprocess.check_output(['/var/tmp/cnetmod-ninja-1.11/bin/ninja','-t','commands','h3_interop_server'],cwd=recorded,text=True).splitlines()
        link=shlex.split(lines[-1]);start=next(i for i,x in enumerate(link)if x.endswith('/clang++'));link=link[start:link.index('&&',start)]
        logging=[]
        for entry in entries:
            directory=Path(entry['directory'])
            try:arguments=helpers.expand(helpers.tokens(entry),directory)
            except FileNotFoundError:continue
            for arg in arguments:
                if not arg.startswith(('-fmodule-file=cnetmod.core.log=', '-fmodule-file=cnetmod.core.log:')):continue
                module=Path(arg.rsplit('=',1)[-1])
                if module.suffix != '.pcm' or module.parent.name != 'cnetmod_runtime.dir':continue
                module_name=arg[len('-fmodule-file='):].rsplit('=',1)[0]
                flag='-fmodule-file='+module_name+'='+str((module if module.is_absolute() else directory/module).resolve())
                if flag not in logging:logging.append(flag)
        if not logging:raise RuntimeError('actual logger PCMs missing from compile modmaps')
        for variant in ('expired-held','expired-released'):
            d=out/variant;d.mkdir()
            sources={'quic_connection.cpp':conn,'http3_server.cpp':server}
            if variant=='expired-held':sources['http3_server.cpp']=once(server,'            expired.clear();','            (void)expired; // negative: retain owning batch across wait')
            for name,source in sources.items():
                (d/name).write_text(source)
                entry=next(e for e in entries if Path(e['file']).name==name)
                command,cwd=helpers.compile_command(entry,name,d,logging)
                (d/(name+'.command.json')).write_text(json.dumps(command,indent=2))
                subprocess.run(command,cwd=cwd,check=True)
            replacements={}
            for library,name in (('libcnetmod_quic.a','quic_connection.cpp'),('libcnetmod_http.a','http3_server.cpp')):
                archive=d/library;shutil.copyfile(build/library,archive)
                members=subprocess.check_output(['ar','t',str(archive)],text=True).splitlines()
                if members.count(name+'.o')!=1:raise RuntimeError('ambiguous archive member '+name)
                subprocess.run(['ar','r',str(archive),str(d/(name+'.o'))],check=True)
                replacements[(build/library).resolve()]=archive
            command=list(link);command[command.index('-o')+1]=str(d/'h3_interop_server')
            for i,arg in enumerate(command):
                if arg.endswith('.a'):
                    original=(build/arg).resolve()
                    if original in replacements:command[i]=str(replacements[original])
                if arg.startswith('--dependency-file='):command[i]='--dependency-file='+str(d/'link.d')
            (d/'link-command.json').write_text(json.dumps(command,indent=2))
            subprocess.run(command,cwd=recorded,check=True)
        (out/'outputs.json').write_text(json.dumps(snap(set(out.rglob('*.cpp'))|set(out.rglob('*.a'))|set(out.rglob('*.o'))|set(out.rglob('h3_interop_server'))),indent=2))
    finally:
        after=snap(paths);(out/'inputs-after.json').write_text(json.dumps(after,indent=2))
        changed=[p for p in before.keys()|after.keys()if before.get(p)!=after.get(p)]
        (out/'integrity.json').write_text(json.dumps({'unchanged':not changed,'changed':changed},indent=2))
        if changed:raise RuntimeError('source/build inputs changed: '+repr(changed))
if __name__=='__main__':main()
