/*
 * Copyright (c) 2003-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2009 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#ifdef HAVE_CONFIG_H
#include "event-config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#ifdef _EVENT_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#ifndef WIN32
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#endif
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "event2/event.h"
#include "event2/event_struct.h"
#include "event2/event_compat.h"
#include "event2/tag.h"
#include "event2/buffer.h"
#include "event2/buffer_compat.h"
#include "event2/util.h"
#include "event-internal.h"
#include "log-internal.h"

#include "regress.h"

#ifndef WIN32
#include "regress.gen.h"
#endif

int pair[2];
int test_ok;
int called;
struct event_base *global_base;

static char wbuf[4096];
static char rbuf[4096];
static int woff;
static int roff;
static int usepersist;
static struct timeval tset;
static struct timeval tcalled;



#define TEST1	"this is a test"
#define SECONDS	1

#ifndef SHUT_WR
#define SHUT_WR 1
#endif

#ifdef WIN32
#define write(fd,buf,len) send((fd),(buf),(len),0)
#define read(fd,buf,len) recv((fd),(buf),(len),0)
#endif

static void
simple_read_cb(int fd, short event, void *arg)
{
	char buf[256];
	int len;

	len = read(fd, buf, sizeof(buf));

	if (len) {
		if (!called) {
			if (event_add(arg, NULL) == -1)
				exit(1);
		}
	} else if (called == 1)
		test_ok = 1;

	called++;
}

static void
simple_write_cb(int fd, short event, void *arg)
{
	int len;

	len = write(fd, TEST1, strlen(TEST1) + 1);
	if (len == -1)
		test_ok = 0;
	else
		test_ok = 1;
}

static void
multiple_write_cb(int fd, short event, void *arg)
{
	struct event *ev = arg;
	int len;

	len = 128;
	if (woff + len >= sizeof(wbuf))
		len = sizeof(wbuf) - woff;

	len = write(fd, wbuf + woff, len);
	if (len == -1) {
		fprintf(stderr, "%s: write\n", __func__);
		if (usepersist)
			event_del(ev);
		return;
	}

	woff += len;
        
	if (woff >= sizeof(wbuf)) {
		shutdown(fd, SHUT_WR);
		if (usepersist)
			event_del(ev);
		return;
	}

	if (!usepersist) {
		if (event_add(ev, NULL) == -1)
			exit(1);
	}
}

static void
multiple_read_cb(int fd, short event, void *arg)
{
	struct event *ev = arg;
	int len;

	len = read(fd, rbuf + roff, sizeof(rbuf) - roff);
	if (len == -1)
		fprintf(stderr, "%s: read\n", __func__);
	if (len <= 0) {
		if (usepersist)
			event_del(ev);
		return;
	}

	roff += len;
	if (!usepersist) {
		if (event_add(ev, NULL) == -1)
			exit(1);
	}
}

static void
timeout_cb(int fd, short event, void *arg)
{
	struct timeval tv;
	int diff;

	evutil_gettimeofday(&tcalled, NULL);
	if (evutil_timercmp(&tcalled, &tset, >))
		evutil_timersub(&tcalled, &tset, &tv);
	else
		evutil_timersub(&tset, &tcalled, &tv);

	diff = tv.tv_sec*1000 + tv.tv_usec/1000 - SECONDS * 1000;
	if (diff < 0)
		diff = -diff;

	if (diff < 100)
		test_ok = 1;
}

struct both {
	struct event ev;
	int nread;
};

static void
combined_read_cb(int fd, short event, void *arg)
{
	struct both *both = arg;
	char buf[128];
	int len;

	len = read(fd, buf, sizeof(buf));
	if (len == -1)
		fprintf(stderr, "%s: read\n", __func__);
	if (len <= 0)
		return;

	both->nread += len;
	if (event_add(&both->ev, NULL) == -1)
		exit(1);
}

static void
combined_write_cb(int fd, short event, void *arg)
{
	struct both *both = arg;
	char buf[128];
	int len;

	len = sizeof(buf);
	if (len > both->nread)
		len = both->nread;

	len = write(fd, buf, len);
	if (len == -1)
		fprintf(stderr, "%s: write\n", __func__);
	if (len <= 0) {
		shutdown(fd, SHUT_WR);
		return;
	}

	both->nread -= len;
	if (event_add(&both->ev, NULL) == -1)
		exit(1);
}

/* Test infrastructure */

