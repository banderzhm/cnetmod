#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <test-executable>" >&2
    exit 2
fi
if [[ -z ${CNETMOD_MYSQL_ROOT_PASSWORD:-} ]]; then
    echo "CNETMOD_MYSQL_ROOT_PASSWORD is required." >&2
    exit 2
fi

test_executable=$1
container=$(docker ps --filter ancestor=mysql:8.4 --format '{{.ID}}' | head -n 1)
if [[ -z $container ]]; then
    echo "The MySQL 8.4 service container was not found." >&2
    exit 1
fi

cleanup() {
    local status=$?
    docker exec "$container" mysql \
        --user=root --password="$CNETMOD_MYSQL_ROOT_PASSWORD" \
        --execute='DROP DATABASE IF EXISTS cnetmod_shard_0; DROP DATABASE IF EXISTS cnetmod_shard_1;' \
        >/dev/null 2>&1 || true
    exit "$status"
}
trap cleanup EXIT

docker exec -i "$container" mysql \
    --user=root --password="$CNETMOD_MYSQL_ROOT_PASSWORD" <<'SQL'
CREATE DATABASE cnetmod_shard_0;
CREATE DATABASE cnetmod_shard_1;
GRANT ALL PRIVILEGES ON cnetmod_shard_0.* TO 'cnetmod_test'@'%';
GRANT ALL PRIVILEGES ON cnetmod_shard_1.* TO 'cnetmod_test'@'%';
FLUSH PRIVILEGES;
CREATE TABLE cnetmod_shard_0.orders_00 (id BIGINT PRIMARY KEY, description VARCHAR(255) NOT NULL);
CREATE TABLE cnetmod_shard_0.orders_01 (id BIGINT PRIMARY KEY, description VARCHAR(255) NOT NULL);
CREATE TABLE cnetmod_shard_0.orders_02 (id BIGINT PRIMARY KEY, description VARCHAR(255) NOT NULL);
CREATE TABLE cnetmod_shard_0.orders_03 (id BIGINT PRIMARY KEY, description VARCHAR(255) NOT NULL);
CREATE TABLE cnetmod_shard_1.orders_00 (id BIGINT PRIMARY KEY, description VARCHAR(255) NOT NULL);
CREATE TABLE cnetmod_shard_1.orders_01 (id BIGINT PRIMARY KEY, description VARCHAR(255) NOT NULL);
CREATE TABLE cnetmod_shard_1.orders_02 (id BIGINT PRIMARY KEY, description VARCHAR(255) NOT NULL);
CREATE TABLE cnetmod_shard_1.orders_03 (id BIGINT PRIMARY KEY, description VARCHAR(255) NOT NULL);
SQL

export CNETMOD_MYSQL_SHARD_INTEGRATION=1
export CNETMOD_MYSQL_SHARD_TEST_USER=cnetmod_test
export CNETMOD_MYSQL_SHARD_TEST_PASSWORD=disposable-test-password
export CNETMOD_MYSQL_SHARD_TEST_DATABASE_0=cnetmod_shard_0
export CNETMOD_MYSQL_SHARD_TEST_DATABASE_1=cnetmod_shard_1
timeout 45 "$test_executable"
