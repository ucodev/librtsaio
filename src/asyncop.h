/**
 * @file asyncop.h
 * @brief Real-Time Scalable Asynchronous Input/Output Library (librtsaio)
 *        RTSAIO Library Low Level Interface Header
 *
 * Date: 01-05-2013
 * 
 * Copyright 2012,2013 Pedro A. Hortas (pah@ucodev.org)
 *
 * This file is part of librtsaio.
 *
 * librtsaio is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * librtsaio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with librtsaio.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef RTSAIO_ASYNCOP_H
#define RTSAIO_ASYNCOP_H

#include <stdint.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <pall/cll.h>
#include <pall/fifo.h>

#ifdef CONFIG_EPOLL
#include <sys/epoll.h>
#endif
#ifdef CONFIG_KEVENT
#include <sys/event.h>
#include <sys/time.h>
#endif

/* Configuration */
#ifdef SIGRTMIN
#define ASYNC_CUSTOM_SIG		SIGRTMIN
#else
#define ASYNC_CUSTOM_SIG		SIGUSR1
#endif
#define ASYNC_SCHED_PRIO_OPT_MIN	1
#define ASYNC_SCHED_PRIO_OPT_MAX	32
#define ASYNC_EPOLL_MAX_EVENTS		16
#define ASYNC_KEVENT_MAX_EVENTS		16
#define ASYNC_NOTIFIER_MULTIPLIER	10
#define ASYNC_NOTIFIER_TIMEOUT		60	/* Value in seconds */
#define ASYNC_NOTIFIER_THRESHOLD	4

/* Asynchronous Operation Status */
#define ASYNCOP_STATUS_ERROR		-1
#define ASYNCOP_STATUS_NODATA		0
#define ASYNCOP_STATUS_INCOMPLETE	1
#define ASYNCOP_STATUS_COMPLETE		2

/* Asynchronous Operation Flags */
#define ASYNCOP_FL_WRITE		0x01
#define ASYNCOP_FL_READ			0x02
#define ASYNCOP_FL_REQCANCEL		0x04
#define ASYNCOP_FL_CANCELED		0x08
#define ASYNCOP_FL_REQSUSPEND		0x10
#define ASYNCOP_FL_COMPLETE		0x20
#define ASYNCOP_FL_REALTIME		0x40
#define ASYNCOP_FL_TCHECK		0x80
#define ASYNCOP_FL_EAGER		0x100

/* Asynchronous Handler Flags */
#define ASYNCH_FL_DEFAULT		0x00
#define ASYNCH_FL_REALTIME		0x01
#define ASYNCH_FL_NOPRIO		0x02
#define ASYNCH_FL_NOPRIO_MOD		0x04 /* Use this flag in order to compute priority
					      * queues index as MOD(fd, nrq).
					      * If unset, priority queue index will be
					      * selected in a round-robin fashion.
					      * Note that if round-robin is used (the
					      * default if this flag is ommited), there's
					      * no way to grant the processing order of the
					      * AIO requests since an operation requested
					      * before receiving a notification of an
					      * already enqueued operation of the same type
					      * for the same file descriptor, may fall in
					      * a different priority queue (thus, different
					      * worker), resulting in undefined behaviour
					      * regarding the processing order.
					      */

/* Unions */
union async_arg {
	void *argp;
	int argi;
};

/* Structures */
/**
 * @struct async_op
 *
 * @brief
 *   Asynchronous Operation Data Structure Descriptor
 *
 * @see rtsaio_write()
 * @see rtsaio_write_eager()
 * @see rtsaio_read()
 * @see rtsaio_read_eager()
 * @see rtsaio_status()
 * @see rtsaio_error()
 * @see rtsaio_count()
 * @see rtsaio_cancel()
 * @see rtsaio_suspend()
 *
 * @var async_op::fd
 *   Target file descriptor.
 *
 * @var async_op::priority
 *   Desired priority for the asynchronous operation. This value shall range
 *   from 0 to 'nrq - 1' (used in the initialization function), being 0 the
 *   highest priority and 'nrq - 1' the lowest.
 *   This value is reserved when no prioritization was initialized by
 *   rtsaio_init().
 *
 * @var async_op::data
 *   The data buffer to be read or written, depending on the operation
 *   requested.
 *
 * @var async_op::count
 *   The number of bytes to be read/written to/from 'data' to the 'fd'.
 *
 * @var async_op::notify
 *   The notification function. This value is optional when librtsaio is
 *   initialized through rtsaio_init(). The default notification functions
 *   passed to rtsaio_init() may be overridden for async_op operations with
 *   'notify' field set to a value different than NULL.
 *
 * @var async_op::arg
 *   The argument to the notification function. This value is optional when
 *   librtsaio is initialized through rtsaio_init(). If the notification
 *   function pointer was overriden, this field may be overriden to with some
 *   user-defined argument.
 *
 * @var async_op::timeout
 *   The amount of time the operation is permitted to be in progress. If
 *   the timeout value is reached, the operation is canceled and the
 *   notification function is called immediately. Operation status will be set
 *   to ASYNCOP_STATUS_ERROR and ECANCELED will be set as the error.
 *
 * @var async_op::msg_flags
 *   4th paramter of sendto()/recvfrom() calls. sendto()/recvfrom() are used
 *   instead of write()/read() when 'msg_addr' isn't NULL.
 *
 * @var async_op::msg_addr
 *   5th parameter of sendto()/recvfrom() calls. sendto()/recvfrom() are used
 *   instead of write()/read() when this field isn't NULL.
 *
 * @var async_op::msg_addrlen
 *   6th parameter of sendto()/recvfrom() calls. sendto()/recvfrom() are used
 *   instead of write()/read() when 'msg_addr' isn't NULL.
 *
 */