static int
setup_test(const char *name)
{
        if (in_legacy_test_wrapper)
                return 0;

	fprintf(stdout, "%s", name);

	if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1) {
		fprintf(stderr, "%s: socketpair\n", __func__);
		exit(1);
	}

        if (evutil_make_socket_nonblocking(pair[0]) == -1)
		fprintf(stderr, "fcntl(O_NONBLOCK)");

        if (evutil_make_socket_nonblocking(pair[1]) == -1)
		fprintf(stderr, "fcntl(O_NONBLOCK)");

	test_ok = 0;
	called = 0;
	return (0);
}

static int
cleanup_test(void)
{
        if (in_legacy_test_wrapper)
                return 0;

#ifndef WIN32
	close(pair[0]);
	close(pair[1]);
#else
	CloseHandle((HANDLE)pair[0]);
	CloseHandle((HANDLE)pair[1]);
#endif
	if (test_ok)
		fprintf(stdout, "OK\n");
	else {
		fprintf(stdout, "FAILED\n");
		exit(1);
	}
        test_ok = 0;
	return (0);
}

static void
test_simpleread(void)
{
	struct event ev;

	/* Very simple read test */
	setup_test("Simple read: ");

	write(pair[0], TEST1, strlen(TEST1)+1);
	shutdown(pair[0], SHUT_WR);

	event_set(&ev, pair[1], EV_READ, simple_read_cb, &ev);
	if (event_add(&ev, NULL) == -1)
		exit(1);
	event_dispatch();

	cleanup_test();
}

static void
test_simplewrite(void)
{
	struct event ev;

	/* Very simple write test */
	setup_test("Simple write: ");

	event_set(&ev, pair[0], EV_WRITE, simple_write_cb, &ev);
	if (event_add(&ev, NULL) == -1)
		exit(1);
	event_dispatch();

	cleanup_test();
}

static void
simpleread_multiple_cb(int fd, short event, void *arg)
{
	if (++called == 2)
		test_ok = 1;
}

static void
test_simpleread_multiple(void)
{
	struct event one, two;

	/* Very simple read test */
	setup_test("Simple read to multiple evens: ");

	write(pair[0], TEST1, strlen(TEST1)+1);
	shutdown(pair[0], SHUT_WR);

	event_set(&one, pair[1], EV_READ, simpleread_multiple_cb, NULL);
	if (event_add(&one, NULL) == -1)
		exit(1);
	event_set(&two, pair[1], EV_READ, simpleread_multiple_cb, NULL);
	if (event_add(&two, NULL) == -1)
		exit(1);
	event_dispatch();

	cleanup_test();
}

static void
test_multiple(void)
{
	struct event ev, ev2;
	int i;

	/* Multiple read and write test */
	setup_test("Multiple read/write: ");
	memset(rbuf, 0, sizeof(rbuf));
	for (i = 0; i < sizeof(wbuf); i++)
		wbuf[i] = i;

	roff = woff = 0;
	usepersist = 0;

	event_set(&ev, pair[0], EV_WRITE, multiple_write_cb, &ev);
	if (event_add(&ev, NULL) == -1)
		exit(1);
	event_set(&ev2, pair[1], EV_READ, multiple_read_cb, &ev2);
	if (event_add(&ev2, NULL) == -1)
		exit(1);
	event_dispatch();

	if (roff == woff)
		test_ok = memcmp(rbuf, wbuf, sizeof(wbuf)) == 0;

	cleanup_test();
}

static void
test_persistent(void)
{
	struct event ev, ev2;
	int i;

	/* Multiple read and write test with persist */
	setup_test("Persist read/write: ");
	memset(rbuf, 0, sizeof(rbuf));
	for (i = 0; i < sizeof(wbuf); i++)
		wbuf[i] = i;

	roff = woff = 0;
	usepersist = 1;

	event_set(&ev, pair[0], EV_WRITE|EV_PERSIST, multiple_write_cb, &ev);
	if (event_add(&ev, NULL) == -1)
		exit(1);
	event_set(&ev2, pair[1], EV_READ|EV_PERSIST, multiple_read_cb, &ev2);
	if (event_add(&ev2, NULL) == -1)
		exit(1);
	event_dispatch();

	if (roff == woff)
		test_ok = memcmp(rbuf, wbuf, sizeof(wbuf)) == 0;

	cleanup_test();
}

