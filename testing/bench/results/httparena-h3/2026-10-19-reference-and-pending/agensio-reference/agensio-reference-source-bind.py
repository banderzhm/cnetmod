from pathlib import Path
import subprocess,json,hashlib
root=Path('/var/tmp/agensio-reference-alpha23-20261019');out=Path('/var/tmp/agensio-reference-identity-20261019')
hashfile=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
head=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
builder_head=subprocess.check_output(['git','--git-dir='+str(out/'builder-git'),'rev-parse','HEAD'],text=True).strip()
assert head==builder_head=='e60c61c7643e47c046fbb5136f8b46f7f7f9289b'
files={str(p.relative_to(root/'src')):hashfile(p) for p in (root/'src').rglob('*') if p.is_file()}
actual={str(p.relative_to(out/'builder-src')):hashfile(p) for p in (out/'builder-src').rglob('*') if p.is_file()}
assert files==actual
assert hashfile(root/'CMakeLists.txt')==hashfile(out/'builder-CMakeLists.txt')
report=json.loads((out/'identity.json').read_text());report.update(source_head=head,builder_head=builder_head,source_files=len(files),source_matches_builder=True,source_hashes=files,cmake_sha256=hashfile(root/'CMakeLists.txt'))
(out/'identity.json').write_text(json.dumps(report,indent=2)+'\n')
print(head,len(files),'source files match builder; runtime binary matches builder')
