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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "event2/event.h"
#include "event2/buffer.h"
#include "event2/buffer_compat.h"
#include "event2/util.h"

#include "evbuffer-internal.h"
#include "log-internal.h"

#include "regress.h"

/* Validates that an evbuffer is good. Returns false if it isn't, true if it
 * is*/
static int
_evbuffer_validate(struct evbuffer *buf)
{
	struct evbuffer_chain *chain, *previous = NULL;
	size_t sum = 0;

	if (buf->first == NULL) {
		tt_assert(buf->last == NULL);
		tt_assert(buf->previous_to_last == NULL);
		tt_assert(buf->total_len == 0);
	}

	if (buf->previous_to_last == NULL) {
		tt_assert(buf->first == buf->last);
	}

	chain = buf->first;
	while (chain != NULL) {
		sum += chain->off;
		if (chain->next == NULL) {
			tt_assert(buf->previous_to_last == previous);
			tt_assert(buf->last == chain);
		}
		tt_assert(chain->buffer_len >= chain->misalign + chain->off);
		previous = chain;
		chain = chain->next;
	}

	tt_assert(sum == buf->total_len);
	return 1;
 end:
	return 0;
}

#define evbuffer_validate(buf)			\
	TT_STMT_BEGIN if (!_evbuffer_validate(buf)) goto end; TT_STMT_END

static void
test_evbuffer(void *ptr)
{
	static char buffer[512], *tmp;
	struct evbuffer *evb = evbuffer_new();
	struct evbuffer *evb_two = evbuffer_new();
	size_t sz_tmp;
	int i;

	evbuffer_validate(evb);
	evbuffer_add_printf(evb, "%s/%d", "hello", 1);
	evbuffer_validate(evb);

	tt_assert(evbuffer_get_length(evb) == 7);
	tt_assert(!memcmp((char*)EVBUFFER_DATA(evb), "hello/1", 1));

	evbuffer_add_buffer(evb, evb_two);
	evbuffer_validate(evb);

	evbuffer_drain(evb, strlen("hello/"));
	evbuffer_validate(evb);
	tt_assert(evbuffer_get_length(evb) == 1);
	tt_assert(!memcmp((char*)EVBUFFER_DATA(evb), "1", 1));

	evbuffer_add_printf(evb_two, "%s", "/hello");
	evbuffer_validate(evb);
	evbuffer_add_buffer(evb, evb_two);
	evbuffer_validate(evb);

	tt_assert(evbuffer_get_length(evb_two) == 0);
	tt_assert(evbuffer_get_length(evb) == 7);
	tt_assert(!memcmp((char*)EVBUFFER_DATA(evb), "1/hello", 7) != 0);

	memset(buffer, 0, sizeof(buffer));
	evbuffer_add(evb, buffer, sizeof(buffer));
	evbuffer_validate(evb);
	tt_assert(evbuffer_get_length(evb) == 7 + 512);

	tmp = (char *)evbuffer_pullup(evb, 7 + 512);
	tt_assert(tmp);
	tt_assert(!strncmp(tmp, "1/hello", 7));
	tt_assert(!memcmp(tmp + 7, buffer, sizeof(buffer)));
	evbuffer_validate(evb);

	evbuffer_prepend(evb, "something", 9);
	evbuffer_validate(evb);
	evbuffer_prepend(evb, "else", 4);
	evbuffer_validate(evb);

	tmp = (char *)evbuffer_pullup(evb, 4 + 9 + 7);
	tt_assert(!strncmp(tmp, "elsesomething1/hello", 4 + 9 + 7));
	evbuffer_validate(evb);

	evbuffer_drain(evb, -1);
	evbuffer_validate(evb);
	evbuffer_drain(evb_two, -1);
	evbuffer_validate(evb);

	for (i = 0; i < 3; ++i) {
		evbuffer_add(evb_two, buffer, sizeof(buffer));
		evbuffer_validate(evb_two);
		evbuffer_add_buffer(evb, evb_two);
		evbuffer_validate(evb);
		evbuffer_validate(evb_two);
	}

	tt_assert(evbuffer_get_length(evb_two) == 0);
	tt_assert(evbuffer_get_length(evb) == i * sizeof(buffer));

	/* test remove buffer */
	sz_tmp = sizeof(buffer)*2.5;
	evbuffer_remove_buffer(evb, evb_two, sz_tmp);
	tt_assert(evbuffer_get_length(evb_two) == sz_tmp);
	tt_assert(evbuffer_get_length(evb) == sizeof(buffer) / 2);
	evbuffer_validate(evb);

	if (memcmp(evbuffer_pullup(
			   evb, -1), buffer, sizeof(buffer) / 2) != 0 ||
	    memcmp(evbuffer_pullup(
			   evb_two, -1), buffer, sizeof(buffer) != 0))
		tt_abort_msg("Pullup did not preserve content");

	evbuffer_validate(evb);


	/* testing reserve and commit */
	{
		u_char *buf;
		int i, j;

		for (i = 0; i < 3; ++i) {
			buf = evbuffer_reserve_space(evb, 10000);
			tt_assert(buf != NULL);
			evbuffer_validate(evb);
			for (j = 0; j < 10000; ++j) {
				buf[j] = j;
			}
			evbuffer_validate(evb);

			tt_assert(evbuffer_commit_space(evb, 10000) == 0);
			evbuffer_validate(evb);

			tt_assert(evbuffer_get_length(evb) >= 10000);

			evbuffer_drain(evb, j * 5000);
			evbuffer_validate(evb);
		}
	}

 end:
	evbuffer_free(evb);
	evbuffer_free(evb_two);
}

