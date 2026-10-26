#!/bin/bash
# Relanza la última partida registrada por xx-lidd antes de que arranque EmulationStation.
# EXPERIMENTAL: pendiente de verificar en la consola cómo convive con el servicio de ES
# (en ROCKNIX se llama emustation.service; comprobar con `systemctl list-units | grep -i emu`).
CMD=/storage/.config/xx-cfw/resume-cmd
MAX_AGE=$((24 * 3600))
[ -r "$CMD" ] || exit 0
ts=$(head -n1 "$CMD"); now=$(date +%s)
if [[ -z "$ts" || $((now - ts)) -gt $MAX_AGE ]]; then rm -f "$CMD"; exit 0; fi
mapfile -d '' -t argv < <(tail -n +2 "$CMD")
rm -f "$CMD"   # se consume una sola vez: si el juego falla, el siguiente arranque es normal
[[ ${#argv[@]} -gt 0 && -x "${argv[0]}" ]] || exit 0
logger -t xx-resume "relanzando: ${argv[*]}"
exec "${argv[@]}"
