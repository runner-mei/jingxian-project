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

#include "event-config.h"
#include "event2/event.h"
#include "event2/event_struct.h"
#include "event2/event_compat.h"
#include "event2/tag.h"
#include "event2/buffer.h"
#include "event2/bufferevent.h"
#include "event2/bufferevent_compat.h"
#include "event2/bufferevent_struct.h"
#include "event2/util.h"

#include "bufferevent-internal.h"

#include "regress.h"

/*
 * simple bufferevent test
 */

static void
readcb(struct bufferevent *bev, void *arg)
{
	if (evbuffer_get_length(bev->input) == 8333) {
		struct evbuffer *evbuf = evbuffer_new();
		assert(evbuf != NULL);

		/* gratuitous test of bufferevent_read_buffer */
		bufferevent_read_buffer(bev, evbuf);

		bufferevent_disable(bev, EV_READ);

		if (evbuffer_get_length(evbuf) == 8333) {
			test_ok++;
                }

		evbuffer_free(evbuf);
	}
}

static void
writecb(struct bufferevent *bev, void *arg)
{
	if (evbuffer_get_length(bev->output) == 0) {
		test_ok++;
        }
}

static void
errorcb(struct bufferevent *bev, short what, void *arg)
{
	test_ok = -2;
}

static void
test_bufferevent_impl(int use_pair)
{
	struct bufferevent *bev1 = NULL, *bev2 = NULL;
	char buffer[8333];
	int i;

	if (use_pair) {
		struct bufferevent *pair[2];
		tt_assert(0 == bufferevent_pair_new(NULL, 0, pair));
		bev1 = pair[0];
		bev2 = pair[1];
		bufferevent_setcb(bev1, readcb, writecb, errorcb, NULL);
		bufferevent_setcb(bev2, readcb, writecb, errorcb, NULL);
	} else {
		bev1 = bufferevent_new(pair[0], readcb, writecb, errorcb, NULL);
		bev2 = bufferevent_new(pair[1], readcb, writecb, errorcb, NULL);
	}

	bufferevent_disable(bev1, EV_READ);
	bufferevent_enable(bev2, EV_READ);

	for (i = 0; i < sizeof(buffer); i++)
		buffer[i] = i;

	bufferevent_write(bev1, buffer, sizeof(buffer));

	event_dispatch();

	bufferevent_free(bev1);
	bufferevent_free(bev2);

	if (test_ok != 2)
		test_ok = 0;
end:
	;
}

static void
test_bufferevent(void)
{
	test_bufferevent_impl(0);
}

static void
test_bufferevent_pair(void)
{
	test_bufferevent_impl(1);
}

/*
 * test watermarks and bufferevent
 */

static void
wm_readcb(struct bufferevent *bev, void *arg)
{
	struct evbuffer *evbuf = evbuffer_new();
	int len = evbuffer_get_length(bev->input);
	static int nread;

	assert(len >= 10 && len <= 20);

	assert(evbuf != NULL);

	/* gratuitous test of bufferevent_read_buffer */
	bufferevent_read_buffer(bev, evbuf);

	nread += len;
	if (nread == 65000) {
		bufferevent_disable(bev, EV_READ);
		test_ok++;
	}

	evbuffer_free(evbuf);
}

static void
wm_writecb(struct bufferevent *bev, void *arg)
{
        assert(evbuffer_get_length(bev->output) <= 100);
	if (evbuffer_get_length(bev->output) == 0) {
                evbuffer_drain(bev->output, evbuffer_get_length(bev->output));
		test_ok++;
        }
}

static void
wm_errorcb(struct bufferevent *bev, short what, void *arg)
{
	test_ok = -2;
}

static void
test_bufferevent_watermarks_impl(int use_pair)
{
	struct bufferevent *bev1 = NULL, *bev2 = NULL;
	char buffer[65000];
	int i;
	test_ok = 0;

	if (use_pair) {
		struct bufferevent *pair[2];
		tt_assert(0 == bufferevent_pair_new(NULL, 0, pair));
		bev1 = pair[0];
		bev2 = pair[1];
		bufferevent_setcb(bev1, NULL, wm_writecb, errorcb, NULL);
		bufferevent_setcb(bev2, wm_readcb, NULL, errorcb, NULL);
	} else {
		bev1 = bufferevent_new(pair[0], NULL, wm_writecb, wm_errorcb, NULL);
		bev2 = bufferevent_new(pair[1], wm_readcb, NULL, wm_errorcb, NULL);
	}
	bufferevent_disable(bev1, EV_READ);
	bufferevent_enable(bev2, EV_READ);

	for (i = 0; i < sizeof(buffer); i++)
		buffer[i] = (char)i;

	/* limit the reading on the receiving bufferevent */
	bufferevent_setwatermark(bev2, EV_READ, 10, 20);

        /* Tell the sending bufferevent not to notify us till it's down to
           100 bytes. */
        bufferevent_setwatermark(bev1, EV_WRITE, 100, 2000);

	bufferevent_write(bev1, buffer, sizeof(buffer));

	event_dispatch();

	tt_int_op(test_ok, ==, 2);

        /* The write callback drained all the data from outbuf, so we
         * should have removed the write event... */
        tt_assert(!event_pending(&bev2->ev_write, EV_WRITE, NULL));

end:
	bufferevent_free(bev1);
	bufferevent_free(bev2);
}