static void
test_combined(void)
{
	struct both r1, r2, w1, w2;

	setup_test("Combined read/write: ");
	memset(&r1, 0, sizeof(r1));
	memset(&r2, 0, sizeof(r2));
	memset(&w1, 0, sizeof(w1));
	memset(&w2, 0, sizeof(w2));

	w1.nread = 4096;
	w2.nread = 8192;

	event_set(&r1.ev, pair[0], EV_READ, combined_read_cb, &r1);
	event_set(&w1.ev, pair[0], EV_WRITE, combined_write_cb, &w1);
	event_set(&r2.ev, pair[1], EV_READ, combined_read_cb, &r2);
	event_set(&w2.ev, pair[1], EV_WRITE, combined_write_cb, &w2);
	if (event_add(&r1.ev, NULL) == -1)
		exit(1);
	if (event_add(&w1.ev, NULL))
		exit(1);
	if (event_add(&r2.ev, NULL))
		exit(1);
	if (event_add(&w2.ev, NULL))
		exit(1);

	event_dispatch();

	if (r1.nread == 8192 && r2.nread == 4096)
		test_ok = 1;

	cleanup_test();
}

static void
test_simpletimeout(void)
{
	struct timeval tv;
	struct event ev;

	setup_test("Simple timeout: ");

	tv.tv_usec = 0;
	tv.tv_sec = SECONDS;
	evtimer_set(&ev, timeout_cb, NULL);
	evtimer_add(&ev, &tv);

	evutil_gettimeofday(&tset, NULL);
	event_dispatch();

	cleanup_test();
}

static void
periodic_timeout_cb(int fd, short event, void *arg)
{
	int *count = arg;

	(*count)++;
	if (*count == 6) {
		/* call loopexit only once - on slow machines(?), it is
		 * apparently possible for this to get called twice. */
		test_ok = 1;
		event_base_loopexit(global_base, NULL);
	}
}

static void
test_persistent_timeout(void)
{
	struct timeval tv;
	struct event ev;
	int count = 0;

	timerclear(&tv);
	tv.tv_usec = 10000;

	event_assign(&ev, global_base, -1, EV_TIMEOUT|EV_PERSIST,
	    periodic_timeout_cb, &count);
	event_add(&ev, &tv);

	event_dispatch();

	event_del(&ev);

}

#ifndef WIN32
static void signal_cb(int fd, short event, void *arg);

extern struct event_base *current_base;

static void
child_signal_cb(int fd, short event, void *arg)
{
	struct timeval tv;
	int *pint = arg;

	*pint = 1;

	tv.tv_usec = 500000;
	tv.tv_sec = 0;
	event_loopexit(&tv);
}

static void
test_fork(void)
{
	int status, got_sigchld = 0;
	struct event ev, sig_ev;
	pid_t pid;

	setup_test("After fork: ");

	write(pair[0], TEST1, strlen(TEST1)+1);

	event_set(&ev, pair[1], EV_READ, simple_read_cb, &ev);
	if (event_add(&ev, NULL) == -1)
		exit(1);

	evsignal_set(&sig_ev, SIGCHLD, child_signal_cb, &got_sigchld);
	evsignal_add(&sig_ev, NULL);

	if ((pid = fork()) == 0) {
		/* in the child */
		if (event_reinit(current_base) == -1) {
			fprintf(stdout, "FAILED (reinit)\n");
			exit(1);
		}

		evsignal_del(&sig_ev);

		called = 0;

		event_dispatch();

		event_base_free(current_base);

		/* we do not send an EOF; simple_read_cb requires an EOF
		 * to set test_ok.  we just verify that the callback was
		 * called. */
		exit(test_ok != 0 || called != 2 ? -2 : 76);
	}

	/* wait for the child to read the data */
	sleep(1);

	write(pair[0], TEST1, strlen(TEST1)+1);

	if (waitpid(pid, &status, 0) == -1) {
		fprintf(stdout, "FAILED (fork)\n");
		exit(1);
	}

	if (WEXITSTATUS(status) != 76) {
		fprintf(stdout, "FAILED (exit): %d\n", WEXITSTATUS(status));
		exit(1);
	}

	/* test that the current event loop still works */
	write(pair[0], TEST1, strlen(TEST1)+1);
	shutdown(pair[0], SHUT_WR);

	event_dispatch();

	if (!got_sigchld) {
		fprintf(stdout, "FAILED (sigchld)\n");
		exit(1);
	}

	evsignal_del(&sig_ev);

	cleanup_test();
}

static void
signal_cb_sa(int sig)
{
	test_ok = 2;
}

static void
signal_cb(int fd, short event, void *arg)
{
	struct event *ev = arg;

	evsignal_del(ev);
	test_ok = 1;
}

