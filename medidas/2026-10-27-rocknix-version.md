# ROCKNIX en la RG34XXSP del usuario

Salida pegada por el usuario por SSH, 2026-10-27.

```
OS_VERSION="20261001"
OS_BUILD="official"
BUILD_ID="c445081a59518f37d9776e5412dd7b14910696f7"
BUILD_BRANCH="next"
BUILD_DATE="Thu Oct  1 20:03:03 UTC 2026"
/sys/power/mem_sleep: s2idle [deep]
```

- Imagen: RG34XX SP (V2 Panel) (DDR4) según el instalador del usuario.
- La memoria correcta es DDR4: la imagen DDR3 no enciende ni el LED rojo.
- La build lleva el stub de suspensión (PR #3316): `deep` disponible.
- El selector de suspend de EmulationStation ofrece Default, FREEZE y MEM.
