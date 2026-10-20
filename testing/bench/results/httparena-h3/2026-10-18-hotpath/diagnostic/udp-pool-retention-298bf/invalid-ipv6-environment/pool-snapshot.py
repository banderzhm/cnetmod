"""GDB memory-only cold snapshot. Source this file; run cnetmod-cold-pools.

No inferior function is called, no pool is created, and no inferior memory is
written. Optional argument: numeric address of an existing http3_server::impl.
Without it, find main's live `server` local through DWARF. Missing information
is reported rather than guessed. Linux x86-64/libc++ or libstdc++ atomics only.
"""
import json
import time
from pathlib import Path
import gdb


POOL_SYMBOLS = {
    "small": "_ZZN7cnetmod6detailW7cnetmodW8executorW8async_op23small_udp_datagram_poolEvE4pool",
    "large": "_ZZN7cnetmod6detailW7cnetmodW8executorW8async_op23large_udp_datagram_poolEvE4pool",
}


def lookup_type(qualified, module_local):
    # Clang's current C++ Modules DWARF exposes several declarations to GDB
    # without their source namespaces. Do not guess memory offsets instead.
    try:
        return gdb.lookup_type(qualified)
    except gdb.error:
        return gdb.lookup_type(module_local)


def integer(value):
    return int(value)


def atomic(value):
    # Existing target's std::atomic<size_t>/uint64_t/int64_t stores its scalar
    # at offset zero. Check width; read memory, never call .load().
    if value.type.sizeof != 8:
        raise ValueError("unsupported atomic layout: " + str(value.type))
    return int(value.address.cast(gdb.lookup_type("unsigned long long").pointer()).dereference())


def children(value):
    value = value.cast(value.type.strip_typedefs())
    if value.type.code not in (gdb.TYPE_CODE_STRUCT, gdb.TYPE_CODE_UNION):
        return
    for field in value.type.fields():
        try:
            if field.is_base_class:
                yield value.cast(field.type)
            elif field.name:
                yield value[field.name]
        except gdb.error:
            continue


def smart_pointer(value, target_name, depth=0):
    kind = value.type.strip_typedefs()
    if kind.code == gdb.TYPE_CODE_PTR:
        if target_name in str(kind.target().strip_typedefs()):
            # Type.target() on an unstripped pointer typedef returns its
            # underlying pointer type rather than the pointed-to object.
            return value.cast(kind)
        raise ValueError("pointer target mismatch")
    if depth < 9:
        # libc++ has an anonymous compressed-pair structure; promoted member
        # lookup reaches its pointer even though that DWARF field has no name.
        for name in ("__ptr_", "_M_ptr", "__value_"):
            try:
                return smart_pointer(value[name], target_name, depth + 1)
            except (ValueError, gdb.error):
                pass
        for child in children(value) or ():
            try:
                return smart_pointer(child, target_name, depth + 1)
            except (ValueError, gdb.error):
                pass
    raise ValueError("smart pointer layout unavailable for " + target_name)


def named_field(value, names, depth=0):
    for name in names:
        try:
            return value[name]
        except gdb.error:
            pass
    if depth < 6:
        for child in children(value) or ():
            try:
                return named_field(child, names, depth + 1)
            except (ValueError, gdb.error):
                pass
    raise ValueError("container layout unavailable: " + ",".join(names))


def vector_elements(value):
    begin = named_field(value, ("__begin_", "_M_start"))
    end = named_field(value, ("__end_", "_M_finish"))
    count = int(end - begin)
    if not 0 <= count <= 4096:
        raise ValueError("unexpected shard vector length")
    return [begin[index] for index in range(count)]