static int reference_cb_called;
static void
reference_cb(void *extra)
{
	tt_want(extra == (void *)0xdeadaffe);
	++reference_cb_called;
}

static void
test_evbuffer_reference(void *ptr)
{
	struct evbuffer *src = evbuffer_new();
	struct evbuffer *dst = evbuffer_new();
	unsigned char *tmp;
	const char *data = "this is what we add as read-only memory.";
	reference_cb_called = 0;

	tt_assert(evbuffer_add_reference(src, data, strlen(data),
		 reference_cb, (void *)0xdeadaffe) != -1);

	tmp = evbuffer_reserve_space(dst, strlen(data));
	tt_assert(evbuffer_remove(src, tmp, 10) != -1);

	evbuffer_validate(src);
	evbuffer_validate(dst);

	/* make sure that we don't write data at the beginning */
	evbuffer_prepend(src, "aaaaa", 5);
	evbuffer_validate(src);
	evbuffer_drain(src, 5);

	tt_assert(evbuffer_remove(src, tmp + 10, strlen(data) - 10) != -1);

	evbuffer_commit_space(dst, strlen(data));
	evbuffer_validate(src);
	evbuffer_validate(dst);

	tt_int_op(reference_cb_called, ==, 1);

	tt_assert(!memcmp(evbuffer_pullup(dst, strlen(data)),
			  data, strlen(data)));

 end:
	evbuffer_free(dst);
	evbuffer_free(src);
}

#ifndef WIN32
static void
test_evbuffer_add_file(void *ptr)
{
	struct evbuffer *src = evbuffer_new();
	const char *data = "this is what we add as file system data.";
	const char *compare;
	evutil_socket_t fd, pair[2];

	if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == -1)
		tt_abort_msg("socketpair failed");

	fd = regress_make_tmpfile(data, strlen(data));

	tt_assert(fd != -1);

	tt_assert(evbuffer_add_file(src, fd, 0, strlen(data)) != -1);

	evbuffer_validate(src);

	while (evbuffer_write(src, pair[0]) > 0) {
		evbuffer_validate(src);
	}

	tt_assert(evbuffer_read(src, pair[1], strlen(data)) == strlen(data));
	compare = (char *)evbuffer_pullup(src, strlen(data));
	tt_assert(compare != NULL);
	if (memcmp(compare, data, strlen(data)))
		tt_abort_msg("Data from add_file differs.");

 end:
	EVUTIL_CLOSESOCKET(pair[0]);
	EVUTIL_CLOSESOCKET(pair[1]);
	evbuffer_free(src);
}
#endif

