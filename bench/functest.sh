#!/usr/bin/env bash
# bench/functest.sh — functional coverage matrix per backend.
# Usage: functest.sh            (runs all backends on the firewall VM)
set -uo pipefail
BENCH_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$BENCH_ROOT/env.sh"

CSV="${CSV:-$BENCH_ROOT/data/functional.csv}"
[ -f "$CSV" ] || printf 'backend,feature,result,note\n' > "$CSV"

# feature name -> [result: pass|limited|n/a|fail] note
check() { # check <backend> <feature> <result> <note>
	printf '%s,%s,%s,%s\n' "$1" "$2" "$3" "$4" >> "$CSV"
}
