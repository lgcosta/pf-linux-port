#!/usr/bin/env bash
# Render every screencast phase into out/screencast/*.mp4 (silent, 1080p30).
#
#   ./scripts/screencast/record-all.sh [phase ...]
#
# Each phase is a real session against the lab; the videos carry no audio.
set -u
cd "$(dirname "$0")/../.."
REPO=$(pwd)
OUT="$REPO/out/screencast"
mkdir -p "$OUT"

render() { # <file> <title> <subtitle> <phase-no> [panel]
	local f=$1 title=$2 sub=$3 no=$4
	local panel=${5:-"alpine (pf0) - lab libvirt - kernel 7.3.4 + pf.ko"}
	echo "=== gravando $f ==="
	python3 "$REPO/scripts/screencast/render.py" \
		--script "$REPO/scripts/screencast/phases/$f" \
		--out "$OUT/${f%.sh}.mp4" \
		--title "$title" --subtitle "$sub" \
		--phase-no "$no" --phase-total 6 --panel "$panel" --min-hold 1.1 \
		--footer "PF on Linux - OpenBSD PF 8.0 no kernel Linux - lab libvirt" || return 1
}

declare -A TITLES=(
	[01-implementacao-kernel]="Fase 1 - Implementacao no kernel"
	[02-ativacao]="Fase 2 - Ativacao"
	[03-regras-basicas]="Fase 3 - Regras basicas"
	[04-roteamento]="Fase 4 - Roteamento"
	[05-balanceamento]="Fase 5 - Balanceamento"
	[06-qos]="Fase 6 - QOS"
)
declare -A SUBS=(
	[01-implementacao-kernel]="Das fontes OpenBSD 8.0 ao modulo pf.ko"
	[02-ativacao]="insmod pf.ko / pfctl -e / pfctl -s info"
	[03-regras-basicas]="tabelas, anchors, keep state, block drop"
	[04-roteamento]="route-to e reply-to: o proximo salto decidido pelo pf"
	[05-balanceamento]="pools round-robin, source-hash e dup-to"
	[06-qos]="filas hfsc do OpenBSD 8.0 no caminho de dados"
)

PHASES=("$@")
[ ${#PHASES[@]} -eq 0 ] && PHASES=(01-implementacao-kernel 02-ativacao 03-regras-basicas \
	04-roteamento 05-balanceamento 06-qos)

for p in "${PHASES[@]}"; do
	f="$p.sh"
	[ -f "$REPO/scripts/screencast/phases/$f" ] || { echo "fase inexistente: $f"; exit 1; }
	# the phase number comes from the phase's own name, not from its position
	# in the list, so a partial re-render keeps the right "FASE x/6" label
	n=$((10#${p%%-*}))
	case "$p" in
	01-*) panel="host de build - repositorio pf_port" ;;
	*)    panel="alpine (pf0) - lab libvirt - kernel 7.3.4 + pf.ko" ;;
	esac
	render "$f" "${TITLES[$p]}" "${SUBS[$p]}" "$n" "$panel" || echo "FALHOU: $f"
done

echo
echo "=== videos gerados ==="
for m in "$OUT"/*.mp4; do
	[ -e "$m" ] || continue
	d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$m")
	printf '%-46s %6.1fs  %5.1f MB\n' "$(basename "$m")" "$d" "$(echo "scale=1; $(stat -c%s "$m")/1000000" | bc)"
done