static void
test_evbuffer_readln(void *ptr)
{
	struct evbuffer *evb = evbuffer_new();
	struct evbuffer *evb_tmp = evbuffer_new();
	const char *s;
	char *cp = NULL;
	size_t sz;

#define tt_line_eq(content)						\
	TT_STMT_BEGIN							\
	if (!cp || sz != strlen(content) || strcmp(cp, content)) {	\
		TT_DIE(("Wanted %s; got %s [%d]", content, cp, (int)sz)); \
	}								\
	TT_STMT_END

	/* Test EOL_ANY. */
	s = "complex silly newline\r\n\n\r\n\n\rmore\0\n";
	evbuffer_add(evb, s, strlen(s)+2);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_ANY);
	tt_line_eq("complex silly newline");
	free(cp);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_ANY);
	if (!cp || sz != 5 || memcmp(cp, "more\0\0", 6))
		tt_abort_msg("Not as expected");
	tt_uint_op(evbuffer_get_length(evb), ==, 0);
	evbuffer_validate(evb);
	s = "\nno newline";
	evbuffer_add(evb, s, strlen(s));
	free(cp);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_ANY);
	tt_line_eq("");
	free(cp);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_ANY);
	tt_assert(!cp);
	evbuffer_validate(evb);
	evbuffer_drain(evb, evbuffer_get_length(evb));
	tt_assert(evbuffer_get_length(evb) == 0);
	evbuffer_validate(evb);

	/* Test EOL_CRLF */
	s = "Line with\rin the middle\nLine with good crlf\r\n\nfinal\n";
	evbuffer_add(evb, s, strlen(s));
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF);
	tt_line_eq("Line with\rin the middle");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF);
	tt_line_eq("Line with good crlf");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF);
	tt_line_eq("");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF);
	tt_line_eq("final");
	s = "x";
	evbuffer_validate(evb);
	evbuffer_add(evb, s, 1);
	evbuffer_validate(evb);
	free(cp);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF);
	tt_assert(!cp);
	evbuffer_validate(evb);

	/* Test CRLF_STRICT */
	s = " and a bad crlf\nand a good one\r\n\r\nMore\r";
	evbuffer_add(evb, s, strlen(s));
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("x and a bad crlf\nand a good one");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_assert(!cp);
	free(cp);
	evbuffer_validate(evb);
	evbuffer_add(evb, "\n", 1);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("More");
	free(cp);
	tt_assert(evbuffer_get_length(evb) == 0);
	evbuffer_validate(evb);

	/* Test LF */
	s = "An\rand a nl\n\nText";
	evbuffer_add(evb, s, strlen(s));
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_LF);
	tt_line_eq("An\rand a nl");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_LF);
	tt_line_eq("");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_LF);
	tt_assert(!cp);
	free(cp);
	evbuffer_add(evb, "\n", 1);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_LF);
	tt_line_eq("Text");
	free(cp);
	evbuffer_validate(evb);

	/* Test CRLF_STRICT - across boundaries*/
	s = " and a bad crlf\nand a good one\r";
	evbuffer_add(evb_tmp, s, strlen(s));
	evbuffer_validate(evb);
	evbuffer_add_buffer(evb, evb_tmp);
	evbuffer_validate(evb);
	s = "\n\r";
	evbuffer_add(evb_tmp, s, strlen(s));
	evbuffer_validate(evb);
	evbuffer_add_buffer(evb, evb_tmp);
	evbuffer_validate(evb);
	s = "\nMore\r";
	evbuffer_add(evb_tmp, s, strlen(s));
	evbuffer_validate(evb);
	evbuffer_add_buffer(evb, evb_tmp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq(" and a bad crlf\nand a good one");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("");
	free(cp);
	evbuffer_validate(evb);

	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_assert(!cp);
	free(cp);
	evbuffer_validate(evb);
	evbuffer_add(evb, "\n", 1);
	evbuffer_validate(evb);
	cp = evbuffer_readln(evb, &sz, EVBUFFER_EOL_CRLF_STRICT);
	tt_line_eq("More");
	free(cp); cp = NULL;
	evbuffer_validate(evb);
	tt_assert(evbuffer_get_length(evb) == 0);

	test_ok = 1;
 end:
	evbuffer_free(evb);
	evbuffer_free(evb_tmp);
	if (cp) free(cp);
}

