"""Offline current ELF type inventory; no inferior, calls or target writes."""
import gdb
import json

assert gdb.selected_inferior().pid == 0
gdb.execute("set may-call-functions off")
result = {}


def find_field_type(value, wanted, depth=0):
    if depth > 12:
        raise ValueError("field recursion bound")
    value = value.strip_typedefs()
    for field in value.fields():
        if field.name == wanted:
            return field.type
    for field in value.fields():
        if field.is_base_class or field.name is None:
            try:
                return find_field_type(field.type, wanted, depth + 1)
            except (gdb.error, ValueError, TypeError):
                pass
    raise ValueError("missing field " + wanted)


for label, names in {
    "detector": ["cnetmod::quic::loss_detector::inflight_packet_map", "loss_detector::inflight_packet_map", "inflight_packet_map"],
    "recovery": ["cnetmod::quic::quic_recovery_controller::sent_packet_map", "quic_recovery_controller::sent_packet_map", "sent_packet_map"],
}.items():
    errors = []
    for name in names:
        try:
            mapping = gdb.lookup_type(name)
            tree = find_field_type(mapping, "__tree_")
            payload = tree.strip_typedefs().template_argument(0)
            node_name = "std::__1::__tree_node<" + str(payload) + ", void *>"
            node = gdb.lookup_type(node_name)
            result[label] = {"map": str(mapping), "payload": str(payload),
                             "node": str(node), "node_bytes": int(node.sizeof),
                             "payload_bytes": int(payload.sizeof)}
            break
        except (gdb.error, ValueError, TypeError, RuntimeError) as error:
            errors.append(name + ": " + str(error))
    else:
        result[label] = {"unavailable": errors}
if "unavailable" in result["recovery"]:
    try:
        spelling = "std::__1::__tree_node<std::__1::__value_type<unsigned long, cnetmod::quic::quic_recovery_controller::sent_packet>, void *>"
        node = gdb.lookup_type(spelling)
        result["recovery"] = {"node": str(node), "node_bytes": int(node.sizeof),
                              "method": "full instantiated node type from DWARF inventory"}
    except gdb.error as error:
        result["recovery"]["full_node_unavailable"] = str(error)
if result.get("detector", {}).get("payload_bytes") == 0:
    result["detector"]["payload_bytes"] = None
    result["detector"]["payload_type_note"] = "DWARF forward declaration reports size0; do not interpret as real zero-sized payload"
result["type_inventory"] = gdb.execute("info types inflight_packet", to_string=True)
result["sent_type_inventory"] = gdb.execute("info types sent_packet", to_string=True)
open("/var/tmp/cnetmod-h3-quic-node-dwarf-05fe.json", "w").write(json.dumps(result, indent=2) + "\n")
print(json.dumps({key: value for key, value in result.items() if "inventory" not in key}, indent=2))
