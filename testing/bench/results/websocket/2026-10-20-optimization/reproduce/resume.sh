#!/bin/bash
set -euo pipefail
root=/root/cnetmod-ws-20261020
scripts=/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod/build-ws-evidence
mkdir -p "$root/optimization"
ip netns add cnetmod-ws-20261020
nsenter --net=/run/netns/cnetmod-ws-20261020 ip link set lo up mtu 1500
nsenter --net=/run/netns/cnetmod-ws-20261020 sysctl -w net.ipv4.ip_local_port_range='1024 65535' net.ipv4.tcp_tw_reuse=1 net.core.somaxconn=65535 net.ipv4.tcp_max_syn_backlog=65535 net.ipv4.tcp_max_tw_buckets=131072 net.ipv4.ip_local_reserved_ports=5432,6379,8080,8081,8082,8443,9090
systemd-run --unit=cnetmod-ws-containerd-net /usr/bin/nsenter --net=/run/netns/cnetmod-ws-20261020 /usr/bin/containerd --root "$root/containerd" --state /run/cnetmod-ws-containerd --address /run/cnetmod-ws-containerd.sock
systemd-run --unit=cnetmod-ws-docker-net /usr/bin/nsenter --net=/run/netns/cnetmod-ws-20261020 /usr/bin/dockerd --containerd=/run/cnetmod-ws-containerd.sock --host=unix:///run/cnetmod-ws-20261020.sock --data-root="$root/docker-isolated" --exec-root=/run/cnetmod-ws-net-daemon --pidfile=/run/cnetmod-ws-net-daemon.pid --containerd-namespace=cnetmodws20261020 --containerd-plugins-namespace=cnetmodws20261020plugins --bridge=none --iptables=false --ip-forward=false --ip-masq=false
for i in $(seq 1 30); do
  if docker -H unix:///run/cnetmod-ws-20261020.sock info >/dev/null 2>&1; then break; fi
  sleep 1
done
echo 'Isolated benchmark runtime ready'