static void
test_evbuffer_iterative(void *ptr)
{
	struct evbuffer *buf = evbuffer_new();
	const char *abc = "abcdefghijklmnopqrstvuwxyzabcdefghijklmnopqrstvuwxyzabcdefghijklmnopqrstvuwxyzabcdefghijklmnopqrstvuwxyz";
	int i, j, sum;

	sum = 0;
	for (i = 0; i < 1000; ++i) {
		for (j = 1; j < strlen(abc); ++j) {
			char format[32];

			evutil_snprintf(format, sizeof(format), "%%%d.%ds", j, j);
			evbuffer_add_printf(buf, format, abc);
			evbuffer_validate(buf);

			sum += j;
		}
	}

	tt_uint_op(sum, ==, evbuffer_get_length(buf));

 end:
	evbuffer_free(buf);

}

static void
test_evbuffer_find(void *ptr)
{
	u_char* p;
	const char* test1 = "1234567890\r\n";
	const char* test2 = "1234567890\r";
#define EVBUFFER_INITIAL_LENGTH 256
	char test3[EVBUFFER_INITIAL_LENGTH];
	unsigned int i;
	struct evbuffer * buf = evbuffer_new();

	/* make sure evbuffer_find doesn't match past the end of the buffer */
	evbuffer_add(buf, (u_char*)test1, strlen(test1));
	evbuffer_validate(buf);
	evbuffer_drain(buf, strlen(test1));
	evbuffer_validate(buf);
	evbuffer_add(buf, (u_char*)test2, strlen(test2));
	evbuffer_validate(buf);
	p = evbuffer_find(buf, (u_char*)"\r\n", 2);
	tt_want(p == NULL);

	/*
	 * drain the buffer and do another find; in r309 this would
	 * read past the allocated buffer causing a valgrind error.
	 */
	evbuffer_drain(buf, strlen(test2));
	evbuffer_validate(buf);
	for (i = 0; i < EVBUFFER_INITIAL_LENGTH; ++i)
		test3[i] = 'a';
	test3[EVBUFFER_INITIAL_LENGTH - 1] = 'x';
	evbuffer_add(buf, (u_char *)test3, EVBUFFER_INITIAL_LENGTH);
	evbuffer_validate(buf);
	p = evbuffer_find(buf, (u_char *)"xy", 2);
	tt_want(p == NULL);

	/* simple test for match at end of allocated buffer */
	p = evbuffer_find(buf, (u_char *)"ax", 2);
	tt_assert(p != NULL);
	tt_want(strncmp((char*)p, "ax", 2) == 0);

end:
	if (buf)
		evbuffer_free(buf);
}

static void
test_evbuffer_ptr_set(void *ptr)
{
	struct evbuffer *buf = evbuffer_new();
	struct evbuffer_ptr pos;

	/* create some chains */
	evbuffer_reserve_space(buf, 5000);
	evbuffer_commit_space(buf, 5000);
	evbuffer_reserve_space(buf, 4000);
	evbuffer_commit_space(buf, 4000);
	evbuffer_reserve_space(buf, 3000);
	evbuffer_commit_space(buf, 3000);

	tt_assert(evbuffer_ptr_set(buf, &pos, 13000, EVBUFFER_PTR_SET) == -1);
	tt_assert(pos.pos == -1);
	tt_assert(evbuffer_ptr_set(buf, &pos, 0, EVBUFFER_PTR_SET) == 0);
	tt_assert(pos.pos == 0);
	tt_assert(evbuffer_ptr_set(buf, &pos, 13000, EVBUFFER_PTR_ADD) == -1);

	tt_assert(evbuffer_ptr_set(buf, &pos, 0, EVBUFFER_PTR_SET) == 0);
	tt_assert(pos.pos == 0);
	tt_assert(evbuffer_ptr_set(buf, &pos, 10000, EVBUFFER_PTR_ADD) == 0);
	tt_assert(pos.pos == 10000);
	tt_assert(evbuffer_ptr_set(buf, &pos, 1000, EVBUFFER_PTR_ADD) == 0);
	tt_assert(pos.pos == 11000);
	tt_assert(evbuffer_ptr_set(buf, &pos, 1000, EVBUFFER_PTR_ADD) == -1);
	tt_assert(pos.pos == -1);

end:
	if (buf)
		evbuffer_free(buf);
}

