/*
 * Copyright (c) 2009 Niels Provos and Nick Mathewson
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

#ifndef _EVENT_IOCP_INTERNAL_H
#define _EVENT_IOCP_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

struct event_overlapped;
struct event_iocp_port;
struct evbuffer;
typedef void (*iocp_callback)(struct event_overlapped *, uintptr_t, ssize_t);

/* This whole file is actually win32 only. We wrap the structures in a win32
 * ifdef so that we can test-compile code that uses these interfaces on
 * non-win32 platforms. */
#ifdef WIN32

/**
   Internal use only.  Wraps an OVERLAPPED that we're using for libevent
   functionality.  Whenever an event_iocp_port gets an event for a given
   OVERLAPPED*, it upcasts the pointer to an event_overlapped, and calls the
   iocp_callback function with the event_overlapped, the iocp key, and the
   number of bytes transferred as arguments.
 */
struct event_overlapped {
	OVERLAPPED overlapped;
	iocp_callback cb;
};

/**
    Internal use only. Stores a Windows IO Completion port, along with
    related data.
 */
struct event_iocp_port {
	/** The port itself */
	HANDLE port;
	/** Number of threads open on the port. */
	int n_threads;
	/** True iff we're shutting down all the threads on this port */
	int shutdown;
	/** How often the threads on this port check for shutdown and other
	 * conditions */
	long ms;
};
#endif

/** Initialize the fields in an event_overlapped.

    @param overlapped The struct event_overlapped to initialize
    @param cb The callback that should be invoked once the IO operation has
        finished.
 */
void event_overlapped_init(struct event_overlapped *, iocp_callback cb);

/** Allocate and return a new evbuffer that supports overlapped IO on a given
    socket.  The socket must be associated with an IO completion port using
    event_iocp_port_associate.
*/
struct evbuffer *evbuffer_overlapped_new(evutil_socket_t fd);

/** Start reading data onto the end of an overlapped evbuffer.

    An evbuffer can only have one read pending at a time.  While the read
    is in progress, no other data may be added to the end of the buffer.
    The buffer must be created with event_overlapped_init().

    @param buf The buffer to read onto
    @param n The number of bytes to try to read.
    @return 0 on success, -1 on error.
 */
int evbuffer_launch_read(struct evbuffer *, size_t n);

/** Start writing data from the start of an evbuffer.

    An evbuffer can only have one write pending at a time.  While the write is
    in progress, no other data may be removed from the front of the buffer.
    The buffer must be created with event_overlapped_init().

    @param buf The buffer to read onto
    @param n The number of bytes to try to read.
    @return 0 on success, -1 on error.
 */
int evbuffer_launch_write(struct evbuffer *, ssize_t n);

/** Create an IOCP, and launch its worker threads.  Internal use only.

    This interface is unstable, and will change.
 */
struct event_iocp_port *event_iocp_port_launch(void);

/** Associate a file descriptor with an iocp, such that overlapped IO on the
    fd will happen on one of the iocp's worker threads.
*/
int event_iocp_port_associate(struct event_iocp_port *port, evutil_socket_t fd,
    uintptr_t key);

/** Shut down all threads serving an iocp. */
void event_iocp_shutdown(struct event_iocp_port *port);

#ifdef __cplusplus
}
#endif

#endif
