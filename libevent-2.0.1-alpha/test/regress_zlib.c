/*
 * Copyright (c) 2008-2009 Niels Provos and Nick Mathewson
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
#ifndef WIN32
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <unistd.h>
#include <netdb.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>
#include <assert.h>
#include <errno.h>

#include "event2/util.h"
#include "event2/event.h"
#include "event2/event_compat.h"
#include "event2/buffer.h"
#include "event2/bufferevent.h"

#include "regress.h"

static int infilter_calls;
static int outfilter_calls;
static int readcb_finished;
static int writecb_finished;
static int errorcb_invoked;

/*
 * Zlib filters
 */

static void
zlib_deflate_free(void *ctx)
{
	z_streamp p = ctx;

	assert(deflateEnd(p) == Z_OK);
}

static void
zlib_inflate_free(void *ctx)
{
	z_streamp p = ctx;

	assert(inflateEnd(p) == Z_OK);
}

static int
getstate(enum bufferevent_flush_mode state)
{
	switch (state) {
	case BEV_FINISHED:
		return Z_FINISH;
	case BEV_FLUSH:
		return Z_SYNC_FLUSH;
	case BEV_NORMAL:
	default:
		return Z_NO_FLUSH;
	}
}

/*
 * The input filter is triggered only on new input read from the network.
 * That means all input data needs to be consumed or the filter needs to
 * initiate its own triggering via a timeout.
 */
static enum bufferevent_filter_result
zlib_input_filter(struct evbuffer *src, struct evbuffer *dst,
    ssize_t lim, enum bufferevent_flush_mode state, void *ctx)
{
	int nread, nwrite;
	int res;

	z_streamp p = ctx;

	do {
		/* let's do some decompression */
		p->avail_in = evbuffer_get_contiguous_space(src);
		p->next_in = evbuffer_pullup(src, p->avail_in);

		p->next_out = evbuffer_reserve_space(dst, 4096);
		p->avail_out = 4096;

		/* we need to flush zlib if we got a flush */
		res = inflate(p, getstate(state));

		/* let's figure out how much was compressed */
		nread = evbuffer_get_contiguous_space(src) - p->avail_in;
		nwrite = 4096 - p->avail_out;

		evbuffer_drain(src, nread);
		evbuffer_commit_space(dst, nwrite);

		if (res==Z_BUF_ERROR) {
			/* We're out of space, or out of decodeable input.
			   Only if nwrite == 0 assume the latter.
			 */
			if (nwrite == 0)
				return BEV_NEED_MORE;
		} else {
			assert(res == Z_OK || res == Z_STREAM_END);
		}

	} while (evbuffer_get_length(src) > 0);

        ++infilter_calls;

	return (BEV_OK);
}

static enum bufferevent_filter_result
zlib_output_filter(struct evbuffer *src, struct evbuffer *dst,
    ssize_t lim, enum bufferevent_flush_mode state, void *ctx)
{
	int nread, nwrite;
	int res;

	z_streamp p = ctx;

	do {
		/* let's do some compression */
		p->avail_in = evbuffer_get_contiguous_space(src);
		p->next_in = evbuffer_pullup(src, p->avail_in);

		p->next_out = evbuffer_reserve_space(dst, 4096);
		p->avail_out = 4096;


		/* we need to flush zlib if we got a flush */
		res = deflate(p, getstate(state));

		/* let's figure out how much was compressed */
		nread = evbuffer_get_contiguous_space(src) - p->avail_in;
		nwrite = 4096 - p->avail_out;

		evbuffer_drain(src, nread);
		evbuffer_commit_space(dst, nwrite);

		if (res==Z_BUF_ERROR) {
			/* We're out of space, or out of decodeable input.
			   Only if nwrite == 0 assume the latter.
			 */
			if (nwrite == 0)
				return BEV_NEED_MORE;
		} else {
			assert(res == Z_OK || res == Z_STREAM_END);
		}

	} while (evbuffer_get_length(src) > 0);

	++outfilter_calls;

	return (BEV_OK);
}

/*
 * simple bufferevent test (over transparent zlib treatment)
 */

static void
readcb(struct bufferevent *bev, void *arg)
{
	if (evbuffer_get_length(bufferevent_get_input(bev)) == 8333) {
		struct evbuffer *evbuf = evbuffer_new();
		assert(evbuf != NULL);

		/* gratuitous test of bufferevent_read_buffer */
		bufferevent_read_buffer(bev, evbuf);

		bufferevent_disable(bev, EV_READ);

		if (evbuffer_get_length(evbuf) == 8333) {
			++readcb_finished;
                }

		evbuffer_free(evbuf);
	}
}

static void
writecb(struct bufferevent *bev, void *arg)
{
	if (evbuffer_get_length(bufferevent_get_output(bev)) == 0) {
		++writecb_finished;
        }
}

static void
errorcb(struct bufferevent *bev, short what, void *arg)
{
	errorcb_invoked = 1;
}

void
test_bufferevent_zlib(void *arg)
{
	struct bufferevent *bev1=NULL, *bev2=NULL, *bev1_orig, *bev2_orig;
	char buffer[8333];
	z_stream z_input, z_output;
	int i, pair[2]={-1,-1}, r;
        (void)arg;

	infilter_calls = outfilter_calls = readcb_finished = writecb_finished
            = errorcb_invoked = 0;

	if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1) {
		tt_fail_perror("socketpair");
	}

	evutil_make_socket_nonblocking(pair[0]);
	evutil_make_socket_nonblocking(pair[1]);

	bev1_orig = bev1 = bufferevent_socket_new(NULL, pair[0], 0);
	bev2_orig = bev2 = bufferevent_socket_new(NULL, pair[1], 0);

	memset(&z_output, 0, sizeof(z_output));
	r = deflateInit(&z_output, Z_DEFAULT_COMPRESSION);
	tt_int_op(r, ==, Z_OK);
	memset(&z_input, 0, sizeof(z_input));
	r = inflateInit(&z_input);

	/* initialize filters */
	bev1 = bufferevent_filter_new(bev1, NULL, zlib_output_filter, 0,
	    zlib_deflate_free, &z_output);
	bev2 = bufferevent_filter_new(bev2, zlib_input_filter,
	    NULL, 0, zlib_inflate_free, &z_input);
	bufferevent_setcb(bev1, readcb, writecb, errorcb, NULL);
	bufferevent_setcb(bev2, readcb, writecb, errorcb, NULL);

	bufferevent_disable(bev1, EV_READ);
	bufferevent_enable(bev1, EV_WRITE);

	bufferevent_enable(bev2, EV_READ);

	for (i = 0; i < sizeof(buffer); i++)
		buffer[i] = i;

	/* break it up into multiple buffer chains */
	bufferevent_write(bev1, buffer, 1800);
	bufferevent_write(bev1, buffer + 1800, sizeof(buffer) - 1800);

	/* we are done writing - we need to flush everything */
	bufferevent_flush(bev1, EV_WRITE, BEV_FINISHED);

	event_dispatch();

        tt_want(infilter_calls);
        tt_want(outfilter_calls);
        tt_want(readcb_finished);
        tt_want(writecb_finished);
        tt_want(!errorcb_invoked);

        test_ok = 1;
end:
        if (bev1)
                bufferevent_free(bev1);
        if (bev2)
		bufferevent_free(bev2);

	if (pair[0] >= 0)
		EVUTIL_CLOSESOCKET(pair[0]);
	if (pair[1] >= 0)
		EVUTIL_CLOSESOCKET(pair[1]);
}
