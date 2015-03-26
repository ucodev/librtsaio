/**
 * @file asyncop.c
 * @brief Real-Time Scalable Asynchronous Input/Output Library (librtsaio)
 *        RTSAIO Library Low Level Interface
 *
 * Date: 26-03-2015
 * 
 * Copyright 2012-2015 Pedro A. Hortas (pah@ucodev.org)
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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#ifndef RTSAIO_NO_SCHED_H_INCL
#include <sched.h>
#endif
#include <pthread.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <unistd.h>
#include <fcntl.h>

#include <pall/cll.h>
#include <pall/fifo.h>

#include "mm.h"
#include "asyncop.h"
#include "timespec.h"

#ifdef CONFIG_EPOLL
#include <sys/epoll.h>
#endif

#ifdef CONFIG_KEVENT
#include <sys/event.h>
#include <sys/time.h>
#endif


/* Prototypes */
static void *_async_notify_process(void *arg);
static int _async_op_cancel(
		struct async_handler *handler,
		struct async_op *asyncop);



/* Engine */
#ifdef RTSAIO_NO_SA_SIGINFO
static void _sigaction_custom_sig_handler(int signum)
{
	return;
}
#else
static void _sigaction_custom_sig_handler(
		int signum,
		siginfo_t *si,
		void *ucontext)
{
	return;
}
#endif

static int _compare_asyncop(const void *d1, const void *d2) {
	const struct async_op *a1 = d1, *a2 = d2;

	if (a1->data > a2->data)
		return 1;

	if (a1->data < a2->data)
		return -1;

	return 0;
}

static void _destroy_nop(void *data) {
	return;
}

static int _async_notifier_create(struct async_handler *handler) {
	int i = 0;

	for (i = 0; i < handler->tnot_limit; i ++) {
		pthread_mutex_lock(&handler->tnot_mutex);

		if (handler->tnot[i].active) {
			pthread_mutex_unlock(&handler->tnot_mutex);
			continue;
		}

		if ((++ handler->tnot_count) > handler->tnot_count_max)
			handler->tnot_count_max = handler->tnot_count;

		handler->tnot[i].active = 1;

		pthread_mutex_unlock(&handler->tnot_mutex);

		if (pthread_create(&handler->tnot[i].tid, NULL, &_async_notify_process, handler)) {
			handler->tnot_count --;
			handler->tnot[i].active = 0;
			return -1;
		}

		return 0;
	}

	return -1;
}

static void _async_notifier_destroy_all(struct async_handler *handler) {
	int i = 0;

	for (i = 0; i < handler->tnot_limit; i ++) {
		pthread_mutex_lock(&handler->tnot_mutex);

		if (handler->tnot[i].active) {
			handler->tnot[i].quit = 1;
			pthread_cond_broadcast(&handler->tnot_cond);
			pthread_mutex_unlock(&handler->tnot_mutex);
			pthread_join(handler->tnot[i].tid, NULL);
			pthread_mutex_lock(&handler->tnot_mutex);
		}

		pthread_mutex_unlock(&handler->tnot_mutex);
	}

	pall_fifo_destroy(handler->qnot);
	mm_free(handler->tnot);
	pthread_mutex_destroy(&handler->tnot_mutex);
#if defined(PTHREAD_PRIO_INHERIT) && (PTHREAD_PRIO_INHERIT != -1) && !defined(RTSAIO_NO_SCHEDPRIO)
	pthread_mutexattr_destroy(&handler->tnot_mutexattr);
#endif
	pthread_cond_destroy(&handler->tnot_cond);
}

static void _async_notify_prepare(
		struct cll_handler *q,
		struct async_worker *t,
		struct async_op *asyncop)
{
	struct async_handler *handler = asyncop->_handler;

	pthread_mutex_lock(&t->mutex);

	q->del(q, asyncop);

	asyncop->_flags |= ASYNCOP_FL_COMPLETE;

	if (asyncop->_flags & ASYNCOP_FL_REQCANCEL) {
		asyncop->_flags |= ASYNCOP_FL_CANCELED;
		pthread_cond_signal(&t->cond_cancel);

		while (asyncop->_flags & ASYNCOP_FL_REQCANCEL)
			pthread_cond_wait(&t->cond_cancel, &t->mutex);
	}

	if (asyncop->_flags & ASYNCOP_FL_REQSUSPEND) {
		pthread_cond_signal(&t->cond_suspend);

		while (asyncop->_flags & ASYNCOP_FL_REQSUSPEND)
			pthread_cond_wait(&t->cond_suspend, &t->mutex);
	}

	pthread_mutex_unlock(&t->mutex);

	if (asyncop->_flags & ASYNCOP_FL_CANCELED) {
		asyncop->_status = ASYNCOP_STATUS_ERROR;
		asyncop->_error = ECANCELED;
	}

	pthread_mutex_lock(&handler->tnot_mutex);

	handler->qnot->push(handler->qnot, asyncop);

	pthread_mutex_unlock(&handler->tnot_mutex);

	if (handler->qnot->count(handler->qnot) > handler->tnot_threshold)
		_async_notifier_create(handler);

	/* Always signal all notifiers */
	pthread_cond_broadcast(&handler->tnot_cond);
}

static void *_async_notify_process(void *arg) {
	int i = 0;
	struct async_handler *handler = arg;
	struct fifo_handler *qnot = handler->qnot;
	struct async_notifier *tnot = handler->tnot;
	struct async_op *aop = NULL;

	for (i = 0; ; i ++) {
		if (i >= handler->tnot_count)
			i = -1; /* Repeat search until a match is found */

		if (!tnot[i].active)
			continue;

		if (pthread_equal(pthread_self(), tnot[i].tid))
			break;
	}

	for (tnot[i].last_ts = time(NULL); ; ) {
		pthread_mutex_lock(&handler->tnot_mutex);

		while (!(aop = qnot->pop(qnot))) {
			if (tnot[i].quit) {
				handler->tnot_count --;
				tnot[i].active = 0;
				pthread_mutex_unlock(&handler->tnot_mutex);
				return NULL;
			}

			/* If this notifier is idle for too long, it should
			 * be destroyed.
			 */
			if ((time(NULL) - tnot[i].last_ts) >= handler->tnot_timeout) {
				handler->tnot_count --;
				tnot[i].active = 0;
				pthread_mutex_unlock(&handler->tnot_mutex);
				return NULL;
			}

			pthread_cond_wait(&handler->tnot_cond, &handler->tnot_mutex);
		}

		pthread_mutex_unlock(&handler->tnot_mutex);

		pthread_setschedparam(pthread_self(), aop->_sched_policy, &aop->_sp);

		aop->notify(aop->arg);

		tnot[i].last_ts = time(NULL);
	}
}

