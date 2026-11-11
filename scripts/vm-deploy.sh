#!/usr/bin/env bash
# vm-deploy.sh — ship build artifacts laptop -> deb -> pf0.
# Default sources (tolerated when absent): out/linux-*-bzImage / out/bzImage,
# module/*.ko, tools/out/* (files or dirs). Explicit paths as args override.
# deb landing zone: $DEPLOY_DIR (/var/lib/pflab/deploy); pf0 gets /opt/pf/.
# Transfer into the guest uses rsync when the guest has it, else scp.
# Exit 0 with a warning when there is nothing to deploy (T6/T7 call freely).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$REPO/scripts/lab-lib.sh"
lab_env

cd "$REPO"
FILES=()
add() { # add <path...> — files only; dirs expanded
  local p
  for p in "$@"; do
    if [ -d "$p" ]; then add "$p"/*
    elif [ -f "$p" ]; then FILES+=("$p")
    fi
  done
}
if [ $# -gt 0 ]; then
  add "$@"
else
  shopt -s nullglob
  add out/linux-*-bzImage out/bzImage out/*.ko module/*.ko tools/out/*
fi
if [ ${#FILES[@]} -eq 0 ]; then
  log "nothing to deploy (out/ bzImage, module/*.ko, tools/out/* all absent) — OK"
  exit 0
fi
log "deploying ${#FILES[@]} artifact(s): ${FILES[*]#$REPO/}"

vm_cmd "$(vm_ip pf0)" 'true' 2>/dev/null || die "pf0 unreachable — run ./scripts/vm-create.sh first"

deb "mkdir -p $DEPLOY_DIR"
rsync -a -e "ssh ${SSH_OPTS[*]}" "${FILES[@]}" "$DEB_USER@$DEB:$DEPLOY_DIR/"
log "staged on deb: $DEPLOY_DIR"

vm_cmd "$(vm_ip pf0)" 'mkdir -p /opt/pf'
if deb "command -v rsync" >/dev/null 2>&1 && \
   vm_cmd "$(vm_ip pf0)" 'command -v rsync' >/dev/null 2>&1; then
  deb_cmd "rsync -a -e 'ssh -i $LAB_KEY_DEB ${SSH_OPTS[*]}' '$DEPLOY_DIR/' root@$(vm_ip pf0):/opt/pf/"
else
  # no rsync in guest: per-file scp
  deb_cmd "for f in '$DEPLOY_DIR'/*; do scp -i $LAB_KEY_DEB ${SSH_OPTS[*]} -q \"\$f\" root@$(vm_ip pf0):/opt/pf/; done"
fi
log "pf0:/opt/pf ->"
vm_cmd "$(vm_ip pf0)" 'ls -l /opt/pf'
