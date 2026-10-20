"""Offline GDB type audit only: no target process, reads/writes/calls."""
import gdb
import json


def field(t, name):
    return next(f for f in t.fields() if f.name == name)


pool = lookup_type("cnetmod::buffer_pool", "buffer_pool")
server = lookup_type("cnetmod::http::v3::http3_server", "http3_server")
impl = lookup_type("cnetmod::http::v3::http3_server::impl", "http3_server::impl")
queue = field(pool, "free_blocks_").type
cell = field(queue, "cells_").type
owner = field(server, "impl_").type
budget_ptr = field(impl, "inbox_budget_").type
rows = []
main_block = gdb.block_for_pc(int(gdb.parse_and_eval("&main")))
main_owner = next(symbol.type for symbol in main_block if symbol.name == "server")
main_pointer = smart_pointer(gdb.Value(bytes(main_owner.sizeof), main_owner), "http3_server")
rows.append({"label": "main.server", "type": str(main_owner),
             "target": str(main_pointer.type.target()), "zero_pointer": int(main_pointer)})
for label, t, needle in (("pool.cells", cell, "cell"),
                         ("server.impl", owner, "http3_server::impl"),
                         ("impl.budget", budget_ptr, "inbox_budget_state")):
    pointer = smart_pointer(gdb.Value(bytes(t.sizeof), t), needle)
    rows.append({"label": label, "object_size": t.sizeof,
                 "zero_pointer": int(pointer), "target": str(pointer.type.target())})
budget = smart_pointer(gdb.Value(bytes(budget_ptr.sizeof), budget_ptr),
                       "inbox_budget_state").type.target()
cursor = field(queue, "enqueue_pos_").type
rows.append({"label": "pool.cursor.atomic", "width": field(cursor, "value").type.sizeof})
for label, t, fields in (
    ("pool", pool, ("block_size_", "max_blocks_", "free_blocks_")),
    ("queue", queue, ("cells_", "capacity_", "mask_", "enqueue_pos_", "dequeue_pos_")),
    ("impl", impl, ("active_connections_", "inbox_budget_", "shards_",
                    "inbox_enqueued_datagrams_", "dropped_inbox_datagrams_",
                    "pooled_receive_datagrams_", "heap_receive_datagrams_")),
    ("budget", budget, ("active_inboxes", "queued", "peak_queued",
                        "capacity_dropped", "budget_dropped", "creation_dropped"))):
    rows.append({"label": label, "size": t.sizeof, "fields": {
        name: {"offset": field(t, name).bitpos // 8,
               "size": field(t, name).type.sizeof} for name in fields}})
print(json.dumps({"scope": "offline DWARF plus debugger-owned zero values",
                  "inferior_pid": gdb.selected_inferior().pid,
                  "rows": rows}, indent=2))
