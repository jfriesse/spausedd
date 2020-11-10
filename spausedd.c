/*
 * Copyright (c) 2018-2020, Red Hat, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND RED HAT, INC. DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL RED HAT, INC. BE LIABLE
 * FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Author: Jan Friesse <jfriesse@redhat.com>
 */

#include <sys/types.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_VMGUESTLIB
#include <vmGuestLib.h>
#endif

#define PROGRAM_NAME			"spausedd"

#define DEFAULT_TIMEOUT			200

/*
 * Maximum allowed timeout is one hour
 */
#define MAX_TIMEOUT			(1000 * 60 * 60)

#define DEFAULT_MAX_STEAL_THRESHOLD	10
#define DEFAULT_MAX_STEAL_THRESHOLD_GL	100

#define NO_NS_IN_SEC			1000000000ULL
#define NO_NS_IN_MSEC			1000000ULL
#define NO_MSEC_IN_SEC			1000ULL

#ifndef LOG_TRACE
#define LOG_TRACE			(LOG_DEBUG + 1)
#endif

/*
 * Globals
 */
static int log_debug = 0;
static int log_to_syslog = 0;
static int log_to_stderr = 0;

static uint64_t times_not_scheduled = 0;

/*
 * If current steal percent is larger than max_steal_threshold warning is shown.
 * Default is DEFAULT_MAX_STEAL_THRESHOLD (or DEFAULT_MAX_STEAL_THRESHOLD_GL if
 * HAVE_VMGUESTLIB and VMGuestLib init success)
 */
static double max_steal_threshold = DEFAULT_MAX_STEAL_THRESHOLD;
static int max_steal_threshold_user_set = 0;

static volatile sig_atomic_t stop_main_loop = 0;

static volatile sig_atomic_t display_statistics = 0;

#ifdef HAVE_VMGUESTLIB
static int use_vmguestlib_stealtime = 0;
static VMGuestLibHandle guestlib_handle;
#endif

/*
 * Definitions (for attributes)
 */
static void	log_printf(int priority, const char *format, ...)
    __attribute__((__format__(__printf__, 2, 3)));

static void	log_vprintf(int priority, const char *format, va_list ap)
    __attribute__((__format__(__printf__, 2, 0)));

/*
 * Logging functions
 */
static const char log_month_str[][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static void
log_vprintf(int priority, const char *format, va_list ap)
{
	va_list ap_copy;
	int final_priority;
	time_t current_time;
	struct tm tm_res;

	if ((priority < LOG_DEBUG) || (priority == LOG_DEBUG && log_debug >= 1)
	    || (priority == LOG_TRACE && log_debug >= 2)) {
		if (log_to_stderr) {
			current_time = time(NULL);
			localtime_r(&current_time, &tm_res);
			fprintf(stderr, "%s %02d %02d:%02d:%02d ",
				log_month_str[tm_res.tm_mon], tm_res.tm_mday, tm_res.tm_hour,
				tm_res.tm_min, tm_res.tm_sec);

			fprintf(stderr, "%s: ", PROGRAM_NAME);
			va_copy(ap_copy, ap);
			vfprintf(stderr, format, ap_copy);
			va_end(ap_copy);
			fprintf(stderr, "\n");
		}

		if (log_to_syslog) {
			final_priority = priority;
			if (priority > LOG_INFO) {
				final_priority = LOG_INFO;
			}

			va_copy(ap_copy, ap);
			vsyslog(final_priority, format, ap);
			va_end(ap_copy);
		}
	}
}

static void
log_printf(int priority, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);

	log_vprintf(priority, format, ap);

	va_end(ap);
}

static void
log_perror(int priority, const char *s)
{
	int stored_errno;

	stored_errno = errno;

	log_printf(priority, "%s (%u): %s", s, stored_errno, strerror(stored_errno));
}

static int
util_strtonum(const char *str, long long int min_val, long long int max_val, long long int *res)
{
	long long int tmp_ll;
	char *ep;

	if (min_val > max_val) {
		return (-1);
	}

	errno = 0;

	tmp_ll = strtoll(str, &ep, 10);
	if (ep == str || *ep != '\0' || errno != 0) {
		return (-1);
	}

	if (tmp_ll < min_val || tmp_ll > max_val) {
		return (-1);
	}

	*res = tmp_ll;

	return (0);
}

/*
 * Utils
 */
static void
utils_mlockall(void)
{
	int res;
	struct rlimit rlimit;

	rlimit.rlim_cur = RLIM_INFINITY;
	rlimit.rlim_max = RLIM_INFINITY;

	setrlimit(RLIMIT_MEMLOCK, &rlimit);

	res = mlockall(MCL_CURRENT | MCL_FUTURE);
	if (res == -1) {
		log_printf(LOG_WARNING, "Could not mlockall");
	}
}

