# xx-lid-diag.sh en ROCKNIX 20261001 (RG34XX-SP, 7.2.0)

- DT: `Anbernic RG34XX-SP`. `/sys/power/state`: freeze mem disk. `mem_sleep`: `s2idle [deep]`.
- Tapa: `/dev/input/event3`, nombre `gpio-keys-lid`, tiene `SW_LID`. GPIO 135 = PE7, activo en bajo.
  `power/wakeup: enabled`. IRQ 177 `sunxi_pio_edge`, `Lid Switch`. `wakeup_count` 0 (aún sin suspender).
- Botón POWER: `event0` `axp20x-pek`.
- RTC: `rtc0` pcf8563 sin wakealarm; `rtc1` sun6i-rtc (IRQ 136) con wakealarm escribible.
- Batería: `battery`, sin `charge_now`; solo `capacity` en % entero (89 % con 3,975 V).
- Reguladores activos despierta (candidatos de ahorro): `vcc-wifi` 3,3 V, `vcc-spkr-amp` 3,3 V,
  `vdd-lcd` 3,3 V, `vcc3v3-mmc2` 3,3 V, `vcc-io` 3,3 V. `vdd-dram` 1,1 V (coherente con LPDDR4).
  El campo `suspend_mem_state` sale `disabled` en todos, incluida la DRAM, así que no es fiable.
- logind: `HandleLidSwitch=suspend`, `HandlePowerKey=suspend`.
- `/usr/lib/systemd/system-sleep/` contiene una entrada llamada `sleep`.
