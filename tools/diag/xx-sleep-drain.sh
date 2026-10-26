#!/bin/sh
# xx-sleep-drain.sh — mide el consumo en suspensión con ciclos despertados por RTC.
#
# Uso: sh xx-sleep-drain.sh [segundos_por_ciclo=3600] [ciclos=3] [estado=mem]
#       opciones: --discard-first   no cuenta el primer ciclo
#                 --os-sleep N      (BSP) escribir N en os_sleep antes de dormir
#                 --csv fichero     ruta del CSV (defecto ./sleep-drain.csv)
# Cierra la tapa (o no: la tapa no importa para la medida) y déjala. Al acabar imprime
# %/h y mAh/h por ciclo y la media. Requiere un RTC con wakealarm escribible.
set -u
SECS=3600; CYCLES=3; STATE=mem; DISCARD=0; OS_SLEEP=""; CSV=./sleep-drain.csv
while [ $# -gt 0 ]; do
	case "$1" in
	--discard-first) DISCARD=1;;
	--os-sleep) OS_SLEEP=$2; shift;;
	--csv) CSV=$2; shift;;
	*) if [ -z "${A1:-}" ]; then A1=$1; elif [ -z "${A2:-}" ]; then A2=$1; else A3=$1; fi;;
	esac; shift
done
SECS=${A1:-$SECS}; CYCLES=${A2:-$CYCLES}; STATE=${A3:-$STATE}

BAT=""
for p in /sys/class/power_supply/*; do
	[ "$(cat $p/type 2>/dev/null)" = "Battery" ] && { BAT=$p; break; }
done
[ -n "$BAT" ] || { echo "no encuentro la batería en /sys/class/power_supply"; exit 1; }
RTC=""
for r in /sys/class/rtc/rtc*; do [ -w $r/wakealarm ] && { RTC=$r; break; }; done
[ -n "$RTC" ] || { echo "ningún RTC con wakealarm escribible"; exit 1; }
FULL=$(cat $BAT/charge_full 2>/dev/null || cat $BAT/charge_full_design 2>/dev/null || echo 0)

read_bat() {
	# imprime: charge_now(uAh) capacity(%) voltage_now(uV)
	printf '%s %s %s' "$(cat $BAT/charge_now 2>/dev/null || echo 0)" \
		"$(cat $BAT/capacity 2>/dev/null || echo 0)" "$(cat $BAT/voltage_now 2>/dev/null || echo 0)"
}

echo "batería=$BAT rtc=$RTC charge_full=${FULL}uAh estado=$STATE ciclos=$CYCLES x ${SECS}s"
[ -n "$OS_SLEEP" ] && echo "$OS_SLEEP" > /sys/class/power_supply/axp2202-battery/os_sleep 2>/dev/null
[ -e "$CSV" ] || echo "ciclo,inicio_epoch,duracion_s,charge_ini_uAh,charge_fin_uAh,cap_ini,cap_fin,v_ini_uV,v_fin_uV,pct_h,mAh_h,wake_count_delta" > "$CSV"

sum_pct=0; n=0
i=1
while [ $i -le $CYCLES ]; do
	set -- $(read_bat); c0=$1; p0=$2; v0=$3
	w0=$(cat /sys/power/wakeup_count 2>/dev/null || echo 0)
	t0=$(date +%s)
	echo 0 > $RTC/wakealarm
	echo $(( $(cat $RTC/since_epoch) + SECS )) > $RTC/wakealarm || { echo "no pude armar la alarma"; exit 1; }
	sync
	echo "ciclo $i: durmiendo ${SECS}s (charge=$c0 cap=$p0 v=$v0)"
	echo "$STATE" > /sys/power/state || { echo "suspend falló (rc=$?)"; exit 1; }
	t1=$(date +%s); dur=$((t1 - t0))
	set -- $(read_bat); c1=$1; p1=$2; v1=$3
	w1=$(cat /sys/power/wakeup_count 2>/dev/null || echo 0)
	if [ "$c0" -gt 0 ] && [ "$FULL" -gt 0 ]; then
		pct_h=$(awk -v a=$c0 -v b=$c1 -v f=$FULL -v d=$dur 'BEGIN{printf "%.2f", (a-b)/f*100*3600/d}')
		mah_h=$(awk -v a=$c0 -v b=$c1 -v d=$dur 'BEGIN{printf "%.1f", (a-b)/1000*3600/d}')
	else
		pct_h=$(awk -v a=$p0 -v b=$p1 -v d=$dur 'BEGIN{printf "%.2f", (a-b)*3600/d}')
		mah_h="n/a"
	fi
	echo "ciclo $i: ${dur}s reales, ${pct_h} %/h, ${mah_h} mAh/h (wakeups +$((w1 - w0)))"
	echo "$i,$t0,$dur,$c0,$c1,$p0,$p1,$v0,$v1,$pct_h,$mah_h,$((w1 - w0))" >> "$CSV"
	if [ $i -gt 1 ] || [ $DISCARD -eq 0 ]; then
		sum_pct=$(awk -v s=$sum_pct -v x=$pct_h 'BEGIN{print s+x}'); n=$((n + 1))
	fi
	[ "$dur" -lt $((SECS / 2)) ] && echo "  aviso: despertó antes de tiempo; ¿otra fuente de wake?"
	i=$((i + 1))
done
[ $n -gt 0 ] && awk -v s=$sum_pct -v n=$n 'BEGIN{printf "media: %.2f %%/h sobre %d ciclos\n", s/n, n}'
echo "CSV: $CSV"
