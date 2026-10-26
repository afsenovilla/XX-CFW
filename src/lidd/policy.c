/* SPDX-License-Identifier: GPL-2.0-or-later
 * Máquina de estados de xx-lidd. Sin llamadas al sistema: todo pasa por `struct platform`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lidd.h"

void lidd_log(struct lidd *d, int level, const char *fmt, ...)
{
	if (level > d->cfg.log_level)
		return;
	va_list ap;
	va_start(ap, fmt);
	d->pf->log(d->pf->ctx, level, fmt, ap);
	va_end(ap);
}

const char *lidd_reason_str(enum wake_reason r)
{
	switch (r) {
	case WAKE_LID:    return "lid";
	case WAKE_ALARM:  return "alarm";
	case WAKE_POWER:  return "power";
	case WAKE_OTHER:  return "other";
	case WAKE_FAILED: return "failed";
	}
	return "unknown";
}

void lidd_config_defaults(struct lidd_config *c)
{
	memset(c, 0, sizeof(*c));
	c->lid_wake_window = 90 * 60;
	c->deep_action = DEEP_SHUTDOWN;
	c->deep_on_ac = false;
	c->debounce_ms = 700;
	c->min_sleep_gap_ms = 1500;
	c->other_wake_grace = 20;
	c->max_suspend_failures = 3;
	c->log_level = LOG_INFO;
}

static int parse_bool(const char *v, bool *out)
{
	if (!strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on")) {
		*out = true;
		return 0;
	}
	if (!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "no") || !strcmp(v, "off")) {
		*out = false;
		return 0;
	}
	return -1;
}

/* Admite sufijos s/m/h para duraciones en segundos. */
static int parse_duration(const char *v, int64_t *out)
{
	char *end;
	long long n = strtoll(v, &end, 10);
	if (end == v || n < 0)
		return -1;
	switch (*end) {
	case '\0': case 's': break;
	case 'm': n *= 60; break;
	case 'h': n *= 3600; break;
	default: return -1;
	}
	*out = n;
	return 0;
}

int lidd_config_parse_line(struct lidd_config *c, const char *line, char *err, int errlen)
{
	char key[64], val[192];
	const char *p = line;

	while (*p == ' ' || *p == '\t')
		p++;
	if (*p == '\0' || *p == '\n' || *p == '#')
		return 0;
	if (sscanf(p, "%63[^= \t\n]%*[ \t]=%*[ \t]%191[^\n]", key, val) != 2 &&
	    sscanf(p, "%63[^= \t\n]=%191[^\n]", key, val) != 2) {
		snprintf(err, errlen, "línea mal formada");
		return -1;
	}
	/* recorta espacios finales del valor */
	for (size_t n = strlen(val); n > 0 && (val[n - 1] == ' ' || val[n - 1] == '\t'); n--)
		val[n - 1] = '\0';

	if (!strcmp(key, "lid_wake_window")) {
		if (parse_duration(val, &c->lid_wake_window)) goto bad;
	} else if (!strcmp(key, "deep_action")) {
		if (!strcmp(val, "none")) c->deep_action = DEEP_NONE;
		else if (!strcmp(val, "shutdown")) c->deep_action = DEEP_SHUTDOWN;
		else if (!strcmp(val, "sleep")) c->deep_action = DEEP_SLEEP;
		else goto bad;
	} else if (!strcmp(key, "deep_on_ac")) {
		if (parse_bool(val, &c->deep_on_ac)) goto bad;
	} else if (!strcmp(key, "debounce_ms")) {
		c->debounce_ms = atoll(val);
	} else if (!strcmp(key, "min_sleep_gap_ms")) {
		c->min_sleep_gap_ms = atoll(val);
	} else if (!strcmp(key, "other_wake_grace")) {
		if (parse_duration(val, &c->other_wake_grace)) goto bad;
	} else if (!strcmp(key, "max_suspend_failures")) {
		c->max_suspend_failures = atoi(val);
	} else if (!strcmp(key, "log_level")) {
		c->log_level = atoi(val);
	} else {
		/* Las claves de plataforma (lid_device, hallkey_path…) las consume platform_linux.c */
		return 1;
	}
	return 0;
bad:
	snprintf(err, errlen, "valor inválido para %s: %s", key, val);
	return -1;
}

/* Clasifica el despertar según el estado observable tras el resume. */
static enum wake_reason classify_wake(struct lidd *d, int64_t alarm_due)
{
	const struct platform *pf = d->pf;
	if (pf->lid_state(pf->ctx) == LID_OPEN)
		return WAKE_LID;
	if (pf->power_key_pressed(pf->ctx))
		return WAKE_POWER;
	if (alarm_due > 0 && pf->now(pf->ctx) >= alarm_due)
		return WAKE_ALARM;
	return WAKE_OTHER;
}

/* Tapa cerrada y ventana agotada: guardar estado y bajar a la acción profunda. */
static enum wake_reason deep_transition(struct lidd *d)
{
	const struct platform *pf = d->pf;

	lidd_log(d, LOG_INFO, "ventana de wake por tapa agotada: transición a sueño profundo");
	pf->run_hooks(pf->ctx, "deep-transition", "alarm");

