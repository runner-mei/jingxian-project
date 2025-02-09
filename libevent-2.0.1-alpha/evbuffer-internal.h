/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
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
#ifndef _EVBUFFER_INTERNAL_H_
#define _EVBUFFER_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "event-config.h"
#include "evutil.h"
#include "defer-internal.h"

#include <sys/queue.h>
/* minimum allocation for a chain. */
#define MIN_BUFFER_SIZE	256

/** A single evbuffer callback for an evbuffer. This function will be invoked
 * when bytes are added to or removed from the evbuffer. */
struct evbuffer_cb_entry {
        /** Structures to implement a doubly-linked queue of callbacks */
	TAILQ_ENTRY(evbuffer_cb_entry) next;
        /** The callback function to invoke when this callback is called.
	    If EVBUFFER_CB_OBSOLETE is set in flags, the cb_obsolete field is
	    valid; otherwise, cb_func is valid. */
        union {
                evbuffer_cb_func cb_func;
                evbuffer_cb cb_obsolete;
        } cb;
        /** Argument to pass to cb. */
	void *cbarg;
        /** Currently set flags on this callback. */
	ev_uint32_t flags;
#if 0
        /** Size of the evbuffer before this callback was suspended, or 0
            if this callback is not suspended. */
	size_t size_before_suspend;
#endif
};

struct evbuffer_chain;
struct evbuffer {
	/** The first chain in this buffer's linked list of chains. */
	struct evbuffer_chain *first;
	/** The last chain in this buffer's linked list of chains. */
	struct evbuffer_chain *last;
	/** The next-to-last chain in this buffer's linked list of chains.
	 * NULL if the buffer has 0 or 1 chains.  Used in case there's an
	 * ongoing read that needs to be split across multiple chains: we want
	 * to add a new chain as a read target, but we don't want to lose our
	 * pointer to the next-to-last chain if the read turns out to be
	 * incomplete.
	 */
	struct evbuffer_chain *previous_to_last;

	/** Total amount of bytes stored in all chains.*/
	size_t total_len;

	/** Number of bytes we have added to the buffer since we last tried to
	 * invoke callbacks. */
        size_t n_add_for_cb;
	/** Number of bytes we have removed from the buffer since we last
	 * tried to invoke callbacks. */
        size_t n_del_for_cb;

#ifndef _EVENT_DISABLE_THREAD_SUPPORT
	/** A lock used to mediate access to this buffer. */
        void *lock;
#endif
	/** True iff we should free the lock field when we free this
	 * evbuffer. */
	unsigned own_lock : 1;
	/** True iff we should not allow changes to the front of the buffer
	 * (drains or prepends). */
	unsigned freeze_start : 1;
	/** True iff we should not allow changes to the end of the buffer
	 * (appends) */
	unsigned freeze_end : 1;
	/** True iff this evbuffer's callbacks are not invoked immediately
	 * upon a change in the buffer, but instead are deferred to be invoked
	 * from the event_base's loop.  Useful for preventing enormous stack
	 * overflows when we have mutually recursive callbacks, and for
	 * serializing callbacks in a single thread. */
	unsigned deferred_cbs : 1;
#ifdef WIN32
	/** True iff this buffer is set up for overlapped IO. */
	unsigned is_overlapped : 1;
#endif

	/** An event_base associated with this evbuffer.  Used to implement
	 * deferred callbacks. */
	struct event_base *ev_base;

	/** For debugging: how many times have we acquired the lock for this
	 * evbuffer? */
        int lock_count;
	/** A reference count on this evbuffer.  When the reference count
	 * reaches 0, the buffer is destroyed.  Manipulated with
	 * evbuffer_incref and evbuffer_decref_and_unlock and
	 * evbuffer_free. */
	int refcnt;

	/** A deferred_cb handle to make all of this buffer's callbacks
	 * invoked from the event loop. */
	struct deferred_cb deferred;

	/** A doubly-linked-list of callback functions */
	TAILQ_HEAD(evbuffer_cb_queue, evbuffer_cb_entry) callbacks;
};

/** A single item in an evbuffer. */
struct evbuffer_chain {
	/** points to next buffer in the chain */
	struct evbuffer_chain *next;

	/** total allocation available in the buffer field. */
	size_t buffer_len;

	/** unused space at the beginning of buffer or an offset into a
	 * file for sendfile buffers. */
	off_t misalign;

	/** Offset into buffer + misalign at which to start writing.
	 * In other words, the total number of bytes actually stored
	 * in buffer. */
	size_t off;

	/** Set if special handling is required for this chain */
	unsigned flags;
#define EVBUFFER_MMAP		0x0001  /**< memory in buffer is mmaped */
#define EVBUFFER_SENDFILE	0x0002  /**< a chain used for sendfile */
#define EVBUFFER_REFERENCE	0x0004	/**< a chain with a mem reference */
#define EVBUFFER_IMMUTABLE	0x0008  /**< read-only chain */
	/** a chain that mustn't be reallocated or freed, or have its contents
	 * memmoved, until the chain is un-pinned. */
#define EVBUFFER_MEM_PINNED_R	0x0010
#define EVBUFFER_MEM_PINNED_W	0x0020
#define EVBUFFER_MEM_PINNED_ANY (EVBUFFER_MEM_PINNED_R|EVBUFFER_MEM_PINNED_W)
	/** a chain that should be freed, but can't be freed until it is
	 * un-pinned. */
#define EVBUFFER_DANGLING	0x0040

