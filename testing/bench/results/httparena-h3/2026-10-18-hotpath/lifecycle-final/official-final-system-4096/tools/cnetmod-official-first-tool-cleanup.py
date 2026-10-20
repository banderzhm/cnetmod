"""Retire only the proven nonce-owned orphan watchdog group from failed setup."""
from pathlib import Path
import json
import os
import signal
import subprocess
import time

root=Path('/var/tmp/cnetmod-h3-final-system-official-mtu4096-gso1-11b22d441e264aa4974532e10f2a8066')
group=753
expected=str(root/'arena/scripts/validate.sh')
def members():
    found=[]
    for path in Path('/proc').iterdir():
        if not path.name.isdigit():continue
        try:
            pid=int(path.name)
            if os.getpgid(pid)!=group:continue
            stat=(path/'stat').read_text().rpartition(')')[2].split()
            if stat[0]=='Z':continue
            argv=(path/'cmdline').read_bytes().split(b'\0')
            found.append({'pid':pid,'ppid':int(stat[1]),'sid':os.getsid(pid),
                          'argv':[v.decode() for v in argv if v]})
        except (FileNotFoundError,ProcessLookupError):pass
    return found
before=members()
leaders=[r for r in before if r['argv']==['bash',expected,'cnetmod-official-optimized']]
assert len(leaders)==1,before
leader=leaders[0]['pid']
assert all(r['sid']==group and (r['pid']==leader or
           (r['ppid']==leader and r['argv']==['sleep','800'])) for r in before),before
os.killpg(group,signal.SIGTERM)
for _ in range(100):
    if not members():break
    time.sleep(.02)
after=members()
docker=subprocess.check_output(['docker','ps','-aq','--filter',
    'label=cnetmod.official.run=11b22d441e264aa4974532e10f2a8066'],text=True).split()
record={'scope':'failed runner setup only; no timed profiles ran','runner_exit':1,
 'reason':'finished validator watchdog descendants held inherited stdout pipe; communicate EOF timed out',
 'group':group,'before':before,'after':after,'docker_owned_remaining':docker}
(root/'first-tool-failure-cleanup.json').write_text(json.dumps(record,indent=2)+'\n')
assert after==[] and docker==[]
print(json.dumps(record,indent=2))
