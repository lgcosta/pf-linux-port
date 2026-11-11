#!/usr/bin/env bash
# helpers for tests/vm/*
DEB="${DEB:-root@192.168.1.242}"
PF0_IP="${PF0_IP:-192.168.122.8}"
CLI_IP=10.0.1.10; SRV_IP=10.0.2.20; GW2_IP=10.0.3.2; GW2_WAN=10.0.2.2
pf0() { ssh -o ConnectTimeout=10 "$DEB" "ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 -i /root/.ssh/pflab_ed root@$PF0_IP \"$*\"" ; }
cli() { ssh -o ConnectTimeout=10 "$DEB" "ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 -i /root/.ssh/pflab_ed root@$PF0_IP \"ssh -o StrictHostKeyChecking=accept-new root@$CLI_IP '$*'\"" ; }
srv() { ssh -o ConnectTimeout=10 "$DEB" "ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 -i /root/.ssh/pflab_ed root@$PF0_IP \"ssh -o StrictHostKeyChecking=accept-new root@$SRV_IP '$*'\"" ; }
ok()   { echo "OK $*"; }
bad()  { echo "NOT OK: $*" >&2; FAILED=1; }
finish(){ [ "${FAILED:-0}" = 1 ] && exit 1; exit 0; }
