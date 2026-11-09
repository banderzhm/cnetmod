#!/usr/bin/env bash
# Same-binary A/A calibration. Run one load generator at a time.
set -euo pipefail
if [[ ${HTTP1_NETNS:-0} == 1 && ${HTTP1_IN_NETNS:-0} != 1 ]]; then
  exec unshare --net env HTTP1_IN_NETNS=1 bash "$0" "$@"
fi
if [[ ${HTTP1_IN_NETNS:-0} == 1 ]]; then
  ip link set lo up
fi
binary=${1:?server binary required}
generator=${2:?gcannon binary required}
requests=${3:?HttpArena requests directory required}
output=${4:?new output directory required}
mkdir "$output"
perf_binary=${PERF_BINARY:-perf}
if [[ ${HTTP1_PROFILE:-1} == 1 ]]; then
  "$perf_binary" version --build-options > "$output/perf-features.txt" 2>&1
  for feature in libelf dwarf-unwind; do
    if ! grep -Eq "^[[:space:]]*$feature:.*\[[[:space:]]*on[[:space:]]*\]" "$output/perf-features.txt"; then
      printf 'Profiling requires %s support; select a complete tool with PERF_BINARY.\n' "$feature" >&2
      exit 2
    fi
  done
fi
binary=$(realpath "$binary")
alternate=${HTTP1_ALTERNATE_BINARY:-}
if [[ -n "$alternate" ]]; then
  alternate=$(realpath "$alternate")
  [[ ${HTTP1_REUSE_SERVER:-0} == 0 ]]
fi
sha256sum "$binary" "$generator" > "$output/binaries.sha256"
if [[ -n "$alternate" ]]; then
  sha256sum "$alternate" >> "$output/binaries.sha256"
fi
lscpu -e > "$output/topology.txt"
cat /proc/meminfo > "$output/memory-before.txt"
duration=${HTTP1_DURATION:-8}
threads=${HTTP1_THREADS:-16}
generator_cpus=${HTTP1_GENERATOR_CPUS:-16-31}
reuse_server=${HTTP1_REUSE_SERVER:-0}
host=${HTTP1_HOST:-127.0.0.1}
server_pid=
load_pid=
route_added=0
cleanup() {
  if [[ -n "$load_pid" ]]; then
    kill "$load_pid" 2>/dev/null || true
    wait "$load_pid" 2>/dev/null || true
  fi
  if [[ -n "$server_pid" ]]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
}
finish() {
  cleanup
  if [[ "$route_added" == 1 ]]; then
    ip route del local "$host/32" dev lo table local
  fi
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
if [[ ${HTTP1_LOCAL_SOURCE:-0} == 1 ]]; then
  [[ "$host" == 127.0.0.2 ]]
  ip route add local "$host/32" dev lo src "$host" table local
  route_added=1
fi
ip route get "$host" > "$output/route.txt"
printf 'host=%s threads=%s generator_cpus=%s reuse_server=%s duration=%s netns=%s preheat=%s\n' \
  "$host" "$threads" "$generator_cpus" "$reuse_server" "$duration" "${HTTP1_IN_NETNS:-0}" "${HTTP1_PREHEAT:-0}" > "$output/settings.txt"

for workers in ${HTTP1_WORKER_COUNTS:-4 8 16}; do
  tier_warmed=0
  for round in 1 2 3; do
    labels=(A B)
    if ((round % 2 == 0)); then labels=(B A); fi
    for label in "${labels[@]}"; do
      prefix="$output/w${workers}-r${round}-${label}"
      run_binary=$binary
      if [[ "$label" == B && -n "$alternate" ]]; then
        run_binary=$alternate
      fi
      if [[ -z "$server_pid" ]]; then
        taskset -c "0-$((workers-1))" env CNETMOD_HTTP_WORKERS="$workers" \
          H1_PORT=18080 H2C_PORT=18082 "$run_binary" > "$prefix.server.txt" 2>&1 &
        server_pid=$!
      fi
      ready=0
      for attempt in $(seq 1 100); do
        kill -0 "$server_pid"
        if curl --noproxy '*' --max-time 1 -fsS \
          "http://$host:18080/baseline11?a=13&b=42" > "$prefix.body"; then
          [[ $(< "$prefix.body") == 55 ]]
          ready=1
          break
        fi
        sleep 0.05
      done
      [[ "$ready" == 1 ]]
      args=("http://$host:18080" -c 512 -t "$threads" -p 1
        --raw "$requests/get.raw,$requests/post_cl.raw,$requests/post_chunked.raw")
      if [[ "$tier_warmed" == 0 && ${HTTP1_PREHEAT:-0} != 0 ]]; then
        taskset -c "$generator_cpus" "$generator" "${args[@]}" -d "${HTTP1_PREHEAT}s" > "$prefix.preheat.txt"
        tier_warmed=1
      fi
      taskset -c "$generator_cpus" "$generator" "${args[@]}" -d 2s > "$prefix.warmup.txt"
      before=$(awk '{print $14+$15}' "/proc/$server_pid/stat")
      cat /proc/pressure/cpu > "$prefix.pressure.txt"
      taskset -c "$generator_cpus" "$generator" "${args[@]}" -d "${duration}s" > "$prefix.load.txt"
      after=$(awk '{print $14+$15}' "/proc/$server_pid/stat")
      printf '%s %s\n' "$((after-before))" "$(getconf CLK_TCK)" > "$prefix.cpu.txt"
      printf 'workers=%s round=%s label=%s ticks=%s ' "$workers" "$round" "$label" "$((after-before))"
      grep 'Throughput:' "$prefix.load.txt"
      if [[ "$reuse_server" != 1 ]]; then
        cleanup
        server_pid=
      fi
      sleep 2
    done
  done
  cleanup
  server_pid=
done
cat /proc/meminfo > "$output/memory-after.txt"
sha256sum "$binary" > "$output/binary-after.sha256"
if [[ -n "$alternate" ]]; then
  sha256sum "$alternate" >> "$output/binary-after.sha256"
fi
[[ ${HTTP1_PROFILE:-1} == 1 ]] || exit 0

# Keep the profile separate from every timed A/A round.
taskset -c 0-7 env CNETMOD_HTTP_WORKERS=8 H1_PORT=18080 H2C_PORT=18082 \
  "$binary" > "$output/profile-server.txt" 2>&1 &
server_pid=$!
sleep 2
taskset -c "$generator_cpus" "$generator" "${args[@]}" -d 8s > "$output/profile-load.txt" &
load_pid=$!
"$perf_binary" record -e cpu-clock -F 199 --call-graph dwarf,2048 -p "$server_pid" \
  -o "$output/http1.data" -- sleep 5
wait "$load_pid"
load_pid=
"$perf_binary" report -i "$output/http1.data" --stdio --no-children --call-graph none \
  --percent-limit 0.3 > "$output/http1-self.txt"
cleanup
server_pid=