static void
test_evbuffer_search(void *ptr)
{
	struct evbuffer *buf = evbuffer_new();
	struct evbuffer *tmp = evbuffer_new();
	struct evbuffer_ptr pos;

	/* set up our chains */
	evbuffer_add_printf(tmp, "hello");  /* 5 chars */
	evbuffer_add_buffer(buf, tmp);
	evbuffer_add_printf(tmp, "foo");    /* 3 chars */
	evbuffer_add_buffer(buf, tmp);
	evbuffer_add_printf(tmp, "cat");    /* 3 chars */
	evbuffer_add_buffer(buf, tmp);
	evbuffer_add_printf(tmp, "attack");
	evbuffer_add_buffer(buf, tmp);

	pos = evbuffer_search(buf, "attack", 6, NULL);
	tt_int_op(pos.pos, ==, 11);
	pos = evbuffer_search(buf, "attacker", 8, NULL);
	tt_int_op(pos.pos, ==, -1);

	/* test continuing search */
	pos = evbuffer_search(buf, "oc", 2, NULL);
	tt_int_op(pos.pos, ==, 7);
	pos = evbuffer_search(buf, "cat", 3, &pos);
	tt_int_op(pos.pos, ==, 8);
	pos = evbuffer_search(buf, "tacking", 7, &pos);
	tt_int_op(pos.pos, ==, -1);

	evbuffer_ptr_set(buf, &pos, 5, EVBUFFER_PTR_SET);
	pos = evbuffer_search(buf, "foo", 3, &pos);
	tt_int_op(pos.pos, ==, 5);

	evbuffer_ptr_set(buf, &pos, 2, EVBUFFER_PTR_ADD);
	pos = evbuffer_search(buf, "tat", 3, &pos);
	tt_int_op(pos.pos, ==, 10);

end:
	if (buf)
		evbuffer_free(buf);
	if (tmp)
		evbuffer_free(tmp);
}

static void
log_change_callback(struct evbuffer *buffer,
    const struct evbuffer_cb_info *cbinfo,
    void *arg)
{

        size_t old_len = cbinfo->orig_size;
        size_t new_len = old_len + cbinfo->n_added - cbinfo->n_deleted;
	struct evbuffer *out = arg;
	evbuffer_add_printf(out, "%lu->%lu; ", (unsigned long)old_len,
			    (unsigned long)new_len);
}
static void
self_draining_callback(struct evbuffer *evbuffer, size_t old_len,
		       size_t new_len, void *arg)
{
	if (new_len > old_len)
		evbuffer_drain(evbuffer, new_len);
}

