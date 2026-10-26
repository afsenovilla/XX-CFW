# 02 · Hechos de hardware que condicionan el diseño

## SoC Allwinner H700 (familia H616)

- 4× Cortex-A53, GPU Mali G31, 2 GiB LPDDR4 en la RG34XXSP (1 GiB en RG35XXSP). El stub de
  suspensión embebe una variante por tipo de DRAM (LPDDR4 probada; LPDDR3 compilada pero sin
  probar) y el BL31 elige la que coincide con el controlador vivo; si ninguna coincide, TF-A
  deniega el suspend.
- **No hay ARISC/SCP.** TF-A lo dice explícitamente para el H616, así que crust (el firmware
  SCP libre de Allwinner) no es una opción. Todo lo que ocurre durante el sueño lo ejecuta el
  propio núcleo 0 desde SRAM A1 (dirección 0x20000) a 32 kHz, o lo gestiona el PMIC con el SoC apagado.
- Interrupciones del PIO principal en el DT mainline: ocho SPIs del GIC (51, 52, 53, 43, 54,
  55, 56, 57), una por banco con EINT. El NMI del PMIC es el SPI 103 y el RTC el SPI 104; el BL31
  del stock ignora esos dos al calcular si hay "fuentes de wake cualificadas".
- Durante el stub, APB1/APB2 corren a 32 kHz pero no se gatea ningún bus: el PIO sigue vivo
  y detecta flancos con su reloj de debounce, por eso el hall despierta.

## Sensor hall de la tapa

- DT mainline de la RG35XXSP: nodo `gpio-keys` con `gpios = <&pio 4 7 GPIO_ACTIVE_LOW>` (PE7),
  `linux,code = <SW_LID>`, `linux,input-type = <EV_SW>`, `wakeup-source`,
  `wakeup-event-action = <EV_ACT_DEASSERTED>` (despierta al **abrir**). La RG34XXSP no tiene
  DTS en mainline a día de hoy (solo rg35xx-2024/plus/h/sp); ROCKNIX lleva el suyo en sus
  parches. Verificar en la consola con `tools/diag/xx-lid-diag.sh` que el pin es el mismo.
- En el kernel BSP el estado se lee en `/sys/class/power_supply/axp2202-battery/hallkey`
  (0/1, polaridad a verificar) y no existe como dispositivo de entrada evdev.
- PE7 pertenece al dominio de E/S principal. No existe un camino hacia el dominio siempre
  encendido (RTC/PMIC). El AXP717 no tiene GPIOs de propósito general que pudieran leerlo.

## PMIC AXP717 / AXP2202

- Fuentes de wake en el dominio siempre encendido: botón POWER (pek), inserción de VBUS, eventos
  de batería/gauge y la alarma del RTC del SoC. `/sys/kernel/debug/wakeup_sources` en muOS lista
  `axp2202-battery`, `axp2202-usb`, `rtc`, `alarmtimer` y el wlan.
- En la ruta profunda del stock el PMIC entra en un modo de sueño con una máscara de raíles
  CPU/SYS/IO derivada del DT (`0x4003`) y máscara de DRAM 0. El stub abierto **no** cambia
  ninguna tensión en suspend. Aquí está la mayor parte del ahorro pendiente.
- `vcc-io` debe seguir encendido (alimenta la SD de arranque y varios bancos de pines).
- La ruta profunda apaga el oscilador de 24 MHz; la superficial lo deja encendido.

## RTC

- RTC interno del SoC (`rtc@7000000`, SPI 104) más un PCF8563 externo por `r_i2c` en la
  RG35XXSP. La alarma de wake se arma por `/sys/class/rtc/rtcN/wakealarm`; `xx-lidd` busca el
  primer RTC cuyo `wakealarm` sea escribible. Verificar con `rtcwake` cuál de los dos despierta
  realmente la consola desde `deep`.

## Periféricos que siguen consumiendo en suspend (fuentes: KAI-PER, ROCK3316)

| Periférico | Estado en suspend hoy (ROCKNIX) | Candidato |
|---|---|---|
| Wi-Fi/BT (SDIO, rtw88) | Alimentado; PG17 (wake) y PG19 (enable) de BT en bajo | Cortar alimentación del módulo (regulador/GPIO) y rebind en resume |
| Panel (SPI-DPI) | PI15 (supply) y PI14 (reset) en bajo; comandos display-off y sleep enviados; puertas de reloj DE/TCON siguen habilitadas | Gatear DE/TCON; verificar raíl de backlight |
| Audio | PI5 (amp enable) en bajo; raíl de altavoz always-on en DT; ASoC PM añadido | Hacer el raíl conmutable |
| USB | Gadget MUSB pierde sesión; re-attach por hook; PHY2 balanceado | Apagar PHY si no hay cable |
| SD de juegos | Mantenida alimentada a propósito | Evaluar apagar y re-escanear al volver |
| LED | Apagado por quirk | — |
| PMIC | Tensiones sin cambiar | Programar tensiones de sleep de DCDC/LDO como el stock |