static void
test_simplesignal(void)
{
	struct event ev;
	struct itimerval itv;

	setup_test("Simple signal: ");
	evsignal_set(&ev, SIGALRM, signal_cb, &ev);
	evsignal_add(&ev, NULL);
	/* find bugs in which operations are re-ordered */
	evsignal_del(&ev);
	evsignal_add(&ev, NULL);

	memset(&itv, 0, sizeof(itv));
	itv.it_value.tv_sec = 1;
	if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
		goto skip_simplesignal;

	event_dispatch();
 skip_simplesignal:
	if (evsignal_del(&ev) == -1)
		test_ok = 0;

	cleanup_test();
}

static void
test_multiplesignal(void)
{
	struct event ev_one, ev_two;
	struct itimerval itv;

	setup_test("Multiple signal: ");

	evsignal_set(&ev_one, SIGALRM, signal_cb, &ev_one);
	evsignal_add(&ev_one, NULL);

	evsignal_set(&ev_two, SIGALRM, signal_cb, &ev_two);
	evsignal_add(&ev_two, NULL);

	memset(&itv, 0, sizeof(itv));
	itv.it_value.tv_sec = 1;
	if (setitimer(ITIMER_REAL, &itv, NULL) == -1)
		goto skip_simplesignal;

	event_dispatch();

 skip_simplesignal:
	if (evsignal_del(&ev_one) == -1)
		test_ok = 0;
	if (evsignal_del(&ev_two) == -1)
		test_ok = 0;

	cleanup_test();
}

static void
test_immediatesignal(void)
{
	struct event ev;

	test_ok = 0;
	printf("Immediate signal: ");
	evsignal_set(&ev, SIGUSR1, signal_cb, &ev);
	evsignal_add(&ev, NULL);
	raise(SIGUSR1);
	event_loop(EVLOOP_NONBLOCK);
	evsignal_del(&ev);
	cleanup_test();
}

static void
test_signal_dealloc(void)
{
	/* make sure that evsignal_event is event_del'ed and pipe closed */
	struct event ev;
	struct event_base *base = event_init();
	printf("Signal dealloc: ");
	evsignal_set(&ev, SIGUSR1, signal_cb, &ev);
	evsignal_add(&ev, NULL);
	evsignal_del(&ev);
	event_base_free(base);
        /* If we got here without asserting, we're fine. */
        test_ok = 1;
	cleanup_test();
}

static void
test_signal_pipeloss(void)
{
	/* make sure that the base1 pipe is closed correctly. */
	struct event_base *base1, *base2;
	int pipe1;
	test_ok = 0;
	printf("Signal pipeloss: ");
	base1 = event_init();
	pipe1 = base1->sig.ev_signal_pair[0];
	base2 = event_init();
	event_base_free(base2);
	event_base_free(base1);
	if (close(pipe1) != -1 || errno!=EBADF) {
		/* fd must be closed, so second close gives -1, EBADF */
		printf("signal pipe not closed. ");
		test_ok = 0;
	} else {
		test_ok = 1;
	}
	cleanup_test();
}

/*
 * make two bases to catch signals, use both of them.  this only works
 * for event mechanisms that use our signal pipe trick.  kqueue handles
 * signals internally, and all interested kqueues get all the signals.
 */
static void
test_signal_switchbase(void)
{
	struct event ev1, ev2;
	struct event_base *base1, *base2;
        int is_kqueue;
	test_ok = 0;
	printf("Signal switchbase: ");
	base1 = event_init();
	base2 = event_init();
        is_kqueue = !strcmp(event_get_method(),"kqueue");
	evsignal_set(&ev1, SIGUSR1, signal_cb, &ev1);
	evsignal_set(&ev2, SIGUSR1, signal_cb, &ev2);
	if (event_base_set(base1, &ev1) ||
	    event_base_set(base2, &ev2) ||
	    event_add(&ev1, NULL) ||
	    event_add(&ev2, NULL)) {
		fprintf(stderr, "%s: cannot set base, add\n", __func__);
		exit(1);
	}

	test_ok = 0;
	/* can handle signal before loop is called */
	raise(SIGUSR1);
	event_base_loop(base2, EVLOOP_NONBLOCK);
        if (is_kqueue) {
                if (!test_ok)
                        goto done;
                test_ok = 0;
        }
	event_base_loop(base1, EVLOOP_NONBLOCK);
	if (test_ok && !is_kqueue) {
		test_ok = 0;

		/* set base1 to handle signals */
		event_base_loop(base1, EVLOOP_NONBLOCK);
		raise(SIGUSR1);
		event_base_loop(base1, EVLOOP_NONBLOCK);
		event_base_loop(base2, EVLOOP_NONBLOCK);
	}
 done:
	event_base_free(base1);
	event_base_free(base2);
	cleanup_test();
}

/*
 * assert that a signal event removed from the event queue really is
 * removed - with no possibility of it's parent handler being fired.
 */
