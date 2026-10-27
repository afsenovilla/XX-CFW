#!/bin/bash
# xx-rail-test.sh — ¿qué raíles y módulos quedan activos justo antes de dormir en ROCKNIX?
#
# Ejecuta los hooks "pre suspend" de ROCKNIX, espera, vuelca el estado de módulos y reguladores
# y ejecuta los hooks "post suspend". NO suspende la consola. Sirve para saber si
# `wifictl disable` y la descarga de módulos llegan a cortar la alimentación del Wi-Fi.
#
# AVISO: el Wi-Fi se apaga durante unos segundos, así que una sesión SSH por Wi-Fi se corta.
# Es normal. Lánzalo en segundo plano y vuelve a entrar a los 40 s a leer /tmp/rails-pre.txt:
#   nohup bash /storage/xx-rail-test.sh >/tmp/railtest.log 2>&1 &
S=/usr/lib/systemd/system-sleep/sleep
OUT=/tmp/rails-pre.txt
{
  echo "== regulator_summary ANTES de los hooks"
  cat /sys/kernel/debug/regulator/regulator_summary
} > "$OUT" 2>&1
/bin/bash "$S" pre suspend
sleep 8
{
  echo
  echo "== módulos cargados DESPUÉS de pre"
  lsmod
  echo
  echo "== regulator_summary DESPUÉS de pre"
  cat /sys/kernel/debug/regulator/regulator_summary
  echo
  echo "== /usr/config/modules.bad"
  cat /usr/config/modules.bad
  echo
  echo "== modules.keep y quirks de sleep"
  ls /usr/lib/autostart/quirks/platforms/*/modules.keep /usr/lib/autostart/quirks/devices/*/modules.keep 2>/dev/null
  ls -R /usr/lib/autostart/quirks/platforms/*/sleep.d /usr/lib/autostart/quirks/devices/*/sleep.d 2>/dev/null
  echo
  echo "== mmc"
  for h in /sys/class/mmc_host/mmc*; do echo "$h: $(ls $h | tr '\n' ' ')"; done
} >> "$OUT" 2>&1
/bin/bash "$S" post suspend
echo "listo: $OUT"
