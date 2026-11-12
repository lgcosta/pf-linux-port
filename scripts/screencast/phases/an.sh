#!/bin/sh
# Analysis helper executed ON the firewall VM (avoids quoting hell through the
# ssh layers).  usage: an.sh <capture-file> <mode>
f=$1
mode=$2
case "$mode" in
req)  echo "requerentes no fio: $(grep -c 'echo request' "$f")" ;;
rep)  echo "respostas no fio:   $(grep -c 'echo reply' "$f")" ;;
src)  echo "origem no fio (contagem por endereco de origem):"
      grep 'echo request' "$f" | sed 's/.*: \([0-9.]*\) > .*/\1/' | sort | uniq -c ;;
uniq) echo "origens distintas: $(grep 'echo request' "$f" | sed 's/.*: \([0-9.]*\) > .*/\1/' | sort -u | tr '\n' ' ')" ;;
lines) grep 'echo request' "$f" | head -2 ;;
*) echo "modo desconhecido: $mode" ;;
esac
