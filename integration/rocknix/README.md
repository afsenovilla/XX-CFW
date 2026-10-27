# Integración con ROCKNIX (RG34XXSP y clamshells H700)

Requisito: ROCKNIX H700 posterior al 19-sep-2026 (`/sys/power/mem_sleep` debe mostrar
`s2idle [deep]`). Si solo muestra `[s2idle]`, la build no lleva el stub de suspensión y el
consumo será ~4 %/h.

## Instalación manual (para probar sin recompilar ROCKNIX)

```sh
# en el PC
make -C src/lidd CROSS_COMPILE=aarch64-linux-gnu-
scp src/lidd/xx-lidd root@<consola>:/storage/xx-cfw/
scp -r src/lidd/hooks integration/rocknix/hooks root@<consola>:/storage/xx-cfw/
scp src/lidd/lidd.conf.example root@<consola>:/storage/.config/xx-lidd.conf

# en la consola (ROCKNIX monta /usr de solo lectura; /storage persiste)
mkdir -p /storage/.config/system/logind.conf.d /storage/.config/xx-lidd/hooks
cp integration/rocknix/logind-xx-lid.conf /storage/.config/system/logind.conf.d/xx-lid.conf   # ver nota
cp -r /storage/xx-cfw/hooks/* /storage/.config/xx-lidd/hooks/
cp integration/rocknix/xx-lidd.service /storage/.config/system.d/
systemctl daemon-reload && systemctl enable --now xx-lidd
journalctl -fu xx-lidd
```

Nota sobre logind: ROCKNIX enlaza `/etc/systemd/logind.conf` a `/storage/.config/system/`
en algunas versiones; si el drop-in no se aplica, añade `HandleLidSwitch=ignore` al fichero
que `systemctl show -p LidSwitchIgnoreInhibited logind` revele como activo y reinicia logind
(`systemctl restart systemd-logind`). Sin esto, logind y xx-lidd se pelearán por la tapa.

El fichero de configuración se busca en `/etc/xx-lidd.conf`; la unidad lo pasa con
`-c /storage/.config/xx-lidd.conf`. Hooks en `/storage/.config/xx-lidd/hooks/`.

## Empaquetado en un fork de ROCKNIX

`package/package.mk` es el esqueleto para `packages/xx-cfw/xx-lidd/`. Instala el binario, la
unidad, el drop-in de logind y los hooks dentro de la imagen. Añadir `xx-lidd` a
`ADDITIONAL_PACKAGES` del proyecto H700 (`projects/ROCKNIX/devices/H700/options`).

## Interacción con lo que ROCKNIX ya hace

- Los hooks de `/usr/lib/systemd/system-sleep/` (silenciar audio, apagar LED, re-attach del
  gadget USB, reintento de rtw88) los ejecuta xx-lidd con `pre suspend` / `post suspend`,
  igual que systemd-sleep, porque escribe `mem` directamente en `/sys/power/state`.
  Si se prefiere pasar por `systemctl suspend`, poner `suspend_cmd` a un script que bloquee
  hasta el resume (p. ej. `systemctl suspend && sleep 2`); el daemon no sabría cuándo volvió.
- El "fake suspend" de EmulationStation debe quedar desactivado (Ajustes → Suspend → Enable
  suspend = off) o se superpondrá: es exactamente el conflicto que kailashrs documentó
  (UI congelada y audio roto al correr los dos caminos).
- `hooks/deep-transition.d/10-retroarch-savestate` usa el comando de red de RetroArch; activar
  `network_cmd_enable = "true"` en `/storage/.config/retroarch/retroarch.cfg`.
- Reanudar tras el apagado: `hooks/post-boot/xx-resume-last-game.sh` (a instalar como servicio
  oneshot tras EmulationStation) lee `/storage/.config/xx-cfw/resume-pending` y relanza el
  último juego con `emulationstation --run-last`/`es-launch`; está marcado como TODO porque
  depende de la versión de EmulationStation-next que lleve la build.

## Corrección tras el diagnóstico real (2026-10-27)

- **Usar `suspend_cmd = systemctl suspend` en ROCKNIX.** Escribir `mem` directamente en
  `/sys/power/state` se salta lo que hace systemd (congelar sesiones, hooks con su entorno), y
  los números de ~3 %/h de ROCKNIX se midieron pasando por systemd. `systemctl suspend`
  bloquea hasta que la consola despierta, así que el daemon sí sabe cuándo ha vuelto.
  (El párrafo anterior que dice lo contrario era erróneo.)
- logind ya trae `HandleLidSwitch=suspend` y `HandlePowerKey=suspend` en `/etc/systemd/logind.conf`.
  Antes de instalar `xx-lidd` hay que comprobar dónde se puede poner el drop-in en la build
  (en `/etc/systemd/logind.conf.d/` existe un README, pero no sabemos si es escribible).
- RTC: en la RG34XX-SP hay dos. `rtc0` (pcf8563 externo) no tiene `wakealarm`; `rtc1`
  (sun6i-rtc, el del SoC) sí, y es el que usa el stub (IRQ 136 del GIC). Sus relojes difieren
  en unos 5 días; no importa porque las alarmas se calculan sobre el propio `since_epoch`.
