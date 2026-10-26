# 03 · Arquitectura

```
┌─────────────────────────────────────────────────────────────────┐
│ Frontend / emulador (EmulationStation, RetroArch, standalone)   │
│   ← hooks pre-sleep.d / post-wake.d (pausar, guardar estado,     │
│      silenciar, restaurar governors…)                            │
├─────────────────────────────────────────────────────────────────┤
│ xx-lidd (userspace, C, sin dependencias)                         │
│   backend de tapa: evdev SW_LID (mainline) | hallkey sysfs (BSP) │
│   máquina de estados: AWAKE → CLOSING → SLEEP_LIDWAKE →          │
│                       DEEP_TRANSITION → (deep | shutdown)        │
│   alarma RTC (wakealarm) · /sys/power/state · os_sleep (BSP)     │
├─────────────────────────────────────────────────────────────────┤
│ Kernel (mainline 7.x en ROCKNIX)                                 │
│   gpio-keys PE7 SW_LID wakeup-source · PM de periféricos         │
│   /sys/power/mem_sleep = s2idle [deep]                           │
├─────────────────────────────────────────────────────────────────┤
│ TF-A (BL31) + h700-suspend-stub en SRAM A1                       │
│   DRAM self-refresh · PLL_CPUX off · CPU/APB a 32 kHz · WFI      │
│   (futuro) raíles PMIC en sleep · periféricos apagados           │
├─────────────────────────────────────────────────────────────────┤
│ Hardware: H700 · AXP717 · hall en PE7 · RTC                      │
└─────────────────────────────────────────────────────────────────┘
```

## Decisión 1: base ROCKNIX, no un CFW desde cero ni la rama BSP

- Desde cero: inviable; un CFW son años de trabajo en emuladores, frontend, drivers y build.
- Rama BSP (muOS/Knulli/BaseOS): el sueño vive en un BL31 cerrado de Allwinner y en
  `os_sleep`. Solo podríamos reproducir la política de ventanas de spruceOS; no hay forma de
  bajar el consumo de la ruta superficial porque no podemos tocar el firmware.
- ROCKNIX: kernel mainline, TF-A abierto, stub de suspensión GPL con fuente, wake por tapa ya
  validado en la RG34XXSP. Todo el margen de mejora está en código que podemos modificar.

`xx-lidd` se mantiene de todas formas portable al kernel BSP (backend `hallkey` + `os_sleep`)
para que la política sirva también a quien prefiera muOS por su emulación. Ver `integration/bsp/`.

## Decisión 2: el daemon sustituye el manejo de tapa de logind

ROCKNIX deja que `systemd-logind` reaccione a `SW_LID` (`HandleLidSwitch=suspend`). Para
implementar ventanas, alarma RTC y transición a sueño profundo necesitamos control del bucle
completo, así que:

- drop-in `/etc/systemd/logind.conf.d/xx-lid.conf` con `HandleLidSwitch=ignore`
  (y `HandleLidSwitchExternalPower=ignore`).
- `xx-lidd` abre el evdev de la tapa con `EVIOCGRAB`? **No.** No se hace grab: logind y
  EmulationStation pueden seguir leyendo el estado; solo se evita que logind actúe.
- Los hooks de sleep ya existentes en ROCKNIX (`/usr/lib/systemd/system-sleep/` y los quirks
  de dispositivo que silencian audio, apagan LED, re-attachan el gadget USB y reintentan el
  Wi-Fi) siguen ejecutándose porque `xx-lidd` suspende a través de `systemctl suspend` cuando
  está disponible y, si no, escribiendo `mem` en `/sys/power/state`.

## Decisión 3: la política temporal es el producto

El hardware fija que el wake por tapa solo existe en la ruta superficial. La experiencia "como
una DS" se consigue con tiempo:

| Fase | Duración (defecto) | Estado | Qué despierta | Coste |
|---|---|---|---|---|
| 1. Sueño con wake por tapa | 0 → `lid_wake_window` (90 min) | `deep` (stub) | tapa, POWER, RTC | ~3 %/h hoy; objetivo ≤1,5 %/h |
| 2. Transición | instantánea | despierta por alarma RTC con la tapa cerrada, pantalla apagada | — | segundos |
| 3. Sueño profundo | hasta abrir | `deep_action`: hoy `shutdown` tras guardar estado; cuando exista, la ruta profunda tipo stock | solo POWER (+tapa si hubiera hardware) | ~0 (apagado) o ~1 %/h |

En la fase 2 el daemon ejecuta `deep-transition.d/` (guardar estado del emulador; en
RetroArch por el comando de red `SAVE_STATE` o la señal que use el frontend) y después toma la
acción configurada. Al encender de nuevo, el frontend relanza el último juego y carga el estado
(ROCKNIX ya tiene autoload de savestate en RetroArch; para standalone se documenta por sistema).

Resultado para el usuario: cierra la tapa, vuelve antes de hora y media → abre y sigue al
instante. Vuelve al día siguiente → pulsa POWER, arranca (~20-30 s) y entra en el juego donde
estaba. La batería no se ha ido en la noche.

## Decisión 4: medir antes de optimizar firmware

El consumo real del modo `deep` de ROCKNIX en la RG34XXSP no está medido con rigor (kailashrs
no reporta %/h; el PR cita 3 %/h en RG34XX no SP). `tools/diag/xx-sleep-drain.sh` automatiza
la medida (ciclos de N minutos despertados por RTC, lectura de `charge_now`/`capacity`/`voltage_now`
antes y después, log CSV). Solo con esa cifra se decide qué periférico atacar primero.

## Interfaces del daemon

- Config: `/etc/xx-lidd.conf` (clave=valor). Ver `src/lidd/lidd.conf.example`.
- Hooks: `/etc/xx-lidd/hooks/{pre-sleep,post-wake,deep-transition}.d/*` ejecutables, en orden
  alfabético, con `XX_LID_REASON` en el entorno (`lid`, `alarm`, `power`, `unknown`).
- Control: `SIGUSR1` fuerza "tapa cerrada" (útil para probar sin tapa), `SIGUSR2` fuerza
  "tapa abierta", `SIGHUP` recarga la configuración.
- Log: stderr (journald bajo systemd), nivel por `log_level`.