static void
test_signal_assert(void)
{
	struct event ev;
	struct event_base *base = event_init();
	test_ok = 0;
	printf("Signal handler assert: ");
	/* use SIGCONT so we don't kill ourselves when we signal to nowhere */
	evsignal_set(&ev, SIGCONT, signal_cb, &ev);
	evsignal_add(&ev, NULL);
	/*
	 * if evsignal_del() fails to reset the handler, it's current handler
	 * will still point to evsig_handler().
	 */
	evsignal_del(&ev);

	raise(SIGCONT);
	/* only way to verify we were in evsig_handler() */
	if (base->sig.evsig_caught)
		test_ok = 0;
	else
		test_ok = 1;

	event_base_free(base);
	cleanup_test();
	return;
}

/*
 * assert that we restore our previous signal handler properly.
 */
static void
test_signal_restore(void)
{
	struct event ev;
	struct event_base *base = event_init();
#ifdef _EVENT_HAVE_SIGACTION
	struct sigaction sa;
#endif

	test_ok = 0;
	printf("Signal handler restore: ");
#ifdef _EVENT_HAVE_SIGACTION
	sa.sa_handler = signal_cb_sa;
	sa.sa_flags = 0x0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) == -1)
		goto out;
#else
	if (signal(SIGUSR1, signal_cb_sa) == SIG_ERR)
		goto out;
#endif
	evsignal_set(&ev, SIGUSR1, signal_cb, &ev);
	evsignal_add(&ev, NULL);
	evsignal_del(&ev);

	raise(SIGUSR1);
	/* 1 == signal_cb, 2 == signal_cb_sa, we want our previous handler */
	if (test_ok != 2)
		test_ok = 0;
out:
	event_base_free(base);
	cleanup_test();
	return;
}

static void
signal_cb_swp(int sig, short event, void *arg)
{
	called++;
	if (called < 5)
		raise(sig);
	else
		event_loopexit(NULL);
}
static void
timeout_cb_swp(int fd, short event, void *arg)
{
	if (called == -1) {
		struct timeval tv = {5, 0};

		called = 0;
		evtimer_add((struct event *)arg, &tv);
		raise(SIGUSR1);
		return;
	}
	test_ok = 0;
	event_loopexit(NULL);
}

static void
test_signal_while_processing(void)
{
	struct event_base *base = event_init();
	struct event ev, ev_timer;
	struct timeval tv = {0, 0};

	setup_test("Receiving a signal while processing other signal: ");

	called = -1;
	test_ok = 1;
	signal_set(&ev, SIGUSR1, signal_cb_swp, NULL);
	signal_add(&ev, NULL);
	evtimer_set(&ev_timer, timeout_cb_swp, &ev_timer);
	evtimer_add(&ev_timer, &tv);
	event_dispatch();

	event_base_free(base);
	cleanup_test();
	return;
}
#endif

static void
test_free_active_base(void)
{
	struct event_base *base1;
	struct event ev1;
	setup_test("Free active base: ");
	base1 = event_init();
	event_set(&ev1, pair[1], EV_READ, simple_read_cb, &ev1);
	event_base_set(base1, &ev1);
	event_add(&ev1, NULL);
	/* event_del(&ev1); */
	event_base_free(base1);
	test_ok = 1;
	cleanup_test();
	event_base_free(global_base);
	global_base = event_init();
}

static void
test_event_base_new(void)
{
	struct event_base *base;
	struct event ev1;
	setup_test("Event base new: ");

	write(pair[0], TEST1, strlen(TEST1)+1);
	shutdown(pair[0], SHUT_WR);

	base = event_base_new();
	event_set(&ev1, pair[1], EV_READ, simple_read_cb, &ev1);
	event_base_set(base, &ev1);
	event_add(&ev1, NULL);

	event_base_dispatch(base);

	event_base_free(base);
	test_ok = 1;
	cleanup_test();
}

static void
test_loopexit(void)
{
	struct timeval tv, tv_start, tv_end;
	struct event ev;

	setup_test("Loop exit: ");

	tv.tv_usec = 0;
	tv.tv_sec = 60*60*24;
	evtimer_set(&ev, timeout_cb, NULL);
	evtimer_add(&ev, &tv);

	tv.tv_usec = 0;
	tv.tv_sec = 1;
	event_loopexit(&tv);

	evutil_gettimeofday(&tv_start, NULL);
	event_dispatch();
	evutil_gettimeofday(&tv_end, NULL);
	evutil_timersub(&tv_end, &tv_start, &tv_end);

	evtimer_del(&ev);

	if (tv.tv_sec < 2)
		test_ok = 1;

	cleanup_test();
}

