#!/usr/bin/env bash

# Build the Go 1.26 and Java 26 servers used by the cross-language benchmark.
set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
java_bin=${CNETMOD_JAVA:-java}
mvn_bin=${CNETMOD_MVN:-mvn}
javac_bin=${CNETMOD_JAVAC:-}

major_version()
{
    sed -nE 's/.*version "([0-9]+)([.][0-9]+)?.*/\1/p' | head -n 1
}

go_version=$(go version | sed -nE 's/^go version go([0-9]+)\.([0-9]+).*/\1 \2/p')
read -r go_major go_minor <<<"${go_version}"
if [[ -z ${go_major:-} || -z ${go_minor:-} ||
      ( ${go_major} -eq 1 && ${go_minor} -lt 26 ) || ${go_major} -lt 1 ]]; then
    printf 'Go 1.26 or newer is required; found: %s\n' "$(go version 2>&1 || true)" >&2
    exit 2
fi

java_version=$("${java_bin}" -version 2>&1 | major_version)
if [[ -z ${java_version} || ${java_version} -lt 26 ]]; then
    printf 'Java 26 or newer is required; found: %s\n' "$("${java_bin}" -version 2>&1 | head -n 1 || true)" >&2
    exit 2
fi

if [[ -z ${javac_bin} ]]; then
    java_home=$(cd "$(dirname "$(command -v "${java_bin}")")/.." && pwd)
    javac_bin="${java_home}/bin/javac"
fi
if [[ ! -x ${javac_bin} ]]; then
    printf 'javac not found: %s (set CNETMOD_JAVAC)\n' "${javac_bin}" >&2
    exit 2
fi

command -v "${mvn_bin}" >/dev/null 2>&1 || {
    printf 'Maven is required; set CNETMOD_MVN if it is not on PATH\n' >&2
    exit 2
}

(cd "${repo}/testing/bench/crosslang/go" && \
    go build -buildvcs=false -trimpath -ldflags='-s -w' -o crosslang-go .)
(cd "${repo}/testing/bench/crosslang/java" && \
    "${javac_bin}" --release 26 JdkVirtualThreadServer.java)
(cd "${repo}/testing/bench/crosslang/java-jetty" && \
    "${mvn_bin}" -q -DskipTests package dependency:copy-dependencies)

printf 'Go and Java 26 benchmark servers built successfully.\n'
