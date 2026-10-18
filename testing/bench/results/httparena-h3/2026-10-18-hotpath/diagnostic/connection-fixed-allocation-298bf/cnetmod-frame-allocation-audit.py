"""Read existing ELF disassembly only; never start the inspected program."""
import argparse
from collections import deque
import hashlib
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
assert not args.out.exists()
command = ["llvm-objdump", "--disassemble", "--demangle", "--no-show-raw-insn", str(args.binary)]
process = subprocess.Popen(command, stdout=subprocess.PIPE, text=True)
function, previous, records = "", deque(maxlen=22), []
for line in process.stdout:
    header = re.match(r"^[0-9a-f]+ <(.*)>:$", line.rstrip())
    if header:
        function = header.group(1)
        previous.clear()
    if ("call" in line and any(name in line for name in
            ("operator new(", "_Znwm", "_Znam", "mi_new", "mi_malloc", "aligned_alloc", "posix_memalign"))):
        context = list(previous) + [line.rstrip()]
        size = None
        for instruction in reversed(context[:-1]):
            found = re.search(r"mov[lq]?\s+\$(0x[0-9a-f]+|[0-9]+), %e?di\b", instruction)
            if found:
                size = int(found.group(1), 0)
                break
        if "cnetmod" in function or (size is not None and size >= 8192):
            records.append({"function": function, "constant_first_argument_candidate": size,
                            "context": context})
    previous.append(line.rstrip())
assert process.wait() == 0
args.out.write_text(json.dumps({"binary": str(args.binary),
    "sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(), "command": command,
    "caveat": "Nearby constant edi is a candidate, not dataflow proof; inspect context before attributing frame sizes.",
    "records": records}, indent=2) + "\n")
for row in sorted(records, key=lambda item: item["constant_first_argument_candidate"] or 0, reverse=True):
    size = row["constant_first_argument_candidate"]
    if size is not None and size >= 8192 and "cnetmod" in row["function"]:
        print(size, row["function"])