struct async_op {
	/* Required */
	int fd;
	int priority;	/* May be optional if no prioritized queues are used */
	volatile void *data;
	size_t count;

	/* Required by asyncop.c interface
	 * Optional on rtsaio.c interface
	 */
	void (*notify) (union async_arg arg);
	union async_arg arg;

	/* Optional */
	struct timespec timeout;
	int msg_flags;
	struct sockaddr *msg_addr;
	socklen_t msg_addrlen;

	/* Reserved */
	int _status;
	int _error;
	unsigned int _flags;
	size_t _cur_count;
	struct timespec _ts_tlim;

	struct sched_param _sp;
	int _sched_policy;

	struct async_handler *_handler;
};


/**
 * @struct async_stat
 *
 * @brief
 *   Queue Statistics
 *
 * @see rtsaio_stat()
 * @see rtsaio_stat_reset()
 *
 * @var async_stat::cur
 *   Number of operations currently in the queue.
 *
 * @var async_stat::max
 *   Maximum number of simultaneous operations in the queue since
 *   initialization or last stat_reset call.
 *
 * @var async_stat::total
 *   Total number of operations processed on this queue. If the queue is not
 *   empty at the time of the stat call, this value also reflects the number
 *   of queued operations that are still in progress.
 *
 */
struct async_stat {
	unsigned long cur;
	unsigned long max;
	unsigned long total;

	unsigned long tnot_cur;
	unsigned long tnot_max;
	unsigned long tnot_lim;
};

struct async_worker {
	pthread_t tid;
	pthread_attr_t attr;
	pthread_cond_t cond;
	pthread_cond_t cond_cancel;
	pthread_cond_t cond_suspend;
	pthread_mutex_t mutex;
	pthread_mutex_t mutex_cancel;
	pthread_mutexattr_t mutexattr;
	pthread_mutexattr_t mutexattr_cancel;
	struct sched_param sp;
	int sched_policy;
#ifdef CONFIG_EPOLL
	int epoll_fd;
	int epoll_max_events;
	struct epoll_event *epoll_events;
#endif
#ifdef CONFIG_KEVENT
	int kevent_kq;
	int kevent_pipe[2];
	int kevent_max_events;
	struct kevent *kevent_evlist;
#endif
	fd_set rset, wset;
	int quit;
};

struct async_notifier {
	pthread_t tid;
	int quit;
	int active;
	time_t last_ts;
};

struct async_handler {
	int nrq;
	int sched_policy;

	unsigned int rrq;
	pthread_mutex_t rrq_mutex;

	unsigned int flags;

	struct cll_handler **qprio;
	struct async_worker *tprio;

	struct fifo_handler *qnot;
	struct async_notifier *tnot;
	pthread_mutex_t tnot_mutex;
	pthread_mutexattr_t tnot_mutexattr;
	pthread_cond_t tnot_cond;
	size_t tnot_count;
	size_t tnot_count_max;
	size_t tnot_limit;
	time_t tnot_timeout;
	int tnot_threshold;

	struct sigaction _oldact;
	struct async_stat *_stat;

	int (*write) (struct async_handler *handler, struct async_op *asyncop);
	int (*write_eager) (struct async_handler *handler, struct async_op *asyncop);
	int (*read) (struct async_handler *handler, struct async_op *asyncop);
	int (*read_eager) (struct async_handler *handler, struct async_op *asyncop);
	int (*cancel) (struct async_handler *handler, struct async_op *asyncop);
	int (*suspend) (struct async_handler *handler, struct async_op *asyncop);
	int (*status) (struct async_handler *handler, struct async_op *asyncop);
	int (*error) (struct async_handler *handler, struct async_op *asyncop);
	size_t (*count) (struct async_handler *handler, struct async_op *asyncop);
	struct async_stat *(*stat) (struct async_handler *handler);
	void (*stat_reset) (struct async_handler *handler);
};


/* Prototypes / Interface */
struct async_handler *async_init(
		int nrq,
		int sched_policy,
		int opt,
		int flags,
		int max_events,
		int nmul,
		int nthreshold,
		int ntimeout);
void async_destroy(struct async_handler *handler);

#endif