static void
test_loopexit_multiple(void)
{
	struct timeval tv;
	struct event_base *base;

	setup_test("Loop Multiple exit: ");

	base = event_base_new();

	tv.tv_usec = 0;
	tv.tv_sec = 1;
	event_base_loopexit(base, &tv);

	tv.tv_usec = 0;
	tv.tv_sec = 2;
	event_base_loopexit(base, &tv);

	event_base_dispatch(base);

	event_base_free(base);

	test_ok = 1;

	cleanup_test();
}

static void
break_cb(int fd, short events, void *arg)
{
	test_ok = 1;
	event_loopbreak();
}

static void
fail_cb(int fd, short events, void *arg)
{
	test_ok = 0;
}

static void
test_loopbreak(void)
{
	struct event ev1, ev2;
	struct timeval tv;

	setup_test("Loop break: ");

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	evtimer_set(&ev1, break_cb, NULL);
	evtimer_add(&ev1, &tv);
	evtimer_set(&ev2, fail_cb, NULL);
	evtimer_add(&ev2, &tv);

	event_dispatch();

	evtimer_del(&ev1);
	evtimer_del(&ev2);

	cleanup_test();
}

static struct event *readd_test_event_last_added = NULL;
static void
re_add_read_cb(int fd, short event, void *arg)
{
	char buf[256];
	int len;
	struct event *ev_other = arg;
	readd_test_event_last_added = ev_other;
	len = read(fd, buf, sizeof(buf));
	event_add(ev_other, NULL);
	++test_ok;
}

static void
test_nonpersist_readd(void)
{
	struct event ev1, ev2;
	int n, m;

	setup_test("Re-add nonpersistent events: ");
	event_set(&ev1, pair[0], EV_READ, re_add_read_cb, &ev2);
	event_set(&ev2, pair[1], EV_READ, re_add_read_cb, &ev1);
	n = write(pair[0], "Hello", 5);
	m = write(pair[1], "Hello", 5);
	if (event_add(&ev1, NULL) == -1 ||
	    event_add(&ev2, NULL) == -1) {
		test_ok = 0;
	}
	if (test_ok != 0)
		exit(1);
	event_loop(EVLOOP_ONCE);
	if (test_ok != 2)
		exit(1);
	/* At this point, we executed both callbacks.  Whichever one got
	 * called first added the second, but the second then immediately got
	 * deleted before its callback was called.  At this point, though, it
	 * re-added the first.
	 */
	if (!readd_test_event_last_added) {
		test_ok = 0;
	} else if (readd_test_event_last_added == &ev1) {
		if (!event_pending(&ev1, EV_READ, NULL) ||
		    event_pending(&ev2, EV_READ, NULL))
			test_ok = 0;
	} else {
		if (event_pending(&ev1, EV_READ, NULL) ||
		    !event_pending(&ev2, EV_READ, NULL))
			test_ok = 0;
	}

	event_del(&ev1);
	event_del(&ev2);

	cleanup_test();
}

struct test_pri_event {
	struct event ev;
	int count;
};

static void
test_priorities_cb(int fd, short what, void *arg)
{
	struct test_pri_event *pri = arg;
	struct timeval tv;

	if (pri->count == 3) {
		event_loopexit(NULL);
		return;
	}

	pri->count++;

	evutil_timerclear(&tv);
	event_add(&pri->ev, &tv);
}

static void
test_priorities_impl(int npriorities)
{
	struct test_pri_event one, two;
	struct timeval tv;

	TT_BLATHER(("Testing Priorities %d: ", npriorities));

	event_base_priority_init(global_base, npriorities);

	memset(&one, 0, sizeof(one));
	memset(&two, 0, sizeof(two));

	timeout_set(&one.ev, test_priorities_cb, &one);
	if (event_priority_set(&one.ev, 0) == -1) {
		fprintf(stderr, "%s: failed to set priority", __func__);
		exit(1);
	}

	timeout_set(&two.ev, test_priorities_cb, &two);
	if (event_priority_set(&two.ev, npriorities - 1) == -1) {
		fprintf(stderr, "%s: failed to set priority", __func__);
		exit(1);
	}

	evutil_timerclear(&tv);

	if (event_add(&one.ev, &tv) == -1)
		exit(1);
	if (event_add(&two.ev, &tv) == -1)
		exit(1);

	event_dispatch();

	event_del(&one.ev);
	event_del(&two.ev);

	if (npriorities == 1) {
		if (one.count == 3 && two.count == 3)
			test_ok = 1;
	} else if (npriorities == 2) {
		/* Two is called once because event_loopexit is priority 1 */
		if (one.count == 3 && two.count == 1)
			test_ok = 1;
	} else {
		if (one.count == 3 && two.count == 0)
			test_ok = 1;
	}
}