static void
utils_tty_detach(void)
{
	int devnull;

	switch (fork()) {
		case -1:
			err(1, "Can't create child process");
			break;
		case 0:
			break;
		default:
			exit(0);
			break;
	}

	/* Create new session */
	(void)setsid();

	/*
	 * Map stdin/out/err to /dev/null.
	 */
	devnull = open("/dev/null", O_RDWR);
	if (devnull == -1) {
		err(1, "Can't open /dev/null");
	}

	if (dup2(devnull, 0) < 0 || dup2(devnull, 1) < 0
	    || dup2(devnull, 2) < 0) {
		close(devnull);
		err(1, "Can't dup2 stdin/out/err to /dev/null");
	}
	close(devnull);
}

static void
utils_set_rr_scheduler(void)
{
#ifdef _POSIX_PRIORITY_SCHEDULING
	int max_prio;
	struct sched_param param;
	int res;

	max_prio = sched_get_priority_max(SCHED_RR);
	if (max_prio == -1) {
		log_perror(LOG_WARNING, "Can't get maximum SCHED_RR priority");
		return ;
	}

	param.sched_priority = max_prio;
	res = sched_setscheduler(0, SCHED_RR, &param);
	if (res == -1) {
		log_perror(LOG_WARNING, "Can't set SCHED_RR");
		return ;
	}
#else
	log_printf(LOG_WARNING, "Platform without sched_get_priority_min");
#endif
}

/*
 * Signal handlers
 */
static void
signal_int_handler(int sig)
{

	stop_main_loop = 1;
}

static void
signal_usr1_handler(int sig)
{

	display_statistics = 1;
}

static void
signal_handlers_register(void)
{
	struct sigaction act;

	act.sa_handler = signal_int_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	sigaction(SIGINT, &act, NULL);

	act.sa_handler = signal_int_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	sigaction(SIGTERM, &act, NULL);

	act.sa_handler = signal_usr1_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	sigaction(SIGUSR1, &act, NULL);
}

/*
 * Function to get current CLOCK_MONOTONIC in nanoseconds
 */
static uint64_t
nano_current_get(void)
{
	uint64_t res;
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	res = (uint64_t)(ts.tv_sec * NO_NS_IN_SEC) + (uint64_t)ts.tv_nsec;
	return (res);
}

/*
 * Get steal time provided by kernel
 */
static uint64_t
nano_stealtime_kernel_get(void)
{
	FILE *f;
	char buf[4096];
	uint64_t s_user, s_nice, s_system, s_idle, s_iowait, s_irq, s_softirq, s_steal;
	uint64_t res_steal;
	long int clock_tick;
	uint64_t factor;

	res_steal = 0;

	f = fopen("/proc/stat", "rt");
	if (f == NULL) {
		return (res_steal);
	}

	while (fgets(buf, sizeof(buf), f) != NULL) {
		s_user = s_nice = s_system = s_idle = s_iowait = s_irq = s_softirq = s_steal = 0;
		if (sscanf(buf, "cpu %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64
		    " %"PRIu64" %"PRIu64,
		    &s_user, &s_nice, &s_system, &s_idle, &s_iowait, &s_irq, &s_softirq,
		    &s_steal) > 4) {
			/*
			 * Got valid line
			 */
			clock_tick = sysconf(_SC_CLK_TCK);
			if (clock_tick == -1) {
				log_printf(LOG_TRACE, "Can't get _SC_CLK_TCK, using 100");
				clock_tick = 100;
			}

			factor = NO_NS_IN_SEC / clock_tick;
			res_steal = s_steal * factor;

			log_printf(LOG_TRACE, "nano_stealtime_get kernel stats: "
			    "user = %"PRIu64", nice = %"PRIu64"s, system = %"PRIu64
			    ", idle = %"PRIu64", iowait = %"PRIu64", irq = %"PRIu64
			    ", softirq = %"PRIu64", steal = %"PRIu64", factor = %"PRIu64
			    ", result steal = %"PRIu64,
			    s_user, s_nice, s_system, s_idle, s_iowait, s_irq, s_softirq, s_steal,
			    factor, res_steal);

			break;
		}
	}

	fclose(f);

	return (res_steal);
}

/*
 * Get steal time provided by vmguestlib
 */
