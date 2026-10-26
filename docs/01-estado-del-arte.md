# 01 · Estado del arte: la tapa y el sueño en las clamshells H700 (octubre 2026)

Consolas afectadas: Anbernic RG34XXSP, RG35XXSP, RG SP. Todas usan el SoC Allwinner H700
(variante del H616: 4× Cortex-A53, Mali G31), PMIC AXP717/AXP2202 y un sensor hall para la tapa.

## 1. Qué hace cada firmware al cerrar la tapa

| Firmware | Kernel | Mecanismo de sueño | ¿Despierta al abrir la tapa? | Consumo dormida (reportado) |
|---|---|---|---|---|
| Stock Anbernic | BSP 4.9 (Allwinner) | `echo mem > /sys/power/state` con PSCI `SYSTEM_SUSPEND` en el BL31 de Allwinner. Dos profundidades según `os_sleep` | Solo en modo "normal standby" (`os_sleep=0`) | Super standby ≈ 1 %/h (5 % en 5 h, RG34XXSP) [K401]. Guías antiguas de la familia 35XX citan 3-5 %/h en el modo normal [RGC] |
| muOS | BSP 4.9 | Mismo `mem`. Desde 2026 usan "un mecanismo de sueño más eficiente" (equivale al super standby) | **No**: hay que pulsar POWER. Los desarrolladores lo consideran comportamiento intencionado; "cero fuentes de wake del interruptor de tapa" [MUOS-FAQ] | No publicado |
| Knulli | BSP 4.9 | Mismo `mem` "como el resto de CFW" | No documentado | En la alpha de la RG34XXSP: 25 % en 5 h, frente a 5 % en stock. Mantenedor: "hemos pasado incontables horas intentándolo" sin lograr el consumo del stock [K401]. Proyecto archivado en junio 2026 |
| BaseOS / spruceOS | BSP 4.9 | `mem` + `os_sleep=16` (super standby) por defecto. spruceOS añadió en oct-2026 "Wake on lid open" (Off/10 min/30 min/Siempre) que escribe `os_sleep=0` durante una ventana y vuelve a 16 vía alarma RTC [SPR1894] | Solo dentro de la ventana | Con el sensor activo "5-6 veces más batería dormida" según el autor de BaseOS [BASE30] |
| ROCKNIX | Mainline (7.x) | Hasta sept-2026 solo *s2idle* (≈3,5-4 %/h, con cuelgues en sesiones largas) o "fake suspend" (congela procesos, apaga pantalla, parkea núcleos, temporizador de apagado) [ROCK-FAKE]. Desde el PR #3316 (mergeado 19-sep-2026): `deep` real mediante un stub en SRAM que pone la LPDDR4 en self-refresh y aparca la CPU en WFI [ROCK3316] | **Sí** (gpio-keys `SW_LID` con `wakeup-source`; validado con dos ciclos de tapa en RG34XXSP por kailashrs [KAI-VAL]) | 2,5 %/h (RG35XX H) y 3 %/h (RG34XX) en sueño nocturno dentro de juego [ROCK3316] |

## 2. Por qué nadie tiene "sueño profundo + despertar con la tapa"

El análisis estático del stock V1.0.6 de la RG34XXSP hecho por kailashrs [KAI-OEM] explica el bloqueo:

- El DT del stock declara `universal-standby = 1`. Linux siempre llama a PSCI `SYSTEM_SUSPEND`
  y es el BL31 quien elige la profundidad en función de las fuentes de wake registradas.
- **Ruta superficial (WFI):** el programa en SRAM para los PLL de CPU y DDR, deja la DRAM en
  self-refresh y la CPU en WFI. Cualquier interrupción del GIC que siga habilitada (botón,
  RTC, y el GPIO del hall en PE7) despierta.
- **Ruta profunda:** si no hay fuentes de wake "cualificadas", además apaga el oscilador de
  24 MHz, graba en el RTC un marcador de standby y una dirección de reanudación, y entra en un
  sueño controlado por el PMIC. Al despertar, **boot0** reconstruye la DRAM sin borrarla y salta
  a la dirección guardada. En este estado el SoC está esencialmente apagado: solo el PMIC
  (botón POWER, VBUS) y el RTC pueden despertar.
- `os_sleep` es un atributo sysfs del driver de batería AXP2202 del stock
  (`/sys/class/power_supply/axp2202-battery/os_sleep`). Con valor distinto de 0 el callback
  de suspend del dispositivo hall no arma su IRQ como wake, lo que permite al BL31 tomar la
  ruta profunda. Con 0 el hall queda armado y el BL31 se queda en la ruta superficial.

