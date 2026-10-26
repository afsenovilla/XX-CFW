# xx-lidd sobre el kernel BSP (muOS, Knulli, BaseOS/spruceOS, stock)

Aquí no hay firmware que mejorar: el BL31 de Allwinner es cerrado y elige la profundidad
según `os_sleep`. Lo que sí se puede hacer es la misma política temporal que en ROCKNIX,
generalizando lo que spruceOS añadió en su PR #1894:

| Fase | BSP |
|---|---|
| Sueño con wake por tapa | `os_sleep=0` + `mem` (ruta superficial, ~5 %/h) |
| Transición al vencer la ventana | alarma RTC → hooks `deep-transition.d` (guardar estado) |
| Sueño profundo | `deep_action = sleep` → `os_sleep=16` + `mem` (super standby, ~1 %/h, solo POWER despierta) |

Configuración mínima (`/etc/xx-lidd.conf` o donde el CFW permita persistir):

```
lid_wake_window = 30m
deep_action = sleep
hallkey_path = /sys/class/power_supply/axp2202-battery/hallkey
hallkey_closed_value = 1          # verificar: cat hallkey con la tapa abierta y cerrada
os_sleep_path = /sys/class/power_supply/axp2202-battery/os_sleep
systemd_sleep_hooks = no
suspend_cmd =                     # vacío: escribe mem en /sys/power/state
```

Puntos a verificar en cada CFW:

- Qué proceso maneja ya la tapa (muOS: su `muxfrontend`/scripts de sleep; spruceOS: el
  "lid watchdog"). Hay que desactivarlo para que no compita con xx-lidd.
- Que escribir `os_sleep` antes de cada `mem` sea suficiente (spruceOS lo hace así y
  funciona en BaseOS 1.4.0 en RG SP y RG35XXSP).
- Qué RTC expone `wakealarm` y despierta de verdad desde super standby.
- Al despertar desde super standby con POWER, el stock reanuda vía boot0; comprobar que el
  frontend del CFW tolera la reanudación (muOS ya lo hace en 2601 Jacaranda).

El script de medida `tools/diag/xx-sleep-drain.sh --os-sleep 16` sirve igual aquí y permite
poner número al "5-6 veces más" en la misma consola.
