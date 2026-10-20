"""GDB Python: offline DWARF only, with no inferior and no calls."""
import gdb
import json

assert gdb.selected_inferior().pid == 0
gdb.execute("set may-call-functions off")
result = {"inferior_pid": 0, "types": {}}
for short in ("quic_write_completion", "quic_write_command", "quic_write_queue", "quic_connection_impl"):
    for candidate in ("cnetmod::quic::" + short, short):
        try:
            value = gdb.lookup_type(candidate)
            result["types"][short] = {"dwarf_name": str(value), "bytes": int(value.sizeof),
                "fields": [{"name": field.name, "offset": int(field.bitpos) // 8,
                            "bytes": int(field.type.sizeof)} for field in value.fields()
                           if field.name and hasattr(field, "bitpos")]}
            break
        except (gdb.error, AttributeError, TypeError):
            continue
    else:
        result["types"][short] = {"unavailable": True}
open("/var/tmp/cnetmod-h3-frame-types-298bf.json", "w").write(json.dumps(result, indent=2) + "\n")
print(json.dumps({name: value.get("bytes", "unavailable") for name, value in result["types"].items()}))
