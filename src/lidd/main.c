/* SPDX-License-Identifier: GPL-2.0-or-later
 * xx-lidd: punto de entrada.
 *
 *   xx-lidd [-c /etc/xx-lidd.conf] [-v] [-n]
 *     -v  más detalle (repetible)
 *     -n  modo ensayo: no suspende ni apaga, solo registra lo que haría
 *
 * Señales: SIGUSR1 = fingir "tapa cerrada", SIGUSR2 = fingir "tapa abierta",
 *          SIGHUP = recargar configuración, SIGTERM/SIGINT = salir.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lidd.h"
#include "platform_linux.h"

static struct linux_platform g_lp;

static void on_signal(int sig)
{
	switch (sig) {
	case SIGUSR1: linux_platform_signal(&g_lp, 'c'); break;
	case SIGUSR2: linux_platform_signal(&g_lp, 'o'); break;
	case SIGHUP:  linux_platform_signal(&g_lp, 'h'); break;
	default:      g_lp.lidd->quit = 1; linux_platform_signal(&g_lp, 'q'); break;
	}
}

static int load_config(const char *path, struct lidd_config *cfg, struct linux_platform *lp)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		if (errno == ENOENT)
			return 0;
		fprintf(stderr, "xx-lidd: %s: %s\n", path, strerror(errno));
		return -1;
	}
	char line[512], err[128];
	int ln = 0, rc = 0;
	while (fgets(line, sizeof(line), f)) {
		ln++;
		int r = lidd_config_parse_line(cfg, line, err, sizeof(err));
		if (r < 0) {
			fprintf(stderr, "xx-lidd: %s:%d: %s\n", path, ln, err);
			rc = -1;
		} else if (r == 1) {
			char key[64], val[256];
			if (sscanf(line, " %63[^= \t\n] = %255[^\n]", key, val) == 2 ||
			    sscanf(line, " %63[^= \t\n]=%255[^\n]", key, val) == 2) {
				for (size_t n = strlen(val); n && (val[n-1] == ' ' || val[n-1] == '\t'); n--)
					val[n-1] = '\0';
				if (linux_platform_parse_line(lp, key, val) != 0)
					fprintf(stderr, "xx-lidd: %s:%d: clave desconocida '%s'\n", path, ln, key);
			}
		}
	}
	fclose(f);
	return rc;
}

static int dry_suspend(void *ctx)
{
	(void)ctx;
	fprintf(stderr, "xx-lidd[DRY] suspend (no se ejecuta); durmiendo 3 s\n");
	sleep(3);
	return 0;
}

static void dry_shutdown(void *ctx)
{
	(void)ctx;
	fprintf(stderr, "xx-lidd[DRY] shutdown (no se ejecuta)\n");
}

int main(int argc, char **argv)
{
	const char *conf = "/etc/xx-lidd.conf";
	int verbose = 0, dry = 0, opt;

	while ((opt = getopt(argc, argv, "c:vnh")) != -1) {
		switch (opt) {
		case 'c': conf = optarg; break;
		case 'v': verbose++; break;
		case 'n': dry = 1; break;
		default:
			fprintf(stderr, "uso: %s [-c conf] [-v] [-n]\n", argv[0]);
			return 2;
		}
	}

	struct lidd d;
	memset(&d, 0, sizeof(d));
	lidd_config_defaults(&d.cfg);
	linux_platform_defaults(&g_lp);
	if (load_config(conf, &d.cfg, &g_lp) < 0)
		return 1;
	if (verbose)
		d.cfg.log_level = LOG_INFO + verbose;
	g_lp.log_level = d.cfg.log_level;

	struct platform pf;
	if (linux_platform_init(&g_lp, &d, &pf) < 0) {
		fprintf(stderr, "xx-lidd: no hay sensor de tapa utilizable; abortando\n");
		return 1;
	}
	if (dry) {
		pf.suspend = dry_suspend;
		pf.shutdown = dry_shutdown;
	}
	d.pf = &pf;

	struct sigaction sa = { .sa_handler = on_signal };
	sigemptyset(&sa.sa_mask);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	lidd_log(&d, LOG_INFO, "xx-lidd listo: ventana=%llds deep_action=%d estado tapa=%d",
		 (long long)d.cfg.lid_wake_window, d.cfg.deep_action, pf.lid_state(pf.ctx));

	/* Si arrancamos con la tapa ya cerrada, tratarlo como un cierre. */
	if (pf.lid_state(pf.ctx) == LID_CLOSED)
		lidd_run_closed_cycle(&d);

	while (!d.quit) {
		lidd_main_loop(&d);
		if (g_lp.reload_requested && !d.quit) {
			g_lp.reload_requested = 0;
			struct lidd_config fresh;
			lidd_config_defaults(&fresh);
			if (load_config(conf, &fresh, &g_lp) == 0) {
				d.cfg = fresh;
				lidd_log(&d, LOG_INFO, "configuración recargada");
			}
		}
	}
	return 0;
}
