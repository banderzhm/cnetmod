"""Run one 35s zero-ingress cold cleanup against an external weak diagnostic.
Only execute in the exclusive network window. Saves actual server output before
TemporaryDirectory teardown; no extra HTTP statistics/probe traffic is added.
"""
from pathlib import Path
import argparse,importlib.util,json,re,sys

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('server',type=Path)
 p.add_argument('--expect-expired',choices=('true','false'),required=True)
 p.add_argument('--output',type=Path,required=True)
 p.add_argument('--repo',type=Path,default=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod'))
 a=p.parse_args()
 sys.path.insert(0,str(a.repo/'testing/http3'))
 spec=importlib.util.spec_from_file_location('cold_cleanup',a.repo/'testing/http3/http3_cold_cleanup.py')
 cold=importlib.util.module_from_spec(spec);spec.loader.exec_module(cold)
 original_popen=cold.subprocess.Popen;original_stop=cold.stop_process;logs={};saved=[]
 def popen(*args,**kwargs):
  process=original_popen(*args,**kwargs)
  stdout=kwargs.get('stdout')
  if hasattr(stdout,'read'):logs[process.pid]=stdout
  return process
 def stop(process):
  result=original_stop(process)
  log=logs.pop(process.pid,None)
  if log is not None:
   log.flush();log.seek(0);saved.append(log.read())
  return result
 cold.subprocess.Popen=popen;cold.stop_process=stop
 sys.argv=['http3_cold_cleanup.py',str(a.server.resolve())]
 try:cold.main()
 finally:
  cold.subprocess.Popen=original_popen
  text='\n'.join(saved)
  a.output.with_suffix('.server.log').write_text(text)
 records=[{'cid':cid,'checkpoint_ms':int(ms),'expired':expired=='true','strong_count':int(count)}
   for cid,ms,expired,count in re.findall(r'weak_release cid=([0-9a-f]+) checkpoint_ms=(100|500) expired=(true|false) strong_count=(\d+)',text)]
 destroyed=re.findall(r'weak_connection_destroy cid=([0-9a-f]+)',text)
 report={'server':str(a.server),'expected_expired':a.expect_expired,'weak_probes':records,'destructor_cids':destroyed,
  'scope':'same-layout external diagnostics; cold routing counters plus actual weak_ptr expiration, no forced collection'}
 a.output.write_text(json.dumps(report,indent=2)+'\n')
 assert records,'no timer-expiry weak probe ran'
 expected=a.expect_expired=='true'
 assert all(x['expired']==expected for x in records),report
 assert {x['checkpoint_ms']for x in records}=={100,500},report
 if expected:assert all(x['strong_count']==0 and x['cid']in destroyed for x in records),report
 print(json.dumps(report,sort_keys=True))
if __name__=='__main__':main()