static void
test_evbuffer_callbacks(void *ptr)
{
	struct evbuffer *buf = evbuffer_new();
	struct evbuffer *buf_out1 = evbuffer_new();
	struct evbuffer *buf_out2 = evbuffer_new();
	struct evbuffer_cb_entry *cb1, *cb2;

	cb1 = evbuffer_add_cb(buf, log_change_callback, buf_out1);
	cb2 = evbuffer_add_cb(buf, log_change_callback, buf_out2);

	/* Let's run through adding and deleting some stuff from the buffer
	 * and turning the callbacks on and off and removing them.  The callback
	 * adds a summary of length changes to buf_out1/buf_out2 when called. */
	/* size: 0-> 36. */
	evbuffer_add_printf(buf, "The %d magic words are spotty pudding", 2);
	evbuffer_cb_set_flags(buf, cb2, 0);
	evbuffer_drain(buf, 10); /*36->26*/
	evbuffer_prepend(buf, "Hello", 5);/*26->31*/
	evbuffer_cb_set_flags(buf, cb2, EVBUFFER_CB_ENABLED);
	evbuffer_add_reference(buf, "Goodbye", 7, NULL, NULL); /*31->38*/
	evbuffer_remove_cb_entry(buf, cb1);
	evbuffer_drain(buf, evbuffer_get_length(buf)); /*38->0*/;
	tt_assert(-1 == evbuffer_remove_cb(buf, log_change_callback, NULL));
	evbuffer_add(buf, "X", 1); /* 0->1 */
	tt_assert(!evbuffer_remove_cb(buf, log_change_callback, buf_out2));

	tt_str_op(evbuffer_pullup(buf_out1, -1), ==,
		  "0->36; 36->26; 26->31; 31->38; ");
	tt_str_op(evbuffer_pullup(buf_out2, -1), ==,
		  "0->36; 31->38; 38->0; 0->1; ");
	evbuffer_drain(buf_out1, evbuffer_get_length(buf_out1));
	evbuffer_drain(buf_out2, evbuffer_get_length(buf_out2));

	/* Let's test the obsolete buffer_setcb function too. */
	cb1 = evbuffer_add_cb(buf, log_change_callback, buf_out1);
	cb2 = evbuffer_add_cb(buf, log_change_callback, buf_out2);
	evbuffer_setcb(buf, self_draining_callback, NULL);
	evbuffer_add_printf(buf, "This should get drained right away.");
	tt_uint_op(evbuffer_get_length(buf), ==, 0);
	tt_uint_op(evbuffer_get_length(buf_out1), ==, 0);
	tt_uint_op(evbuffer_get_length(buf_out2), ==, 0);
	evbuffer_setcb(buf, NULL, NULL);
	evbuffer_add_printf(buf, "This will not.");
	tt_str_op(evbuffer_pullup(buf, -1), ==, "This will not.");

	evbuffer_drain(buf, evbuffer_get_length(buf));

#if 0
	/* Now let's try a suspended callback. */
	cb1 = evbuffer_add_cb(buf, log_change_callback, buf_out1);
	cb2 = evbuffer_add_cb(buf, log_change_callback, buf_out2);
	evbuffer_cb_suspend(buf,cb2);
	evbuffer_prepend(buf,"Hello world",11); /*0->11*/
	evbuffer_validate(buf);
	evbuffer_cb_suspend(buf,cb1);
	evbuffer_add(buf,"more",4); /* 11->15 */
	evbuffer_cb_unsuspend(buf,cb2);
	evbuffer_drain(buf, 4); /* 15->11 */
	evbuffer_cb_unsuspend(buf,cb1);
	evbuffer_drain(buf, evbuffer_get_length(buf)); /* 11->0 */

	tt_str_op(evbuffer_pullup(buf_out1, -1), ==,
		  "0->11; 11->11; 11->0; ");
	tt_str_op(evbuffer_pullup(buf_out2, -1), ==,
		  "0->15; 15->11; 11->0; ");
#endif

 end:
	if (buf)
		evbuffer_free(buf);
	if (buf_out1)
		evbuffer_free(buf_out1);
	if (buf_out2)
		evbuffer_free(buf_out2);
}

static int ref_done_cb_called_count = 0;
static void *ref_done_cb_called_with = NULL;
static void ref_done_cb(void *data)
{
	++ref_done_cb_called_count;
	ref_done_cb_called_with = data;
}

static void
test_evbuffer_add_reference(void *ptr)
{
	const char chunk1[] = "If you have found the answer to such a problem";
	const char chunk2[] = "you ought to write it up for publication";
			  /* -- Knuth's "Notes on the Exercises" from TAOCP */
	char tmp[16];
	size_t len1 = strlen(chunk1), len2=strlen(chunk2);

	struct evbuffer *buf1 = NULL, *buf2 = NULL;

	buf1 = evbuffer_new();
	tt_assert(buf1);

	evbuffer_add_reference(buf1, chunk1, len1, ref_done_cb, (void*)111);
	evbuffer_add(buf1, ", ", 2);
	evbuffer_add_reference(buf1, chunk2, len2, ref_done_cb, (void*)222);
	tt_int_op(evbuffer_get_length(buf1), ==, len1+len2+2);

	/* Make sure we can drain a little from a reference. */
	tt_int_op(evbuffer_remove(buf1, tmp, 6), ==, 6);
	tt_int_op(memcmp(tmp, "If you", 6), ==, 0);
	tt_int_op(evbuffer_remove(buf1, tmp, 5), ==, 5);
	tt_int_op(memcmp(tmp, " have", 5), ==, 0);

	/* Make sure that prepending does not meddle with immutable data */
	tt_int_op(evbuffer_prepend(buf1, "I have ", 7), ==, 0);
	tt_int_op(memcmp(chunk1, "If you", 6), ==, 0);

	/* Make sure that when the chunk is over, the callback is invoked. */
	evbuffer_drain(buf1, 7); /* Remove prepended stuff. */
	evbuffer_drain(buf1, len1-11-1); /* remove all but one byte of chunk1 */
	tt_int_op(ref_done_cb_called_count, ==, 0);
	evbuffer_remove(buf1, tmp, 1);
	tt_int_op(tmp[0], ==, 'm');
	tt_assert(ref_done_cb_called_with == (void*)111);
	tt_int_op(ref_done_cb_called_count, ==, 1);

	/* Drain some of the remaining chunk, then add it to another buffer */
	evbuffer_drain(buf1, 6); /* Remove the ", you ". */
	buf2 = evbuffer_new();
	tt_assert(buf2);
	tt_int_op(ref_done_cb_called_count, ==, 1);
	evbuffer_add(buf2, "I ", 2);

	evbuffer_add_buffer(buf2, buf1);
	tt_int_op(ref_done_cb_called_count, ==, 1);
	evbuffer_remove(buf2, tmp, 16);
	tt_int_op(memcmp("I ought to write", tmp, 16), ==, 0);
	evbuffer_drain(buf2, evbuffer_get_length(buf2));
	tt_int_op(ref_done_cb_called_count, ==, 2);
	tt_assert(ref_done_cb_called_with == (void*)222);

	/* Now add more stuff to buf1 and make sure that it gets removed on
	 * free. */
	evbuffer_add(buf1, "You shake and shake the ", 24);
	evbuffer_add_reference(buf1, "ketchup bottle", 14, ref_done_cb,
	    (void*)3333);
	evbuffer_add(buf1, ". Nothing comes and then a lot'll.", 42);
	evbuffer_free(buf1);
	buf1 = NULL;
	tt_int_op(ref_done_cb_called_count, ==, 3);
	tt_assert(ref_done_cb_called_with == (void*)3333);

end:
	if (buf1)
		evbuffer_free(buf1);
	if (buf2)
		evbuffer_free(buf2);
}

