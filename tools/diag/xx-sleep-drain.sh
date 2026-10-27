#!/bin/sh
# xx-sleep-drain.sh — mide el consumo en suspensión con ciclos despertados por RTC.
#
# Uso: sh xx-sleep-drain.sh [segundos_por_ciclo=3600] [ciclos=3] [opciones]
#   --raw              escribir en /sys/power/state en vez de `systemctl suspend`.
#                      OJO: así NO se ejecutan los hooks de ROCKNIX (Wi-Fi, audio, núcleos),
#                      y el consumo medido no es el real. Solo para comparar.
#   --mem-sleep MODO   fijar /sys/power/mem_sleep (deep o s2idle) antes de medir
#   --discard-first    no contar el primer ciclo
#   --csv FICHERO      ruta del CSV (defecto ./sleep-drain.csv)
#
# La consola de ROCKNIX solo expone `capacity` en % enteros (sin charge_now), así que la
# resolución es de 1 %. Por eso el resumen final usa el descenso TOTAL entre el principio y
# el final: cuanto más largo el total, más fina la medida (8 h dan ±0,12 %/h).
set -u
SECS=3600; CYCLES=3; RAW=0; DISCARD=0; MEMSLEEP=""; CSV=./sleep-drain.csv; N=0
while [ $# -gt 0 ]; do
	case "$1" in
	--raw) RAW=1;;
	--mem-sleep) MEMSLEEP=$2; shift;;
	--discard-first) DISCARD=1;;
	--csv) CSV=$2; shift;;
	*) N=$((N + 1)); [ $N -eq 1 ] && SECS=$1; [ $N -eq 2 ] && CYCLES=$1;;
	esac; shift
done

BAT=""
for p in /sys/class/power_supply/*; do
	[ "$(cat $p/type 2>/dev/null)" = "Battery" ] && { BAT=$p; break; }
done
[ -n "$BAT" ] || { echo "no encuentro la batería en /sys/class/power_supply"; exit 1; }
RTC=""
for r in /sys/class/rtc/rtc*; do [ -w $r/wakealarm ] && { RTC=$r; break; }; done
[ -n "$RTC" ] || { echo "ningún RTC con wakealarm escribible"; exit 1; }
[ -n "$MEMSLEEP" ] && echo "$MEMSLEEP" > /sys/power/mem_sleep
echo "batería=$BAT rtc=$RTC mem_sleep=$(cat /sys/power/mem_sleep) raw=$RAW ciclos=$CYCLES x ${SECS}s"

up() { cut -d' ' -f1 /proc/uptime | cut -d. -f1; }   # incluye el tiempo suspendido
cap() { cat $BAT/capacity 2>/dev/null || echo 0; }
volt() { cat $BAT/voltage_now 2>/dev/null || echo 0; }

[ -e "$CSV" ] || echo "ciclo,duracion_s,cap_ini,cap_fin,v_ini_uV,v_fin_uV,wakeups" > "$CSV"
i=1; first_cap=""; first_up=""; last_cap=""; last_up=""
while [ $i -le $CYCLES ]; do
	p0=$(cap); v0=$(volt); t0=$(up); w0=$(cat /sys/power/wakeup_count 2>/dev/null || echo 0)
	echo 0 > $RTC/wakealarm
	echo $(( $(cat $RTC/since_epoch) + SECS )) > $RTC/wakealarm || { echo "no pude armar la alarma"; exit 1; }
	sync
	echo "ciclo $i: durmiendo ${SECS}s (capacidad=$p0% v=$v0)"
	if [ $RAW -eq 1 ]; then
		echo mem > /sys/power/state || { echo "suspend falló"; exit 1; }
	else
		systemctl suspend || { echo "systemctl suspend falló"; exit 1; }
	fi
	sleep 5
	t1=$(up); dur=$((t1 - t0)); p1=$(cap); v1=$(volt)
	w1=$(cat /sys/power/wakeup_count 2>/dev/null || echo 0)
	echo "ciclo $i: ${dur}s reales, capacidad $p0% -> $p1%, wakeups +$((w1 - w0))"
	echo "$i,$dur,$p0,$p1,$v0,$v1,$((w1 - w0))" >> "$CSV"
	[ "$dur" -lt $((SECS * 8 / 10)) ] && echo "  aviso: despertó antes de tiempo; hubo otra fuente de wake"
	if [ $i -gt 1 ] || [ $DISCARD -eq 0 ]; then
		[ -z "$first_cap" ] && { first_cap=$p0; first_up=$t0; }
		last_cap=$p1; last_up=$t1
	fi
	i=$((i + 1))
done
if [ -n "$first_cap" ]; then
	total=$((last_up - first_up))
	awk -v a=$first_cap -v b=$last_cap -v t=$total 'BEGIN{
		h=t/3600; if (h<=0) exit;
		printf "TOTAL: %d%% -> %d%% en %.2f h = %.2f %%/h (resolución ±%.2f %%/h)\n", a, b, h, (a-b)/h, 1/h }'
fi
echo "CSV: $CSV"
