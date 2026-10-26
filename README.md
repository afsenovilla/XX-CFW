# XX-CFW — suspensión real al cerrar la tapa en Anbernic RG34XXSP (y resto de clamshells H700)

Objetivo: que cerrar la tapa de una RG34XXSP / RG35XXSP / RG SP se comporte como en una
GBA SP o una DS: la consola duerme al instante, gasta lo mínimo posible, y al abrir la tapa
sigue exactamente donde la dejaste en uno o dos segundos.

Este repositorio **no reinventa un CFW completo**. Se construye como una capa sobre
[ROCKNIX](https://rocknix.org) (kernel mainline, firmware abierto) y aporta las tres
piezas que hoy faltan para esa experiencia:

| Capa | Qué aporta XX-CFW | Estado |
|---|---|---|
| Política de tapa (userspace) | `xx-lidd`: daemon que convierte el sensor hall en un flujo "cerrar → suspender con wake por tapa → tras N min guardar estado y bajar a sueño profundo / apagado" | Código escrito y probado en host; sin probar en consola |
| Firmware (TF-A + stub SRAM) | Plan y puntos de intervención para reducir el consumo del sueño con wake por tapa (hoy ~2,5-3 %/h en ROCKNIX) hacia ~1 %/h | Documentado, sin implementar |
| Diagnóstico | Scripts para medir en la consola real qué funciona y cuánto gasta cada modo | Escritos, sin ejecutar en consola |

## Por qué así (resumen de la investigación)

Lo que cuenta aquí está desarrollado con fuentes en `docs/01-estado-del-arte.md` y `docs/02-hardware.md`.

1. **El sensor hall de la tapa es un GPIO del dominio principal del SoC (PE7).** Solo puede
   despertar la consola si ese dominio sigue alimentado durante el sueño.
2. **El firmware stock tiene dos profundidades de sueño.** La profunda ("Super standby",
   `os_sleep=16`) apaga el SoC mediante el PMIC y resucita por boot0; gasta poco (~1 %/h) pero
   **la tapa no puede despertar**, solo el botón de encendido o el RTC. La superficial
   (`os_sleep=0`) deja un núcleo en WFI con la DRAM en self-refresh; la tapa sí despierta pero
   consume 5-6 veces más. muOS, Knulli, BaseOS y spruceOS viven dentro de ese kernel BSP y de
   ese firmware cerrado, por eso ninguno despierta con la tapa en su modo eficiente.
3. **ROCKNIX ya tiene la ruta superficial en firmware abierto** (stub en SRAM de kailashrs,
   mergeado en septiembre de 2026) con wake por tapa validado precisamente en la RG34XXSP.
   Gasta ~2,5-3 %/h porque todavía no toca los raíles del PMIC ni apaga Wi-Fi/pantalla/SD como el stock.
4. Por tanto la meta realista es: **wake por tapa instantáneo con consumo cercano al stock
   superficial, y una política temporal que baje a algo aún más barato cuando la tapa lleva
   mucho tiempo cerrada** (guardando el estado antes). Eso es lo que implementa esta capa.

## Estructura

```
docs/                       diseño y plan (en español)
src/lidd/                   daemon xx-lidd (C, sin dependencias) + tests de host
tools/diag/                 scripts para ejecutar en la consola
integration/rocknix/        paquete, unidad systemd, hooks de sleep y drop-in de logind
integration/bsp/            notas para usar xx-lidd sobre muOS/Knulli/BaseOS (kernel BSP, os_sleep)
```

## Arranque rápido (desarrollo en PC)

```sh
make -C src/lidd            # compila xx-lidd para el host
make -C src/lidd test       # ejecuta los tests de la máquina de estados
make -C src/lidd CROSS_COMPILE=aarch64-linux-gnu-   # binario para la consola
```

## Arranque rápido (en la consola, ROCKNIX)

1. Copia `tools/diag/xx-lid-diag.sh` a la consola y ejecútalo por SSH. Dice si el kernel ve
   la tapa como `SW_LID`, si `/sys/power/mem_sleep` ofrece `deep` y qué fuentes de wake hay.
2. Ejecuta `tools/diag/xx-sleep-drain.sh 3600` con la tapa cerrada para medir %/h reales.
3. Sigue `integration/rocknix/README.md` para instalar `xx-lidd` y desactivar el manejo de
   tapa de logind.

## Licencia

GPL-2.0-or-later, como el stub de suspensión y el kernel en los que se apoya.
