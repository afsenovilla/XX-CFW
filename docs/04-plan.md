# 04 · Plan de trabajo y protocolo de medición

## Fase 0 · Caracterizar la consola real (1-2 tardes, sin compilar nada)

Requiere ROCKNIX nightly posterior al 19-sep-2026 (contiene el PR #3316) en una RG34XXSP y SSH.

1. `tools/diag/xx-lid-diag.sh` → confirmar: `mem_sleep` muestra `s2idle [deep]`, existe un
   evdev con `SW_LID`, el GPIO es PE7, `wakeup_sources` lista el lid, `wakealarm` escribible.
2. Ciclo manual: cerrar tapa, esperar 10 min, abrir. Confirmar que vuelve en <3 s con audio,
   Wi-Fi y mandos. Anotar `wake_irq` del stub si está expuesto en los registros de RTC.
3. `tools/diag/xx-sleep-drain.sh 3600 3` → %/h del modo `deep` con tapa cerrada. Repetir con
   Wi-Fi desactivado desde el menú para aislar su coste. Repetir con `s2idle` como referencia.
4. Misma medida en stock (super standby y normal) y en muOS si hay tarjeta, para tener la
   tabla completa en el mismo aparato y la misma batería.

Salida: `docs/medidas/<fecha>-rg34xxsp.md` con la tabla.

## Fase 1 · Política de tapa (`xx-lidd`) sobre ROCKNIX

1. Cross-compilar `src/lidd` y copiarlo a `/storage/xx-cfw/`.
2. Instalar drop-in de logind, unidad systemd y hooks de `integration/rocknix/`.
3. Probar: ventana corta (`lid_wake_window=120`) para ver la transición a `shutdown` con
   savestate en 2 min; luego valores reales.
4. Afinar el hook de RetroArch (`network_cmd_enable = true` en retroarch.cfg; `SAVE_STATE` por
   UDP al puerto 55355) y documentar standalone (PPSSPP, Drastic, etc.).
5. Empaquetar como `packages/xx-cfw/xx-lidd/package.mk` en un fork de ROCKNIX.

Criterio de hecho: 20 ciclos abrir/cerrar sin fallo; una noche cerrada acaba en apagado con
estado guardado y al encender se reanuda la partida.

## Fase 2 · Bajar el consumo del sueño con wake por tapa (firmware/kernel)

Objetivo: de ~3 %/h a ≤1,5 %/h manteniendo wake por tapa. Orden por relación ahorro/esfuerzo:

1. **Wi-Fi/BT**: cortar la alimentación del módulo SDIO en `pre-sleep` (regulador o GPIO de
   enable; ver DT de ROCKNIX para la RG34XXSP) y hacer rebind de `mmc`/`rtw88_sdio` en
   `post-wake`. Medir. Es probablemente el mayor consumidor.
2. **Raíles del PMIC**: en el stub (o en el driver axp20x con `regulator-state-mem`) bajar
   DCDC de CPU/GPU/SYS a tensión de sleep y apagar LDOs no críticos durante `SYSTEM_SUSPEND`.
   Referencia: máscara `0x4003` del stock. `vcc-io` se queda. Medir cada raíl por separado.
3. **Pantalla**: gatear DE/TCON y el raíl de backlight (hoy las puertas siguen habilitadas).
4. **SD de juegos**: probar a desalimentarla y re-escanear; descartar si el resume tarda más de 1 s.
5. **Oscilador 24 MHz / PLL_PERI0**: solo si el PIO puede seguir detectando flancos con el
   reloj de debounce a 32 kHz (el stock lo hace solo en la ruta profunda; comprobar).

Cada paso: parche aislado + medida con `xx-sleep-drain.sh` + 20 ciclos de wake por tapa.

## Fase 3 · Ruta profunda tipo stock (opcional, largo plazo)

Replicar el "Super standby" del stock en firmware abierto: marcador RTC + dirección de
reanudación, sleep del PMIC y resume asistido por SPL de U-Boot que reinicialice la DRAM sin
perder contenido. kailashrs lo describe como no implementado y como cambios coordinados en
Linux, stub/TF-A y SPL. **La tapa no despertará de este modo** (hardware); su valor es que la
fase 3 de la política pase de "apagar" a "dormir a ~1 %/h con resume instantáneo por POWER".

## Protocolo de medición (resumen)

- Misma consola, misma batería, carga 60-90 %, temperatura ambiente estable.
- Métrica primaria: `charge_now` (µAh) si el gauge lo expone; secundaria `capacity` (%),
  terciaria `voltage_now`. Mínimo 3 ciclos de 1 h por configuración.
- Cada ciclo: armar `wakealarm` a +3600 s, suspender, al despertar registrar y volver a dormir.
  El script descarta el primer ciclo (calentamiento) si se le pasa `--discard-first`.
- Reportar %/h y mA equivalentes (`mAh_batería × %/h / 100`).

## Preguntas abiertas (para la Fase 0)

1. ¿Despierta el RTC interno o el PCF8563 desde `deep`? ¿Cuál expone `wakealarm`?
2. Polaridad de `hallkey` en el BSP (¿1 = cerrada?).
3. ¿El `wakeup-event-action = EV_ACT_DEASSERTED` evita que un cierre durante el resume vuelva a
   dormir la consola? Si no, `xx-lidd` lo gestiona con `debounce_ms`.
4. ¿El gadget USB y el Wi-Fi vuelven de forma fiable tras 50 ciclos?
5. ¿Qué parte de los 3 %/h es Wi-Fi? (medida con y sin módulo).
