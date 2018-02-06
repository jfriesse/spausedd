/*
 * Copyright (c) 2018, Red Hat, Inc.
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

#define PROGRAM_NAME		"spausedd"

#define DEFAULT_TIMEOUT		2000

#define NO_NS_IN_SEC		1000000000ULL
#define NO_NS_IN_MSEC		1000000ULL

/*
 * Globals
 */
static int log_debug = 0;
static int log_to_syslog = 0;
static int log_to_stderr = 0;

static uint64_t times_not_scheduled = 0;

static volatile sig_atomic_t stop_main_loop = 0;

static volatile sig_atomic_t display_statistics = 0;

/*
 * Logging functions
 */
static void
log_vprintf(int priority, const char *format, va_list ap)
{
	va_list ap_copy;
	int final_priority;

	if (priority != LOG_DEBUG || log_debug) {
		if (log_to_stderr) {
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

	log_printf(priority, "%s (%u): %s", stored_errno, strerror(stored_errno));
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
 * MAIN FUNCTIONALITY
 */
static void
print_statistics(uint64_t tv_start)
{
	uint64_t tv_diff;
	uint64_t tv_now;

	tv_now = nano_current_get();
	tv_diff = tv_now - tv_start;
	log_printf(LOG_INFO, "During %0.4f ms runtime %s was %"PRIu64"x not scheduled on time",
	    (double)tv_diff / NO_NS_IN_MSEC, PROGRAM_NAME, times_not_scheduled);
}

static void
poll_run(uint64_t timeout)
{
	uint64_t tv_now;
	uint64_t tv_prev;	// Time before poll syscall
	uint64_t tv_diff;
	uint64_t tv_max_allowed_diff;
	uint64_t tv_start;
	int poll_res;
	int poll_timeout;

	tv_max_allowed_diff = timeout * NO_NS_IN_MSEC;
	poll_timeout = timeout / 3;
	tv_start = nano_current_get();

	log_printf(LOG_INFO, "Running main poll loop with maximum timeout %"PRIu64, timeout);

	while (!stop_main_loop) {
		tv_prev = tv_now = nano_current_get();

		if (display_statistics) {
			print_statistics(tv_start);

			display_statistics = 0;
		}

		log_printf(LOG_DEBUG, "now = %0.4f ms, max_diff = %0.4f ms, poll_timeout = %u ms",
		    (double)tv_now / NO_NS_IN_MSEC, (double)tv_max_allowed_diff / NO_NS_IN_MSEC,
		    poll_timeout);

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

		tv_now = nano_current_get();
		tv_diff = tv_now - tv_prev;

		if (tv_diff > tv_max_allowed_diff) {
			log_printf(LOG_ERR, "Not scheduled for %0.4f ms (threshold is %0.4f ms)",
			    (double)tv_diff / NO_NS_IN_MSEC,
			    (double)tv_max_allowed_diff / NO_NS_IN_MSEC);

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
	printf("usage: %s [-dfhp] [-t timeout]\n", PROGRAM_NAME);
	printf("\n");
	printf("  -d            Display debug messages\n");
	printf("  -f            Run foreground - do not daemonize\n");
	printf("  -h            Show help\n");
	printf("  -p            Do not set RR scheduler\n");
	printf("  -t timeout    Set timeout value (default: %u)\n", DEFAULT_TIMEOUT);
}

int
main(int argc, char **argv)
{
	int ch;
	int foreground;
	long long int tmpll;
	uint64_t timeout;
	char *ep;
	int set_prio;

	foreground = 0;
	timeout = DEFAULT_TIMEOUT;
	set_prio = 1;

	while ((ch = getopt(argc, argv, "dfhpt:")) != -1) {
		switch (ch) {
		case 'f':
			foreground = 1;
			break;
		case 'd':
			log_debug = 1;
			break;
		case 't':
			tmpll = strtoll(optarg, &ep, 10);
			if (tmpll <= 0 || errno != 0 || *ep != '\0') {
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

	poll_run(timeout);

	if (!foreground) {
		closelog();
	}

	return (0);
}
