"""Offline ELF-only DWARF/symbol validation. No inferior is created."""
import gdb
import json
assert gdb.selected_inferior().pid == 0
gdb.execute('set may-call-functions off')
result={'inferior_pid':0,'types':{},'symbols':{}}
def lookup(*names):
    for name in names:
        try: return gdb.lookup_type(name).strip_typedefs()
        except gdb.error: pass
    raise ValueError('missing type '+repr(names))
def fields(name, wanted):
    t=lookup(name, name.split('::')[-1])
    fs={f.name:f for f in t.fields() if f.name}
    out={'sizeof':int(t.sizeof),'fields':{}}
    for field in wanted:
        f=fs[field]
        out['fields'][field]={'type':str(f.type),'bytes':int(f.type.sizeof),
                              'bitpos':int(f.bitpos),'bitsize':f.bitsize}
    result['types'][name]=out
fields('http3_server::impl',('connections_','sessions_','timers_',
    'active_connections_','inbox_budget_','shards_'))
fields('mi_heap_t',('subproc','abandoned_count','theaps','next'))
fields('mi_theap_t',('pages','hnext'))
fields('mi_page_t',('block_size','capacity','reserved','xused','xthread_free','next'))
fields('buffer_pool',('free_blocks_','block_size_','max_blocks_'))
for symbol in (
 '_ZZN7cnetmod6detailW7cnetmodW8executorW8async_op23small_udp_datagram_poolEvE4pool',
 '_ZZN7cnetmod6detailW7cnetmodW8executorW8async_op23large_udp_datagram_poolEvE4pool',
 '_ZL20mi_process_heap_main','_mi_theap_empty'):
    v=gdb.parse_and_eval("&'"+symbol+"'")
    result['symbols'][symbol]={'link_address':hex(int(v)),'type':str(v.type)}
# Snapshot dynamically resolves these symbols again after ASLR; these link
# addresses are validation evidence only, never substituted into the helper.
result['ok']=True
open('/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-cold12-f971/verified-layout.json','w').write(json.dumps(result,indent=2)+'\n')
print('F971_OFFLINE_LAYOUT_OK',json.dumps(result))
