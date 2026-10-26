/* SPDX-License-Identifier: GPL-2.0-or-later
 * Plataforma Linux real para xx-lidd.
 *
 * Backends de tapa:
 *   evdev   — dispositivo con EV_SW/SW_LID (kernel mainline, gpio-keys). Por defecto.
 *   hallkey — fichero sysfs 0/1 (kernel BSP de Anbernic), por sondeo.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "lidd.h"
#include "platform_linux.h"

#define NBITS(x) (((x) + 7) / 8)
#define TEST_BIT(bit, arr) ((arr)[(bit) / 8] & (1 << ((bit) % 8)))

static void vlog(void *ctx, int level, const char *fmt, va_list ap)
{
	(void)ctx;
	static const char *tag[] = { "ERR", "WARN", "INFO", "DBG" };
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm tm;
	localtime_r(&ts.tv_sec, &tm);
	fprintf(stderr, "%02d:%02d:%02d xx-lidd[%s] ", tm.tm_hour, tm.tm_min, tm.tm_sec,
		tag[level < 0 ? 0 : level > 3 ? 3 : level]);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
}

static void logf_(struct linux_platform *lp, int level, const char *fmt, ...)
{
	if (level > lp->log_level)
		return;
	va_list ap;
	va_start(ap, fmt);
	vlog(lp, level, fmt, ap);
	va_end(ap);
}

static int64_t now_boottime(void *ctx)
{
	(void)ctx;
	struct timespec ts;
	clock_gettime(CLOCK_BOOTTIME, &ts);
	return ts.tv_sec;
}

static int read_file_str(const char *path, char *buf, size_t len)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	ssize_t n = read(fd, buf, len - 1);
	close(fd);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
		buf[--n] = '\0';
	return 0;
}

static int write_file_str(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	ssize_t n = write(fd, val, strlen(val));
	int err = errno;
	close(fd);
	if (n < 0) {
		errno = err;
		return -1;
	}
	return 0;
}

/* ---------- descubrimiento de dispositivos evdev ---------- */

static int evdev_has(int fd, int type, int code)
{
	unsigned char bits[NBITS(KEY_MAX + 1)];
	memset(bits, 0, sizeof(bits));
	if (ioctl(fd, EVIOCGBIT(type, sizeof(bits)), bits) < 0)
		return 0;
	return TEST_BIT(code, bits) != 0;
}

static int open_evdev_matching(int type, int code, char *path_out, size_t len)
{
	DIR *dir = opendir("/dev/input");
	if (!dir)
		return -1;
	struct dirent *de;
	int found = -1;
	while ((de = readdir(dir))) {
		if (strncmp(de->d_name, "event", 5))
			continue;
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (evdev_has(fd, type, code)) {
			found = fd;
			if (path_out) {
				strncpy(path_out, path, len - 1);
				path_out[len - 1] = '\0';
			}
			break;
		}
		close(fd);
	}
	closedir(dir);
	return found;
}

static int evdev_lid_state(int fd)
{
	unsigned char sw[NBITS(SW_MAX + 1)];
	memset(sw, 0, sizeof(sw));
	if (ioctl(fd, EVIOCGSW(sizeof(sw)), sw) < 0)
		return LID_UNKNOWN;
	return TEST_BIT(SW_LID, sw) ? LID_CLOSED : LID_OPEN;
}

/* Vacía la cola de eventos de un fd no bloqueante; devuelve true si vio key/code pulsado. */
static bool evdev_drain(int fd, int want_type, int want_code)
{
	struct input_event ev[32];
	bool seen = false;
	for (;;) {
		ssize_t n = read(fd, ev, sizeof(ev));
		if (n <= 0)
			break;
		for (size_t i = 0; i < (size_t)n / sizeof(ev[0]); i++)
			if (ev[i].type == want_type && ev[i].code == want_code && ev[i].value)
				seen = true;
	}
	return seen;
}

/* ---------- backend hallkey (BSP) ---------- */

static int hallkey_state(struct linux_platform *lp)
{
	char buf[16];
	if (read_file_str(lp->hallkey_path, buf, sizeof(buf)) < 0)
		return LID_UNKNOWN;
	return atoi(buf) == lp->hallkey_closed_value ? LID_CLOSED : LID_OPEN;
}