Conclusión: el sensor hall cuelga de **PE7, un pin del dominio principal del SoC**. Para que
despierte, ese dominio tiene que estar alimentado y el GIC vivo, es decir, hace falta la ruta
superficial. La ruta profunda del stock apaga ese dominio, así que no hay forma de que la tapa
la despierte sin cambiar hardware. No es una limitación de software de muOS/Knulli: es de la
placa.

## 3. Dónde está el margen

Si la ruta superficial del stock gasta "5-6 veces más" que el super standby (~1 %/h) estamos
hablando de ~5 %/h en stock; ROCKNIX ya la deja en 2,5-3 %/h sin tocar el PMIC. Con una
batería de 3 300 mAh, 3 %/h son ~100 mA medios, muy por encima de lo que necesita un A53
aparcado a 32 kHz con la DRAM en self-refresh (decenas de mA). El consumo sobrante está en
periféricos que siguen encendidos:

- Módulo Wi-Fi/BT por SDIO: tanto el driver stock como rtw88 mantienen la alimentación SDIO
  durante el suspend [KAI-PER].
- Raíles del PMIC sin modificar en suspend (el stub no toca tensiones) [KAI-DESIGN].
- Tarjeta SD de juegos mantenida alimentada a propósito (`vcc3v3-mmc2`) y `vcc-io` always-on
  porque alimenta la SD de arranque y varios bancos de pines [ROCK3316].
- Amplificador y raíl de altavoz always-on en el DT [KAI-PER].
- Oscilador de 24 MHz y PLL_PERI0 encendidos (solo la ruta profunda del stock los apaga).

Cada uno de esos puntos es un candidato de medición y optimización en `docs/04-plan.md`.

## 4. Lo que ya existe y reutilizamos

- **h700-suspend-stub** (kailashrs; empaquetado por Jacob-Matthew-Cook / mburakov, GPL-2.0+):
  programa SRAM A1 que TF-A ejecuta para `SYSTEM_SUSPEND`. `clocks_down()` pasa CPU/AXI, APB1 y
  APB2 a 32 kHz y apaga PLL_CPUX; no toca puertas de bus ni el PIO, lo que explica que el GPIO
  del hall siga detectando flancos. Espera con WFI con IRQ/FIQ enrutadas a EL3 y al volver lee
  en el GIC la primera interrupción pendiente y habilitada (`wake_irq`). Lleva watchdog en la
  reanudación y registra etapas en registros del RTC para depurar [STUB].
- **ROCKNIX PR #3316**: parches de kernel 0157-0163 y 0210 (filtrado del botón de encendido
  para que el wake no vuelva a dormir la consola, máscara DAT0 del controlador eMMC, SD de
  juegos alimentada, fix de reloj PHY2, gating de pantalla/pads/códec, OPP de suspend a
  480 MHz), quirks (LED apagado, re-attach del gadget USB tras resume, reintento de probe de
  Wi-Fi) [ROCK3316].
- **kailashrs/H700_rocknix_enhancement**: fuente del stub, parches de TF-A y U-Boot, política
  de suspend real/falso para la RG34XXSP y los documentos OEM_STANDBY/DESIGN/PERIPHERALS/
  VALIDATION usados aquí [KAI].
- **spruceOS PR #1894**: la política de "ventana de wake por tapa + alarma RTC + volver al
  modo barato" que generalizamos en `xx-lidd` [SPR1894].

## Fuentes

- [K401] https://github.com/knulli-cfw/distribution/issues/401
- [RGC] https://retrogamecorps.com/2024/06/07/anbernic-rg35xx-family-starter-guide/
- [MUOS-FAQ] https://community.muos.dev/t/opening-device-no-longer-wakes-from-sleep/17
- [BASE30] https://github.com/pvaibhav/BaseOS/issues/30
- [SPR1894] https://github.com/spruceUI/spruceOS/pull/1894 (commit b9c35fa)
- [ROCK3316] https://github.com/ROCKNIX/distribution/pull/3316
- [ROCK-FAKE] https://rocknix.org/configure/fake-suspend/
- [STUB] https://github.com/Jacob-Matthew-Cook/h700-suspend-stub (src/main.c, src/clock.c)
- [KAI] https://github.com/kailashrs/H700_rocknix_enhancement
- [KAI-OEM] …/docs/OEM_STANDBY.md · [KAI-DESIGN] …/docs/DESIGN.md · [KAI-PER] …/docs/PERIPHERALS.md · [KAI-VAL] …/docs/VALIDATION.md
- [DTS-SP] https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/allwinner/sun50i-h700-anbernic-rg35xx-sp.dts
- [TFA] https://trustedfirmware-a.readthedocs.io/en/latest/plat/allwinner.html ("el H616 carece del procesador de gestión ARISC")
