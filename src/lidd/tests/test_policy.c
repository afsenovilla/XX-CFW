/* SPDX-License-Identifier: GPL-2.0-or-later
 * Tests de host de la máquina de estados de xx-lidd con una plataforma simulada.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lidd.h"

/* Qué ocurre en cada llamada a suspend(): cuánto avanza el reloj si no salta la
 * alarma antes, cómo queda la tapa y si hubo pulsación de POWER. */
struct outcome {
	int64_t dt;
	int lid_after;
	bool power;
	bool fail;      /* el suspend no entra */
};

struct fake {
	int64_t now;
	int lid;
	bool ac;
	bool lid_wake;
	int64_t alarm_at;          /* 0 = sin alarma */
	struct outcome script[16];
	int nscript, iscript;
	int suspends;
	int shutdowns;
	char hooks[512];           /* secuencia "pre-sleep,deep-transition,post-wake," */
	int pending_power;
	int wait_lid_result;       /* qué devuelve wait_lid_change en el debounce */
	int lid_wake_false_count;
};

static int64_t f_now(void *c) { return ((struct fake *)c)->now; }
static int f_lid(void *c) { return ((struct fake *)c)->lid; }
static int f_wait(void *c, int ms)
{
	struct fake *f = c;
	f->now += ms / 1000;
	if (f->wait_lid_result != LID_UNKNOWN) {
		int r = f->wait_lid_result;
		f->wait_lid_result = LID_UNKNOWN;
		f->lid = r;
		return r;
	}
	return LID_UNKNOWN;
}
static int f_set_lid_wake(void *c, bool e)
{
	struct fake *f = c;
	f->lid_wake = e;
	if (!e)
		f->lid_wake_false_count++;
	return 0;
}
static int f_alarm(void *c, int64_t s) { struct fake *f = c; f->alarm_at = s ? f->now + s : 0; return 0; }
static int f_suspend(void *c)
{
	struct fake *f = c;
	f->suspends++;
	if (f->iscript >= f->nscript) {
		fprintf(stderr, "  script agotado en suspend #%d\n", f->suspends);
		abort();
	}
	struct outcome o = f->script[f->iscript++];
	if (o.fail)
		return -1;
	int64_t target = f->now + o.dt;
	if (f->alarm_at && f->alarm_at < target) {
		f->now = f->alarm_at;           /* despierta la alarma, tapa sin cambios */
		f->alarm_at = 0;
		return 0;
	}
	f->now = target;
	f->lid = o.lid_after;
	f->pending_power = o.power;
	return 0;
}
static bool f_power(void *c) { struct fake *f = c; bool p = f->pending_power; f->pending_power = 0; return p; }
static bool f_ac(void *c) { return ((struct fake *)c)->ac; }
static void f_hooks(void *c, const char *ph, const char *r)
{
	struct fake *f = c;
	(void)r;
	strncat(f->hooks, ph, sizeof(f->hooks) - strlen(f->hooks) - 2);
	strncat(f->hooks, ",", sizeof(f->hooks) - strlen(f->hooks) - 1);
}
static void f_shutdown(void *c) { ((struct fake *)c)->shutdowns++; }
static void f_sleep(void *c, int64_t ms) { ((struct fake *)c)->now += ms / 1000; }
static void f_log(void *c, int l, const char *fmt, va_list ap)
{
	(void)c; (void)l;
	if (getenv("VERBOSE")) { vfprintf(stderr, fmt, ap); fputc('\n', stderr); }
}

static const struct platform FPF = {
	.now = f_now, .lid_state = f_lid, .wait_lid_change = f_wait,
	.set_lid_wake = f_set_lid_wake, .arm_alarm = f_alarm, .suspend = f_suspend,
	.power_key_pressed = f_power, .on_ac = f_ac, .run_hooks = f_hooks,
	.shutdown = f_shutdown, .sleep_ms = f_sleep, .log = f_log,
};