static void _nb_op_post(
		struct cll_handler *q,
		struct async_worker *t,
		struct async_op *asyncop,
		ssize_t count,
		int errsv)
{
	asyncop->_error = errsv;

	if (count < 0) {
		switch (errsv) {
			case EAGAIN:
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
			case EWOULDBLOCK:
#endif
			return;
		}

		asyncop->_status = ASYNCOP_STATUS_ERROR;
	} else if (count > 0) {
		asyncop->_cur_count += count;

		if (asyncop->_cur_count != asyncop->count) {
			if (!(asyncop->_flags & ASYNCOP_FL_EAGER))
				return;

			asyncop->_status = ASYNCOP_STATUS_INCOMPLETE;
		} else {
			asyncop->_status = ASYNCOP_STATUS_COMPLETE;
		}
	} else /* if (!count) */ {
		if (!asyncop->_cur_count) {
			asyncop->_status = ASYNCOP_STATUS_NODATA;
		} else if (asyncop->_cur_count < asyncop->count) {
			asyncop->_status = ASYNCOP_STATUS_INCOMPLETE;
		} else {
			/* This shall never happen */
			asyncop->_status = ASYNCOP_STATUS_COMPLETE;
		}
	}

	_async_notify_prepare(q, t, asyncop);
}

static inline void _nb_read(
		struct cll_handler *q,
		struct async_worker *t,
		struct async_op *asyncop)
{
	int errsv = 0;
	ssize_t ret = 0;
	sigset_t n, o;

	sigfillset(&n);
	sigemptyset(&o);

	pthread_sigmask(SIG_SETMASK, &n, &o);

	if (asyncop->msg_addr) {
		ret = recvfrom(asyncop->fd, ((char *) asyncop->data) + asyncop->_cur_count, asyncop->count - asyncop->_cur_count, asyncop->msg_flags, asyncop->msg_addr, &asyncop->msg_addrlen);
		errsv = errno;
	} else {
		ret = read(asyncop->fd, ((char *) asyncop->data) + asyncop->_cur_count, asyncop->count - asyncop->_cur_count);
		errsv = errno;
	}

	pthread_sigmask(SIG_SETMASK, &o, NULL);

	_nb_op_post(q, t, asyncop, ret, errsv);
}

static inline void _nb_write(
		struct cll_handler *q,
		struct async_worker *t,
		struct async_op *asyncop)
{
	int errsv = 0;
	ssize_t ret = 0;
	sigset_t n, o;

	sigfillset(&n);
	sigemptyset(&o);

	pthread_sigmask(SIG_SETMASK, &n, &o);

	if (asyncop->msg_addr) {
		ret = sendto(asyncop->fd, ((char *) asyncop->data) + asyncop->_cur_count, asyncop->count - asyncop->_cur_count, asyncop->msg_flags, asyncop->msg_addr, asyncop->msg_addrlen);
		errsv = errno;
	} else {
		ret = write(asyncop->fd, ((char *) asyncop->data) + asyncop->_cur_count, asyncop->count - asyncop->_cur_count);
		errsv = errno;
	}

	pthread_sigmask(SIG_SETMASK, &o, NULL);

	_nb_op_post(q, t, asyncop, ret, errsv);
}

static void _async_prio_handler_process(
		struct cll_handler *q,
		struct async_worker *t)
{
	struct async_op *asyncop_iter = NULL;
	struct timespec ts_cur, ts_zero = { 0, 0 }, *ptimeout = NULL;
	struct timespec ts_delta, ts_p = { 0, 0 };
#ifdef RTSAIO_NO_PSELECT
	struct timeval ptimeout_val;
#endif
	sigset_t sigset_cur, sigset_prev;
#ifdef CONFIG_EPOLL
	int n = 0;
	int epoll_nfds = 0;
	struct epoll_event epoll_ev = { 0, { 0 } };
#elif defined(CONFIG_KEVENT)
	struct kevent *kevent_chlist = NULL;
	int n = 0;
	int kevent_nchanges = 0;
	int kevent_count = 0;
	int kevent_nfds = 0;
	char kevent_buf[16];
#else
	int fd_max = 0;
#endif

	sigfillset(&sigset_cur);
	sigemptyset(&sigset_prev);

	pthread_mutex_lock(&t->mutex);

#ifndef CONFIG_KEVENT
	FD_ZERO(&t->rset);
	FD_ZERO(&t->wset);
#endif

	while (!q->count(q) && !t->quit) {
		pthread_cond_signal(&t->cond_suspend);
		pthread_cond_wait(&t->cond, &t->mutex);
	}

	/* Entering critical region */
	pthread_sigmask(SIG_SETMASK, &sigset_cur, &sigset_prev);

	if (t->quit) {
		pthread_sigmask(SIG_SETMASK, &sigset_prev, NULL);
		pthread_mutex_unlock(&t->mutex);
		return;
	}

#ifdef CONFIG_KEVENT
	kevent_nchanges = q->count(q) + 1;
	assert(kevent_chlist = mm_alloc(kevent_nchanges * sizeof(struct kevent)));

