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