static void
test_priorities(void)
{
        test_priorities_impl(1);
        if (test_ok)
                test_priorities_impl(2);
        if (test_ok)
                test_priorities_impl(3);
}


static void
test_multiple_cb(int fd, short event, void *arg)
{
	if (event & EV_READ)
		test_ok |= 1;
	else if (event & EV_WRITE)
		test_ok |= 2;
}

static void
test_multiple_events_for_same_fd(void)
{
   struct event e1, e2;

   setup_test("Multiple events for same fd: ");

   event_set(&e1, pair[0], EV_READ, test_multiple_cb, NULL);
   event_add(&e1, NULL);
   event_set(&e2, pair[0], EV_WRITE, test_multiple_cb, NULL);
   event_add(&e2, NULL);
   event_loop(EVLOOP_ONCE);
   event_del(&e2);
   write(pair[1], TEST1, strlen(TEST1)+1);
   event_loop(EVLOOP_ONCE);
   event_del(&e1);

   if (test_ok != 3)
	   test_ok = 0;

   cleanup_test();
}

int evtag_decode_int(ev_uint32_t *pnumber, struct evbuffer *evbuf);
int evtag_encode_tag(struct evbuffer *evbuf, ev_uint32_t number);
int evtag_decode_tag(ev_uint32_t *pnumber, struct evbuffer *evbuf);

static void
read_once_cb(int fd, short event, void *arg)
{
	char buf[256];
	int len;

	len = read(fd, buf, sizeof(buf));

	if (called) {
		test_ok = 0;
	} else if (len) {
		/* Assumes global pair[0] can be used for writing */
		write(pair[0], TEST1, strlen(TEST1)+1);
		test_ok = 1;
	}

	called++;
}

static void
test_want_only_once(void)
{
	struct event ev;
	struct timeval tv;

	/* Very simple read test */
	setup_test("Want read only once: ");

	write(pair[0], TEST1, strlen(TEST1)+1);

	/* Setup the loop termination */
	evutil_timerclear(&tv);
	tv.tv_sec = 1;
	event_loopexit(&tv);

	event_set(&ev, pair[1], EV_READ, read_once_cb, &ev);
	if (event_add(&ev, NULL) == -1)
		exit(1);
	event_dispatch();

	cleanup_test();
}

#define TEST_MAX_INT	6

static void
evtag_int_test(void)
{
	struct evbuffer *tmp = evbuffer_new();
	ev_uint32_t integers[TEST_MAX_INT] = {
		0xaf0, 0x1000, 0x1, 0xdeadbeef, 0x00, 0xbef000
	};
	ev_uint32_t integer;
	int i;

	for (i = 0; i < TEST_MAX_INT; i++) {
		int oldlen, newlen;
		oldlen = EVBUFFER_LENGTH(tmp);
		encode_int(tmp, integers[i]);
		newlen = EVBUFFER_LENGTH(tmp);
                TT_BLATHER(("encoded 0x%08x with %d bytes",
                        (unsigned)integers[i], newlen - oldlen));
	}

	for (i = 0; i < TEST_MAX_INT; i++) {
                tt_assert(evtag_decode_int(&integer, tmp) != -1);
                tt_uint_op(integer, ==, integers[i]);
	}

        tt_uint_op(EVBUFFER_LENGTH(tmp), ==, 0);
end:
	evbuffer_free(tmp);
}

static void
evtag_fuzz(void)
{
	u_char buffer[4096];
	struct evbuffer *tmp = evbuffer_new();
	struct timeval tv;
	int i, j;

	int not_failed = 0;
	for (j = 0; j < 100; j++) {
		for (i = 0; i < sizeof(buffer); i++)
			buffer[i] = rand();
		evbuffer_drain(tmp, -1);
		evbuffer_add(tmp, buffer, sizeof(buffer));

		if (evtag_unmarshal_timeval(tmp, 0, &tv) != -1)
			not_failed++;
	}

	/* The majority of decodes should fail */
	tt_int_op(not_failed, <, 10);

	/* Now insert some corruption into the tag length field */
	evbuffer_drain(tmp, -1);
	evutil_timerclear(&tv);
	tv.tv_sec = 1;
	evtag_marshal_timeval(tmp, 0, &tv);
	evbuffer_add(tmp, buffer, sizeof(buffer));

	((char *)EVBUFFER_DATA(tmp))[1] = 0xff;
	if (evtag_unmarshal_timeval(tmp, 0, &tv) != -1) {
                tt_abort_msg("evtag_unmarshal_timeval should have failed");
	}

end:
	evbuffer_free(tmp);
}