/* ---------- interfaz platform ---------- */

static int pf_lid_state(void *ctx)
{
	struct linux_platform *lp = ctx;
	if (lp->override != LID_UNKNOWN)
		return lp->override;
	if (lp->lid_fd >= 0)
		return evdev_lid_state(lp->lid_fd);
	if (lp->hallkey_path[0])
		return hallkey_state(lp);
	return LID_UNKNOWN;
}

/* Procesa bytes del self-pipe escritos por el manejador de señales. Devuelve el estado
 * forzado si hubo uno, LID_UNKNOWN si no. */
static int consume_signal_pipe(struct linux_platform *lp)
{
	char c;
	int forced = LID_UNKNOWN;
	while (read(lp->sigpipe[0], &c, 1) == 1) {
		switch (c) {
		case 'c': lp->override = LID_CLOSED; forced = LID_CLOSED; break;
		case 'o': lp->override = LID_OPEN;   forced = LID_OPEN;   break;
		case 'h': lp->reload_requested = 1; break;
		case 'q': lp->lidd->quit = 1; break;
		}
	}
	return forced;
}

static int pf_wait_lid_change(void *ctx, int timeout_ms)
{
	struct linux_platform *lp = ctx;
	int before = pf_lid_state(ctx);
	int64_t t0 = now_boottime(ctx) * 1000;

	for (;;) {
		struct pollfd fds[2];
		int n = 0;
		fds[n].fd = lp->sigpipe[0]; fds[n].events = POLLIN; n++;
		if (lp->lid_fd >= 0) {
			fds[n].fd = lp->lid_fd; fds[n].events = POLLIN; n++;
		}
		int wait = timeout_ms;
		if (lp->lid_fd < 0) {
			/* sondeo del fichero hallkey */
			if (wait < 0 || wait > lp->hallkey_poll_ms)
				wait = lp->hallkey_poll_ms;
		}
		int r = poll(fds, n, wait);
		if (r < 0 && errno != EINTR)
			return LID_UNKNOWN;

		if (r > 0 && (fds[0].revents & POLLIN)) {
			int forced = consume_signal_pipe(lp);
			if (lp->lidd->quit || lp->reload_requested)
				return LID_UNKNOWN;
			if (forced != LID_UNKNOWN && forced != before)
				return forced;
		}
		if (lp->lid_fd >= 0 && n > 1 && (fds[1].revents & POLLIN)) {
			struct input_event ev[16];
			ssize_t rd = read(lp->lid_fd, ev, sizeof(ev));
			for (size_t i = 0; rd > 0 && i < (size_t)rd / sizeof(ev[0]); i++) {
				if (ev[i].type == EV_SW && ev[i].code == SW_LID) {
					lp->override = LID_UNKNOWN; /* la tapa real manda */
					int st = ev[i].value ? LID_CLOSED : LID_OPEN;
					if (st != before)
						return st;
				}
			}
		}
		if (lp->lid_fd < 0) {
			int st = pf_lid_state(ctx);
			if (st != before && st != LID_UNKNOWN)
				return st;
		}
		if (timeout_ms >= 0 && now_boottime(ctx) * 1000 - t0 >= timeout_ms)
			return LID_UNKNOWN;
	}
}

static int pf_set_lid_wake(void *ctx, bool enable)
{
	struct linux_platform *lp = ctx;
	if (!lp->os_sleep_path[0])
		return 0; /* mainline: gpio-keys ya es wakeup-source */
	const char *v = enable ? lp->os_sleep_on : lp->os_sleep_off;
	if (write_file_str(lp->os_sleep_path, v) < 0) {
		logf_(lp, LOG_WARN, "no pude escribir %s en %s: %s", v, lp->os_sleep_path,
		      strerror(errno));
		return -1;
	}
	logf_(lp, LOG_DBG, "os_sleep=%s", v);
	return 0;
}

