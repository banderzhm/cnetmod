# Database interoperability tests

These tests compare cnetmod with the official mainstream Python clients against
the same live PostgreSQL and MongoDB services. No endpoint or credential is
stored in the repository.

Install the test dependencies in an isolated environment:

```bash
python -m pip install -r testing/database/requirements.txt
```

Provide only the services you want to test:

```bash
export CNETMOD_POSTGRESQL_URI='postgresql://user:password@127.0.0.1:5432/database'
export CNETMOD_MONGODB_URI='mongodb://user:password@127.0.0.1:27017/database?authSource=admin'
export CNETMOD_POSTGRESQL_DRIVER=/absolute/path/to/postgresql_interoperability_driver
export CNETMOD_MONGODB_DRIVER=/absolute/path/to/mongodb_interoperability_driver
python -m pytest -c testing/database/pytest.ini testing/database
```

The URI variables run the Python reference tests. Driver variables enable the
native JSON-lines tests; missing live-service or executable variables cause only
the corresponding tests to skip. In CI, inject secrets through the runner's
secret store and use a private network or SSH tunnel. Never put database ports
or passwords in source-controlled configuration.

Each native executable accepts one version-1 request on standard input and
emits exactly one JSON response on standard output. Logs must go through the
cnetmod logger and must not contaminate the JSON channel.
# Required execution gate

## MySQL Application test over a local tunnel

`test_application_mysql_live` accepts `CNETMOD_MYSQL_TEST_PORT` (default 3306),
while keeping its destination fixed at `127.0.0.1`. Set the port to an established
SSH tunnel's local port rather than exposing the remote MySQL listener publicly.
The integration opt-in is `CNETMOD_MYSQL_INTEGRATION=1`; user, password and database
come from `CNETMOD_MYSQL_TEST_USER`, `CNETMOD_MYSQL_TEST_PASSWORD`, and
`CNETMOD_MYSQL_TEST_DATABASE`. Use only a dedicated test database/account. Port
values must be decimal integers from 1 to 65535, without whitespace or suffixes;
invalid values fail before any test or socket setup. Do not put credentials in
command-line arguments, result files, or source control.

The test driver owns its top-level coroutine rather than detaching it. Exceptions
request supervised shutdown and attempt service cleanup before being reported to
the test harness. `check_mysql_live_configuration.py EXECUTABLE` checks invalid
ports and a reserved, unlistened loopback port: the latter must produce a completed
failing test within a 15-second outer watchdog. It never connects to an unrelated
local database. This negative-path check does not validate live authentication,
successful queries, or cleanup under every possible exception.
The watchdog explicitly selects `mysql_live_authentication_health_and_supervised_stop`
and overrides inherited test filters. It does not run the other live host tests,
whose startup budgets require a separate full integration invocation.
Completion requires the named failure record and the summary for exactly one
executed, failed test. Printing only the test name, skipping execution, or
terminating before the summary does not satisfy this check.


Linux Release CI invokes `run_linux_ci.sh` after building both native drivers.
It provisions PostgreSQL 17 and a three-member MongoDB 8.0 replica set on an
isolated Actions runner. MongoDB test commands, failpoint tests and primary
step-down tests are enabled; storage is ephemeral, listeners are loopback-only,
startup waits are bounded, and exit traps remove only created container IDs.
The script refuses non-Actions environments to protect existing local services.
These are protocol interoperability suites, not proof of complete Application
health/recovery/OTEL coverage. The new container workflow has only been syntax
checked locally; actual CI execution is pending.

Set `CNETMOD_DATABASE_REQUIRED=1` when invoking `run_optional_pytest.py` in a
mandatory integration job. Missing Python dependencies then fail instead of
returning CTest skip code 77. Skipped test execution or skipped collection also
makes an otherwise successful pytest session fail. Without this variable, local
optional behavior is unchanged. This gate does not provision a database: provide
the driver executable and a reachable test URI before enabling it. In particular,
the MongoDB physical-connection suite requires a non-SRV URI.

Run the dependency-free gate checks with:

```text
python -m unittest discover -s testing/database -p test_required_runner.py
```