	switch (d->cfg.deep_action) {
	case DEEP_SHUTDOWN:
		lidd_log(d, LOG_INFO, "apagando (deep_action=shutdown)");
		pf->shutdown(pf->ctx);
		/* Si shutdown vuelve, algo falló: seguimos dormidos con wake por tapa. */
		lidd_log(d, LOG_ERR, "shutdown no se completó; vuelvo a dormir con wake por tapa");
		return WAKE_FAILED;
	case DEEP_SLEEP:
		pf->set_lid_wake(pf->ctx, false);
		pf->arm_alarm(pf->ctx, 0);
		for (;;) {
			if (pf->suspend(pf->ctx) < 0) {
				lidd_log(d, LOG_ERR, "suspend profundo falló");
				pf->set_lid_wake(pf->ctx, true);
				return WAKE_FAILED;
			}
			enum wake_reason r = classify_wake(d, 0);
			/* Desde el sueño profundo solo nos saca POWER (o la tapa si el hardware lo
			 * permitiera). Un wake por USB/batería vuelve a dormir. */
			if (r == WAKE_OTHER) {
				pf->sleep_ms(pf->ctx, d->cfg.min_sleep_gap_ms);
				continue;
			}
			pf->set_lid_wake(pf->ctx, true);
			return r;
		}
	case DEEP_NONE:
		break;
	}
	return WAKE_FAILED; /* seguir en el bucle superficial */
}

enum wake_reason lidd_run_closed_cycle(struct lidd *d)
{
	const struct platform *pf = d->pf;
	const struct lidd_config *c = &d->cfg;
	int failures = 0;
	enum wake_reason reason = WAKE_OTHER;

	/* Debounce: si la tapa se abre antes de debounce_ms no hacemos nada. */
	if (c->debounce_ms > 0) {
		int st = pf->wait_lid_change(pf->ctx, (int)c->debounce_ms);
		if (st == LID_OPEN || pf->lid_state(pf->ctx) != LID_CLOSED) {
			lidd_log(d, LOG_DBG, "cierre descartado por debounce");
			return WAKE_LID;
		}
	}

	int64_t start = pf->now(pf->ctx);
	int64_t deadline = c->lid_wake_window > 0 ? start + c->lid_wake_window : 0;
	bool deep_done = false;

	lidd_log(d, LOG_INFO, "tapa cerrada: durmiendo con wake por tapa (ventana %llds)",
		 (long long)c->lid_wake_window);
	pf->run_hooks(pf->ctx, "pre-sleep", "lid");
	pf->set_lid_wake(pf->ctx, true);

	for (;;) {
		if (d->quit)
			break;

		int64_t now = pf->now(pf->ctx);
		int64_t alarm_due = 0;
		bool want_deep = deadline > 0 && !deep_done && c->deep_action != DEEP_NONE &&
				 (c->deep_on_ac || !pf->on_ac(pf->ctx));

		if (want_deep && now >= deadline) {
			reason = deep_transition(d);
			deep_done = true;
			if (reason == WAKE_LID || reason == WAKE_POWER)
				break;
			/* WAKE_FAILED / WAKE_OTHER: continuamos dormidos con wake por tapa */
			continue;
		}

		if (want_deep) {
			alarm_due = deadline;
			pf->arm_alarm(pf->ctx, deadline - now);
		} else {
			pf->arm_alarm(pf->ctx, 0);
		}

		if (pf->suspend(pf->ctx) < 0) {
			failures++;
			lidd_log(d, LOG_ERR, "suspend falló (%d/%d)", failures, c->max_suspend_failures);
			if (failures >= c->max_suspend_failures) {
				reason = WAKE_FAILED;
				break;
			}
			pf->sleep_ms(pf->ctx, c->min_sleep_gap_ms);
			continue;
		}
		failures = 0;

		reason = classify_wake(d, alarm_due);
		lidd_log(d, LOG_DBG, "resume: motivo=%s", lidd_reason_str(reason));

		if (reason == WAKE_LID || reason == WAKE_POWER)
			break;

		if (reason == WAKE_OTHER) {
			/* USB, batería o wake espurio con la tapa cerrada: dar un margen por si
			 * el usuario abre, y volver a dormir. */
			int st = pf->wait_lid_change(pf->ctx, (int)(c->other_wake_grace * 1000));
			if (st == LID_OPEN || pf->lid_state(pf->ctx) == LID_OPEN) {
				reason = WAKE_LID;
				break;
			}
			pf->sleep_ms(pf->ctx, c->min_sleep_gap_ms);
		}
		/* WAKE_ALARM: la siguiente vuelta ejecuta la transición. */
	}

	pf->arm_alarm(pf->ctx, 0);
	pf->set_lid_wake(pf->ctx, true);
	lidd_log(d, LOG_INFO, "despierta (motivo=%s, %llds dormida)",
		 lidd_reason_str(reason), (long long)(pf->now(pf->ctx) - start));
	pf->run_hooks(pf->ctx, "post-wake", lidd_reason_str(reason));
	return reason;
}

int lidd_main_loop(struct lidd *d)
{
	const struct platform *pf = d->pf;

	while (!d->quit) {
		int st = pf->wait_lid_change(pf->ctx, -1);
		if (d->quit)
			break;
		if (st == LID_CLOSED)
			lidd_run_closed_cycle(d);
	}
	return 0;
}
