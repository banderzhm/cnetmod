"""Prepared only: freeze an already-built deferred-bulk-only candidate and its OFF proof.

Never edits source or builds. --execute requires the coordinator's exclusive
window (loader binding probe runs without arguments and never starts a listener).
--baseline-variant points to an object in the AB runner's documented format.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('rx_ab',HERE/'cnetmod-h3-deferred-bulk-ab.py')
ab=importlib.util.module_from_spec(spec);spec.loader.exec_module(ab)

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,obj):Path(path).write_text(json.dumps(obj,indent=2)+'\n')

def source_paths(repo):
    result=[]
    for directory in ('src','include','cmake','testing'):
        for current,dirs,names in os.walk(repo/directory):
            dirs[:]=[d for d in dirs if d not in ('results','__pycache__','.git')]
            for name in names:
                p=Path(current)/name
                if p.is_file() and ab.source_name(p.relative_to(repo).as_posix()):result.append(p)
    return sorted([repo/'CMakeLists.txt',*result])

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--execute',action='store_true')
    ap.add_argument('--baseline-variant',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--build',type=Path,default=Path('/tmp/cnetmod-h3-arena-build'))
    ap.add_argument('--source-root',type=Path,default=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod'))
    ap.add_argument('--prepared-source-snapshot',type=Path,required=True)
    args=ap.parse_args();assert args.execute,'prepare only; exclusive window required'
    assert re.fullmatch(r'/var/tmp/cnetmod-h3-deferred-bulk-candidate-[0-9a-f]{32}',str(args.output))
    assert not args.output.exists()
    assert not os.environ.get('LD_PRELOAD') and not os.environ.get('LD_AUDIT')
    baseline=json.loads(args.baseline_variant.read_text())
    baseline_meta_path=Path(baseline['source_snapshot'])/'metadata.json'
    baseline_meta=json.loads(baseline_meta_path.read_text(encoding='utf-8-sig'))
    baseline_sources={n:h.lower() for n,h in baseline_meta['sources'].items() if ab.source_name(n)}
    baseline_proof=json.loads(Path(baseline['allocator_proof']).read_text())
    assert sha(baseline['binary'])==baseline['binary_sha256']==baseline_proof['binary_sha256']
    # A mutable current build is not acceptable as the old proof backing root.
    for name,digest in baseline_proof['files'].items():assert sha(Path(baseline['proof_build_root'])/name)==digest.lower(),name
    paths=source_paths(args.source_root)
    sources={p.relative_to(args.source_root).as_posix():sha(p) for p in paths}
    assert sources.keys()==baseline_sources.keys()
    differences={n for n,h in sources.items() if baseline_sources[n]!=h}
    assert differences=={ab.RX_SOURCE,'testing/http3/deadline_frame_parser.py'},differences
    build=args.build
    binary=build/'testing/bench/arena_h3_server'
    assert binary.read_bytes()[:4]==b'\x7fELF'
    cache=(build/'CMakeCache.txt').read_text()
    assert 'CNETMOD_USE_MIMALLOC:BOOL=OFF' in cache
    args.output.mkdir()
    snapshot=args.output/'source-snapshot';snapshot.mkdir()
    # Native Windows Git generated the managed-worktree HEAD/diff. WSL only
    # verifies every source byte against it; it never rewrites .git paths.
    prepared_meta=json.loads((args.prepared_source_snapshot/'metadata.json').read_text())
    assert prepared_meta['sources']==sources
    assert sha(args.prepared_source_snapshot/'source.diff')==prepared_meta['diff_sha256']
    shutil.copy2(args.prepared_source_snapshot/'source.diff',snapshot/'source.diff')
    shutil.copy2(args.prepared_source_snapshot/'metadata.json',snapshot/'metadata.json')
    shutil.copy2(args.prepared_source_snapshot/'deadline_frame_parser.py',snapshot/'deadline_frame_parser.py')
    proof_dir=args.output/'system-allocator';proof_dir.mkdir()
    backing=args.output/'proof-build-root';backing.mkdir()
    files=['CMakeCache.txt','build.ninja','libcnetmod_runtime.a','libcnetmod_quic.a','libcnetmod_http.a','testing/bench/arena_h3_server','testing/tests/test_udp_batch']
    originals={name:sha(build/name) for name in files}
    for name in files:
        dst=backing/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(build/name,dst)
        assert sha(dst)==originals[name]
    frozen_binary=backing/'testing/bench/arena_h3_server'
    commands=subprocess.check_output(['/var/tmp/cnetmod-ninja-1.11/bin/ninja','-t','commands','arena_h3_server'],cwd=build,text=True).splitlines()
    link=next(c for c in reversed(commands) if ' -o testing/bench/arena_h3_server ' in c)
    (proof_dir/'arena-link-command.txt').write_text(link+'\n');assert 'mimalloc' not in link.lower()
    outputs={}
    for name,command in [('nm-dynamic',['nm','-D',str(frozen_binary)]),('ldd',['ldd',str(frozen_binary)]),('readelf-dynamic',['readelf','-d',str(frozen_binary)]),('runtime-archive-members',['llvm-ar','t',str(backing/'libcnetmod_runtime.a')])]:
        result=subprocess.run(command,capture_output=True,text=True)
        (proof_dir/(name+'.stdout')).write_text(result.stdout)
        (proof_dir/(name+'.stderr')).write_text(result.stderr)
        assert result.returncode==0,name
        outputs[name]=result.stdout
    assert 'mimalloc' not in outputs['ldd'].lower()
    assert not re.search(r'\b[TW]\s+(malloc|calloc|realloc|free)(@|\s|$)',outputs['nm-dynamic'],re.M)
    assert re.search(r'\bU\s+malloc@',outputs['nm-dynamic'])
    env=os.environ.copy()
    env.pop('SSLKEYLOGFILE',None);env.update(LD_DEBUG='bindings,libs',LD_BIND_NOW='1')
    with (proof_dir/'arena-noargs.stdout').open('w') as stdout,(proof_dir/'arena-bindings.stderr').open('w') as stderr:
        result=subprocess.run([str(frozen_binary)],env=env,stdout=stdout,stderr=stderr,timeout=15)
    assert result.returncode==2
    bindings=(proof_dir/'arena-bindings.stderr').read_text()
    selected=[line for line in bindings.splitlines() if re.search(r"symbol [`'](malloc|calloc|realloc|free|_Znwm|_Znam|_ZdlPv|_ZdaPv)'",line)]
    (proof_dir/'allocator-bindings.txt').write_text('\n'.join(selected)+'\n')
    assert any("symbol `malloc'" in line and 'libc.so.6' in line and 'binding file '+str(frozen_binary) in line for line in selected)
    assert any("symbol `_Znwm'" in line and ('libc++.so' in line or 'libc++abi.so' in line) and 'binding file '+str(frozen_binary) in line for line in selected)
    assert not any('mimalloc' in line.lower() for line in selected)
    proof=dict(system_allocator_verified=True,binary=str(frozen_binary),binary_sha256=sha(frozen_binary),noargs_returncode=2,
        ld_preload=None,ld_audit=None,cmake_use_mimalloc_off=True,files=originals)
    save(proof_dir/'allocator-proof.json',proof)
    candidate=dict(binary=str(frozen_binary),binary_sha256=sha(frozen_binary),source_snapshot=str(snapshot),
        allocator_proof=str(proof_dir/'allocator-proof.json'),proof_build_root=str(backing))
    ab.verify_source_difference(dict(baseline=baseline,candidate=candidate),dict(baseline=baseline_sources,candidate=sources),differences)
    save(args.output/'candidate-variant.json',candidate)
    save(args.output/'variants.json',dict(baseline=baseline,candidate=candidate))
    assert all(sha(p)==sources[p.relative_to(args.source_root).as_posix()] for p in paths)
    assert all(sha(build/name)==digest for name,digest in originals.items())
    assert all(sha(Path(baseline['proof_build_root'])/n)==h.lower() for n,h in baseline_proof['files'].items())
    save(args.output/'freeze-result.json',dict(valid=True,source_difference=sorted(differences),source_count=len(sources),
        candidate=candidate,source_unchanged_during_freeze=True,baseline_backing_unchanged=True,
        scripts={str(p):sha(p) for p in [Path(__file__),HERE/'cnetmod-h3-deferred-bulk-ab.py',HERE/'cnetmod-h3-deferred-plaintext-review-5afc/deferred-plaintext.patch']}))
    print('FROZEN',args.output,proof['binary_sha256'],flush=True)

if __name__=='__main__':main()