/* Some cases that we didn't get in test_evbuffer() above, for more coverage. */
static void
test_evbuffer_prepend(void *ptr)
{
	struct evbuffer *buf1 = NULL, *buf2 = NULL;
	char tmp[128];
	int n;

	buf1 = evbuffer_new();
	tt_assert(buf1);

	evbuffer_add(buf1, "This string has 29 characters", 29);

	/* Case 1: Prepend goes entirely in new chunk. */
	evbuffer_prepend(buf1, "Short.", 6);
	evbuffer_validate(buf1);

	/* Case 2: prepend goes entirely in first chunk. */
	evbuffer_drain(buf1, 6+11);
	evbuffer_prepend(buf1, "it", 2);
	evbuffer_validate(buf1);
	tt_assert(!memcmp(buf1->first->buffer+buf1->first->misalign,
		"it has", 6));

	/* Case 3: prepend is split over multiple chunks. */
	evbuffer_prepend(buf1, "It is no longer true to say ", 28);
	evbuffer_validate(buf1);
	n = evbuffer_remove(buf1, tmp, sizeof(tmp)-1);
	tmp[n]='\0';
	tt_str_op(tmp,==,"It is no longer true to say it has 29 characters");

	buf2 = evbuffer_new();
	tt_assert(buf2);

	/* Case 4: prepend a buffer to an empty buffer. */
	n = 999;
	evbuffer_add_printf(buf1, "Here is string %d. ", n++);
	evbuffer_prepend_buffer(buf2, buf1);
	evbuffer_validate(buf2);

	/* Case 5: prepend a buffer to a nonempty buffer. */
	evbuffer_add_printf(buf1, "Here is string %d. ", n++);
	evbuffer_prepend_buffer(buf2, buf1);
	evbuffer_validate(buf2);
	n = evbuffer_remove(buf2, tmp, sizeof(tmp)-1);
	tmp[n]='\0';
	tt_str_op(tmp,==,"Here is string 1000. Here is string 999. ");

end:
	if (buf1)
		evbuffer_free(buf1);
	if (buf2)
		evbuffer_free(buf2);

}

/* Check whether evbuffer freezing works right.  This is called twice,
   once with the argument "start" and once with the argument "end".
   When we test "start", we freeze the start of an evbuffer and make sure
   that modifying the start of the buffer doesn't work.  When we test
   "end", we freeze the end of an evbuffer and make sure that modifying
   the end of the buffer doesn't work.
 */