#ifdef HAVE_VMGUESTLIB
static uint64_t
nano_stealtime_vmguestlib_get(void)
{
	VMGuestLibError gl_err;
	uint64_t stolen_ms;
	uint64_t res_steal;
	uint64_t used_ms, elapsed_ms;
	static uint64_t prev_stolen_ms, prev_used_ms, prev_elapsed_ms;

	gl_err = VMGuestLib_UpdateInfo(guestlib_handle);
	if (gl_err != VMGUESTLIB_ERROR_SUCCESS) {
		log_printf(LOG_WARNING, "Can't update stolen time from guestlib: %s",
		    VMGuestLib_GetErrorText(gl_err));

		return (0);
	}

	gl_err = VMGuestLib_GetCpuStolenMs(guestlib_handle, &stolen_ms);
	if (gl_err != VMGUESTLIB_ERROR_SUCCESS) {
		log_printf(LOG_WARNING, "Can't get stolen time from guestlib: %s",
		    VMGuestLib_GetErrorText(gl_err));

		return (0);
	}

	/*
	 * For debug purpose, returned errors ignored
	 */
	used_ms = elapsed_ms = 0;
	(void)VMGuestLib_GetCpuUsedMs(guestlib_handle, &used_ms);
	(void)VMGuestLib_GetElapsedMs(guestlib_handle, &elapsed_ms);

	log_printf(LOG_TRACE, "nano_stealtime_vmguestlib_get stats: "
	    "stolen = %"PRIu64" (%"PRIu64"), used = %"PRIu64" (%"PRIu64"), "
	    "elapsed = %"PRIu64" (%"PRIu64")",
	    stolen_ms, stolen_ms - prev_stolen_ms, used_ms, used_ms - prev_used_ms,
	    elapsed_ms, elapsed_ms - prev_elapsed_ms);

	prev_stolen_ms = stolen_ms;
	prev_used_ms = used_ms;
	prev_elapsed_ms = elapsed_ms;

	res_steal = NO_NS_IN_MSEC * stolen_ms;

	return (res_steal);
}
#endif

/*
 * Get steal time
 */
static uint64_t
nano_stealtime_get(void)
{
	uint64_t res;

#ifdef HAVE_VMGUESTLIB
	if (use_vmguestlib_stealtime) {
		res = nano_stealtime_vmguestlib_get();
	} else {
		res = nano_stealtime_kernel_get();
	}
#else
	res = nano_stealtime_kernel_get();
#endif

	return (res);
}


/*
 * VMGuestlib
 */
static void
guestlib_init(void)
{
#ifdef HAVE_VMGUESTLIB
	VMGuestLibError gl_err;

	gl_err = VMGuestLib_OpenHandle(&guestlib_handle);
	if (gl_err != VMGUESTLIB_ERROR_SUCCESS) {
		log_printf(LOG_DEBUG, "Can't open guestlib handle: %s", VMGuestLib_GetErrorText(gl_err));
		return ;
	}

	log_printf(LOG_INFO, "Using VMGuestLib");

	use_vmguestlib_stealtime = 1;

	if (!max_steal_threshold_user_set) {
		max_steal_threshold = DEFAULT_MAX_STEAL_THRESHOLD_GL;
	}
#endif
}

static void
guestlib_fini(void)
{
#ifdef HAVE_VMGUESTLIB
	VMGuestLibError gl_err;

	if (use_vmguestlib_stealtime) {
		gl_err = VMGuestLib_CloseHandle(guestlib_handle);

		if (gl_err != VMGUESTLIB_ERROR_SUCCESS) {
			log_printf(LOG_DEBUG, "Can't close guestlib handle: %s", VMGuestLib_GetErrorText(gl_err));
		}
	}
#endif
}

/*
 * MAIN FUNCTIONALITY
 */
static void
print_statistics(uint64_t tv_start)
{
	uint64_t tv_diff;
	uint64_t tv_now;

	tv_now = nano_current_get();
	tv_diff = tv_now - tv_start;
	log_printf(LOG_INFO, "During %0.4fs runtime %s was %"PRIu64"x not scheduled on time",
	    (double)tv_diff / NO_NS_IN_SEC, PROGRAM_NAME, times_not_scheduled);
}

