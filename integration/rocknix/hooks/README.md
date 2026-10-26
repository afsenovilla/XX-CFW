# Hooks específicos de ROCKNIX (además de los genéricos en src/lidd/hooks/)

- `post-boot/xx-resume-last-game.sh` (TODO): servicio oneshot que, si existe
  `/storage/.config/xx-cfw/resume-pending` con menos de 24 h, relanza el último juego y
  borra el marcador. RetroArch cargará el estado automáticamente si `savestate_auto_load`
  está activo (ROCKNIX lo expone por juego/sistema en EmulationStation).
- Nada más por ahora: silenciar audio, LED, Wi-Fi y gadget USB ya lo hacen los quirks de
  ROCKNIX vía `/usr/lib/systemd/system-sleep/`, que xx-lidd ejecuta.