static void
evtag_tag_encoding(void)
{
	struct evbuffer *tmp = evbuffer_new();
	ev_uint32_t integers[TEST_MAX_INT] = {
		0xaf0, 0x1000, 0x1, 0xdeadbeef, 0x00, 0xbef000
	};
	ev_uint32_t integer;
	int i;

	for (i = 0; i < TEST_MAX_INT; i++) {
		int oldlen, newlen;
		oldlen = EVBUFFER_LENGTH(tmp);
		evtag_encode_tag(tmp, integers[i]);
		newlen = EVBUFFER_LENGTH(tmp);
                TT_BLATHER(("encoded 0x%08x with %d bytes",
                        (unsigned)integers[i], newlen - oldlen));
	}

	for (i = 0; i < TEST_MAX_INT; i++) {
                tt_int_op(evtag_decode_tag(&integer, tmp), !=, -1);
                tt_uint_op(integer, ==, integers[i]);
	}

        tt_uint_op(EVBUFFER_LENGTH(tmp), ==, 0);
end:
	evbuffer_free(tmp);
}

static void
test_evtag(void)
{
	evtag_init();
	evtag_int_test();
	evtag_fuzz();
	evtag_tag_encoding();
        test_ok = 1;
}

static void
test_methods(void *ptr)
{
	const char **methods = event_get_supported_methods();
	struct event_config *cfg = NULL;
	struct event_base *base = NULL;
	const char *backend;
	int n_methods = 0;

        tt_assert(methods);

	backend = methods[0];
	while (*methods != NULL) {
		TT_BLATHER(("Support method: %s", *methods));
		++methods;
		++n_methods;
	}

	if (n_methods == 1) {
		/* only one method supported; can't continue. */
                goto end;
	}

	cfg = event_config_new();
	assert(cfg != NULL);

	tt_int_op(event_config_avoid_method(cfg, backend), ==, 0);

	base = event_base_new_with_config(cfg);
        tt_assert(base);

	tt_str_op(backend, !=, event_base_get_method(base));

end:
        if (base)
                event_base_free(base);
        if (cfg)
                event_config_free(cfg);
}

struct testcase_t legacy_testcases[] = {
        /* Some converted-over tests */
        { "methods", test_methods, TT_FORK, NULL, NULL },

        /* These are still using the old API */
        LEGACY(persistent_timeout, TT_FORK|TT_NEED_BASE),
        LEGACY(priorities, TT_FORK|TT_NEED_BASE),

        LEGACY(free_active_base, TT_FORK|TT_NEED_BASE),
        LEGACY(event_base_new, TT_FORK|TT_NEED_SOCKETPAIR),

        /* These legacy tests may not all need all of these flags. */
        LEGACY(simpleread, TT_ISOLATED),
        LEGACY(simpleread_multiple, TT_ISOLATED),
        LEGACY(simplewrite, TT_ISOLATED),
        LEGACY(multiple, TT_ISOLATED),
        LEGACY(persistent, TT_ISOLATED),
        LEGACY(combined, TT_ISOLATED),
        LEGACY(simpletimeout, TT_ISOLATED),
        LEGACY(loopbreak, TT_ISOLATED),
        LEGACY(loopexit, TT_ISOLATED),
	LEGACY(loopexit_multiple, TT_ISOLATED),
	LEGACY(nonpersist_readd, TT_ISOLATED),
	LEGACY(multiple_events_for_same_fd, TT_ISOLATED),
	LEGACY(want_only_once, TT_ISOLATED),

#ifndef WIN32
        LEGACY(fork, TT_ISOLATED),
#endif

        LEGACY(evtag, TT_ISOLATED),

        END_OF_TESTCASES
};

struct testcase_t signal_testcases[] = {
#ifndef WIN32
	LEGACY(simplesignal, TT_ISOLATED),
	LEGACY(multiplesignal, TT_ISOLATED),
	LEGACY(immediatesignal, TT_ISOLATED),
        LEGACY(signal_dealloc, TT_ISOLATED),
	LEGACY(signal_pipeloss, TT_ISOLATED),
	LEGACY(signal_switchbase, TT_ISOLATED),
	LEGACY(signal_restore, TT_ISOLATED),
	LEGACY(signal_assert, TT_ISOLATED),
	LEGACY(signal_while_processing, TT_ISOLATED),
#endif
        END_OF_TESTCASES
};

