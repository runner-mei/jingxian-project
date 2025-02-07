/*
 * Copyright (c) 2002-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2009 Niels Provos, Nick Mathewson
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

#ifdef _EVENT_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _EVENT_HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef WIN32
#include <winsock2.h>
#endif

#include "event2/util.h"
#include "event2/bufferevent.h"
#include "event2/buffer.h"
#include "event2/buffer_compat.h"
#include "event2/bufferevent_struct.h"
#include "event2/bufferevent_compat.h"
#include "event2/event.h"
#include "log-internal.h"
#include "mm-internal.h"
#include "bufferevent-internal.h"
#include "util-internal.h"

void
bufferevent_wm_suspend_read(struct bufferevent *bufev)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);
	BEV_LOCK(bufev);
	if (!bufev_private->read_suspended) {
		bufev->be_ops->disable(bufev, EV_READ);
		bufev_private->read_suspended = 1;
	}
	BEV_LOCK(bufev);
}

void
bufferevent_wm_unsuspend_read(struct bufferevent *bufev)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);

	BEV_LOCK(bufev);
	if (bufev_private->read_suspended) {
		bufev_private->read_suspended = 0;
		if (bufev->enabled & EV_READ)
			bufev->be_ops->enable(bufev, EV_READ);
	}
	BEV_LOCK(bufev);
}

/* Callback to implement watermarks on the input buffer.  Only enabled
 * if the watermark is set. */
static void
bufferevent_inbuf_wm_cb(struct evbuffer *buf,
    const struct evbuffer_cb_info *cbinfo,
    void *arg)
{
	struct bufferevent *bufev = arg;
        size_t size;

	size = evbuffer_get_length(buf);

	if (cbinfo->n_added > cbinfo->n_deleted) {
		/* Data got added.  If it put us over the watermark, stop
		 * reading. */
		if (size >= bufev->wm_read.high)
			bufferevent_wm_suspend_read(bufev);
	} else {
		/* Data got removed.  If it puts us under the watermark,
		   stop reading. */
		if (size < bufev->wm_read.high)
			bufferevent_wm_unsuspend_read(bufev);
	}
}

int
bufferevent_init_common(struct bufferevent_private *bufev_private,
    struct event_base *base,
    const struct bufferevent_ops *ops,
    enum bufferevent_options options)
{
	struct bufferevent *bufev = &bufev_private->bev;

	if ((bufev->input = evbuffer_new()) == NULL)
		return -1;

	if ((bufev->output = evbuffer_new()) == NULL) {
		evbuffer_free(bufev->input);
		return -1;
	}

	bufev_private->refcnt = 1;
	bufev->ev_base = base;

	/* Disable timeouts. */
	evutil_timerclear(&bufev->timeout_read);
	evutil_timerclear(&bufev->timeout_write);

	bufev->be_ops = ops;

	/*
	 * Set to EV_WRITE so that using bufferevent_write is going to
	 * trigger a callback.  Reading needs to be explicitly enabled
	 * because otherwise no data will be available.
	 */
	bufev->enabled = EV_WRITE;

#ifndef _EVENT_DISABLE_THREAD_SUPPORT
	if (options & BEV_OPT_THREADSAFE) {
		if (bufferevent_enable_locking(bufev, NULL) < 0) {
			/* cleanup */
			return -1;
		}
	}
#endif

	bufev_private->options = options;

	return 0;
}

void
bufferevent_setcb(struct bufferevent *bufev,
    evbuffercb readcb, evbuffercb writecb, everrorcb errorcb, void *cbarg)
{
	BEV_LOCK(bufev);

	bufev->readcb = readcb;
	bufev->writecb = writecb;
	bufev->errorcb = errorcb;

	bufev->cbarg = cbarg;
	BEV_UNLOCK(bufev);
}

struct evbuffer *
bufferevent_get_input(struct bufferevent *bufev)
{
	return bufev->input;
}

struct evbuffer *
bufferevent_get_output(struct bufferevent *bufev)
{
	return bufev->output;
}

/*
 * Returns 0 on success;
 *        -1 on failure.
 */

int
bufferevent_write(struct bufferevent *bufev, const void *data, size_t size)
{
	if (evbuffer_add(bufev->output, data, size) == -1)
		return (-1);

	return 0;
}

int
bufferevent_write_buffer(struct bufferevent *bufev, struct evbuffer *buf)
{
	if (evbuffer_add_buffer(bufev->output, buf) == -1)
		return (-1);

	return 0;
}

size_t
bufferevent_read(struct bufferevent *bufev, void *data, size_t size)
{
	return (evbuffer_remove(bufev->input, data, size));
}

int
bufferevent_read_buffer(struct bufferevent *bufev, struct evbuffer *buf)
{
	return (evbuffer_add_buffer(buf, bufev->input));
}

int
bufferevent_enable(struct bufferevent *bufev, short event)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);
	short impl_events = event;
	int r = 0;

	BEV_LOCK(bufev);
	if (bufev_private->read_suspended)
		impl_events &= ~EV_READ;

	bufev->enabled |= event;

	if (bufev->be_ops->enable(bufev, impl_events) < 0)
		r = -1;

	BEV_UNLOCK(bufev);
	return r;
}

