/* SPDX-License-Identifier: GPL-2.0-or-later
 * xx-lidd — política de tapa para clamshells Allwinner H700 (RG34XXSP y familia).
 *
 * La lógica (policy.c) no toca el sistema: habla con una `struct platform` que en la
 * consola implementa platform_linux.c y en los tests una plataforma falsa.
 */
#ifndef XX_LIDD_H
#define XX_LIDD_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

enum lid_state { LID_UNKNOWN = -1, LID_OPEN = 0, LID_CLOSED = 1 };

enum wake_reason {
	WAKE_LID,     /* la tapa está abierta al volver                      */
	WAKE_ALARM,   /* la alarma RTC armada por nosotros ya venció         */
	WAKE_POWER,   /* se detectó KEY_POWER justo después de volver        */
	WAKE_OTHER,   /* USB, batería, wake espurio…                         */
	WAKE_FAILED,  /* el suspend no llegó a entrar                        */
};

enum deep_action {
	DEEP_NONE,      /* seguir dormida con wake por tapa hasta que se abra */
	DEEP_SHUTDOWN,  /* guardar estado (hooks) y apagar                    */
	DEEP_SLEEP,     /* guardar estado y dormir sin wake por tapa          */
};

enum log_level { LOG_ERR = 0, LOG_WARN = 1, LOG_INFO = 2, LOG_DBG = 3 };

struct lidd_config {
	int64_t lid_wake_window;   /* s con wake por tapa antes de la transición; 0 = siempre */
	enum deep_action deep_action;
	bool deep_on_ac;           /* hacer la transición aunque haya cargador                 */
	int64_t debounce_ms;       /* la tapa debe seguir cerrada este tiempo antes de dormir   */
	int64_t min_sleep_gap_ms;  /* espera mínima entre un resume espurio y volver a dormir   */
	int64_t other_wake_grace;  /* s despierta tras un wake "other" antes de volver a dormir  */
	int max_suspend_failures;  /* tras N fallos seguidos se abandona el ciclo               */
	int log_level;
};

struct platform {
	void *ctx;
	/* Reloj que avanza también durante el suspend (CLOCK_BOOTTIME), en segundos. */
	int64_t (*now)(void *ctx);
	/* Estado actual de la tapa. */
	int (*lid_state)(void *ctx);
	/* Bloquea hasta que la tapa cambie, llegue una señal o pase timeout_ms (<0 = sin
	 * límite). Devuelve el nuevo estado, LID_UNKNOWN si expiró o fue interrumpido. */
	int (*wait_lid_change)(void *ctx, int timeout_ms);
	/* Permitir o no que la tapa despierte el siguiente suspend (BSP: os_sleep). */
	int (*set_lid_wake)(void *ctx, bool enable);
	/* Armar alarma RTC a +seconds; 0 limpia. */
	int (*arm_alarm)(void *ctx, int64_t seconds);
	/* Entra en suspend; vuelve al reanudar. 0 ok, <0 si no se pudo suspender. */
	int (*suspend)(void *ctx);
	/* true si hubo una pulsación de POWER en la ventana posterior al resume. */
	bool (*power_key_pressed)(void *ctx);
	bool (*on_ac)(void *ctx);
	void (*run_hooks)(void *ctx, const char *phase, const char *reason);
	/* No vuelve. */
	void (*shutdown)(void *ctx);
	void (*sleep_ms)(void *ctx, int64_t ms);
	void (*log)(void *ctx, int level, const char *fmt, va_list ap);
};

struct lidd {
	struct lidd_config cfg;
	const struct platform *pf;
	volatile int quit;
};

void lidd_config_defaults(struct lidd_config *c);
int  lidd_config_parse_line(struct lidd_config *c, const char *line, char *err, int errlen);

/* Ejecuta un ciclo completo de "tapa cerrada" y vuelve cuando la consola está despierta.
 * Devuelve la razón final del despertar. */
enum wake_reason lidd_run_closed_cycle(struct lidd *d);

/* Bucle principal: espera cierres de tapa y lanza ciclos hasta d->quit. */
int lidd_main_loop(struct lidd *d);

const char *lidd_reason_str(enum wake_reason r);
void lidd_log(struct lidd *d, int level, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

#endif
