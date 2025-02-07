/*	$OpenBSD: select.c,v 1.2 2002/06/25 15:50:15 mickey Exp $	*/

/*
 * Copyright 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright 2007-2009 Niels Provos and Nick Mathewson
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
#include "event-config.h"

#include <sys/types.h>
#ifdef _EVENT_HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <sys/_time.h>
#endif
#ifdef _EVENT_HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#undef WIN32_LEAN_AND_MEAN
#endif
#include <sys/queue.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _EVENT_HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <assert.h>

#include "event-internal.h"
#include "evsignal-internal.h"
#include "log-internal.h"
#include "evmap-internal.h"

#ifndef howmany
#define        howmany(x, y)   (((x)+((y)-1))/(y))
#endif

struct selectop {
	int event_fds;		/* Highest fd in fd set */
	int event_fdsz;
	fd_set *event_readset_in;
	fd_set *event_writeset_in;
	fd_set *event_readset_out;
	fd_set *event_writeset_out;
};

static void *select_init	(struct event_base *);
static int select_add(struct event_base *, int, short old, short events, void*);
static int select_del(struct event_base *, int, short old, short events, void*);
static int select_dispatch	(struct event_base *, struct timeval *);
static void select_dealloc     (struct event_base *);

const struct eventop selectops = {
	"select",
	select_init,
	select_add,
	select_del,
	select_dispatch,
	select_dealloc,
	0, /* doesn't need reinit. */
	EV_FEATURE_FDS,
	0,
};

static int select_resize(struct selectop *sop, int fdsz);

static void *
select_init(struct event_base *base)
{
	struct selectop *sop;

	if (!(sop = mm_calloc(1, sizeof(struct selectop))))
		return (NULL);

	select_resize(sop, howmany(32 + 1, NFDBITS)*sizeof(fd_mask));

	evsig_init(base);

	return (sop);
}

#ifdef CHECK_INVARIANTS
static void
check_selectop(struct selectop *sop)
{
	/* nothing to be done here */
}
#else
#define check_selectop(sop) do { (void) sop; } while (0)
#endif

static int
select_dispatch(struct event_base *base, struct timeval *tv)
{
	int res, i;
	struct selectop *sop = base->evbase;

	check_selectop(sop);

	memcpy(sop->event_readset_out, sop->event_readset_in,
	       sop->event_fdsz);
	memcpy(sop->event_writeset_out, sop->event_writeset_in,
	       sop->event_fdsz);

	res = select(sop->event_fds + 1, sop->event_readset_out,
	    sop->event_writeset_out, NULL, tv);

	check_selectop(sop);

	if (res == -1) {
		if (errno != EINTR) {
			event_warn("select");
			return (-1);
		}

		evsig_process(base);
		return (0);
	} else if (base->sig.evsig_caught) {
		evsig_process(base);
	}

	event_debug(("%s: select reports %d", __func__, res));

	check_selectop(sop);
	for (i = 0; i <= sop->event_fds; ++i) {
		res = 0;
		if (FD_ISSET(i, sop->event_readset_out))
			res |= EV_READ;
		if (FD_ISSET(i, sop->event_writeset_out))
			res |= EV_WRITE;

		if (res == 0)
			continue;

		evmap_io_active(base, i, res);
	}
	check_selectop(sop);

	return (0);
}


static int
select_resize(struct selectop *sop, int fdsz)
{
	int n_events, n_events_old;

	fd_set *readset_in = NULL;
	fd_set *writeset_in = NULL;
	fd_set *readset_out = NULL;
	fd_set *writeset_out = NULL;

	n_events = (fdsz/sizeof(fd_mask)) * NFDBITS;
	n_events_old = (sop->event_fdsz/sizeof(fd_mask)) * NFDBITS;

	if (sop->event_readset_in)
		check_selectop(sop);

	if ((readset_in = mm_realloc(sop->event_readset_in, fdsz)) == NULL)
		goto error;
	sop->event_readset_in = readset_in;
	if ((readset_out = mm_realloc(sop->event_readset_out, fdsz)) == NULL)
		goto error;
	sop->event_readset_out = readset_out;
	if ((writeset_in = mm_realloc(sop->event_writeset_in, fdsz)) == NULL)
		goto error;
	sop->event_writeset_in = writeset_in;
	if ((writeset_out = mm_realloc(sop->event_writeset_out, fdsz)) == NULL)
		goto error;
	sop->event_writeset_out = writeset_out;

	memset((char *)sop->event_readset_in + sop->event_fdsz, 0,
	    fdsz - sop->event_fdsz);
	memset((char *)sop->event_writeset_in + sop->event_fdsz, 0,
	    fdsz - sop->event_fdsz);

	sop->event_fdsz = fdsz;
	check_selectop(sop);

	return (0);

 error:
	event_warn("malloc");
	return (-1);
}


static int
select_add(struct event_base *base, int fd, short old, short events, void *p)
{
	struct selectop *sop = base->evbase;
	(void) p;

	assert((events & EV_SIGNAL) == 0);
	check_selectop(sop);
	/*
	 * Keep track of the highest fd, so that we can calculate the size
	 * of the fd_sets for select(2)
	 */
	if (sop->event_fds < fd) {
		int fdsz = sop->event_fdsz;

		if (fdsz < sizeof(fd_mask))
			fdsz = sizeof(fd_mask);

		while (fdsz <
		    (howmany(fd + 1, NFDBITS) * sizeof(fd_mask)))
			fdsz *= 2;

		if (fdsz != sop->event_fdsz) {
			if (select_resize(sop, fdsz)) {
				check_selectop(sop);
				return (-1);
			}
		}

		sop->event_fds = fd;
	}

	if (events & EV_READ)
		FD_SET(fd, sop->event_readset_in);
	if (events & EV_WRITE)
		FD_SET(fd, sop->event_writeset_in);
	check_selectop(sop);

	return (0);
}

/*
 * Nothing to be done here.
 */

static int
select_del(struct event_base *base, int fd, short old, short events, void *p)
{
	struct selectop *sop = base->evbase;
	(void)p;

	assert((events & EV_SIGNAL) == 0);
	check_selectop(sop);

	if (sop->event_fds < fd) {
		check_selectop(sop);
		return (0);
	}

	if (events & EV_READ)
		FD_CLR(fd, sop->event_readset_in);

	if (events & EV_WRITE)
		FD_CLR(fd, sop->event_writeset_in);

	check_selectop(sop);
	return (0);
}

static void
select_dealloc(struct event_base *base)
{
	struct selectop *sop = base->evbase;

	evsig_dealloc(base);
	if (sop->event_readset_in)
		mm_free(sop->event_readset_in);
	if (sop->event_writeset_in)
		mm_free(sop->event_writeset_in);
	if (sop->event_readset_out)
		mm_free(sop->event_readset_out);
	if (sop->event_writeset_out)
		mm_free(sop->event_writeset_out);

	memset(sop, 0, sizeof(struct selectop));
	mm_free(sop);
}