static int failures;
#define CHECK(cond) do { if (!(cond)) { failures++; \
	fprintf(stderr, "  FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void setup(struct fake *f, struct lidd *d, struct platform *pf)
{
	memset(f, 0, sizeof(*f));
	f->lid = LID_CLOSED;
	f->lid_wake = true;
	f->wait_lid_result = LID_UNKNOWN;
	memset(d, 0, sizeof(*d));
	lidd_config_defaults(&d->cfg);
	d->cfg.log_level = LOG_DBG;
	*pf = FPF;
	pf->ctx = f;
	d->pf = pf;
}

static void add(struct fake *f, int64_t dt, int lid, bool power, bool fail)
{
	f->script[f->nscript++] = (struct outcome){ dt, lid, power, fail };
}

static void test_open_within_window(void)
{
	struct fake f; struct lidd d; struct platform pf;
	setup(&f, &d, &pf);
	add(&f, 600, LID_OPEN, false, false);
	enum wake_reason r = lidd_run_closed_cycle(&d);
	CHECK(r == WAKE_LID);
	CHECK(f.suspends == 1);
	CHECK(f.shutdowns == 0);
	CHECK(!strcmp(f.hooks, "pre-sleep,post-wake,"));
	CHECK(f.alarm_at == 0);        /* limpiada al salir */
	CHECK(f.lid_wake == true);
	printf("open_within_window: ok\n");
}

static void test_window_expires_shutdown(void)
{
	struct fake f; struct lidd d; struct platform pf;
	setup(&f, &d, &pf);
	d.cfg.lid_wake_window = 3600;
	/* 1: duerme "para siempre", la alarma salta a +3600. 2: tras el shutdown fallido
	 * seguimos dormidos; el usuario abre más tarde. */
	add(&f, 100000, LID_CLOSED, false, false);
	add(&f, 500, LID_OPEN, false, false);
	enum wake_reason r = lidd_run_closed_cycle(&d);
	CHECK(f.shutdowns == 1);
	CHECK(strstr(f.hooks, "deep-transition,") != NULL);
	CHECK(r == WAKE_LID);
	CHECK(f.suspends == 2);
	printf("window_expires_shutdown: ok (hooks=%s)\n", f.hooks);
}

static void test_alarm_not_before_deadline(void)
{
	/* Despertar espurio antes de la ventana: no debe disparar la transición. */
	struct fake f; struct lidd d; struct platform pf;
	setup(&f, &d, &pf);
	d.cfg.lid_wake_window = 3600;
	d.cfg.other_wake_grace = 5;
	add(&f, 120, LID_CLOSED, false, false);   /* wake por USB a los 2 min */
	add(&f, 60, LID_OPEN, false, false);
	enum wake_reason r = lidd_run_closed_cycle(&d);
	CHECK(r == WAKE_LID);
	CHECK(f.shutdowns == 0);
	CHECK(f.suspends == 2);
	CHECK(strstr(f.hooks, "deep-transition") == NULL);
	printf("alarm_not_before_deadline: ok\n");
}

static void test_power_key_with_lid_closed(void)
{
	struct fake f; struct lidd d; struct platform pf;
	setup(&f, &d, &pf);
	add(&f, 30, LID_CLOSED, true, false);
	enum wake_reason r = lidd_run_closed_cycle(&d);
	CHECK(r == WAKE_POWER);
	CHECK(f.suspends == 1);
	printf("power_key_with_lid_closed: ok\n");
}

static void test_deep_sleep_mode(void)
{
	struct fake f; struct lidd d; struct platform pf;
	setup(&f, &d, &pf);
	d.cfg.lid_wake_window = 600;
	d.cfg.deep_action = DEEP_SLEEP;
	add(&f, 100000, LID_CLOSED, false, false);  /* alarma a +600 */
	add(&f, 5000, LID_CLOSED, false, false);    /* sueño profundo: wake "other" → sigue */
	add(&f, 5000, LID_CLOSED, true, false);     /* POWER → despierta */
	enum wake_reason r = lidd_run_closed_cycle(&d);
	CHECK(r == WAKE_POWER);
	CHECK(f.lid_wake_false_count == 1);
	CHECK(f.lid_wake == true);                  /* restaurado al salir */
	CHECK(f.suspends == 3);
	CHECK(f.shutdowns == 0);
	printf("deep_sleep_mode: ok\n");
}

static void test_on_ac_no_deep(void)
{
	struct fake f; struct lidd d; struct platform pf;
	setup(&f, &d, &pf);
	f.ac = true;
	d.cfg.lid_wake_window = 600;
	add(&f, 100000, LID_OPEN, false, false);
	enum wake_reason r = lidd_run_closed_cycle(&d);
	CHECK(r == WAKE_LID);
	CHECK(f.shutdowns == 0);
	CHECK(f.now >= 100000);                     /* no saltó ninguna alarma */
	printf("on_ac_no_deep: ok\n");
}

static void test_debounce(void)
{
	struct fake f; struct lidd d; struct platform pf;
	setup(&f, &d, &pf);
	f.wait_lid_result = LID_OPEN;               /* se abre durante el debounce */
	enum wake_reason r = lidd_run_closed_cycle(&d);
	CHECK(r == WAKE_LID);
	CHECK(f.suspends == 0);
	CHECK(f.hooks[0] == '\0');
	printf("debounce: ok\n");
}

static void test_suspend_failures(void)
{
	struct fake f; struct lidd d; struct platform pf;
	setup(&f, &d, &pf);
	d.cfg.max_suspend_failures = 2;
	add(&f, 0, LID_CLOSED, false, true);
	add(&f, 0, LID_CLOSED, false, true);
	enum wake_reason r = lidd_run_closed_cycle(&d);
	CHECK(r == WAKE_FAILED);
	CHECK(f.suspends == 2);
	CHECK(!strcmp(f.hooks, "pre-sleep,post-wake,"));
	printf("suspend_failures: ok\n");
}

static void test_window_zero_never_deep(void)
{
	struct fake f; struct lidd d; struct platform pf;
	setup(&f, &d, &pf);
	d.cfg.lid_wake_window = 0;
	add(&f, 100000, LID_OPEN, false, false);
	enum wake_reason r = lidd_run_closed_cycle(&d);
	CHECK(r == WAKE_LID);
	CHECK(f.shutdowns == 0);
	CHECK(f.alarm_at == 0);
	printf("window_zero_never_deep: ok\n");
}

static void test_config(void)
{
	struct lidd_config c;
	char err[128];
	lidd_config_defaults(&c);
	CHECK(lidd_config_parse_line(&c, "lid_wake_window = 2h\n", err, sizeof(err)) == 0);
	CHECK(c.lid_wake_window == 7200);
	CHECK(lidd_config_parse_line(&c, "lid_wake_window=45m", err, sizeof(err)) == 0);
	CHECK(c.lid_wake_window == 2700);
	CHECK(lidd_config_parse_line(&c, "deep_action = sleep", err, sizeof(err)) == 0);
	CHECK(c.deep_action == DEEP_SLEEP);
	CHECK(lidd_config_parse_line(&c, "deep_action = hibernate", err, sizeof(err)) < 0);
	CHECK(lidd_config_parse_line(&c, "deep_on_ac = yes", err, sizeof(err)) == 0 && c.deep_on_ac);
	CHECK(lidd_config_parse_line(&c, "# comentario", err, sizeof(err)) == 0);
	CHECK(lidd_config_parse_line(&c, "   \n", err, sizeof(err)) == 0);
	CHECK(lidd_config_parse_line(&c, "hallkey_path = /sys/x", err, sizeof(err)) == 1);
	CHECK(lidd_config_parse_line(&c, "lid_wake_window = -5", err, sizeof(err)) < 0);
	printf("config: ok\n");
}

int main(void)
{
	test_open_within_window();
	test_window_expires_shutdown();
	test_alarm_not_before_deadline();
	test_power_key_with_lid_closed();
	test_deep_sleep_mode();
	test_on_ac_no_deep();
	test_debounce();
	test_suspend_failures();
	test_window_zero_never_deep();
	test_config();
	if (failures) {
		printf("%d comprobaciones fallidas\n", failures);
		return 1;
	}
	printf("todos los tests pasan\n");
	return 0;
}
