from pathlib import Path
import hashlib,json,subprocess,shutil
root=Path('/var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0')
freeze=Path('/var/tmp/cnetmod-h3-owned-fast-candidate-fbaeb04a21614732b24a7fdae850fcd0')
ab=Path('/var/tmp/cnetmod-h3-owned-three-ab-2e4e71d05d0b47d1a5f5b334ce9ea8be')
prep=Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-owned-fast-execution-fbaeb04a21614732b24a7fdae850fcd0')
out=Path('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-owned-fast-evidence-fbaeb04a21614732b24a7fdae850fcd0');out.mkdir()
summary=json.loads((ab/'summary.json').read_text());assert summary['valid'] and summary['inputs_unchanged'] and summary['cleanup']['returncode']==0 and not summary['cleanup']['remaining'] and len(summary['rounds'])==54
shutil.copytree(ab,out/'ab54',ignore=shutil.ignore_patterns('arena_h3_server'))
shutil.copytree(root,out/'linux-tests-build-asm')
shutil.copytree(prep,out/'execution-scripts-source',ignore=shutil.ignore_patterns('__pycache__'))
for name in ['source-snapshot','system-allocator']:shutil.copytree(freeze/name,out/'fast-freeze'/name)
for name in ['candidate-variant.json','mixed-fast-variants.json','three-variants.json','freeze-result.json']:shutil.copy2(freeze/name,out/'fast-freeze'/name)
analysis=subprocess.check_output(['python',str(prep/'analyze-three.py'),str(ab/'summary.json')]);(out/'analysis.json').write_bytes(analysis)
v=json.loads((freeze/'three-variants.json').read_text());external=[]
for label,value in v.items():
 b=Path(value['binary']);assert hashlib.sha256(b.read_bytes()).hexdigest()==value['binary_sha256']
 proof=json.loads(Path(value['allocator_proof']).read_text())
 for n,digest in proof['files'].items():
  f=Path(value['proof_build_root'])/n;assert hashlib.sha256(f.read_bytes()).hexdigest()==digest.lower();external.append(dict(variant=label,path=str(f),bytes=f.stat().st_size,sha256=digest.lower()))
 for i,method in enumerate(json.loads((root/'owned-asm/packer-manifest.json').read_text())[label]):
  if not method['name'].startswith('cnetmod::quic::quic_connection@'):continue
  addresses=[line.split(':',1)[0].strip() for line in method['relevant_calls']]
  cmd=['addr2line','-f','-C','-i','-e',str(b),*['0x'+a for a in addresses]]
  data=subprocess.check_output(cmd);dest=out/'linux-tests-build-asm/owned-asm'/label/'packer-insert-inline-dwarf.txt';dest.write_bytes(data)
(out/'external-binary-proof-manifest.json').write_text(json.dumps(external,indent=2)+'\n')
files=[]
for f in sorted(out.rglob('*')):
 if f.is_file():files.append(dict(path=f.relative_to(out).as_posix(),bytes=f.stat().st_size,sha256=hashlib.sha256(f.read_bytes()).hexdigest()))
(out/'manifest.json').write_text(json.dumps(dict(valid=True,files=files,source='preserved actual raw output; no historical snapshots modified; ELF/archive backing inputs remain external'),indent=2)+'\n')
print(json.dumps(dict(output=str(out),files=len(files),bytes=sum(f['bytes'] for f in files),valid=True)))