static void
poll_run(uint64_t timeout)
{
	uint64_t tv_now;
	uint64_t tv_prev;	// Time before poll syscall
	uint64_t tv_diff;
	uint64_t tv_max_allowed_diff;
	uint64_t tv_start;
	uint64_t steal_now;
	uint64_t steal_prev;
	uint64_t steal_diff;
	int poll_res;
	int poll_timeout;
	double steal_perc;

	tv_max_allowed_diff = timeout * NO_NS_IN_MSEC;
	poll_timeout = timeout / 3;
	tv_start = nano_current_get();

	log_printf(LOG_INFO, "Running main poll loop with maximum timeout %"PRIu64
	    " and steal threshold %0.0f%%", timeout, max_steal_threshold);

	while (!stop_main_loop) {
		/*
		 * Fetching stealtime can block so get it before monotonic time
		 */
		steal_prev = steal_now = nano_stealtime_get();
		tv_prev = tv_now = nano_current_get();

		if (display_statistics) {
			print_statistics(tv_start);

			display_statistics = 0;
		}

		log_printf(LOG_DEBUG, "now = %0.4fs, max_diff = %0.4fs, poll_timeout = %0.4fs, "
		    "steal_time = %0.4fs",
		    (double)tv_now / NO_NS_IN_SEC, (double)tv_max_allowed_diff / NO_NS_IN_SEC,
		    (double)poll_timeout / NO_MSEC_IN_SEC, (double)steal_now / NO_NS_IN_SEC);

		if (poll_timeout < 0) {
			poll_timeout = 0;
		}

		poll_res = poll(NULL, 0, poll_timeout);
		if (poll_res == -1) {
			if (errno != EINTR) {
				log_perror(LOG_ERR, "Poll error");
				exit(2);
			}
		}

		/*
		 * Fetching stealtime can block so first get monotonic and then steal time
		 */
		tv_now = nano_current_get();
		tv_diff = tv_now - tv_prev;

		steal_now = nano_stealtime_get();
		steal_diff = steal_now - steal_prev;
		steal_perc = ((double)steal_diff / tv_diff) * (double)100;


		if (tv_diff > tv_max_allowed_diff) {
			log_printf(LOG_ERR, "Not scheduled for %0.4fs (threshold is %0.4fs), "
			    "steal time is %0.4fs (%0.2f%%)",
			    (double)tv_diff / NO_NS_IN_SEC,
			    (double)tv_max_allowed_diff / NO_NS_IN_SEC,
			    (double)steal_diff / NO_NS_IN_SEC,
			    steal_perc);

			if (steal_perc > max_steal_threshold) {
				log_printf(LOG_WARNING, "Steal time is > %0.1f%%, this is usually because "
				    "of overloaded host machine", max_steal_threshold);
			}
			times_not_scheduled++;
		}
	}

	log_printf(LOG_INFO, "Main poll loop stopped");
	print_statistics(tv_start);
}

/*
 * CLI
 */
static void
usage(void)
{
	printf("usage: %s [-dDfhp] [-m steal_th] [-t timeout]\n", PROGRAM_NAME);
	printf("\n");
	printf("  -d            Display debug messages\n");
	printf("  -D            Run on background - daemonize\n");
	printf("  -f            Run foreground - do not daemonize (default)\n");
	printf("  -h            Show help\n");
	printf("  -p            Do not set RR scheduler\n");
	printf("  -m steal_th   Steal percent threshold\n");
	printf("  -t timeout    Set timeout value (default: %u)\n", DEFAULT_TIMEOUT);
}

int
main(int argc, char **argv)
{
	int ch;
	int foreground;
	long long int tmpll;
	uint64_t timeout;
	int set_prio;

	foreground = 1;
	timeout = DEFAULT_TIMEOUT;
	set_prio = 1;
	max_steal_threshold = DEFAULT_MAX_STEAL_THRESHOLD;
	max_steal_threshold_user_set = 0;

	while ((ch = getopt(argc, argv, "dDfhpm:t:")) != -1) {
		switch (ch) {
		case 'D':
			foreground = 0;
			break;
		case 'd':
			log_debug++;
			break;
		case 'f':
			foreground = 1;
			break;
		case 'm':
			if (util_strtonum(optarg, 1, UINT32_MAX, &tmpll) != 0) {
				errx(1, "Steal percent threshold %s is invalid", optarg);
			}
			max_steal_threshold_user_set = 1;
			max_steal_threshold = tmpll;
			break;
		case 't':
			if (util_strtonum(optarg, 1, MAX_TIMEOUT, &tmpll) != 0) {
				errx(1, "Timeout %s is invalid", optarg);
			}

			timeout = (uint64_t)tmpll;
			break;
		case 'h':
		case '?':
			usage();
			exit(1);
			break;
		case 'p':
			set_prio = 0;
			break;
		default:
			errx(1, "Unhandled option %c", ch);
		}
	}

	if (foreground) {
		log_to_stderr = 1;
	} else {
		log_to_syslog = 1;
		utils_tty_detach();
		openlog(PROGRAM_NAME, LOG_PID, LOG_DAEMON);
	}

	utils_mlockall();

	if (set_prio) {
		utils_set_rr_scheduler();
	}

	signal_handlers_register();

	guestlib_init();

	poll_run(timeout);

	guestlib_fini();

	if (!foreground) {
		closelog();
	}

	return (0);
}
