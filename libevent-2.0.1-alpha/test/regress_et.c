
#include <event-config.h>

#ifdef WIN32
#include <winsock2.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#ifdef _EVENT_HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <event2/event.h>
#include <event2/util.h>

#include "regress.h"

static int was_et = 0;

static void
read_cb(int fd, short event, void *arg)
{
	char buf;
	int len;

	len = read(fd, &buf, sizeof(buf));

	/*printf("%s: %s %d%s\n", __func__, event & EV_ET ? "etread" : "read",
	         len, len ? "" : " - means EOF");
	*/

	called++;
	if (event & EV_ET)
		was_et = 1;

	if (!len)
		event_del(arg);
}

#ifndef SHUT_WR
#define SHUT_WR 1
#endif

#ifdef WIN32
#define LOCAL_SOCKETPAIR_AF AF_INET
#else
#define LOCAL_SOCKETPAIR_AF AF_UNIX
#endif

static void
test_edgetriggered(void *et)
{
	struct event *ev = NULL;
    struct event_base *base = NULL;
	const char *test = "test string";
	evutil_socket_t pair[2] = {-1,-1};
    int supports_et;
	int success;

	if (evutil_socketpair(LOCAL_SOCKETPAIR_AF, SOCK_STREAM, 0, pair) == -1) {
		tt_fail_perror("socketpair");
	}

	called = was_et = 0;

	write(pair[0], test, strlen(test)+1);
	shutdown(pair[0], SHUT_WR);

	/* Initalize the event library */
	base = event_base_new();

	if (!strcmp(event_base_get_method(base), "epoll") ||
		!strcmp(event_base_get_method(base), "kqueue"))
		supports_et = 1;
	else
		supports_et = 0;

	TT_BLATHER(("Checking for edge-triggered events with %s, which should %s"
				"support edge-triggering", event_base_get_method(base),
				supports_et?"":"not "));

	/* Initalize one event */
    ev = event_new(base, pair[1], EV_READ|EV_ET|EV_PERSIST, read_cb, &ev);

	event_add(ev, NULL);

	/* We're going to call the dispatch function twice.  The first invocation
	 * will read a single byte from pair[1] in either case.  If we're edge
	 * triggered, we'll only see the event once (since we only see transitions
	 * from no data to data), so the second invocation of event_base_loop will
	 * do nothing.  If we're level triggered, the second invocation of
	 * event_base_loop will also activate the event (because there's still
	 * data to read). */
	event_base_loop(base,EVLOOP_NONBLOCK|EVLOOP_ONCE);
	event_base_loop(base,EVLOOP_NONBLOCK|EVLOOP_ONCE);

	if (supports_et) {
		tt_int_op(called, ==, 1);
		tt_assert(was_et);
	} else {
		tt_int_op(called, ==, 2);
		tt_assert(!was_et);
		success = (called == 2) && !was_et;

	}

 end:
	if (ev) {
		event_del(ev);
		event_free(ev);
	}
	if (base)
		event_base_free(base);
	EVUTIL_CLOSESOCKET(pair[0]);
	EVUTIL_CLOSESOCKET(pair[1]);
}

struct testcase_t edgetriggered_testcases[] = {
	{ "et", test_edgetriggered, TT_FORK, NULL, NULL },
	END_OF_TESTCASES
};