def pool_snapshot(label, symbol):
    result = {"symbol": symbol}
    try:
        address = gdb.parse_and_eval("&'" + symbol + "'")
        pool_type = lookup_type("cnetmod::buffer_pool", "buffer_pool")
        pointer = address.cast(pool_type.pointer().pointer()).dereference()
        result["address"] = hex(int(pointer))
        if not int(pointer):
            result["constructed"] = False
            return result
        result["constructed"] = True
        value = pointer.dereference()
        queue = value["free_blocks_"]
        block = int(value["block_size_"])
        cap = int(queue["capacity_"])
        put = atomic(queue["enqueue_pos_"]["value"])
        take = atomic(queue["dequeue_pos_"]["value"])
        count = min(cap, put - take if put >= take else 0)
        cells = smart_pointer(queue["cells_"], "cell")
        # Same formula as pool_size()/approximate_size(). Full stop avoids
        # changing counters but can catch an interrupted enqueue/dequeue.
        result.update(block_size=block, max_blocks=int(value["max_blocks_"]),
                      capacity=cap, enqueue_position=put, dequeue_position=take,
                      cached_blocks_approx=count,
                      cached_payload_bytes=count * block,
                      queue_cell_bytes=cells.type.target().sizeof,
                      queue_storage_bytes=cap * cells.type.target().sizeof,
                      sampled_raw_allocation_addresses=[])
        # Read only ready cells, without popping them. Pre-change pointers
        # point at the one-byte empty block_node, not data+1.
        pointer_type = gdb.lookup_type("void").pointer()
        for index in range(min(count, 4)):
            position = take + index
            cell = cells[position & int(queue["mask_"])]
            if atomic(cell["sequence"]) != position + 1:
                result.setdefault("interrupted_queue_slots", []).append(position)
                continue
            raw = cell["storage"].address.cast(pointer_type.pointer()).dereference()
            result["sampled_raw_allocation_addresses"].append(hex(int(raw)))
    except (gdb.error, ValueError, RuntimeError) as error:
        result["unavailable"] = str(error)
    return result


def automatic_impl():
    original = gdb.selected_thread()
    try:
        for thread in gdb.selected_inferior().threads():
            thread.switch()
            frame = gdb.newest_frame()
            while frame:
                if frame.name() == "main":
                    server = frame.read_var("server")
                    outer = smart_pointer(server, "http3_server").dereference()
                    return smart_pointer(outer["impl_"], "http3_server::impl")
                frame = frame.older()
    finally:
        if original:
            original.switch()
    raise ValueError("main/server local unavailable; supply an existing impl address")


def server_snapshot(argument):
    result = {}
    try:
        if argument:
            address = int(argument, 0)  # Numeric addresses only; no expressions/calls.
            impl = gdb.Value(address).cast(lookup_type(
                "cnetmod::http::v3::http3_server::impl", "http3_server::impl").pointer())
        else:
            impl = automatic_impl()
        value = impl.dereference()
        budget = smart_pointer(value["inbox_budget_"], "inbox_budget_state").dereference()
        result.update(impl_address=hex(int(impl)),
            shared_budget={field: atomic(budget[field]) for field in (
                "active_inboxes", "queued", "peak_queued", "capacity_dropped",
                "budget_dropped", "creation_dropped")}, shards=[])
        pending = [impl]
        seen = set()
        while pending:
            current = pending.pop()
            if int(current) in seen:
                raise ValueError("duplicate shard pointer")
            seen.add(int(current))
            item = current.dereference()
            record = {"address": hex(int(current))}
            for field in ("active_connections_", "inbox_enqueued_datagrams_",
                          "dropped_inbox_datagrams_", "pooled_receive_datagrams_",
                          "heap_receive_datagrams_"):
                record[field] = atomic(item[field])
            result["shards"].append(record)
            for child in vector_elements(item["shards_"]):
                pending.append(smart_pointer(child, "http3_server::impl"))
        result["active_connections_sum"] = sum(
            item["active_connections_"] for item in result["shards"])
        result["budget_counted_once"] = True
    except (gdb.error, ValueError, RuntimeError) as error:
        result["unavailable"] = str(error)
    return result


