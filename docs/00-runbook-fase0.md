# 00 · Guion de la Fase 0: primera tarde con la RG34XXSP

Objetivo: salir con tres números (%/h en `deep`, en `s2idle` y con Wi-Fi apagado) y la
confirmación de que la tapa despierta la consola desde `deep`. Nada de esto modifica la
consola de forma permanente: todo va en una microSD aparte.

## Material

- RG34XXSP cargada al 60-90 %.
- Una microSD de 16 GB o más para ROCKNIX (la de stock no se toca).
- PC con balenaEtcher o `dd`, y Wi-Fi para el SSH.
- Este repositorio clonado (`dist/xx-lidd-aarch64-static`, `tools/diag/`).

## 1. Grabar ROCKNIX

1. Descargar la imagen `ROCKNIX-H700.aarch64-<fecha>.img.gz` de
   https://github.com/ROCKNIX/distribution/releases. Debe ser la release 20261001 o
   posterior, o un nightly posterior al 19-sep-2026, para que incluya el PR #3316 (deep sleep).
2. Grabarla en la microSD con Etcher. Primer arranque: ROCKNIX redimensiona la partición y
   reinicia solo. Espera a ver EmulationStation.
3. En EmulationStation: Ajustes → Network → conectar Wi-Fi y activar SSH. Anotar la IP.
   Ajustes → System Settings → Suspend → **Enable suspend = off** (desactiva el "fake suspend"
   para que no se mezcle con el suspend real).
4. Desde el PC: `ssh root@<ip>` (contraseña por defecto `rocknix`; si no entra, mirar la
   contraseña en Ajustes → Network).

## 2. Radiografía

```sh
scp tools/diag/*.sh root@<ip>:/storage/
ssh root@<ip> 'sh /storage/xx-lid-diag.sh' | tee medidas/$(date +%F)-diag.txt
```

Lo que tiene que aparecer:

- `/sys/power/mem_sleep: s2idle [deep]` → la build lleva el stub. Si pone `[s2idle]`, cambiar de build.
- Un `/dev/input/eventN` con `-> tiene SW_LID`.
- En `fuentes de wake`, una línea con `lid` o el nombre del gpio-keys.
- Un RTC con `writable=si`.

Si falla alguno, pega el fichero completo en el chat y paramos ahí.

## 3. Prueba manual de la tapa (5 minutos)

1. Lanza un juego cualquiera.
2. Cierra la tapa. El LED debería apagarse en 1-2 s.
3. Espera 2 minutos. Abre la tapa. Cronometra hasta que ves imagen y responde a los mandos.
4. Repite 5 veces. Anota tiempos y si audio, Wi-Fi (`ping` desde el PC) y mandos vuelven.
5. `ssh root@<ip> 'dmesg | grep -iE "suspend|resume|wake" | tail -30'` y guárdalo.

## 4. Medida de consumo (la parte larga)

Con la consola **dentro de un juego** (es el caso real) y la tapa cerrada:

```sh
ssh root@<ip> 'sh /storage/xx-sleep-drain.sh 3600 3 --csv /storage/drain-deep.csv'
```

Son 3 horas. El script despierta la consola cada hora por RTC, apunta la batería y vuelve a
dormir. Al final imprime la media. Luego, en sesiones distintas:

```sh
# referencia s2idle (lo que había antes del stub)
ssh root@<ip> 'sh /storage/xx-sleep-drain.sh 3600 2 s2idle --csv /storage/drain-s2idle.csv'
# deep con Wi-Fi apagado desde Ajustes → Network, para aislar su coste
ssh root@<ip> 'sh /storage/xx-sleep-drain.sh 3600 2 --csv /storage/drain-deep-nowifi.csv'
```

Y si tienes la microSD del stock a mano, la misma consola con el stock en reposo una noche
(anotar % antes y después, con el reloj) da la cuarta referencia.

## 5. Primer contacto con xx-lidd (opcional, si todo lo anterior va bien)

```sh
scp dist/xx-lidd-aarch64-static root@<ip>:/storage/xx-lidd
scp src/lidd/lidd.conf.example root@<ip>:/storage/.config/xx-lidd.conf
ssh root@<ip>
  mkdir -p /storage/.config/system/logind.conf.d
  printf '[Login]\nHandleLidSwitch=ignore\nHandleLidSwitchExternalPower=ignore\n' > /storage/.config/system/logind.conf.d/xx-lid.conf
  # si logind no lee ese directorio, editar el logind.conf que apunte `systemctl show logind`
  systemctl restart systemd-logind
  chmod +x /storage/xx-lidd
  /storage/xx-lidd -vv -c /storage/.config/xx-lidd.conf      # en primer plano, Ctrl-C para salir
```

Con eso en marcha, cierra y abre la tapa unas veces y copia lo que imprime. Para probar la
transición sin esperar 90 minutos, pon `lid_wake_window = 3m` en la configuración.

## 6. Qué me devuelves

- `medidas/<fecha>-diag.txt`, los CSV de drenaje y el `dmesg`.
- Tiempos de resume de la prueba manual y cualquier cosa rara (Wi-Fi que no vuelve, audio,
  segunda pulsación necesaria…).

Con eso relleno `docs/04-plan.md` con cifras reales y decido el primer parche de firmware.

## Si no arranca (aprendido a base de golpes)

- **La tarjeta del sistema va en la ranura INT (TF1).** En EXT (TF2) no arranca nada, con
  ninguna imagen, y los síntomas son idénticos a una batería muerta o una imagen equivocada.
- **LED naranja con el cargador puesto, incluso sin tarjeta**: es el LED de carga del AXP717,
  gobernado por hardware. Significa que cable, cargador y batería están bien. Si parpadea,
  es un fallo de carga.
- **LED rojo**: lo enciende el software. Si se ve, el SoC ha arrancado; un rojo fijo con
  pantalla negra apunta al panel, al DTB o a que el sistema cree que la tapa está cerrada.
- **Tras una descarga profunda la consola puede quedarse colgada** y no responder a POWER
  aunque el LED de carga esté encendido y la batería lleve horas cargando. **Pulsar el botón
  RESET** (agujero en el lateral, con un clip) la saca de ese estado. Probar esto antes de
  regrabar nada.
- Para distinguir "imagen de DRAM equivocada" de "panel equivocado" en ROCKNIX: arrancar
  3 minutos, apagar y mirar en el PC si la tarjeta tiene una partición nueva. Si la tiene,
  Linux arrancó y el problema es el panel (variante V2); si no, es la DRAM (DDR3/DDR4).
- Cargadores: usar cable USB-A a USB-C y un cargador de 5 V normal. Los cargadores de carga
  rápida y los cables C-a-C fallan en parte de las unidades Anbernic.
