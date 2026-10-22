import shutil, subprocess
from pathlib import Path
root=Path('/root/cnetmod-ws-20261020')
out=root/'optimization'
symfs=out/'symfs'
shutil.copytree(root/'runtime-rootfs',symfs,dirs_exist_ok=True)
shutil.copyfile(out/'images/cnetmod-ws-reviewed/bench_ws_server',symfs/'bench_ws_server')
with (out/'final-perf-report.txt').open('w') as stream:
    subprocess.run(['perf','report','-i',str(out/'final-perf.data'),'--symfs',str(symfs),'--kallsyms',str(out/'final-perf.kallsyms'),'--stdio',
                    '--no-children','--no-inline','-g','none','--sort','dso,symbol','--percent-limit','0.4'],stdout=stream,check=True)
lines=[' '.join(line.split()) for line in (out/'final-perf-report.txt').read_text().splitlines()]
(out/'final-perf-report.compact.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(line[:240] for line in lines[:65]))