	for (kevent_count = 0, q->rewind(q, 0); (asyncop_iter = q->iterate(q)); kevent_count ++) {
#else
	for (q->rewind(q, 0); (asyncop_iter = q->iterate(q)); ) {
#endif
		if (asyncop_iter->_flags & ASYNCOP_FL_TCHECK) {
			/* Check for operation timeout */
			timespec_get(&ts_cur, (asyncop_iter->_flags & ASYNCOP_FL_REALTIME));

			if (timespec_compare(&ts_cur, &asyncop_iter->_ts_tlim) >= 0) {
				asyncop_iter->_flags |= ASYNCOP_FL_CANCELED;
				/* Force event waiter to return immediately */
				ptimeout = &ts_zero;
			}

			/* Next event waiter timeout */

			/* If next timeout is zero, ignore delta calc */
			if (ptimeout == &ts_zero)
				continue;

			ts_delta = asyncop_iter->_ts_tlim;

			timespec_sub(&ts_delta, &ts_cur);

			if ((timespec_compare(&ts_delta, &ts_p) < 0) || (!ts_p.tv_sec && !ts_p.tv_nsec))
				ts_p = ts_delta;

			ptimeout = &ts_p;
		}

		/* Queue is iterated from First In element to Last In.
		 * The same file descriptor may appear multiple times in the
		 * queue for the same type of operation, so a check is
		 * performed to verify if the file descriptor already exists
		 * in the respective pselect() / epoll_wait() / kevent() set.
		 * This will grant the same processing order by which the
		 * operations were requested.
		 */
		if (asyncop_iter->_flags & ASYNCOP_FL_WRITE) {
#ifndef CONFIG_KEVENT
			if (FD_ISSET(asyncop_iter->fd, &t->wset))
				continue;

			FD_SET(asyncop_iter->fd, &t->wset);
#endif
#ifdef CONFIG_EPOLL
			epoll_ev.events = EPOLLOUT;
			epoll_ev.data.fd = asyncop_iter->fd;

			/* Still use fd_set bitmaps for performance reasons */
			if (FD_ISSET(asyncop_iter->fd, &t->rset)) {
				/* If the file descriptor is already in epoll
				 * set for EPOLLIN operation, we need to
				 * modify the epoll set in order to make it
				 * aware of the current EPOLLOUT in order to
				 * grant RT asynchronous read/write operations.
				 */
				epoll_ev.events = EPOLLIN | EPOLLOUT;
				epoll_ctl(t->epoll_fd, EPOLL_CTL_MOD, asyncop_iter->fd, &epoll_ev);
			} else {
				epoll_ctl(t->epoll_fd, EPOLL_CTL_ADD, asyncop_iter->fd, &epoll_ev);
			}
#elif defined(CONFIG_KEVENT)
			EV_SET(&kevent_chlist[kevent_count], asyncop_iter->fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
#endif
		} else if (asyncop_iter->_flags & ASYNCOP_FL_READ) {
#ifndef CONFIG_KEVENT
			if (FD_ISSET(asyncop_iter->fd, &t->rset))
				continue;

			FD_SET(asyncop_iter->fd, &t->rset);
#endif
#ifdef CONFIG_EPOLL
			epoll_ev.events = EPOLLIN;
			epoll_ev.data.fd = asyncop_iter->fd;

			/* Still use fd_set bitmaps for performance reasons */
			if (FD_ISSET(asyncop_iter->fd, &t->wset)) {
				/* If the file descriptor is already in epoll
				 * set for EPOLLOUT operation, we need to
				 * modify the epoll set in order to make it
				 * aware of the current EPOLLIN in order to
				 * grant RT asynchronous read/write operations.
				 */
				epoll_ev.events = EPOLLIN | EPOLLOUT;
				epoll_ctl(t->epoll_fd, EPOLL_CTL_MOD, asyncop_iter->fd, &epoll_ev);
			} else {
				epoll_ctl(t->epoll_fd, EPOLL_CTL_ADD, asyncop_iter->fd, &epoll_ev);
			}
#elif defined(CONFIG_KEVENT)
			EV_SET(&kevent_chlist[kevent_count], asyncop_iter->fd, EVFILT_READ, EV_ADD, NOTE_LOWAT, 1, NULL);

#endif
		} else {
			assert(!(asyncop_iter->_flags & (ASYNCOP_FL_WRITE | ASYNCOP_FL_READ)));
		}

#if !defined(CONFIG_EPOLL) && !defined(CONFIG_KEVENT)
		if (asyncop_iter->fd > fd_max)
			fd_max = asyncop_iter->fd;
#endif
	}

	pthread_mutex_unlock(&t->mutex);

#ifdef CONFIG_KEVENT
	/* Also monitor the control file descriptor (read end of kevent_pipe) */
	EV_SET(&kevent_chlist[kevent_count ++], t->kevent_pipe[0], EVFILT_READ, EV_ADD, 0, 0, NULL);

	/* Leaving critical region */
	pthread_sigmask(SIG_SETMASK, &sigset_prev, NULL);
#endif

#ifdef CONFIG_EPOLL
	epoll_nfds = epoll_pwait(t->epoll_fd, t->epoll_events, t->epoll_max_events, ptimeout ? (ptimeout->tv_sec * 1000) + (ptimeout->tv_nsec / 1000000) : -1, &sigset_prev);
#elif defined(CONFIG_KEVENT)
	kevent_nfds = kevent(t->kevent_kq, kevent_chlist, kevent_count, t->kevent_evlist, t->kevent_max_events, ptimeout);
	mm_free(kevent_chlist);
#elif defined(RTSAIO_NO_PSELECT)
	ptimeout_val.tv_sec = ptimeout->tv_sec;
	ptimeout_val.tv_usec = ptimeout->tv_nsec / 1000;

	/* Leaving critical region */
	pthread_sigmask(SIG_SETMASK, &sigset_prev, NULL);

	/* NOTE: Systems not supporting pselect() are prone to race condition here regarding
	 * sigmask
	 */
	select(fd_max + 1, &t->rset, &t->wset, NULL, &ptimeout_val);

	ptimeout->tv_sec = ptimeout_val.tv_sec;
	ptimeout->tv_nsec = ptimeout_val.tv_usec * 1000;
#else
	pselect(fd_max + 1, &t->rset, &t->wset, NULL, ptimeout, &sigset_prev);
#endif

#ifndef CONFIG_KEVENT
	/* Leaving critical region */
	pthread_sigmask(SIG_SETMASK, &sigset_prev, NULL);
#endif

	/* 
	 * It _IS SAFE_ to iterate the pool without locking the main mutex
	 * (t->mutex) here:
	 *
	 * libpall supports delete operations inside iterations, except if
	 * they are performed concurrently (libpall isn't thread safe and
	 * the use of locking mechanisms on multi-threaded implementations is
	 * required).
	 *
	 * No concurrent deletes are possible since they are all performed
	 * in the current worker (and the ones performed by async_destroy()
	 * ensure that this worker is canceled or killed before destroying the
	 * pool).
	 *
	 * Concurrent searches do not affect iterations since LRU isn't used.
	 *
	 * Inserts may affect the iteration if a newly inserted element is
	 * placed right after the current element. This will cause a miss.
	 * However, at this stage, it is safe to ignore new elements that may
	 * be placed in the pool since their file descriptors weren't yet
	 * processed by pselect() / epoll_wait() / kevent().
	 *
	 * Concurrent iterations are not permitted by libpall but there are
	 * no iterations targetting the same pool by different workers.
	 *
	 */
	pthread_mutex_lock(&t->mutex_cancel);

	for (q->rewind(q, 0); (asyncop_iter = q->iterate(q)); ) {
		if ((asyncop_iter->_flags & ASYNCOP_FL_REQCANCEL) || (asyncop_iter->_flags & ASYNCOP_FL_CANCELED)) {
#ifdef CONFIG_EPOLL
			epoll_ctl(t->epoll_fd, EPOLL_CTL_DEL, asyncop_iter->fd, NULL);
#endif
			_async_notify_prepare(q, t, asyncop_iter);
			continue;
#ifdef CONFIG_EPOLL
		}

		epoll_ctl(t->epoll_fd, EPOLL_CTL_DEL, asyncop_iter->fd, NULL);

		for (n = 0; n < epoll_nfds; n ++) {
			if (t->epoll_events[n].data.fd != asyncop_iter->fd)
				continue;

			if (asyncop_iter->_flags & ASYNCOP_FL_READ) {
				_nb_read(q, t, asyncop_iter);
				break;
			} else if (asyncop_iter->_flags & ASYNCOP_FL_WRITE) {
				_nb_write(q, t, asyncop_iter);
				break;
			}
#elif defined(CONFIG_KEVENT)
		}

		for (n = 0; n < kevent_nfds; n ++) {
			if (t->kevent_evlist[n].ident == t->kevent_pipe[0]) {
				/* Empty available data in the control pipe */
				while (read(t->kevent_pipe[0], kevent_buf, sizeof(kevent_buf)) >= 0)
					continue; /* O_NONBLOCK */

				continue;
			}

			if (t->kevent_evlist[n].ident != asyncop_iter->fd)
				continue;

			if (asyncop_iter->_flags & ASYNCOP_FL_READ) {
				_nb_read(q, t, asyncop_iter);
				break;
			} else if (asyncop_iter->_flags & ASYNCOP_FL_WRITE) {
				_nb_write(q, t, asyncop_iter);
				break;
			}
#else
		} else if (FD_ISSET(asyncop_iter->fd, &t->rset) && (asyncop_iter->_flags & ASYNCOP_FL_READ)) {
			_nb_read(q, t, asyncop_iter);
		} else if (FD_ISSET(asyncop_iter->fd, &t->wset) && (asyncop_iter->_flags & ASYNCOP_FL_WRITE)) {
			_nb_write(q, t, asyncop_iter);
#endif
		}
	}

	pthread_mutex_unlock(&t->mutex_cancel);
}

static void *_async_prio_handler(void *arg) {
	int i = 0;
	struct async_handler *h = arg;
	struct cll_handler *q = NULL;
	struct async_worker *t = NULL;

	for (i = 0; ; i ++) {
		if (i >= h->nrq)
			i = -1; /* Repeat search until pthread_create()
				* updates 'tid'
				*/

		if (pthread_equal(pthread_self(), h->tprio[i].tid))
			break;
	}

	q = h->qprio[i];
	t = &h->tprio[i];

	for (; !t->quit; )
		_async_prio_handler_process(q, t);

	return NULL;
}

static int _async_op_io_generic(
		struct async_handler *handler,
		struct async_op *asyncop)
{
	int flags = 0, errsv = 0;
	struct cll_handler *q = NULL;
	struct async_worker *t = NULL;

	if (handler->flags & ASYNCH_FL_NOPRIO) {
		if (handler->flags & ASYNCH_FL_NOPRIO_MOD) {
			/* Use modulus to compute priority queue index */
			asyncop->priority = asyncop->fd % handler->nrq;
		} else {
			/* Atomic */
			pthread_mutex_lock(&handler->rrq_mutex);

			/* Compute priority queue index in a round-robin
			 * fashion
			 */
			asyncop->priority = handler->rrq ++;

			if (handler->rrq >= handler->nrq)
				handler->rrq = 0;

			pthread_mutex_unlock(&handler->rrq_mutex);
		}
	} else if (asyncop->priority >= handler->nrq) {
		errno = EINVAL;
		return -1;
	}

	q = handler->qprio[asyncop->priority];
	t = &handler->tprio[asyncop->priority];

	if ((flags = fcntl(asyncop->fd, F_GETFL)) < 0)
		return -1;

	if (!(flags & O_NONBLOCK)) {
		if (fcntl(asyncop->fd, F_SETFL, flags | O_NONBLOCK) < 0)
			return -1;
	}

	asyncop->_cur_count = 0;
	asyncop->_status = ASYNCOP_STATUS_ERROR;
	asyncop->_error = EINPROGRESS;

	memcpy(&asyncop->_sp, &t->sp, sizeof(struct sched_param));
	asyncop->_sched_policy = t->sched_policy;
	asyncop->_handler = handler;

	if (asyncop->timeout.tv_sec || asyncop->timeout.tv_nsec) {
		asyncop->_flags |= ASYNCOP_FL_TCHECK;
		asyncop->_flags |= (handler->flags & ASYNCH_FL_REALTIME) ? ASYNCOP_FL_REALTIME : 0;

		timespec_get(&asyncop->_ts_tlim, handler->flags & ASYNCH_FL_REALTIME);

		timespec_add(&asyncop->_ts_tlim, &asyncop->timeout);
	}

	pthread_mutex_lock(&t->mutex);

	if (q->insert(q, asyncop) < 0) {
		errsv = errno;
		pthread_mutex_unlock(&t->mutex);
		errno = errsv;
		return -1;
	}

	/* Check if fd is present in the event set before waking up the
	 * worker. If so, do not wake up the worker since a previous
	 * operation targetting the same fd is already in progress.
	 */
	if (FD_ISSET(asyncop->fd, asyncop->_flags & ASYNCOP_FL_WRITE ? &t->wset : &t->rset)) {
		pthread_mutex_unlock(&t->mutex);
		return 0;
	}

	if (q->count(q) == 1) {
		pthread_cond_signal(&t->cond);
#ifdef CONFIG_KEVENT
	} else if (write(t->kevent_pipe[1], (char [1]) { 0 }, 1) != 1) {
#else
	} else if (pthread_kill(t->tid, ASYNC_CUSTOM_SIG)) {
#endif
		errsv = errno;
		pthread_mutex_unlock(&t->mutex);
		errno = errsv;
		return -1;
	}

	pthread_mutex_unlock(&t->mutex);

	return 0;
}

static int _async_op_write(
		struct async_handler *handler,
		struct async_op *asyncop)
{
	if (!handler || !asyncop) {
		errno = EINVAL;
		return -1;
	}

	asyncop->_flags = ASYNCOP_FL_WRITE;

	return _async_op_io_generic(handler, asyncop);
}

static int _async_op_write_eager(
		struct async_handler *handler,
		struct async_op *asyncop)
{
	if (!handler || !asyncop) {
		errno = EINVAL;
		return -1;
	}

	asyncop->_flags = ASYNCOP_FL_WRITE | ASYNCOP_FL_EAGER;

	return _async_op_io_generic(handler, asyncop);
}

static int _async_op_read(
		struct async_handler *handler,
		struct async_op *asyncop)
{
	if (!handler || !asyncop) {
		errno = EINVAL;
		return -1;
	}

	asyncop->_flags = ASYNCOP_FL_READ;

	return _async_op_io_generic(handler, asyncop);
}

static int _async_op_read_eager(
		struct async_handler *handler,
		struct async_op *asyncop)
{
	if (!handler || !asyncop) {
		errno = EINVAL;
		return -1;
	}

	asyncop->_flags = ASYNCOP_FL_READ | ASYNCOP_FL_EAGER;

	return _async_op_io_generic(handler, asyncop);
}

static int _async_op_cancel_all(struct async_handler *handler) {
	int n = 0;
	struct cll_handler *q = NULL;
	struct async_worker *t = NULL;
	struct async_op *asyncop_iter = NULL;

	for (n = 0; n < handler->nrq; n ++) {
		q = handler->qprio[n];
		t = &handler->tprio[n];

		pthread_mutex_lock(&t->mutex_cancel);
		pthread_mutex_lock(&t->mutex);

		for (q->rewind(q, 0); (asyncop_iter = q->iterate(q)); )
			asyncop_iter->_flags |= ASYNCOP_FL_CANCELED;

#ifdef CONFIG_KEVENT
		write(t->kevent_pipe[1], (char [1]) { 0 }, 1);
#else
		pthread_kill(t->tid, ASYNC_CUSTOM_SIG);
#endif

		pthread_mutex_unlock(&t->mutex);
		pthread_mutex_unlock(&t->mutex_cancel);
	}

	return 0;
}

static int _async_op_cancel(
		struct async_handler *handler,
		struct async_op *asyncop)
{
	int errsv = 0;
	struct cll_handler *q = NULL;
	struct async_worker *t = NULL;

	if (!handler) {
		errno = EINVAL;
		return -1;
	}

	if (!asyncop)
		return _async_op_cancel_all(handler);

	if (asyncop->_flags & ASYNCOP_FL_COMPLETE)
		return 0;

	if (asyncop->priority >= handler->nrq) {
		errno = EINVAL;
		return -1;
	}

	q = handler->qprio[asyncop->priority];
	t = &handler->tprio[asyncop->priority];

	pthread_mutex_lock(&t->mutex);

	if (!q->search(q, asyncop)) {
		pthread_mutex_unlock(&t->mutex);
		errno = EINVAL;
		return -1;
	}

	asyncop->_flags |= ASYNCOP_FL_REQCANCEL;

#ifdef CONFIG_KEVENT
	if (write(t->kevent_pipe[1], (char [1]) { 0 }, 1) != 1) {
#else
	if (pthread_kill(t->tid, ASYNC_CUSTOM_SIG)) {
#endif
		errsv = errno;
		asyncop->_flags &= ~ASYNCOP_FL_REQCANCEL;
		pthread_mutex_unlock(&t->mutex);
		errno = errsv;
		return -1;
	}

	while (!(asyncop->_flags & ASYNCOP_FL_CANCELED))
		pthread_cond_wait(&t->cond_cancel, &t->mutex);

	asyncop->_flags &= ~ASYNCOP_FL_REQCANCEL;

	pthread_cond_signal(&t->cond_cancel);

	pthread_mutex_unlock(&t->mutex);

	return 0;
}

static int _async_op_suspend_all(struct async_handler *handler) {
	int i = 0;
	struct cll_handler *q = NULL;
	struct async_worker *t = NULL;

	for (i = 0; i < handler->nrq; i ++) {
		q = handler->qprio[i];
		t = &handler->tprio[i];

		pthread_mutex_lock(&t->mutex);

		while (q->count(q))
			pthread_cond_wait(&t->cond_suspend, &t->mutex);

		pthread_mutex_unlock(&t->mutex);
	}

	return 0;
}

static int _async_op_suspend(
		struct async_handler *handler,
		struct async_op *asyncop)
{
	struct cll_handler *q = NULL;
	struct async_worker *t = NULL;

	if (!asyncop)
		return _async_op_suspend_all(handler);

	if (asyncop->_flags & ASYNCOP_FL_COMPLETE)
		return 0;

	if (asyncop->priority >= handler->nrq) {
		errno = EINVAL;
		return -1;
	}

	q = handler->qprio[asyncop->priority];
	t = &handler->tprio[asyncop->priority];

	pthread_mutex_lock(&t->mutex);

	if (!q->search(q, asyncop)) {
		pthread_mutex_unlock(&t->mutex);
		errno = EINVAL;
		return -1;
	}

	asyncop->_flags |= ASYNCOP_FL_REQSUSPEND;

	while (!(asyncop->_flags & ASYNCOP_FL_COMPLETE))
		pthread_cond_wait(&t->cond_suspend, &t->mutex);

	asyncop->_flags &= ~ASYNCOP_FL_REQSUSPEND;

	pthread_cond_signal(&t->cond_suspend);

	pthread_mutex_unlock(&t->mutex);

	return 0;
}

static int _async_op_status(
		struct async_handler *handler,
		struct async_op *asyncop)
{
	return asyncop->_status;
}

static int _async_op_error(
		struct async_handler *handler,
		struct async_op *asyncop)
{
	return asyncop->_error;
}

static size_t _async_op_count(
		struct async_handler *handler,
		struct async_op *asyncop)
{
	return asyncop->_cur_count;
}

static struct async_stat *_async_op_stat(struct async_handler *handler) {
	int i = 0;
	struct cll_stat *stat = NULL;

	for (i = 0; i < handler->nrq; i ++) {
		if (!(stat = handler->qprio[i]->stat(handler->qprio[i])))
			continue;

		handler->_stat[i].total = stat->insert;
		handler->_stat[i].cur = stat->elem_count_cur;
		handler->_stat[i].max = stat->elem_count_max;

		handler->_stat[i].tnot_cur = handler->tnot_count;
		handler->_stat[i].tnot_max = handler->tnot_count_max;
		handler->_stat[i].tnot_lim = handler->tnot_limit;
	}

	return handler->_stat;
}

static void _async_op_stat_reset(struct async_handler *handler) {
	int i = 0;

	for (i = 0; i < handler->nrq; i ++)
		handler->qprio[i]->stat_reset(handler->qprio[i]);
}

struct async_handler *async_init(
		int nrq,
		int sched_policy,
		int opt,
		int flags,
		int max_events,
		int nmul,
		int nthreshold,
		int ntimeout)
{
	int i = 0, errsv = 0, spmax = 0, spmin = 0;
	struct async_handler *handler = NULL;
	struct sigaction sa;
#ifdef CONFIG_KEVENT
	int pipe_flags = 0;
#endif

	if (!nrq) {
		errno = EINVAL;
		return NULL;
	}

#ifdef RTSAIO_NO_SCHEDPRIO
	spmax = ASYNC_SCHED_PRIO_OPT_MAX;
	spmin = ASYNC_SCHED_PRIO_OPT_MIN;
#else
	if ((spmax = sched_get_priority_max(sched_policy)) < 0)
		return NULL;

	if ((spmin = sched_get_priority_min(sched_policy)) < 0)
		return NULL;
#endif

	if (!(handler = mm_alloc(sizeof(struct async_handler))))
		return NULL;

	memset(handler, 0, sizeof(struct async_handler));

	/* Assign user-defined flags */
	handler->flags = flags;

	/* Negative nrq indicates no prioritized queues */
	if (nrq < 0) {
		nrq = -nrq;
		handler->flags |= ASYNCH_FL_NOPRIO;

		if ((opt < ASYNC_SCHED_PRIO_OPT_MIN) || (opt > ASYNC_SCHED_PRIO_OPT_MAX)) {
			mm_free(handler);
			errno = EINVAL;
			return NULL;
		}

		if (pthread_mutex_init(&handler->rrq_mutex, NULL)) {
			errsv = errno;
			mm_free(handler);
			errno = errsv;
			return NULL;
		}
	}

	handler->nrq = nrq;
	handler->sched_policy = sched_policy;

	if ((sched_policy == SCHED_FIFO) || (sched_policy == SCHED_RR))
		handler->flags |= ASYNCH_FL_REALTIME;

	/* Install signal handlers */
	memset(&sa, 0, sizeof(struct sigaction));

#ifdef RTSAIO_NO_SA_SIGINFO
	sa.sa_handler = &_sigaction_custom_sig_handler;
#else
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = &_sigaction_custom_sig_handler;
#endif

	if (sigemptyset(&sa.sa_mask) < 0) {
		errsv = errno;
		mm_free(handler);
		errno = errsv;
		return NULL;
	}
		
	if (sigaction(ASYNC_CUSTOM_SIG, &sa, &handler->_oldact) < 0) {
		errsv = errno;
		mm_free(handler);
		errno = errsv;
		return NULL;
	}

	/* Initialize stats */
	if (!(handler->_stat = mm_alloc(sizeof(struct async_stat) * nrq))) {
		errsv = errno;
		sigaction(ASYNC_CUSTOM_SIG, &handler->_oldact, NULL);
		mm_free(handler);
		errno = errsv;
		return NULL;
	}

	memset(handler->_stat, 0, sizeof(struct async_stat) * nrq);

	/* Initialize Notifiers */
	if (!(handler->qnot = pall_fifo_init(&_destroy_nop, NULL, NULL)))
		return NULL;

	handler->tnot_timeout = ntimeout ? ntimeout : ASYNC_NOTIFIER_TIMEOUT;
	handler->tnot_limit = nmul ? (handler->nrq * nmul) : (handler->nrq * ASYNC_NOTIFIER_MULTIPLIER);
	handler->tnot_threshold = nthreshold ? nthreshold : ASYNC_NOTIFIER_THRESHOLD;

	if (!(handler->tnot = mm_alloc(sizeof(struct async_notifier) * handler->tnot_limit))) {
		errsv = errno;
		pall_fifo_destroy(handler->qnot);
		errno = errsv;
		return NULL;
	}

	memset(handler->tnot, 0, sizeof(struct async_notifier) * handler->tnot_limit);

#if defined(PTHREAD_PRIO_INHERIT) && (PTHREAD_PRIO_INHERIT != -1) && !defined(RTSAIO_NO_SCHEDPRIO)
	if ((errno = pthread_mutexattr_init(&handler->tnot_mutexattr))) {
		errsv = errno;
		pall_fifo_destroy(handler->qnot);
		mm_free(handler->tnot);
		errno = errsv;
		return NULL;
	}

	if ((errno = pthread_mutexattr_setprotocol(&handler->tnot_mutexattr, PTHREAD_PRIO_INHERIT))) {
		errsv = errno;
		pthread_mutexattr_destroy(&handler->tnot_mutexattr);
		pall_fifo_destroy(handler->qnot);
		mm_free(handler->tnot);
		errno = errsv;
		return NULL;
	}

	if ((errno = pthread_mutex_init(&handler->tnot_mutex, &handler->tnot_mutexattr))) {
		errsv = errno;
		pthread_mutexattr_destroy(&handler->tnot_mutexattr);
		pall_fifo_destroy(handler->qnot);
		mm_free(handler->tnot);
		errno = errsv;
		return NULL;
	}
#else
	if ((errno = pthread_mutex_init(&handler->tnot_mutex, NULL))) {
		errsv = errno;
		pall_fifo_destroy(handler->qnot);
		mm_free(handler->tnot);
		errno = errsv;
		return NULL;
	}
#endif

	if ((errno = pthread_cond_init(&handler->tnot_cond, NULL))) {
		errsv = errno;
		pall_fifo_destroy(handler->qnot);
		mm_free(handler->tnot);
		pthread_mutex_destroy(&handler->tnot_mutex);
#if defined(PTHREAD_PRIO_INHERIT) && (PTHREAD_PRIO_INHERIT != -1) && !defined(RTSAIO_NO_SCHEDPRIO)
		pthread_mutexattr_destroy(&handler->tnot_mutexattr);
#endif
		errno = errsv;
		return NULL;
	}

	for (i = 0; i < handler->nrq; i ++) {
		if (_async_notifier_create(handler) < 0) {
			errsv = errno;
			mm_free(handler->_stat);
			sigaction(ASYNC_CUSTOM_SIG, &handler->_oldact, NULL);
			_async_notifier_destroy_all(handler);
			mm_free(handler);
			errno = errsv;
			return NULL;
		}
	}

	/* Initialize workers */
	if (!(handler->tprio = mm_alloc(sizeof(struct async_worker) * nrq))) {
		errsv = errno;
		mm_free(handler->_stat);
		sigaction(ASYNC_CUSTOM_SIG, &handler->_oldact, NULL);
		_async_notifier_destroy_all(handler);
		mm_free(handler);
		errno = errsv;
		return NULL;
	}

	memset(handler->tprio, 0, sizeof(struct async_worker) * nrq);

	if (!(handler->qprio = mm_alloc(sizeof(struct cll_handler *) * nrq))) {
		errsv = errno;
		mm_free(handler->tprio);
		mm_free(handler->_stat);
		sigaction(ASYNC_CUSTOM_SIG, &handler->_oldact, NULL);
		_async_notifier_destroy_all(handler);
		mm_free(handler);
		errno = errsv;
		return NULL;
	}

	memset(handler->qprio, 0, sizeof(struct cll_handler *) * nrq);

	for (i = 0; i < nrq; i ++) {
		if (!(handler->qprio[i] = pall_cll_init(&_compare_asyncop, &_destroy_nop, NULL, NULL))) {
			errsv = errno;
			for (-- i; i >= 0; i --)
				pall_cll_destroy(handler->qprio[i]);
			mm_free(handler->qprio);
			mm_free(handler->tprio);
			mm_free(handler->_stat);
			sigaction(ASYNC_CUSTOM_SIG, &handler->_oldact, NULL);
			_async_notifier_destroy_all(handler);
			mm_free(handler);
			errno = errsv;
			return NULL;
		}

		/* Tail inserts grant the same processing order by which the 
		 * operations are requested.
		 * This is valid based on the fact that there's no cross
		 * priority requests for the same file descriptor.
		 */
		handler->qprio[i]->set_config(handler->qprio[i], CONFIG_SEARCH_FORWARD | CONFIG_INSERT_TAIL);
	}

	for (i = 0; i < nrq; i ++) {
#ifdef CONFIG_EPOLL
		handler->tprio[i].epoll_max_events = max_events ? max_events : ASYNC_EPOLL_MAX_EVENTS;

		if (!(handler->tprio[i].epoll_events = mm_alloc(sizeof(struct epoll_event) * handler->tprio[i].epoll_max_events))) {
			errsv = errno;
			break;
		}
#elif defined(CONFIG_KEVENT)
		handler->tprio[i].kevent_max_events = max_events ? max_events : ASYNC_KEVENT_MAX_EVENTS;

		if (!(handler->tprio[i].kevent_evlist = mm_alloc(sizeof(struct kevent) * handler->tprio[i].kevent_max_events))) {
			errsv = errno;
			break;
		}
#endif
		handler->tprio[i].sched_policy = sched_policy;

		memset(&handler->tprio[i].sp, 0, sizeof(struct sched_param));

		if (handler->flags & ASYNCH_FL_NOPRIO) {
			/* On negative nrq, opt value is the sched_priority.
			 * opt shall not be greater than
			 * ASYNC_SCHED_PRIO_OPT_MAX nor lesser than
			 * ASYNC_SCHED_PRIO_OPT_MIN.
			 */
			handler->tprio[i].sp.sched_priority = (int) (((opt - 1) * ((spmax - spmin) / (float) (ASYNC_SCHED_PRIO_OPT_MAX - ASYNC_SCHED_PRIO_OPT_MIN))) + spmin);
		} else {
			if (opt >= 0) {
				handler->tprio[i].sp.sched_priority = (int) spmax - ((i * ((ASYNC_SCHED_PRIO_OPT_MAX - ASYNC_SCHED_PRIO_OPT_MIN) / (float) nrq)) * ((spmax - spmin - (opt - 1)) / (float) (ASYNC_SCHED_PRIO_OPT_MAX - ASYNC_SCHED_PRIO_OPT_MIN)));
			} else {
				handler->tprio[i].sp.sched_priority = (int) (spmax + (opt + 1)) - ((i * ((ASYNC_SCHED_PRIO_OPT_MAX - ASYNC_SCHED_PRIO_OPT_MIN) / (float) nrq)) * ((spmax - spmin + (opt + 1)) / (float) (ASYNC_SCHED_PRIO_OPT_MAX - ASYNC_SCHED_PRIO_OPT_MIN)));
			}
		}

#ifdef CONFIG_EPOLL
		if ((handler->tprio[i].epoll_fd = epoll_create1(0)) < 0) {
			errsv = errno;
			mm_free(handler->tprio[i].epoll_events);
			break;
		}
#endif

#ifdef CONFIG_KEVENT
		if ((handler->tprio[i].kevent_kq = kqueue()) < 0) {
			errsv = errno;
			mm_free(handler->tprio[i].kevent_evlist);
			break;
		}

		if (pipe(handler->tprio[i].kevent_pipe) < 0) {
			errsv = errno;
			close(handler->tprio[i].kevent_kq);
			mm_free(handler->tprio[i].kevent_evlist);
			break;
		}

		if ((pipe_flags = fcntl(handler->tprio[i].kevent_pipe[0], F_GETFL)) < 0) {
			errsv = errno;
			close(handler->tprio[i].kevent_pipe[0]);
			close(handler->tprio[i].kevent_pipe[1]);
			close(handler->tprio[i].kevent_kq);
			mm_free(handler->tprio[i].kevent_evlist);
			break;
		}

		if (!(pipe_flags & O_NONBLOCK)) {
			if (fcntl(handler->tprio[i].kevent_pipe[0], F_SETFL, pipe_flags | O_NONBLOCK) < 0) {
				errsv = errno;
				close(handler->tprio[i].kevent_pipe[0]);
				close(handler->tprio[i].kevent_pipe[1]);
				close(handler->tprio[i].kevent_kq);
				mm_free(handler->tprio[i].kevent_evlist);
				break;
			}
		}

		if ((pipe_flags = fcntl(handler->tprio[i].kevent_pipe[1], F_GETFL)) < 0) {
			errsv = errno;
			close(handler->tprio[i].kevent_pipe[0]);
			close(handler->tprio[i].kevent_pipe[1]);
			close(handler->tprio[i].kevent_kq);
			mm_free(handler->tprio[i].kevent_evlist);
			break;
		}

		if (!(pipe_flags & O_NONBLOCK)) {
			if (fcntl(handler->tprio[i].kevent_pipe[1], F_SETFL, pipe_flags | O_NONBLOCK) < 0) {
				errsv = errno;
				close(handler->tprio[i].kevent_pipe[0]);
				close(handler->tprio[i].kevent_pipe[1]);
				close(handler->tprio[i].kevent_kq);
				mm_free(handler->tprio[i].kevent_evlist);
				break;
			}
		}
#endif

#if defined(PTHREAD_PRIO_INHERIT) && (PTHREAD_PRIO_INHERIT != -1) && !defined(RTSAIO_NO_SCHEDPRIO)
		if ((errno = pthread_mutexattr_init(&handler->tprio[i].mutexattr))) {
			errsv = errno;
			break;
		}

		if ((errno = pthread_mutexattr_setprotocol(&handler->tprio[i].mutexattr, PTHREAD_PRIO_INHERIT))) {
			errsv = errno;
			pthread_mutexattr_destroy(&handler->tprio[i].mutexattr);
			break;
		}

		if ((errno = pthread_mutex_init(&handler->tprio[i].mutex, &handler->tprio[i].mutexattr))) {
			errsv = errno;
			pthread_mutexattr_destroy(&handler->tprio[i].mutexattr);
			break;
		}
#else
		if ((errno = pthread_mutex_init(&handler->tprio[i].mutex, NULL))) {
			errsv = errno;
			break;
		}
#endif

#if defined(PTHREAD_PRIO_INHERIT) && (PTHREAD_PRIO_INHERIT != -1) && !defined(RTSAIO_NO_SCHEDPRIO)
		if ((errno = pthread_mutexattr_init(&handler->tprio[i].mutexattr_cancel))) {
			errsv = errno;
			break;
		}

		if ((errno = pthread_mutexattr_setprotocol(&handler->tprio[i].mutexattr_cancel, PTHREAD_PRIO_INHERIT))) {
			errsv = errno;
			pthread_mutexattr_destroy(&handler->tprio[i].mutexattr_cancel);
			break;
		}

		if ((errno = pthread_mutex_init(&handler->tprio[i].mutex_cancel, &handler->tprio[i].mutexattr_cancel))) {
			errsv = errno;
			pthread_mutexattr_destroy(&handler->tprio[i].mutexattr_cancel);
			break;
		}
#else
		if ((errno = pthread_mutex_init(&handler->tprio[i].mutex_cancel, NULL))) {
			errsv = errno;
			break;
		}
#endif

		if ((errno = pthread_cond_init(&handler->tprio[i].cond, NULL))) {
			errsv = errno;
			pthread_mutex_destroy(&handler->tprio[i].mutex);
			pthread_mutex_destroy(&handler->tprio[i].mutex_cancel);
			break;
		}

		if ((errno = pthread_cond_init(&handler->tprio[i].cond_cancel, NULL))) {
			errsv = errno;
			pthread_mutex_destroy(&handler->tprio[i].mutex);
			pthread_mutex_destroy(&handler->tprio[i].mutex_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond);
			break;
		}
			
		if ((errno = pthread_cond_init(&handler->tprio[i].cond_suspend, NULL))) {
			errsv = errno;
			pthread_mutex_destroy(&handler->tprio[i].mutex);
			pthread_mutex_destroy(&handler->tprio[i].mutex_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond);
			pthread_cond_destroy(&handler->tprio[i].cond_cancel);
			break;
		}

		if ((errno = pthread_attr_init(&handler->tprio[i].attr))) {
			errsv = errno;
			pthread_mutex_destroy(&handler->tprio[i].mutex);
			pthread_mutex_destroy(&handler->tprio[i].mutex_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond);
			pthread_cond_destroy(&handler->tprio[i].cond_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond_suspend);
			break;
		}

#ifndef RTSAIO_NO_SCHEDPRIO
		if ((errno = pthread_attr_setinheritsched(&handler->tprio[i].attr, PTHREAD_EXPLICIT_SCHED))) {
			errsv = errno;
			pthread_mutex_destroy(&handler->tprio[i].mutex);
			pthread_mutex_destroy(&handler->tprio[i].mutex_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond);
			pthread_cond_destroy(&handler->tprio[i].cond_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond_suspend);
			pthread_attr_destroy(&handler->tprio[i].attr);
			break;
		}

		if ((errno = pthread_attr_setschedpolicy(&handler->tprio[i].attr, sched_policy))) {
			errsv = errno;
			pthread_mutex_destroy(&handler->tprio[i].mutex);
			pthread_mutex_destroy(&handler->tprio[i].mutex_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond);
			pthread_cond_destroy(&handler->tprio[i].cond_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond_suspend);
			pthread_attr_destroy(&handler->tprio[i].attr);
			break;
		}

		if ((errno = pthread_attr_setschedparam(&handler->tprio[i].attr, &handler->tprio[i].sp))) {
			errsv = errno;
			pthread_mutex_destroy(&handler->tprio[i].mutex);
			pthread_mutex_destroy(&handler->tprio[i].mutex_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond);
			pthread_cond_destroy(&handler->tprio[i].cond_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond_suspend);
			pthread_attr_destroy(&handler->tprio[i].attr);
			break;
		}
#endif

		if ((errno = pthread_create(&handler->tprio[i].tid, &handler->tprio[i].attr, &_async_prio_handler, handler))) {
			errsv = errno;
			pthread_mutex_destroy(&handler->tprio[i].mutex);
			pthread_mutex_destroy(&handler->tprio[i].mutex_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond);
			pthread_cond_destroy(&handler->tprio[i].cond_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond_suspend);
			pthread_attr_destroy(&handler->tprio[i].attr);
			break;
		}
	}

	/* If something went wrong, cleanup everything and return error */
	if (i != nrq) {
		pall_cll_destroy(handler->qprio[i]);

		for (-- i; i >= 0; i --) {
#ifdef CONFIG_EPOLL
			close(handler->tprio[i].epoll_fd);
			mm_free(handler->tprio[i].epoll_events);
#elif defined(CONFIG_KEVENT)
			close(handler->tprio[i].kevent_kq);
			close(handler->tprio[i].kevent_pipe[0]);
			close(handler->tprio[i].kevent_pipe[1]);
			mm_free(handler->tprio[i].kevent_evlist);
#endif
			pthread_mutex_lock(&handler->tprio[i].mutex);
			handler->tprio[i].quit = 1;
#ifdef CONFIG_KEVENT
			write(handler->tprio[i].kevent_pipe[1], (char [1]) { 0 }, 1);
#else
			pthread_kill(handler->tprio[i].tid, ASYNC_CUSTOM_SIG);
#endif
			pthread_cond_signal(&handler->tprio[i].cond);
			pthread_cond_signal(&handler->tprio[i].cond_cancel);
			pthread_cond_signal(&handler->tprio[i].cond_suspend);
			pthread_mutex_unlock(&handler->tprio[i].mutex);
			pthread_join(handler->tprio[i].tid, NULL);
			pthread_attr_destroy(&handler->tprio[i].attr);
			pthread_cond_destroy(&handler->tprio[i].cond);
			pthread_cond_destroy(&handler->tprio[i].cond_cancel);
			pthread_cond_destroy(&handler->tprio[i].cond_suspend);
#if defined(PTHREAD_PRIO_INHERIT) && (PTHREAD_PRIO_INHERIT != -1) && !defined(RTSAIO_NO_SCHEDPRIO)
			pthread_mutexattr_destroy(&handler->tprio[i].mutexattr);
#endif
			pthread_mutex_destroy(&handler->tprio[i].mutex_cancel);
			pthread_mutex_destroy(&handler->tprio[i].mutex);
			pall_cll_destroy(handler->qprio[i]);
		}

		if (handler->flags & ASYNCH_FL_NOPRIO)
			pthread_mutex_destroy(&handler->rrq_mutex);

		sigaction(ASYNC_CUSTOM_SIG, &handler->_oldact, NULL);

		mm_free(handler->qprio);
		mm_free(handler->tprio);
		mm_free(handler->_stat);
		_async_notifier_destroy_all(handler);
		mm_free(handler);

		errno = errsv;
		return NULL;
	}


	/* Initialize handler methods */
	handler->write = &_async_op_write;
	handler->write_eager = &_async_op_write_eager;
	handler->read = &_async_op_read;
	handler->read_eager = &_async_op_read_eager;
	handler->cancel = &_async_op_cancel;
	handler->status = &_async_op_status;
	handler->error = &_async_op_error;
	handler->count = &_async_op_count;
	handler->suspend = &_async_op_suspend;
	handler->stat = &_async_op_stat;
	handler->stat_reset = &_async_op_stat_reset;

	return handler;
}

void async_destroy(struct async_handler *handler) {
	int i = 0;

	for (i = 0; i < handler->nrq; i ++) {
#ifdef CONFIG_EPOLL
		close(handler->tprio[i].epoll_fd);
		mm_free(handler->tprio[i].epoll_events);
#elif defined(CONFIG_KEVENT)
		close(handler->tprio[i].kevent_kq);
		close(handler->tprio[i].kevent_pipe[0]);
		close(handler->tprio[i].kevent_pipe[1]);
		mm_free(handler->tprio[i].kevent_evlist);
#endif
		pthread_mutex_lock(&handler->tprio[i].mutex);
		handler->tprio[i].quit = 1;
#ifdef CONFIG_KEVENT
		write(handler->tprio[i].kevent_pipe[1], (char [1]) { 0 }, 1);
#else
		pthread_kill(handler->tprio[i].tid, ASYNC_CUSTOM_SIG);
#endif
		pthread_cond_signal(&handler->tprio[i].cond);
		pthread_cond_signal(&handler->tprio[i].cond_cancel);
		pthread_cond_signal(&handler->tprio[i].cond_suspend);
		pthread_mutex_unlock(&handler->tprio[i].mutex);
		pthread_join(handler->tprio[i].tid, NULL);
		pthread_attr_destroy(&handler->tprio[i].attr);
		pthread_cond_destroy(&handler->tprio[i].cond);
		pthread_cond_destroy(&handler->tprio[i].cond_cancel);
		pthread_cond_destroy(&handler->tprio[i].cond_suspend);
#if defined(PTHREAD_PRIO_INHERIT) && (PTHREAD_PRIO_INHERIT != -1) && !defined(RTSAIO_NO_SCHEDPRIO)
		pthread_mutexattr_destroy(&handler->tprio[i].mutexattr);
#endif
		pthread_mutex_destroy(&handler->tprio[i].mutex_cancel);
		pthread_mutex_destroy(&handler->tprio[i].mutex);
		pall_cll_destroy(handler->qprio[i]);
	}

	mm_free(handler->_stat);
	mm_free(handler->tprio);
	mm_free(handler->qprio);

	sigaction(ASYNC_CUSTOM_SIG, &handler->_oldact, NULL);

	if (handler->flags & ASYNCH_FL_NOPRIO)
		pthread_mutex_destroy(&handler->rrq_mutex);

	_async_notifier_destroy_all(handler);

	mm_free(handler);
}