def allocator_snapshot():
    result = {"scope": "raw subprocess OS accounting; not live-allocation bytes",
              "malloc_live_bytes": None,
              "reason": "native build MI_STATS=OFF; never infer live=0 from disabled counters"}
    try:
        address = gdb.parse_and_eval("&'_ZL20mi_process_heap_main'")
        heap = address.cast(gdb.lookup_type("mi_heap_t").pointer()).dereference()
        subproc = heap["subproc"].dereference()
        stats = subproc["stats"]
        result["stats_version"] = int(stats["version"])
        for name in ("reserved", "committed"):
            result[name] = {field: int(stats[name][field]) for field in (
                "current", "peak", "total")}
        result["arena_count"] = atomic(subproc["arena_count"])
        result["thread_count"] = atomic(subproc["thread_count"])
        result["heap_count"] = atomic(subproc["heap_count"])
    except (gdb.error, ValueError, RuntimeError) as error:
        result["unavailable"] = str(error)
    try:
        # Pure debugger-side arithmetic, then read the static bin table. This
        # neither calls mi_good_size/mi_usable_size nor visits live heap pages.
        empty = gdb.parse_and_eval("'_mi_theap_empty'")
        result["static_bin_table"] = {}
        for requested in (2048, 2049, 65536, 65537):
            word_count_minus_one = (requested + 7) // 8 - 1
            highest = word_count_minus_one.bit_length() - 1
            bin_index = ((highest << 2) + (
                (word_count_minus_one >> (highest - 2)) & 3)) - 3
            result["static_bin_table"][str(requested)] = {
                "bin": bin_index,
                "block_size": int(empty["pages"][bin_index]["block_size"])}
        result["static_bin_table_scope"] = (
            "Release MI_PADDING=0, MI_GUARDED=OFF assumptions; class sizes, not live block counts")
    except (gdb.error, ValueError, RuntimeError) as error:
        result["static_bin_table_unavailable"] = str(error)
    return result


