#!/bin/sh
# xx-lid-diag.sh — radiografía de la tapa y del sueño en una consola H700.
# Funciona en ROCKNIX (mainline) y en muOS/Knulli/BaseOS (kernel BSP). Solo lee.
# Uso: sh xx-lid-diag.sh > diag.txt
set -u
say() { printf '\n== %s ==\n' "$*"; }
cat_if() { for f in "$@"; do [ -r "$f" ] && { printf '%s: ' "$f"; cat "$f" 2>/dev/null | head -c 400; echo; }; done; }

say "sistema"
uname -a
cat_if /etc/os-release /storage/.config/system/configs/system.cfg 2>/dev/null | head -5
[ -r /proc/device-tree/model ] && { printf 'modelo DT: '; tr -d '\0' < /proc/device-tree/model; echo; }
[ -r /proc/device-tree/universal-standby ] && { printf 'universal-standby (DT stock): '; od -An -tx1 /proc/device-tree/universal-standby; }

say "estados de sueño"
cat_if /sys/power/state /sys/power/mem_sleep /sys/power/pm_debug_messages /sys/power/wakeup_count

say "tapa (mainline: evdev SW_LID)"
for ev in /dev/input/event*; do
	n=$(basename "$ev")
	name=$(cat /sys/class/input/$n/device/name 2>/dev/null)
	sw=$(cat /sys/class/input/$n/device/capabilities/sw 2>/dev/null)
	key=$(cat /sys/class/input/$n/device/capabilities/key 2>/dev/null)
	printf '%s  name="%s"  sw=%s\n' "$ev" "$name" "${sw:-0}"
	case "$sw" in *1|*3|*5|*7|*9|*b|*d|*f) echo "   -> tiene SW_LID (bit 0)";; esac
	echo "$key" | grep -q . && printf '   key caps: %s\n' "$(echo "$key" | tail -c 40)"
done
for d in /sys/bus/platform/devices/*keys* /sys/devices/platform/*lid* /sys/devices/platform/*key*; do
	[ -e "$d" ] || continue
	echo "$d"; cat_if "$d/power/wakeup" "$d/power/wakeup_count" "$d/power/wakeup_active_count"
done
if [ -r /sys/kernel/debug/gpio ]; then
	say "GPIO (busca PE7 / lid)"
	grep -iE 'lid|hall|PE7|gpio-135' /sys/kernel/debug/gpio || grep -A2 -i 'gpiochip.*300b000' /sys/kernel/debug/gpio | head -5
fi

say "tapa (BSP: hallkey / os_sleep)"
cat_if /sys/class/power_supply/axp2202-battery/hallkey /sys/class/power_supply/axp2202-battery/os_sleep
ls /sys/class/power_supply/axp2202-battery/ 2>/dev/null | tr '\n' ' '; echo

say "fuentes de wake"
if [ -r /sys/kernel/debug/wakeup_sources ]; then
	awk 'NR==1 || $2>0 || $3>0' /sys/kernel/debug/wakeup_sources | cut -c1-100
else
	echo "(monta debugfs: mount -t debugfs none /sys/kernel/debug)"
fi
grep -iE 'lid|key|axp|rtc|pio|nmi' /proc/interrupts | cut -c1-110

say "RTC"
for r in /sys/class/rtc/rtc*; do
	[ -e "$r" ] || continue
	printf '%s name=%s since_epoch=%s wakealarm=%s writable=%s\n' "$r" "$(cat $r/name 2>/dev/null)" \
		"$(cat $r/since_epoch 2>/dev/null)" "$(cat $r/wakealarm 2>/dev/null)" "$([ -w $r/wakealarm ] && echo si || echo no)"
done
cat_if /sys/class/rtc/rtc0/device/power/wakeup

say "batería / cargador"
for p in /sys/class/power_supply/*; do
	printf '%s type=%s online=%s status=%s capacity=%s charge_now=%s charge_full=%s voltage_now=%s current_now=%s\n' \
		"$(basename $p)" "$(cat $p/type 2>/dev/null)" "$(cat $p/online 2>/dev/null)" "$(cat $p/status 2>/dev/null)" \
		"$(cat $p/capacity 2>/dev/null)" "$(cat $p/charge_now 2>/dev/null)" "$(cat $p/charge_full 2>/dev/null)" \
		"$(cat $p/voltage_now 2>/dev/null)" "$(cat $p/current_now 2>/dev/null)"
done

say "reguladores (estado y modo en suspend si el driver lo expone)"
for r in /sys/class/regulator/regulator.*; do
	printf '%-12s %-14s state=%-9s uV=%s suspend_mem_state=%s\n' "$(basename $r)" "$(cat $r/name 2>/dev/null)" \
		"$(cat $r/state 2>/dev/null)" "$(cat $r/microvolts 2>/dev/null)" "$(cat $r/suspend_mem_state 2>/dev/null)"
done 2>/dev/null | head -40

say "logind (ROCKNIX)"
grep -rE 'HandleLidSwitch|HandlePowerKey' /etc/systemd/logind.conf /etc/systemd/logind.conf.d/ /usr/lib/systemd/logind.conf.d/ 2>/dev/null
ls /usr/lib/systemd/system-sleep/ /etc/systemd/system-sleep/ 2>/dev/null

say "últimos mensajes de suspend/resume del kernel"
dmesg 2>/dev/null | grep -iE 'suspend|resume|wake|PM:|psci|lid' | tail -40
