/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XX_LIDD_PLATFORM_LINUX_H
#define XX_LIDD_PLATFORM_LINUX_H

#include <stdbool.h>
#include <stdint.h>

#include "lidd.h"

struct linux_platform {
	struct lidd *lidd;
	int lid_fd;              /* evdev con SW_LID, o -1               */
	int power_fd;            /* evdev con KEY_POWER, o -1            */
	int sigpipe[2];          /* self-pipe para señales               */
	int override;            /* estado forzado por SIGUSR1/2         */
	int reload_requested;
	int64_t last_resume;
	int log_level;

	char lid_device[128];
	char power_device[128];
	char hallkey_path[128];
	int  hallkey_closed_value;
	int  hallkey_poll_ms;
	char os_sleep_path[128];
	char os_sleep_on[8];
	char os_sleep_off[8];
	char wakealarm_path[128];
	char suspend_cmd[256];
	char shutdown_cmd[128];
	char sleep_state[16];
	char hooks_dir[200];
	bool systemd_sleep_hooks;
	int  power_key_window_ms;
};

void linux_platform_defaults(struct linux_platform *lp);
int  linux_platform_parse_line(struct linux_platform *lp, const char *key, const char *val);
int  linux_platform_init(struct linux_platform *lp, struct lidd *d, struct platform *pf);
void linux_platform_signal(struct linux_platform *lp, char what);

#endif
