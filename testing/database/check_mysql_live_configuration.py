"""Verify live-test configuration rejection without contacting a database."""

import argparse
import os
import socket
import subprocess

SCENARIO = "mysql_live_authentication_health_and_supervised_stop"


def completed_failure(result):
    lines = result.stdout.splitlines()
    return (result.returncode == 1
            and any(line.startswith(f"[  FAILED  ] {SCENARIO} (".encode()) for line in lines)
            and b"[==========] 1 test(s) ran." in lines
            and b"[  PASSED  ] 0 test(s)." in lines
            and b"[  FAILED  ] 1 test(s)." in lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    args = parser.parse_args()
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith("CNETMOD_MYSQL_")}
    environment.update(CNETMOD_MYSQL_INTEGRATION="1", CNETMOD_MYSQL_TEST_USER="fixture",
                       CNETMOD_MYSQL_TEST_PASSWORD="fixture", CNETMOD_MYSQL_TEST_DATABASE="fixture")
    # This watchdog checks one negative-path scenario, not the live host suite.
    # Override inherited filters so callers cannot silently change its coverage.
    environment["CNETMOD_TEST_FILTER"] = SCENARIO
    for port in ("0", "65536", "-1", "+3306", "3306junk", " 3306", "999999999999999999999"):
        environment["CNETMOD_MYSQL_TEST_PORT"] = port
        result = subprocess.run([args.executable], env=environment, capture_output=True, timeout=3)
        if result.returncode != 1 or result.stdout or result.stderr:
            raise RuntimeError("invalid configuration did not fail cleanly before test execution")
    print("Seven invalid MySQL test port configurations rejected")
    # Hold an unlistened loopback port so no unrelated database can be reached.
    with socket.socket() as reserved:
        reserved.bind(("127.0.0.1", 0))
        environment["CNETMOD_MYSQL_TEST_PORT"] = str(reserved.getsockname()[1])
        result = subprocess.run([args.executable], env=environment, capture_output=True, timeout=15)
        if not completed_failure(result):
            raise RuntimeError("unavailable MySQL fixture did not produce a completed failing test")
    print("Unavailable loopback database fails and exits within the watchdog")


if __name__ == "__main__":
    main()