	/** Usually points to the read-write memory belonging to this
	 * buffer allocated as part of the evbuffer_chain allocation.
	 * For mmap, this can be a read-only buffer and
	 * EVBUFFER_IMMUTABLE will be set in flags.  For sendfile, it
	 * may point to NULL.
	 */
	unsigned char *buffer;
};

/* this is currently used by both mmap and sendfile */
/* TODO(niels): something strange needs to happen for Windows here, I am not
 * sure what that is, but it needs to get looked into.
 */
struct evbuffer_chain_fd {
	int fd;	/**< the fd associated with this chain */
};

/** callback for a reference buffer; lets us know what to do with it when
 * we're done with it. */
struct evbuffer_chain_reference {
	void (*cleanupfn)(void *extra);
	void *extra;
};

#define EVBUFFER_CHAIN_SIZE sizeof(struct evbuffer_chain)
/** Return a pointer to extra data allocated along with an evbuffer. */
#define EVBUFFER_CHAIN_EXTRA(t, c) (t *)((struct evbuffer_chain *)(c) + 1)

/** Assert that somebody (hopefully us) is holding the lock on an evbuffer */
#define ASSERT_EVBUFFER_LOCKED(buffer)                  \
	do {                                            \
		assert((buffer)->lock_count > 0);       \
	} while (0)
/** Assert that nobody is holding the lock on an evbuffer */
#define ASSERT_EVBUFFER_UNLOCKED(buffer)                  \
	do {                                            \
		assert((buffer)->lock_count == 0);	\
	} while (0)
#define _EVBUFFER_INCREMENT_LOCK_COUNT(buffer)                 \
	do {                                                   \
		((struct evbuffer*)(buffer))->lock_count++;    \
	} while (0)
#define _EVBUFFER_DECREMENT_LOCK_COUNT(buffer)		      \
	do {						      \
		ASSERT_EVBUFFER_LOCKED(buffer);		      \
		((struct evbuffer*)(buffer))->lock_count--;   \
	} while (0)

#define EVBUFFER_LOCK(buffer, mode)					\
	do {								\
		EVLOCK_LOCK((buffer)->lock, (mode));			\
		_EVBUFFER_INCREMENT_LOCK_COUNT(buffer);			\
	} while(0)
#define EVBUFFER_UNLOCK(buffer, mode)					\
	do {								\
		_EVBUFFER_DECREMENT_LOCK_COUNT(buffer);			\
		EVLOCK_UNLOCK((buffer)->lock, (mode));			\
	} while(0)

#define EVBUFFER_LOCK2(buffer1, buffer2)				\
	do {								\
		EVLOCK_LOCK2((buffer1)->lock, (buffer2)->lock,		\
		    EVTHREAD_WRITE, EVTHREAD_WRITE);			\
		_EVBUFFER_INCREMENT_LOCK_COUNT(buffer1);		\
		_EVBUFFER_INCREMENT_LOCK_COUNT(buffer2);		\
	} while(0)
#define EVBUFFER_UNLOCK2(buffer1, buffer2)				\
	do {								\
		_EVBUFFER_DECREMENT_LOCK_COUNT(buffer1);		\
		_EVBUFFER_DECREMENT_LOCK_COUNT(buffer2);		\
		EVLOCK_UNLOCK2((buffer1)->lock, (buffer2)->lock,	\
		    EVTHREAD_WRITE, EVTHREAD_WRITE);			\
	} while(0)

/** Increase the reference count of buf by one. */
void _evbuffer_incref(struct evbuffer *buf);
/** Pin a single buffer chain using a given flag. A pinned chunk may not be
 * moved or freed until it is unpinned. */
void _evbuffer_chain_pin(struct evbuffer_chain *chain, unsigned flag);
/** Unpin a single buffer chain using a given flag. */
void _evbuffer_chain_unpin(struct evbuffer_chain *chain, unsigned flag);
/** As evbuffer_free, but requires that we hold a lock on the buffer, and
 * releases the lock before freeing it and the buffer. */
void _evbuffer_decref_and_unlock(struct evbuffer *buffer);
/** As evbuffer_expand, but does not guarantee that the newly allocated memory
 * is contiguous.  Instead, it may be split across two chunks. */
int _evbuffer_expand_fast(struct evbuffer *, size_t);

#ifdef _EVENT_HAVE_SYS_UIO_H
/** Helper: prepares for a readv/WSARecv call by expanding the buffer to
 * hold enough memory to read 'howmuch' bytes in possibly noncontiguous memory.
 * Sets up the one or two iovecs in 'vecs' to point to the free memory and its
 * extent, and *chainp to poitn to the first chain that we'll try to read into.
 * Returns the number of vecs used.
 */
int _evbuffer_read_setup_vecs(struct evbuffer *buf, ssize_t howmuch,
    struct iovec *vecs, struct evbuffer_chain **chainp);
#elif defined(WIN32)
int _evbuffer_read_setup_vecs(struct evbuffer *buf, ssize_t howmuch,
    WSABUF *vecs, struct evbuffer_chain **chainp);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _EVBUFFER_INTERNAL_H_ */