void
bufferevent_set_timeouts(struct bufferevent *bufev,
			 const struct timeval *tv_read,
			 const struct timeval *tv_write)
{
	BEV_LOCK(bufev);
	if (tv_read) {
		bufev->timeout_read = *tv_read;
	} else {
		evutil_timerclear(&bufev->timeout_read);
	}
	if (tv_write) {
		bufev->timeout_write = *tv_write;
	} else {
		evutil_timerclear(&bufev->timeout_write);
	}

	if (bufev->be_ops->adj_timeouts)
		bufev->be_ops->adj_timeouts(bufev);
	BEV_UNLOCK(bufev);
}


/* Obsolete; use bufferevent_set_timeouts */
void
bufferevent_settimeout(struct bufferevent *bufev,
		       int timeout_read, int timeout_write)
{
	struct timeval tv_read, tv_write;
	struct timeval *ptv_read = NULL, *ptv_write = NULL;

	memset(&tv_read, 0, sizeof(tv_read));
	memset(&tv_write, 0, sizeof(tv_write));

	if (timeout_read) {
		tv_read.tv_sec = timeout_read;
		ptv_read = &tv_read;
	}
	if (timeout_write) {
		tv_write.tv_sec = timeout_write;
		ptv_write = &tv_write;
	}

	bufferevent_set_timeouts(bufev, ptv_read, ptv_write);
}


int
bufferevent_disable(struct bufferevent *bufev, short event)
{
	int r = 0;

	BEV_LOCK(bufev);
	bufev->enabled &= ~event;

	if (bufev->be_ops->disable(bufev, event) < 0)
		r = -1;

	BEV_UNLOCK(bufev);
	return r;
}

/*
 * Sets the water marks
 */

void
bufferevent_setwatermark(struct bufferevent *bufev, short events,
    size_t lowmark, size_t highmark)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);

	BEV_LOCK(bufev);
	if (events & EV_WRITE) {
		bufev->wm_write.low = lowmark;
		bufev->wm_write.high = highmark;
	}

	if (events & EV_READ) {
		bufev->wm_read.low = lowmark;
		bufev->wm_read.high = highmark;

		if (highmark) {
			/* There is now a new high-water mark for read.
			   enable the callback if needed, and see if we should
			   suspend/bufferevent_wm_unsuspend. */

			if (bufev_private->read_watermarks_cb == NULL) {
				bufev_private->read_watermarks_cb =
				    evbuffer_add_cb(bufev->input,
						    bufferevent_inbuf_wm_cb,
						    bufev);
			}
			evbuffer_cb_set_flags(bufev->input,
					      bufev_private->read_watermarks_cb,
					      EVBUFFER_CB_ENABLED);

			if (evbuffer_get_length(bufev->input) > highmark)
				bufferevent_wm_suspend_read(bufev);
			else if (evbuffer_get_length(bufev->input) < highmark)
				bufferevent_wm_unsuspend_read(bufev);
		} else {
			/* There is now no high-water mark for read. */
			if (bufev_private->read_watermarks_cb)
				evbuffer_cb_set_flags(bufev->input,
				    bufev_private->read_watermarks_cb,
				    EVBUFFER_CB_DISABLED);
			bufferevent_wm_unsuspend_read(bufev);
		}
	}
	BEV_UNLOCK(bufev);
}

int
bufferevent_flush(struct bufferevent *bufev,
    short iotype,
    enum bufferevent_flush_mode mode)
{
	int r = -1;
	BEV_LOCK(bufev);
        if (bufev->be_ops->flush)
                r = bufev->be_ops->flush(bufev, iotype, mode);
	BEV_UNLOCK(bufev);
	return r;
}

void
_bufferevent_decref_and_unlock(struct bufferevent *bufev)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);

	if (--bufev_private->refcnt) {
		BEV_UNLOCK(bufev);
		return;
	}

	/* Clean up the shared info */
	if (bufev->be_ops->destruct)
		bufev->be_ops->destruct(bufev);

	/* evbuffer will free the callbacks */
	evbuffer_free(bufev->input);
	evbuffer_free(bufev->output);

	BEV_UNLOCK(bufev);
	if (bufev_private->own_lock)
		EVTHREAD_FREE_LOCK(bufev_private->lock);

	/* Free the actual allocated memory. */
	mm_free(bufev - bufev->be_ops->mem_offset);
}

void
bufferevent_free(struct bufferevent *bufev)
{
	BEV_LOCK(bufev);
	_bufferevent_decref_and_unlock(bufev);
}

void
bufferevent_incref(struct bufferevent *bufev)
{
	struct bufferevent_private *bufev_private =
	    EVUTIL_UPCAST(bufev, struct bufferevent_private, bev);

	BEV_LOCK(bufev);
	++bufev_private->refcnt;
	BEV_UNLOCK(bufev);
}

int
bufferevent_enable_locking(struct bufferevent *bufev, void *lock)
{
#ifdef _EVENT_DISABLE_THREAD_SUPPORT
	return -1;
#else
	if (BEV_UPCAST(bufev)->lock)
		return -1;

	if (!lock) {
		EVTHREAD_ALLOC_LOCK(lock);
		if (!lock)
			return -1;
		BEV_UPCAST(bufev)->lock = lock;
		BEV_UPCAST(bufev)->own_lock = 1;
	} else {
		BEV_UPCAST(bufev)->lock = lock;
		BEV_UPCAST(bufev)->own_lock = 0;
	}
	evbuffer_enable_locking(bufev->input, lock);
	evbuffer_enable_locking(bufev->output, lock);

	return 0;
#endif
}