static int pf_arm_alarm(void *ctx, int64_t seconds)
{
	struct linux_platform *lp = ctx;
	if (!lp->wakealarm_path[0])
		return seconds ? -1 : 0;
	/* Siempre limpiar primero: el kernel rechaza re-armar una alarma pendiente. */
	write_file_str(lp->wakealarm_path, "0");
	if (seconds <= 0)
		return 0;
	char since[64], val[64];
	char epoch_path[256];
	snprintf(epoch_path, sizeof(epoch_path), "%s", lp->wakealarm_path);
	char *slash = strrchr(epoch_path, '/');
	if (slash)
		strcpy(slash + 1, "since_epoch");
	if (read_file_str(epoch_path, since, sizeof(since)) == 0)
		snprintf(val, sizeof(val), "%lld", (long long)atoll(since) + seconds);
	else
		snprintf(val, sizeof(val), "+%lld", (long long)seconds);
	if (write_file_str(lp->wakealarm_path, val) < 0) {
		logf_(lp, LOG_WARN, "no pude armar wakealarm (%s): %s", val, strerror(errno));
		return -1;
	}
	logf_(lp, LOG_DBG, "wakealarm +%llds", (long long)seconds);
	return 0;
}

static int run_cmd(const char *const argv[], const char *const envp_extra[])
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		for (int i = 0; envp_extra && envp_extra[i]; i++)
			putenv((char *)envp_extra[i]);
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Ejecuta los scripts de un directorio en orden alfabético con los argumentos dados. */
static void run_dir(struct linux_platform *lp, const char *dir, const char *arg1,
		    const char *arg2, const char *const envp[])
{
	struct dirent **list;
	int n = scandir(dir, &list, NULL, alphasort);
	if (n < 0)
		return;
	for (int i = 0; i < n; i++) {
		if (list[i]->d_name[0] == '.')
			goto next;
		char path[PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s", dir, list[i]->d_name);
		if (access(path, X_OK) != 0)
			goto next;
		const char *argv[] = { path, arg1, arg2, NULL };
		int rc = run_cmd(argv, envp);
		logf_(lp, rc ? LOG_WARN : LOG_DBG, "hook %s %s %s -> %d", path,
		      arg1 ? arg1 : "", arg2 ? arg2 : "", rc);
next:
		free(list[i]);
	}
	free(list);
}

static void systemd_sleep_scripts(struct linux_platform *lp, const char *when)
{
	if (!lp->systemd_sleep_hooks)
		return;
	run_dir(lp, "/usr/lib/systemd/system-sleep", when, "suspend", NULL);
	run_dir(lp, "/etc/systemd/system-sleep", when, "suspend", NULL);
}

static int pf_suspend(void *ctx)
{
	struct linux_platform *lp = ctx;

	/* Descartar pulsaciones viejas para no confundirlas con el wake. */
	if (lp->power_fd >= 0)
		evdev_drain(lp->power_fd, EV_KEY, KEY_POWER);

	int rc;
	if (lp->suspend_cmd[0]) {
		const char *argv[] = { "/bin/sh", "-c", lp->suspend_cmd, NULL };
		rc = run_cmd(argv, NULL);
	} else {
		systemd_sleep_scripts(lp, "pre");
		sync();
		rc = write_file_str("/sys/power/state", lp->sleep_state);
		int err = errno;
		systemd_sleep_scripts(lp, "post");
		if (rc < 0)
			logf_(lp, LOG_ERR, "echo %s > /sys/power/state: %s", lp->sleep_state,
			      strerror(err));
	}
	lp->last_resume = now_boottime(ctx);
	return rc < 0 ? -1 : 0;
}

static bool pf_power_key_pressed(void *ctx)
{
	struct linux_platform *lp = ctx;
	if (lp->power_fd < 0)
		return false;
	/* El evento del botón que despertó llega poco después del resume. */
	struct pollfd pfd = { .fd = lp->power_fd, .events = POLLIN };
	int64_t deadline_ms = lp->power_key_window_ms;
	bool seen = false;
	while (deadline_ms > 0) {
		int r = poll(&pfd, 1, 50);
		if (r > 0)
			seen |= evdev_drain(lp->power_fd, EV_KEY, KEY_POWER);
		if (seen)
			break;
		deadline_ms -= 50;
	}
	return seen;
}

static bool pf_on_ac(void *ctx)
{
	(void)ctx;
	DIR *dir = opendir("/sys/class/power_supply");
	if (!dir)
		return false;
	struct dirent *de;
	bool ac = false;
	while ((de = readdir(dir)) && !ac) {
		if (de->d_name[0] == '.')
			continue;
		char path[PATH_MAX], buf[32];
		snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", de->d_name);
		if (read_file_str(path, buf, sizeof(buf)) < 0 || !strcmp(buf, "Battery"))
			continue;
		snprintf(path, sizeof(path), "/sys/class/power_supply/%s/online", de->d_name);
		if (read_file_str(path, buf, sizeof(buf)) == 0 && atoi(buf) == 1)
			ac = true;
	}
	closedir(dir);
	return ac;
}

static void pf_run_hooks(void *ctx, const char *phase, const char *reason)
{
	struct linux_platform *lp = ctx;
	char dir[300], env_reason[64], env_phase[64];
	snprintf(dir, sizeof(dir), "%s/%s.d", lp->hooks_dir, phase);
	snprintf(env_reason, sizeof(env_reason), "XX_LID_REASON=%s", reason);
	snprintf(env_phase, sizeof(env_phase), "XX_LID_PHASE=%s", phase);
	const char *envp[] = { env_reason, env_phase, NULL };
	run_dir(lp, dir, phase, reason, envp);
}

static void pf_shutdown(void *ctx)
{
	struct linux_platform *lp = ctx;
	logf_(lp, LOG_INFO, "ejecutando: %s", lp->shutdown_cmd);
	const char *argv[] = { "/bin/sh", "-c", lp->shutdown_cmd, NULL };
	run_cmd(argv, NULL);
	/* Dar tiempo a systemd a parar el sistema; si seguimos aquí, el apagado falló. */
	sleep(60);
}

static void pf_sleep_ms(void *ctx, int64_t ms)
{
	(void)ctx;
	struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

/* ---------- configuración y arranque ---------- */

void linux_platform_defaults(struct linux_platform *lp)
{
	memset(lp, 0, sizeof(*lp));
	lp->lid_fd = -1;
	lp->power_fd = -1;
	lp->override = LID_UNKNOWN;
	lp->sigpipe[0] = lp->sigpipe[1] = -1;
	lp->hallkey_closed_value = 1;
	lp->hallkey_poll_ms = 500;
	lp->power_key_window_ms = 400;
	lp->systemd_sleep_hooks = true;
	lp->log_level = LOG_INFO;
	snprintf(lp->sleep_state, sizeof(lp->sleep_state), "mem");
	snprintf(lp->hooks_dir, sizeof(lp->hooks_dir), "/etc/xx-lidd/hooks");
	snprintf(lp->shutdown_cmd, sizeof(lp->shutdown_cmd), "systemctl poweroff || poweroff");
	snprintf(lp->os_sleep_on, sizeof(lp->os_sleep_on), "0");
	snprintf(lp->os_sleep_off, sizeof(lp->os_sleep_off), "16");
}

int linux_platform_parse_line(struct linux_platform *lp, const char *key, const char *val)
{
#define STR(k, f) if (!strcmp(key, k)) { snprintf(lp->f, sizeof(lp->f), "%s", val); return 0; }
	STR("lid_device", lid_device)
	STR("power_device", power_device)
	STR("hallkey_path", hallkey_path)
	STR("os_sleep_path", os_sleep_path)
	STR("os_sleep_on", os_sleep_on)
	STR("os_sleep_off", os_sleep_off)
	STR("wakealarm_path", wakealarm_path)
	STR("suspend_cmd", suspend_cmd)
	STR("shutdown_cmd", shutdown_cmd)
	STR("sleep_state", sleep_state)
	STR("hooks_dir", hooks_dir)
#undef STR
	if (!strcmp(key, "hallkey_closed_value")) { lp->hallkey_closed_value = atoi(val); return 0; }
	if (!strcmp(key, "hallkey_poll_ms")) { lp->hallkey_poll_ms = atoi(val); return 0; }
	if (!strcmp(key, "power_key_window_ms")) { lp->power_key_window_ms = atoi(val); return 0; }
	if (!strcmp(key, "systemd_sleep_hooks")) {
		lp->systemd_sleep_hooks = !strcmp(val, "1") || !strcmp(val, "true") ||
					  !strcmp(val, "yes") || !strcmp(val, "on");
		return 0;
	}
	return 1;
}

static void autodetect(struct linux_platform *lp)
{
	char path[64];

	if (lp->lid_device[0]) {
		lp->lid_fd = open(lp->lid_device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	} else if (!lp->hallkey_path[0]) {
		lp->lid_fd = open_evdev_matching(EV_SW, SW_LID, path, sizeof(path));
		if (lp->lid_fd >= 0)
			logf_(lp, LOG_INFO, "tapa: evdev %s", path);
	}
	if (lp->lid_fd < 0 && !lp->hallkey_path[0]) {
		const char *bsp = "/sys/class/power_supply/axp2202-battery/hallkey";
		if (access(bsp, R_OK) == 0) {
			snprintf(lp->hallkey_path, sizeof(lp->hallkey_path), "%s", bsp);
			logf_(lp, LOG_INFO, "tapa: sondeo de %s (kernel BSP)", bsp);
		}
	}
	if (lp->lid_fd < 0 && !lp->hallkey_path[0])
		logf_(lp, LOG_ERR, "no encuentro la tapa: ni evdev SW_LID ni hallkey");

	if (lp->power_device[0])
		lp->power_fd = open(lp->power_device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	else
		lp->power_fd = open_evdev_matching(EV_KEY, KEY_POWER, path, sizeof(path));
	if (lp->power_fd >= 0)
		logf_(lp, LOG_DBG, "botón POWER: evdev %s", lp->power_device[0] ? lp->power_device : path);

	if (!lp->os_sleep_path[0]) {
		const char *bsp = "/sys/class/power_supply/axp2202-battery/os_sleep";
		if (access(bsp, W_OK) == 0) {
			snprintf(lp->os_sleep_path, sizeof(lp->os_sleep_path), "%s", bsp);
			logf_(lp, LOG_INFO, "os_sleep: %s (super standby del BSP controlable)", bsp);
		}
	}

	if (!lp->wakealarm_path[0]) {
		for (int i = 0; i < 4; i++) {
			snprintf(path, sizeof(path), "/sys/class/rtc/rtc%d/wakealarm", i);
			if (access(path, W_OK) == 0) {
				snprintf(lp->wakealarm_path, sizeof(lp->wakealarm_path), "%s", path);
				break;
			}
		}
	}
	if (lp->wakealarm_path[0])
		logf_(lp, LOG_INFO, "alarma RTC: %s", lp->wakealarm_path);
	else
		logf_(lp, LOG_WARN, "sin wakealarm: no habrá transición a sueño profundo");
}

int linux_platform_init(struct linux_platform *lp, struct lidd *d, struct platform *pf)
{
	lp->lidd = d;
	if (pipe2(lp->sigpipe, O_NONBLOCK | O_CLOEXEC) < 0)
		return -1;
	autodetect(lp);

	pf->ctx = lp;
	pf->now = now_boottime;
	pf->lid_state = pf_lid_state;
	pf->wait_lid_change = pf_wait_lid_change;
	pf->set_lid_wake = pf_set_lid_wake;
	pf->arm_alarm = pf_arm_alarm;
	pf->suspend = pf_suspend;
	pf->power_key_pressed = pf_power_key_pressed;
	pf->on_ac = pf_on_ac;
	pf->run_hooks = pf_run_hooks;
	pf->shutdown = pf_shutdown;
	pf->sleep_ms = pf_sleep_ms;
	pf->log = vlog;
	return (lp->lid_fd >= 0 || lp->hallkey_path[0]) ? 0 : -1;
}

void linux_platform_signal(struct linux_platform *lp, char what)
{
	if (lp->sigpipe[1] >= 0) {
		ssize_t r = write(lp->sigpipe[1], &what, 1);
		(void)r;
	}
}
