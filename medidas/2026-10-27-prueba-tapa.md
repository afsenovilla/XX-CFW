# Prueba de tapa en ROCKNIX 20261001, RG34XX-SP (modo MEM)

Pruebas del usuario dentro de un juego: 2 min y 5 min cerrada, vuelve sin problema.

## Registro de systemd (extracto, 08:44 a 08:46)

- Suspend: "Suspending..." 08:46:10, "PM: suspend entry (deep)" 08:46:12. Unos 2 s.
- Resume: "PM: suspend exit" y "Lid opened" en el mismo segundo (08:46:39). Hooks de ROCKNIX
  (Modules, Power state, Input sense, Enabling WIFI, volumen, brillo) terminados en 2 s.
  "Operation 'suspend' finished" a +2 s.
- Los hooks de ROCKNIX ya hacen "Disabling WIFI" antes de dormir y "Enabling WIFI" al volver.
- Tras la primera reanudación (08:45:40), cierres de tapa a +9 s, +18 s y +25 s: logind los
  ignora. El cierre a +30 s (08:46:10) suspende de inmediato.

## Causa del retraso al volver a suspender

`HoldoffTimeoutSec=30s` (valor por defecto, comentado en /etc/systemd/logind.conf). No es
lentitud de los hooks, que acaban en 2 s. Propuesta: bajarlo a 3 s.

## Pendiente

- `cat /usr/lib/systemd/system-sleep/sleep` (3854 bytes) para ver qué hace "Disabling WIFI"
  y si corta o no el raíl `vcc-wifi`.
- `wakeup_count` de gpio-keys-lid.
