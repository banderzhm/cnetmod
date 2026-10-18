from pathlib import Path
import hashlib,json,subprocess
build=Path('/var/tmp/cnetmod-h3-arena-build');recorded=Path('/tmp/cnetmod-h3-arena-build');out=Path('/var/tmp/cnetmod-h3-cid-failure-controls-final')
recorded.mkdir(parents=True,exist_ok=True);subprocess.run(['mount','--bind',str(build),str(recorded)],check=True);subprocess.run(['mount','-o','remount,bind,ro',str(recorded)],check=True)
proof=[]
for name in ['without-cid-close','with-cid-close']:
 d=out/name;archive=d/'libcnetmod_quic.a';member=subprocess.check_output(['ar','p',str(archive),'quic_connection.cpp.o']);sha=hashlib.sha256(member).hexdigest()
 entry={'variant':name,'member_sha256':sha}
 if name=='without-cid-close':
  object_sha=hashlib.sha256((d/'quic_connection.cpp.o').read_bytes()).hexdigest();assert sha==object_sha
  original=hashlib.sha256(subprocess.check_output(['ar','p',str(build/'libcnetmod_quic.a'),'quic_connection.cpp.o'])).hexdigest();assert sha!=original;entry.update(external_object_sha256=object_sha,production_member_sha256=original)
 command=json.loads((d/'corrected-link-command.json').read_text());assert [x for x in command if Path(x).name=='libcnetmod_quic.a']==[str(archive)]
 command += ['-Wl,--trace', '-Wl,-Map,'+str(d/'corrected-link.map')]
 (d/'verified-link-command.json').write_text(json.dumps(command,indent=2));linked=subprocess.run(command,cwd=recorded,check=True,capture_output=True,text=True); (d/'verified-link-trace.log').write_text(linked.stdout+linked.stderr)
 text=(d/'verified-link-trace.log').read_text(); assert str(archive) in text and 'quic_connection.cpp.o' in text, text[-3000:]
 entry.update(binary_sha256=hashlib.sha256((d/'quic_retire_stream_server-corrected').read_bytes()).hexdigest(),trace_verified=True)
 proof.append(entry)
(out/'link-member-proof.json').write_text(json.dumps(proof,indent=2))
