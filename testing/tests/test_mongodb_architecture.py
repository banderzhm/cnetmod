"""Enforce MongoDB package ownership and dependency direction without a server."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2] / "src" / "protocol" / "mongodb"
OWNERS = {
    "authentication": "auth", "aws_credentials": "auth", "aws_sasl": "auth", "oidc_credentials": "auth", "sasl_reply": "auth",
    "server_address": "endpoint", "srv_seedlist": "discovery", "system_srv_resolver": "discovery", "connection_string": "configuration",
    "error": "root", "bson_document": "bson", "wire_protocol": "wire",
    "scram_sha256": "auth", "connection_options": "transport", "connection": "transport",
    "connection_pool": "pool", "server_description": "topology", "topology_monitor": "topology",
    "topology_connection_pool": "topology", "client_session": "session",
    "retryable_operation": "session", "change_stream": "cursor", "cursor": "cursor",
}
ALLOWED = {
    "root": {"root"}, "bson": {"root", "bson"}, "wire": {"root", "bson", "wire"},
    "auth": {"root", "bson", "auth"}, "transport": {"root", "bson", "wire", "auth", "transport"},
    "pool": {"root", "transport", "pool"},
    "topology": {"root", "bson", "transport", "pool", "topology", "discovery", "endpoint", "auth"},
    "endpoint": {"root", "endpoint"},
    "discovery": {"root", "endpoint", "discovery"},
    "configuration": {"root", "auth", "bson", "transport", "pool", "topology", "discovery", "endpoint", "configuration", "wire"},
    "session": {"root", "bson", "transport", "pool", "topology", "session", "endpoint"},
    "cursor": {"root", "bson", "transport", "pool", "session", "cursor"},
}

def main():
    errors = []
    partitions = {}
    for path in sorted(ROOT.rglob("*")):
        if path.suffix not in {".cpp", ".cppm"}:
            continue
        relative = path.relative_to(ROOT)
        package = relative.parts[0] if len(relative.parts) > 1 else "root"
        if path.stem.lower() in {"types", "common", "utils", "helpers", "base"} or path.stem.lower().endswith("_types"):
            errors.append(f"{relative}: use a responsibility-specific filename")
        text = path.read_text(encoding="utf-8")
        if package not in ALLOWED:
            errors.append(f"{relative}: unregistered package")
            continue
        for name in re.findall(r"export module cnetmod\.protocol\.mongodb:(\w+)\s*;", text):
            if OWNERS.get(name) != package:
                errors.append(f"{relative}: partition {name} belongs in {OWNERS.get(name)}")
            if name in partitions:
                errors.append(f"{relative}: duplicate partition {name}")
            partitions[name] = path
        for dependency in re.findall(r"^[ \t]*(?:export\s+)?import\s+:(\w+)\s*;", text, re.MULTILINE):
            if OWNERS.get(dependency) not in ALLOWED[package]:
                errors.append(f"{relative}: forbidden dependency on :{dependency}")
        if re.search(r"^[ \t]*(?:export\s+)?import\s+cnetmod\.((?:application|integration)\b|protocol\.(?!mongodb\b))", text, re.MULTILINE):
            errors.append(f"{relative}: protocol depends on an application or unrelated protocol")
    for name in OWNERS.keys() - partitions.keys():
        errors.append(f"missing partition :{name}")
    cloud_root = ROOT.parents[1] / "integration" / "mongodb_cloud"
    if not cloud_root.is_dir():
        errors.append("missing MongoDB cloud integration source directory")
    for path in sorted(cloud_root.glob("*")):
        if path.suffix not in {".cpp", ".cppm"}:
            continue
        if path.stem.lower() in {"types", "common", "utils", "helpers", "base"} or path.stem.lower().endswith("_types"):
            errors.append(f"mongodb_cloud/{path.name}: use a responsibility-specific filename")
        if re.search(r"^[ \t]*(?:export\s+)?import\s+cnetmod\.application\b", path.read_text(encoding="utf-8"), re.MULTILINE):
            errors.append(f"mongodb_cloud/{path.name}: cloud providers must not import Application")
    if errors:
        print("\n".join(errors))
        return 1
    print(f"MongoDB architecture: {len(partitions)} partitions, no boundary violations")
    return 0

if __name__ == "__main__":
    sys.exit(main())