static void
test_evbuffer_freeze(void *ptr)
{
	struct evbuffer *buf = NULL, *tmp_buf=NULL;
	const char string[] = /* Year's End, Richard Wilbur */
	    "I've known the wind by water banks to shake\n"
	    "The late leaves down, which frozen where they fell\n"
	    "And held in ice as dancers in a spell\n"
	    "Fluttered all winter long into a lake...";
	const int start = !strcmp(ptr, "start");
	char *cp;
	char charbuf[128];
	int r;
	size_t orig_length;

	if (!start)
		tt_str_op(ptr, ==, "end");

	buf = evbuffer_new();
	tmp_buf = evbuffer_new();
	tt_assert(tmp_buf);

	evbuffer_add(buf, string, strlen(string));
	evbuffer_freeze(buf, start); /* Freeze the start or the end.*/

#define FREEZE_EQ(a, startcase, endcase)		\
	do {						\
	    if (start) {				\
		    tt_int_op((a), ==, (startcase));	\
	    } else {					\
		    tt_int_op((a), ==, (endcase));	\
	    }						\
	} while (0)


	orig_length = evbuffer_get_length(buf);

	/* These functions all manipulate the end of buf. */
	r = evbuffer_add(buf, "abc", 0);
	FREEZE_EQ(r, 0, -1);
	cp = (char*)evbuffer_reserve_space(buf, 10);
	FREEZE_EQ(cp==NULL, 0, 1);
	if (cp)
		memset(cp, 'X', 10);
	r = evbuffer_commit_space(buf, 10);
	FREEZE_EQ(r, 0, -1);
	r = evbuffer_add_reference(buf, string, 5, NULL, NULL);
	FREEZE_EQ(r, 0, -1);
	r = evbuffer_add_printf(buf, "Hello %s", "world");
	FREEZE_EQ(r, 11, -1);
	// TODO: test add_buffer, add_file, read

	if (!start)
		tt_int_op(orig_length, ==, evbuffer_get_length(buf));

	orig_length = evbuffer_get_length(buf);

	/* These functions all manipulate the start of buf. */
	r = evbuffer_remove(buf, charbuf, 1);
	FREEZE_EQ(r, -1, 1);
	r = evbuffer_drain(buf, 3);
	FREEZE_EQ(r, -1, 0);
	r = evbuffer_prepend(buf, "dummy", 5);
	FREEZE_EQ(r, -1, 0);
	cp = evbuffer_readln(buf, NULL, EVBUFFER_EOL_LF);
	FREEZE_EQ(cp==NULL, 1, 0);
	if (cp)
		free(cp);
	// TODO: Test remove_buffer, add_buffer, write, prepend_buffer

	if (start)
		tt_int_op(orig_length, ==, evbuffer_get_length(buf));

end:
	if (buf)
		evbuffer_free(buf);

	if (tmp_buf)
		evbuffer_free(tmp_buf);
}

static void *
setup_passthrough(const struct testcase_t *testcase)
{
	return testcase->setup_data;
}
static int
cleanup_passthrough(const struct testcase_t *testcase, void *ptr)
{
	(void) ptr;
	return 1;
}

static const struct testcase_setup_t nil_setup = {
	setup_passthrough,
	cleanup_passthrough
};

struct testcase_t evbuffer_testcases[] = {
	{ "evbuffer", test_evbuffer, 0, NULL, NULL },
	{ "reference", test_evbuffer_reference, 0, NULL, NULL },
	{ "iterative", test_evbuffer_iterative, 0, NULL, NULL },
	{ "readln", test_evbuffer_readln, 0, NULL, NULL },
	{ "find", test_evbuffer_find, 0, NULL, NULL },
	{ "ptr_set", test_evbuffer_ptr_set, 0, NULL, NULL },
	{ "search", test_evbuffer_search, 0, NULL, NULL },
	{ "callbacks", test_evbuffer_callbacks, 0, NULL, NULL },
	{ "add_reference", test_evbuffer_add_reference, 0, NULL, NULL },
	{ "prepend", test_evbuffer_prepend, 0, NULL, NULL },
	{ "freeze_start", test_evbuffer_freeze, 0, &nil_setup, (void*)"start" },
	{ "freeze_end", test_evbuffer_freeze, 0, &nil_setup, (void*)"end" },
#ifndef WIN32
	/* TODO: need a temp file implementation for Windows */
	{ "add_file", test_evbuffer_add_file, 0, NULL, NULL },
#endif

	END_OF_TESTCASES
};
