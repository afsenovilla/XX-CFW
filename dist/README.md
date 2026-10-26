# Binarios precompilados

- `xx-lidd-aarch64-static`: xx-lidd para la consola (ARM64, estático, sin dependencias).
  Compilado con `make CROSS_COMPILE=aarch64-linux-gnu- LDFLAGS=-static` desde el commit
  indicado en `git log -- dist/`. Comprobar con `sha256sum -c SHA256SUMS`.
