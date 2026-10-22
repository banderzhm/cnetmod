import json, subprocess, sys
from pathlib import Path
root=Path('/root/cnetmod-ws-20261020')
settings={'net.core.somaxconn':'65535','net.ipv4.tcp_max_syn_backlog':'65535',
          'net.core.netdev_max_backlog':'65535','net.ipv4.ip_local_port_range':'1024 65535',
          'net.ipv4.tcp_max_tw_buckets':'131072','net.ipv4.tcp_tw_reuse':'1',
          'net.ipv4.ip_local_reserved_ports':'5432,6379,8080,8081,8082,8443,9090',
          'net.core.rmem_max':'7500000','net.core.wmem_max':'7500000'}
if sys.argv[1]=='apply':
    settings={key: value for key,value in settings.items() if Path('/proc/sys/'+key.replace('.','/')).exists()}
    before={key: subprocess.check_output(['sysctl','-n',key],text=True).strip() for key in settings}
    before['mtu']=Path('/sys/class/net/lo/mtu').read_text().strip()
    (root/'network-before.json').write_text(json.dumps(before,indent=2))
else:
    before=json.loads((root/'network-before.json').read_text())
    settings={key: value for key,value in before.items() if key!='mtu'}
for key,value in settings.items():
    subprocess.run(['sysctl','-w',f'{key}={value}'],check=True)
subprocess.run(['ip','link','set','lo','mtu','1500' if sys.argv[1]=='apply' else before['mtu']],check=True)