def owned_pages_snapshot():
    """Bounded linked ownership graph only; never claim complete heap census."""
    result = {"scope": "owned theap page queues plus detached metadata theap",
              "abandoned_pages_enumerated": False,
              "live_bytes_exact": False,
              "used_includes_deferred_thread_free": True,
              "limits": {"heaps": 256, "theaps": 2048, "pages": 32768,
                         "debugger_wall_seconds": 5}, "bins": {}}
    heaps, theaps, pages = set(), set(), set()
    deadline = time.monotonic() + 5

    def bound(collection, maximum):
        if len(collection) > maximum or time.monotonic() > deadline:
            raise ValueError("owned-page traversal bound reached")

    def chain(pointer, field, maximum):
        local = set()
        while int(pointer):
            address = int(pointer)
            if address in local or len(local) >= maximum:
                raise ValueError("cycle or chain length limit: " + field)
            if time.monotonic() > deadline:
                raise ValueError("owned-page traversal deadline reached")
            local.add(address)
            value = pointer.dereference()
            yield address, value
            pointer = value[field]

    def visit_theap(pointer):
        address = int(pointer)
        if not address or address in theaps:
            return
        theaps.add(address)
        bound(theaps, 2048)
        value = pointer.dereference()
        queues = value["pages"]
        low, high = queues.type.strip_typedefs().range()
        if low != 0 or not 1 <= high + 1 <= 128:
            raise ValueError("unsupported mimalloc page queue array")
        for index in range(high + 1):
            for page_address, page in chain(queues[index]["first"], "next", 32768):
                if page_address in pages:
                    raise ValueError("page occurs in more than one owned queue")
                pages.add(page_address)
                bound(pages, 32768)
                block = int(page["block_size"])
                capacity, reserved = int(page["capacity"]), int(page["reserved"])
                used = int(page["xused"]["used_alloc"]) & 0xFFFF
                if not (0 < block <= (1 << 40) and 0 <= used <= capacity <= reserved <= 65535):
                    raise ValueError("page invariant unavailable (possibly interrupted mutation)")
                # xthread_free has a low ownership flag; count pages with a
                # deferred remote-free list but never collect or follow it.
                has_thread_free = bool(atomic(page["xthread_free"]) & ~1)
                row = result["bins"].setdefault(str(block), dict(
                    pages=0, used_blocks_upper=0, used_bytes_upper=0,
                    committed_block_capacity_bytes=0, reserved_block_capacity_bytes=0,
                    definitely_empty_pages=0, empty_reserved_block_capacity_bytes=0,
                    pages_with_deferred_thread_free=0))
                row["pages"] += 1
                row["used_blocks_upper"] += used
                row["used_bytes_upper"] += used * block
                row["committed_block_capacity_bytes"] += capacity * block
                row["reserved_block_capacity_bytes"] += reserved * block
                row["pages_with_deferred_thread_free"] += int(has_thread_free)
                if not used:
                    if has_thread_free:
                        raise ValueError("empty page has deferred thread-free blocks")
                    row["definitely_empty_pages"] += 1
                    row["empty_reserved_block_capacity_bytes"] += reserved * block

    try:
        main_address = gdb.parse_and_eval("&'_ZL20mi_process_heap_main'")
        main_pointer = main_address.cast(gdb.lookup_type("mi_heap_t").pointer())
        subproc = main_pointer.dereference()["subproc"].dereference()
        heap_pointers = [main_pointer]
        for address, heap in chain(subproc["heaps"], "next", 256):
            heap_pointers.append(gdb.Value(address).cast(main_pointer.type))
        for pointer in heap_pointers:
            if int(pointer) in heaps:
                continue
            heaps.add(int(pointer))
            bound(heaps, 256)
            for address, theap in chain(pointer.dereference()["theaps"], "hnext", 2048):
                visit_theap(gdb.Value(address).cast(gdb.lookup_type("mi_theap_t").pointer()))
        visit_theap(subproc["theap_meta"])
        result["complete_within_declared_scope"] = True
    except (gdb.error, ValueError, RuntimeError) as error:
        result["complete_within_declared_scope"] = False
        result["unavailable"] = str(error)
    result.update(heaps_visited=len(heaps), theaps_visited=len(theaps), pages_visited=len(pages))
    result["owned_used_bytes_upper"] = sum(row["used_bytes_upper"] for row in result["bins"].values())
    result["owned_empty_reserved_capacity_bytes"] = sum(
        row["empty_reserved_block_capacity_bytes"] for row in result["bins"].values())
    return result


class ColdPools(gdb.Command):
    def __init__(self):
        super().__init__("cnetmod-cold-pools", gdb.COMMAND_USER)

    def invoke(self, argument, from_tty):
        self.dont_repeat()
        inferior = gdb.selected_inferior()
        if any(thread.is_running() for thread in inferior.threads()):
            raise gdb.GdbError("all target threads must be stopped")
        if int(gdb.lookup_type("void").pointer().sizeof) != 8:
            raise gdb.GdbError("snapshot prepared for Linux x86-64 only")
        gdb.execute("set may-call-functions off", to_string=True)
        result = {"pid": inferior.pid, "snapshot_time": time.time(),
                  "inferior_function_calls": False, "inferior_writes": False,
                  "pools": {name: pool_snapshot(name, symbol)
                            for name, symbol in POOL_SYMBOLS.items()},
                  "server": server_snapshot(argument.strip()),
                  "mimalloc": allocator_snapshot(),
                  "owned_pages": owned_pages_snapshot()}
        if inferior.pid:
            for name in ("status", "smaps_rollup"):
                try:
                    result["proc_" + name] = Path(
                        f"/proc/{inferior.pid}/{name}").read_text()
                except OSError as error:
                    result["proc_" + name] = {"unavailable": str(error)}
        gdb.write("CNETMOD_COLD_SNAPSHOT " + json.dumps(result, sort_keys=True) + "\n")


ColdPools()
