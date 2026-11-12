#!/usr/bin/env bash
# Build every screencast: capture -> narration -> video -> full movie.
#
#   ./scripts/screencast/build-all.sh            # everything
#   ./scripts/screencast/build-all.sh 04-roteamento
set -uo pipefail
cd "$(dirname "$0")/../.."
REPO=$(pwd)

PHASES=("$@")
[ ${#PHASES[@]} -eq 0 ] && PHASES=(00-ambiente 01-implementacao 02-ativacao 03-regras \
	04-roteamento 05-balanceamento 06-qos)

echo "== narration audio =="
for lang in en pt; do
	python3 scripts/screencast/make_audio.py --lang "$lang" | tail -1
done

echo "== sessions =="
for p in "${PHASES[@]}"; do
	# the activation phase shows the module being loaded, so it is the one
	# that must not have it preloaded
	extra=""
	[ "$p" = "02-ativacao" ] || extra="--ensure-pf"
	python3 scripts/screencast/record.py --plan "scripts/screencast/plans/$p.json" \
		--out "out/screencast/caps/$p.cast" --pace $extra
done

echo "== videos =="
for lang in en pt; do
	for p in "${PHASES[@]}"; do
		python3 scripts/screencast/assemble.py --phase "$p" --lang "$lang" || echo "FALHOU: $p $lang"
	done
done

echo "== full movie =="
for lang in en pt; do
	python3 scripts/screencast/join.py --lang "$lang" || echo "FALHOU: join $lang"
done

echo
echo "== resultado =="
for f in out/screencast/en/*.mp4 out/screencast/pt/*.mp4; do
	[ -e "$f" ] || continue
	d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f")
	a=$(ffprobe -v error -select_streams a -show_entries stream=codec_name -of csv=p=0 "$f")
	printf '%-52s %6.1fs  audio=%s\n' "$f" "$d" "${a:-none}"
done