static void
test_bufferevent_watermarks(void)
{
	test_bufferevent_watermarks_impl(0);
}

static void
test_bufferevent_pair_watermarks(void)
{
	test_bufferevent_watermarks_impl(1);
}

/*
 * Test bufferevent filters
 */

/* strip an 'x' from each byte */

static enum bufferevent_filter_result
bufferevent_input_filter(struct evbuffer *src, struct evbuffer *dst,
    ssize_t lim, enum bufferevent_flush_mode state, void *ctx)
{
	const unsigned char *buffer;
	int i;

	buffer = evbuffer_pullup(src, evbuffer_get_length(src));
	for (i = 0; i < evbuffer_get_length(src); i += 2) {
		assert(buffer[i] == 'x');
		evbuffer_add(dst, buffer + i + 1, 1);

		if (i + 2 > evbuffer_get_length(src))
			break;
	}

	evbuffer_drain(src, i);
	return (BEV_OK);
}

/* add an 'x' before each byte */

static enum bufferevent_filter_result
bufferevent_output_filter(struct evbuffer *src, struct evbuffer *dst,
    ssize_t lim, enum bufferevent_flush_mode state, void *ctx)
{
	const unsigned char *buffer;
	int i;

	buffer = evbuffer_pullup(src, evbuffer_get_length(src));
	for (i = 0; i < evbuffer_get_length(src); ++i) {
		evbuffer_add(dst, "x", 1);
		evbuffer_add(dst, buffer + i, 1);
	}

	evbuffer_drain(src, evbuffer_get_length(src));
	return (BEV_OK);
}


static void
test_bufferevent_filters_impl(int use_pair)
{
	struct bufferevent *bev1 = NULL, *bev2 = NULL;
	char buffer[8333];
	int i;

        test_ok = 0;

	if (use_pair) {
		struct bufferevent *pair[2];
		tt_assert(0 == bufferevent_pair_new(NULL, 0, pair));
		bev1 = pair[0];
		bev2 = pair[1];
	} else {
		bev1 = bufferevent_socket_new(NULL, pair[0], 0);
		bev2 = bufferevent_socket_new(NULL, pair[1], 0);
	}

	for (i = 0; i < sizeof(buffer); i++)
		buffer[i] = i;

	bev1 = bufferevent_filter_new(bev1, NULL, bufferevent_output_filter,
				      0, NULL, NULL);

	bev2 = bufferevent_filter_new(bev2, bufferevent_input_filter,
				      NULL, 0, NULL, NULL);
	bufferevent_setcb(bev1, NULL, writecb, errorcb, NULL);
	bufferevent_setcb(bev2, readcb, NULL, errorcb, NULL);

	bufferevent_disable(bev1, EV_READ);
	bufferevent_enable(bev2, EV_READ);
	/* insert some filters */
	bufferevent_write(bev1, buffer, sizeof(buffer));

	event_dispatch();

	if (test_ok != 2)
		test_ok = 0;

end:
	bufferevent_free(bev1);
	bufferevent_free(bev2);

}

static void
test_bufferevent_filters(void)
{
	test_bufferevent_filters_impl(0);
}

static void
test_bufferevent_pair_filters(void)
{
	test_bufferevent_filters_impl(1);
}

struct testcase_t bufferevent_testcases[] = {

        LEGACY(bufferevent, TT_ISOLATED),
        LEGACY(bufferevent_pair, TT_ISOLATED),
        LEGACY(bufferevent_watermarks, TT_ISOLATED),
        LEGACY(bufferevent_pair_watermarks, TT_ISOLATED),
        LEGACY(bufferevent_filters, TT_ISOLATED),
        LEGACY(bufferevent_pair_filters, TT_ISOLATED),
#ifdef _EVENT_HAVE_LIBZ
        LEGACY(bufferevent_zlib, TT_ISOLATED),
#else
        { "bufferevent_zlib", NULL, TT_SKIP, NULL, NULL },
#endif

        END_OF_TESTCASES,
};
