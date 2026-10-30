#!/usr/bin/env python3
"""Run required CTest suites against disposable, loopback-only local databases."""

import argparse
from contextlib import ExitStack
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

from pymongo import MongoClient
from pymongo.errors import PyMongoError


def wait_ready(process, probe, description):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"{description} exited during startup")
        if probe():
            return
        time.sleep(0.1)
    raise RuntimeError(f"{description} startup timed out")


def tcp_ready(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
            return True
    except OSError:
        return False


def start_process(stack, root, name, command):
    output = stack.enter_context((root / f"{name}-console.log").open("w"))
    process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)

    def stop():
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    stack.callback(stop)
    return process


def start_mysql(stack, root, binaries, environment):
    data = root / "mysql"
    # The existing shard and tenant tests use port 3306. Refuse collisions;
    # never stop or reconfigure an unrelated developer database.
    with socket.socket() as reserved:
        reserved.bind(("127.0.0.1", 3306))
        subprocess.run([str(binaries / "mysqld"), "--no-defaults",
                        "--initialize-insecure", f"--datadir={data}"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                       check=True, timeout=90)
    mysql_socket = root / "mysql.sock"
    process = start_process(stack, root, "mysql", [
        str(binaries / "mysqld"), "--no-defaults", f"--datadir={data}",
        "--bind-address=127.0.0.1", "--port=3306", "--mysqlx=OFF",
        f"--socket={mysql_socket}", f"--pid-file={root / 'mysql.pid'}",
        f"--log-error={root / 'mysql-error.log'}", "--skip-log-bin",
        "--innodb-buffer-pool-size=64M", "--max-connections=64",
    ])
    client = [str(binaries / "mysql"), "--no-defaults", "--protocol=SOCKET",
              f"--socket={mysql_socket}", "--user=root"]

    def mysql_ready():
        return subprocess.run(client + ["--execute=SELECT 1"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                              timeout=3).returncode == 0

    wait_ready(process, mysql_ready, "MySQL")
    password = secrets.token_hex(24)
    databases = ("cnetmod_test", "cnetmod_shard_0", "cnetmod_shard_1", "cnetmod_tenant")
    sql = [f"CREATE USER 'cnetmod_test'@'127.0.0.1' IDENTIFIED BY '{password}';"]
    for database in databases:
        sql += [f"CREATE DATABASE {database};",
                f"GRANT ALL ON {database}.* TO 'cnetmod_test'@'127.0.0.1';"]
    for database in databases[1:3]:
        for table in range(4):
            sql.append(f"CREATE TABLE {database}.orders_{table:02d} "
                       "(id BIGINT PRIMARY KEY, description VARCHAR(255) NOT NULL);")
    subprocess.run(client, input="\n".join(sql), text=True, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=10)
    environment.update(CNETMOD_MYSQL_INTEGRATION="1", CNETMOD_MYSQL_SHARD_INTEGRATION="1",
                       CNETMOD_MYSQL_TENANT_INTEGRATION="1", CNETMOD_MYSQL_TEST_HOST="127.0.0.1",
                       CNETMOD_MYSQL_TEST_PORT="3306", CNETMOD_MYSQL_TEST_DATABASE=databases[0],
                       CNETMOD_MYSQL_SHARD_TEST_DATABASE_0=databases[1],
                       CNETMOD_MYSQL_SHARD_TEST_DATABASE_1=databases[2],
                       CNETMOD_MYSQL_TENANT_TEST_DATABASE=databases[3])
    for prefix in ("CNETMOD_MYSQL_TEST", "CNETMOD_MYSQL_SHARD_TEST", "CNETMOD_MYSQL_TENANT_TEST"):
        environment[prefix + "_USER"] = "cnetmod_test"
        environment[prefix + "_PASSWORD"] = password


def free_ports(count):
    with ExitStack() as sockets:
        ports = []
        for _ in range(count):
            listener = sockets.enter_context(socket.socket())
            listener.bind(("127.0.0.1", 0))
            ports.append(listener.getsockname()[1])
        return ports


def start_mongodb(stack, root, executable, environment):
    ports = free_ports(3)
    processes = []
    for index, port in enumerate(ports):
        data = root / f"mongo-{index}"
        data.mkdir()
        process = start_process(stack, root, f"mongo-{index}", [
            executable, "--bind_ip", "127.0.0.1", "--port", str(port),
            "--dbpath", str(data), "--replSet", "cnetmod-local",
            "--wiredTigerCacheSizeGB", "0.25", "--oplogSize", "128",
            "--setParameter", "enableTestCommands=1",
        ])
        processes.append(process)
        wait_ready(process, lambda port=port: tcp_ready(port), f"MongoDB node {index}")
    with MongoClient(f"mongodb://127.0.0.1:{ports[0]}/?directConnection=true",
                     serverSelectionTimeoutMS=2000) as seed:
        seed.admin.command({"replSetInitiate": {
            "_id": "cnetmod-local", "members": [
                {"_id": index, "host": f"127.0.0.1:{port}"}
                for index, port in enumerate(ports)],
            "settings": {"electionTimeoutMillis": 2000}}})
    uri = "mongodb://" + ",".join(f"127.0.0.1:{port}" for port in ports)
    uri += "/cnetmod_interop?replicaSet=cnetmod-local"
    with MongoClient(uri, serverSelectionTimeoutMS=2000) as client:
        def elected():
            try:
                members = client.admin.command("replSetGetStatus")["members"]
                return len(members) == 3 and all(
                    member["health"] == 1 and member["stateStr"] in ("PRIMARY", "SECONDARY")
                    for member in members)
            except PyMongoError:
                return False
        wait_ready(processes[0], elected, "MongoDB replica set")
        primary_port = client.primary[1]
    environment.update(CNETMOD_MONGODB_URI=uri, CNETMOD_MONGODB_TEST_PORT=str(primary_port),
                       CNETMOD_MONGODB_TEST_REPLICA_SET="cnetmod-local",
                       CNETMOD_MONGODB_FAILPOINT_TEST="1", CNETMOD_MONGODB_FAILOVER_TEST="1")


def start_clickhouse(stack, root, executable, environment):
    port = free_ports(1)[0]
    path = escape(str(root))
    configuration = root / "clickhouse.xml"
    configuration.write_text(f"""<clickhouse>
<logger><level>warning</level><log>{path}/clickhouse.log</log></logger>
<path>{path}/clickhouse/</path><tmp_path>{path}/clickhouse-tmp/</tmp_path>
<listen_host>127.0.0.1</listen_host><tcp_port>{port}</tcp_port>
<max_connections>16</max_connections><max_server_memory_usage>1073741824</max_server_memory_usage>
<background_schedule_pool_size>4</background_schedule_pool_size><background_pool_size>16</background_pool_size>
<profiles><default><max_threads>2</max_threads><max_memory_usage>268435456</max_memory_usage></default></profiles>
<users><default><password></password><networks><ip>127.0.0.1</ip></networks><profile>default</profile><quota>default</quota></default></users>
<quotas><default><interval><duration>3600</duration><queries>0</queries></interval></default></quotas>
</clickhouse>""", encoding="utf-8")
    process = start_process(stack, root, "clickhouse", [
        executable, "server", f"--config-file={configuration}"])
    wait_ready(process, lambda: tcp_ready(port), "ClickHouse")
    environment["CNETMOD_CLICKHOUSE_TEST_PORT"] = str(port)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--mysql-bin", required=True, type=Path)
    parser.add_argument("--mongod", default=shutil.which("mongod"))
    parser.add_argument("--clickhouse", default=shutil.which("clickhouse"))
    args = parser.parse_args()
    if not args.mongod or not args.clickhouse:
        parser.error("mongod and clickhouse executables are required")
    build = args.build.resolve()
    inventory = subprocess.run(["ctest", "--test-dir", str(build), "--show-only=json-v1"],
                               capture_output=True, text=True, check=True)
    names = {test["name"] for test in json.loads(inventory.stdout)["tests"]}
    required = {"test_application_mysql_live", "test_application_mysql_sharding_live",
                "test_orm_tenant_live", "python_h2_interop", "python_mongodb_interoperability"}
    if not required.issubset(names):
        parser.error("build must enable HTTP, MySQL, MongoDB, ORM and their tests")
    report = build / "required-local-tests.xml"
    report.unlink(missing_ok=True)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith("CNETMOD_")}
    environment["CNETMOD_DATABASE_REQUIRED"] = "1"
    with tempfile.TemporaryDirectory(prefix="cnetmod-live-") as directory, ExitStack() as stack:
        root = Path(directory)
        print("Starting disposable MySQL, three-node MongoDB and ClickHouse", flush=True)
        start_mysql(stack, root, args.mysql_bin.resolve(), environment)
        start_mongodb(stack, root, args.mongod, environment)
        start_clickhouse(stack, root, args.clickhouse, environment)
        # Run this before any other MySQL client can populate the server's
        # caching_sha2_password cache, then repeat it in the complete suite.
        cold_auth = subprocess.run([
            "ctest", "--test-dir", str(build), "--output-on-failure", "--no-tests=error",
            "-R", "^test_orm_tenant_live$"], env=environment)
        if cold_auth.returncode:
            return cold_auth.returncode
        completed = subprocess.run([
            "ctest", "--test-dir", str(build), "--output-on-failure", "--no-tests=error",
            "--output-junit", str(report)], env=environment)
        if completed.returncode:
            return completed.returncode
        tests = list(ET.parse(report).iter("testcase"))
        if not tests or any(test.find("skipped") is not None for test in tests):
            raise RuntimeError("required local regression must execute every test without skips")
        print(f"Required local regression: {len(tests)} executed, no failures or skips", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
