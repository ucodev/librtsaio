/**
 * @file rtsaio.h
 * @brief Real-Time Scalable Asynchronous Input/Output Library (librtsaio)
 *        RTSAIO Library Interface Header
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

#ifndef RTSAIO_H
#define RTSAIO_H

#include <stdio.h>
#include <stdint.h>

#include "asyncop.h"

/* Flags */
#define RTSAIO_FL_NOPRIO_MOD	ASYNCH_FL_NOPRIO_MOD

/* Structures */
struct rtsaio_handler {
	struct async_handler *async;

	void (*_write_notify) (struct async_op *aop);
	void (*_read_notify) (struct async_op *aop);
};


/* Prototypes / Interface */

/**
 * @brief
 *   Enqueues an asynchronous I/O write request pointed by 'aop'. The
 *   write notification function is called when all aop->count bytes of
 *   aop->data buffer are written to aop->fd, causing calls to rtsaio_status()
 *   to return ASYNCOP_STATUS_COMPLETE. If it was not possible to write any
 *   data to aop->fd (write() returned zero), rtsaio_status() returns
 *   ASYNCOP_STATUS_NODATA, rtsaio_count() returns zero and rtsaio_error()
 *   shall be used to determine if an error ocurred. If it was not possible to
 *   write aop->count bytes, rtsaio_status() returns ASYNCOP_STATUS_INCOMPLETE,
 *   rtsaio_count() returns the number of bytes written and rtsaio_error() shall
 *   be used to determined if an error occurred. If the operation is canceled,
 *   rtsaio_status() returns ASYNCOP_STATUS_ERROR, rtsaio_error() returns
 *   ECANCELED and rtsaio_count() returns the number of bytes written before
 *   the cancellation was triggered. If an error occur, rtsaio_status() returns
 *   ASYNCOP_STATUS_ERROR, rtsaio_error() returns the errno set by the call who
 *   detected the error, and rtsaio_count() returns the number of bytes written
 *   before the error occurred.
 *
 * @param aop
 *   The Asynchronous I/O operation request.
 *
 * @return
 *   On success, the request is enqueued and zero is returned. On error, -1 is
 *   returned, errno is set appropriately, and the request is not enqueued.
 *   \n\n
 *   Errors: Same as fcntl(), kill(), ENOMEM and EINVAL.
 *
 * @see rtsaio_init()
 * @see rtsaio_write_eager()
 * @see rtsaio_status()
 * @see rtsaio_error()
 * @see rtsaio_count()
 * @see async_op
 *
 */
int rtsaio_write(struct async_op *aop);

/**
 * @brief
 *   Enqueues an asynchronous I/O write request pointed by 'aop'. The
 *   write notification function is called as soon as any data is written from
 *   aop->data buffer to aop->fd. If the total amount of aop->count bytes are
 *   written to aop->fd, rtsaio_status() returns ASYNCOP_STATUS_COMPLETE.
 *   If less than aop->count bytes are written, rtsaio_status() returns
 *   ASYNCOP_STATUS_INCOMPLETE, rtsaio_count() returns the number of bytes
 *   written to aop->fd and rtsaio_error() shall be used to determined if an
 *   error occurred. If no data was written (write() returned zero),
 *   rtsaio_status() returns ASYNCOP_STATUS_NODATA, rtsaio_count() returns
 *   zero and rtsaio_error() shall be used to determine if an error occurred.
 *   If the operation is canceled, rtsaio_status() returns
 *   ASYNCOP_STATUS_ERROR, rtsaio_error() returns ECANCELED and rtsaio_count()
 *   returns the number of bytes written before the cancellation was triggered.
 *   If an error occur, rtsaio_status() returns ASYNCOP_STATUS_ERROR,
 *   rtsaio_error() returns the errno set by the call who detected the error,
 *   and rtsaio_count() returns the number of bytes written before the error
 *   occurred.
 *
 * @param aop
 *   The Asynchronous I/O operation request.
 *
 * @return
 *   On success, the request is enqueued and zero is returned. On error, -1 is
 *   returned, errno is set appropriately, and the request is not enqueued.
 *   \n\n
 *   Errors: Same as fcntl(), kill(), ENOMEM and EINVAL.
 *
 * @see rtsaio_init()
 * @see rtsaio_write()
 * @see rtsaio_status()
 * @see rtsaio_error()
 * @see rtsaio_count()
 * @see async_op
 *
 */
int rtsaio_write_eager(struct async_op *aop);

/**
 * @brief
 *   Enqueues an asynchronous I/O read request pointed by 'aop'. The
 *   read notification function is called when all aop->count bytes are read
 *   from aop->fd to aop->data buffer, causing calls to rtsaio_status()
 *   to return ASYNCOP_STATUS_COMPLETE. If it was not possible to read any
 *   data from aop->fd (read() returned zero), rtsaio_status() returns
 *   ASYNCOP_STATUS_NODATA, rtsaio_count() returns zero and rtsaio_error()
 *   shall be used to determine if an error ocurred. If it was not possible to
 *   read aop->count bytes, rtsaio_status() returns ASYNCOP_STATUS_INCOMPLETE,
 *   rtsaio_count() returns the number of bytes read and rtsaio_error() shall
 *   be used to determined if an error occurred. If the operation is canceled,
 *   rtsaio_status() returns ASYNCOP_STATUS_ERROR, rtsaio_error() returns
 *   ECANCELED and rtsaio_count() returns the number of bytes read before
 *   the cancellation was triggered. If an error occur, rtsaio_status() returns
 *   ASYNCOP_STATUS_ERROR, rtsaio_error() returns the errno set by the call who
 *   detected the error, and rtsaio_count() returns the number of bytes read 
 *   before the error occurred.
 *
 * @param aop
 *   The Asynchronous I/O operation request.
 *
 * @return
 *   On success, the request is enqueued and zero is returned. On error, -1 is
 *   returned, errno is set appropriately, and the request is not enqueued.
 *   \n\n
 *   Errors: Same as fcntl(), kill(), ENOMEM and EINVAL.
 *
 * @see rtsaio_init()
 * @see rtsaio_read_eager()
 * @see rtsaio_status()
 * @see rtsaio_error()
 * @see rtsaio_count()
 * @see async_op
 *
 */
int rtsaio_read(struct async_op *aop);

/**
 * @brief
 *   Enqueues an asynchronous I/O read request pointed by 'aop'. The
 *   read notification function is called as soon as any data is read from
 *   aop->fd to aop->data buffer. If the total amount of aop->count bytes are
 *   read from aop->fd, rtsaio_status() returns ASYNCOP_STATUS_COMPLETE.
 *   If less than aop->count bytes are read, rtsaio_status() returns
 *   ASYNCOP_STATUS_INCOMPLETE, rtsaio_count() returns the number of bytes
 *   read from aop->fd and rtsaio_error() shall be used to determined if an
 *   error occurred. If no data was read (read() returned zero),
 *   rtsaio_status() returns ASYNCOP_STATUS_NODATA, rtsaio_count() returns
 *   zero and rtsaio_error() shall be used to determine if an error occurred.
 *   If the operation is canceled, rtsaio_status() returns
 *   ASYNCOP_STATUS_ERROR, rtsaio_error() returns ECANCELED and rtsaio_count()
 *   returns the number of bytes read before the cancellation was triggered.
 *   If an error occur, rtsaio_status() returns ASYNCOP_STATUS_ERROR,
 *   rtsaio_error() returns the errno set by the call who detected the error,
 *   and rtsaio_count() returns the number of bytes read before the error
 *   occurred.
 *
 * @param aop
 *   The Asynchronous I/O operation request.
 *
 * @return
 *   On success, the request is enqueued and zero is returned. On error, -1 is
 *   returned, errno is set appropriately, and the request is not enqueued.
 *   \n\n
 *   Errors: Same as fcntl(), kill(), ENOMEM and EINVAL.
 *
 * @see rtsaio_init()
 * @see rtsaio_read()
 * @see rtsaio_status()
 * @see rtsaio_error()
 * @see rtsaio_count()
 * @see async_op
 *
 */
int rtsaio_read_eager(struct async_op *aop);

/**
 * @brief
 *   Cancels an Asynchronous I/O operation pointed by 'aop'. If 'aop' points
 *   to a valid requested operation, cancellation always succeeds.
 *
 * @param aop
 *   The Asynchronous I/O operation request to be canceled. If NULL is used,
 *   all enqueued operations are cancelled.
 *
 * @return
 *   On success, zero is returned and the operation pointed by 'aop' is
 *   cancelled. The notification function is still executed causing
 *   rtsaio_status() to return ASYNCOP_STATUS_ERROR, and rtsaio_error() to
 *   return ECANCELED. On error, -1 is returned and errno is set appropriately.
 *   \n\n
 *   Errors: EINVAL
 *
 * @see rtsaio_init()
 * @see rtsaio_status()
 * @see rtsaio_error()
 * @see rtsaio_suspend()
 * @see async_op
 *
 */
int rtsaio_cancel(struct async_op *aop);

/**
 * @brief
 *   Suspends the code execution until the operation 'aop' completes.
 *
 * @param aop
 *   The Asynchronous I/O operation request. If NULL is used, the execution is
 *   suspended until all operations in progress are completed.
 *   Note that the 'completed' term doesn't include the execution of the
 *   notification function. The operation is assumed to be completed at the
 *   time the notification function is to be called.
 *
 * @return
 *   On success, zero is returned. On error, -1 is returned and errno is set
 *   appropriately.
 *   \n\n
 *   Errors: EINVAL
 *
 * @see rtsaio_init()
 * @see rtsaio_cancel()
 * @see async_op
 *
 */
int rtsaio_suspend(struct async_op *aop);

/**
 * @brief
 *   Return the error status for an operation request pointed by 'aop'. This
 *   function is intended to be used inside notification functions in order to
 *   determine the state of the requested operation. However, it can be used
 *   elsewhere.
 *
 * @param aop
 *   The Asynchronous I/O operation request.
 *
 * @return
 *   If zero is returned, no error occured. Otherwise a positive integer is
 *   returned indicating the errno value.
 *
 * @see rtsaio_init()
 * @see rtsaio_write()
 * @see rtsaio_write_eager()
 * @see rtsaio_read()
 * @see rtsaio_read_eager()
 * @see rtsaio_status()
 * @see async_op
 *
 */ 
int rtsaio_error(struct async_op *aop);

/**
 * @brief
 *   Return the status for an operation requested pointed by 'aop'. This
 *   function is intended to be used inside notification functions in order to
 *   determine the state of the requested operation. However, it can be used
 *   elsewhere.
 *
 * @param aop
 *   The Asynchronous I/O operation request.
 *
 * @return
 *   Returns ASYNCOP_STATUS_ERROR if an error occured, ASYNCOP_STATUS_NODATA
 *   if no data was read or written, ASYNCOP_STATUS_COMPLETE if the operation
 *   successfully completed and ASYNCOP_STATUS_INCOMPLETE if the operation
 *   completed with partial data read or written.
 *
 * @see rtsaio_init()
 * @see rtsaio_write()
 * @see rtsaio_write_eager()
 * @see rtsaio_read()
 * @see rtsaio_read_eager()
 * @see rtsaio_cancel()
 * @see rtsaio_error()
 *
 */
int rtsaio_status(struct async_op *aop);

/**
 * @brief
 *   Returns the amount of bytes read or written for a requested operation
 *   pointed by 'aop'. This function is intended to be used inside
 *   notification functions in order to determine the state of the requested
 *   operation. However, it can be used elsewhere.
 *
 * @param aop
 *   The Asynchronous I/O operation request.
 *
 * @return
 *   Returns the number of bytes read or written for a requested operation.
 *
 * @see rtsaio_init()
 * @see rtsaio_write()
 * @see rtsaio_write_eager()
 * @see rtsaio_read()
 * @see rtsaio_read_eager()
 * @see rtsaio_status()
 * @see rtsaio_error()
 * @see async_op
 *
 */
size_t rtsaio_count(struct async_op *aop);

/**
 * @brief
 *   Returns an array os async_stat structures with 'nrq' elements. Each array
 *   index contains an async_stat structure reflecting the corresponding queue
 *   statistics.
 *
 * @return
 *   Returns an array os async_stat structures with 'nrq' elements on success.
 *   On error, NULL is returned.
 *
 * @see rtsaio_init()
 * @see rtsaio_stat_reset()
 *
 */
struct async_stat *rtsaio_stat(void);

/**
 * @brief
 *   Resets the statistical counters for all rtsaio queues.
 *
 * @see rtsaio_init()
 * @see rtsaio_stat_reset()
 *
 */
void rtsaio_stat_reset(void);

/**
 * @brief
 *   Initializes the librtsaio.
 *
 * @param nrq
 *   The number of queues that shall be initialized and one thread per queue is
 *   created. This parameter may receive a positive integer, indicating the use
 *   of prioritized queues, or a negative integer, indicating the absence of
 *   prioritization for the created queues. When prioritized queues are used,
 *   each thread handling the corresponding queue is initialized with different
 *   scheduling priorities. For 'nrq' queues created, the queue 0 will be the
 *   highest priority queue (the thread will have the highest priority
 *   scheduler value), and the 'nrq - 1' queue will have the lowest. If no
 *   prioritization is used, all the queue handlers will have the same scheduler
 *   priority, defined by the parameter 'opt' that shall range from 0 to 32,
 *   where 0 is the highest priority and 32 the lowest for a given
 *   'sched_policy'.
 *
 * @param sched_policy
 *   The scheduler policy that shall be used for the queue handlers (threads).
 *   If real-time is intended, SCHED_FIFO or SCHED_RR shall be used, and
 *   librtsaio will internally use CLOCK_REALTIME for timeout operations.
 *   If no real-time is intended, SCHED_OTHER may be used and librtsaio will
 *   internally use CLOCK_MONOTONIC. For systems that not implementing
 *   clock_gettime(), CLOCK_MONOTONIC nor CLOCK_REALTIME, gettimeofday() is
 *   used instead.
 *
 * @param opt
 *   When 'nrq' is negative, all queue handlers will have the same 'opt'
 *   priority. The 'opt' value shall range from 1 to 32, where 1 is the highest
 *   'sched_policy' priority value and 32 the lowest.
 *   When 'nrq' is positve, the 'opt' value may take a positive or negative value.
 *   In this case, if 'opt' is positive, the value is added to the minimum priority value
 *   (spmin). If the value is negative, the absolute value of 'opt' is subtracted from the
 *   maximum priority value (spmax). The spmin and spmax values are the return values of
 *   sched_get_priority_min() and sched_get_priority_max() system calls, respectively. So
 *   if the 'opt' value is different than 0, it shall take into account these values and
 *   not the range 1 to 32.
 *
 * @param write_notify
 *   Write notification function. This function is called for each time a
 *   write operation completes (either enqueued by rtsaio_write() or
 *   rtsaio_write_eager()). The completeness of an operation doesn't mean
 *   that it completed successfuly. Status and error checking shall be
 *   performed by using the rtsaio_status() and rtsaio_error() functions.
 *
 * @param read_notify
 *   Read notification function. This function is called for each time a
 *   read operation completes (either enqueued by rtsaio_read() or
 *   rtsaio_read_eager()). The completeness of an operation doesn't mean
 *   that it completed successfuly. Status and error checking shall be
 *   performed by using the rtsaio_status() and rtsaio_error() functions.
 *
 * @return
 *   On success, zero is returned. On error, -1 is returned and errno is set
 *   appropriately.
 *   \n\n
 *   Errors: EINVAL, ENOMEM, EAGAIN and EPERM
 * 
 * @see rtsaio_init1()
 * @see rtsaio_destroy()
 * @see rtsaio_write()
 * @see rtsaio_write_eager()
 * @see rtsaio_read()
 * @see rtsaio_read_eager()
 * @see rtsaio_status()
 * @see rtsaio_error()
 * @see rtsaio_count()
 * @see async_op
 *
 */
int rtsaio_init(int nrq,
		int sched_policy,
		int opt,
		void (*write_notify) (struct async_op *aop),
		void (*read_notify) (struct async_op *aop));

/**
 * @brief
 *   Initializes the librtsaio.
 *
 * @param nrq
 *   The number of queues that shall be initialized and one thread per queue is
 *   created. This parameter may receive a positive integer, indicating the use
 *   of prioritized queues, or a negative integer, indicating the absence of
 *   prioritization for the created queues. When prioritized queues are used,
 *   each thread handling the corresponding queue is initialized with different
 *   scheduling priorities. For 'nrq' queues created, the queue 0 will be the
 *   highest priority queue (the thread will have the highest priority
 *   scheduler value), and the 'nrq - 1' queue will have the lowest. If no
 *   prioritization is used, all the queue handlers will have the same scheduler
 *   priority, defined by the parameter 'opt' that shall range from 0 to 32,
 *   where 0 is the highest priority and 32 the lowest for a given
 *   'sched_policy'.
 *
 * @param sched_policy
 *   The scheduler policy that shall be used for the queue handlers (threads).
 *   If real-time is intended, SCHED_FIFO or SCHED_RR shall be used, and
 *   librtsaio will internally use CLOCK_REALTIME for timeout operations.
 *   If no real-time is intended, SCHED_OTHER may be used and librtsaio will
 *   internally use CLOCK_MONOTONIC. For systems that not implementing
 *   clock_gettime(), CLOCK_MONOTONIC nor CLOCK_REALTIME, gettimeofday() is
 *   used instead.
 *
 * @param opt
 *   When 'nrq' is negative, all queue handlers will have the same 'opt'
 *   priority. The 'opt' value shall range from 1 to 32, where 1 is the highest
 *   'sched_policy' priority value and 32 the lowest.
 *   When 'nrq' is positve, the 'opt' value may take a positive or negative value.
 *   In this case, if 'opt' is positive, the value is added to the minimum priority value
 *   (spmin). If the value is negative, the absolute value of 'opt' is subtracted from the
 *   maximum priority value (spmax). The spmin and spmax values are the return values of
 *   sched_get_priority_min() and sched_get_priority_max() system calls, respectively. So
 *   if the 'opt' value is different than 0, it shall take into account these values and
 *   not the range 1 to 32.
 *
 * @param write_notify
 *   Write notification function. This function is called for each time a
 *   write operation completes (either enqueued by rtsaio_write() or
 *   rtsaio_write_eager()). The completeness of an operation doesn't mean
 *   that it completed successfuly. Status and error checking shall be
 *   performed by using the rtsaio_status() and rtsaio_error() functions.
 *
 * @param read_notify
 *   Read notification function. This function is called for each time a
 *   read operation completes (either enqueued by rtsaio_read() or
 *   rtsaio_read_eager()). The completeness of an operation doesn't mean
 *   that it completed successfuly. Status and error checking shall be
 *   performed by using the rtsaio_status() and rtsaio_error() functions.
 *
 * @param flags
 *   Flags field for rtsaio handler configuration.
 *   Currently, only one flag is supported:
 *
 *   RTSAIO_FL_NOPRIO_MOD:
 *   When 'nrq' is negative, indicating the absence of prioritization for the
 *   created queues, the requests will be distributed along the number of
 *   queues (nrq) in a round-robin fashion. This may lead to unpredictable
 *   behaviours if two requests of the same type targetting the same file
 *   descriptor are enqueued at the same time (as in, before the first
 *   request completes) because the order by which these request will be
 *   processed isn't predictable. If more than one request of the same
 *   type targetting the same file descriptor may occur in your implementation,
 *   it is strongly recommended the use of RTSAIO_FL_NOPRIO_MOD in order to
 *   distribute the requests based on the formula MOD(fd, nrq), which will
 *   ensure that all requests targetting the same file descriptor will be
 *   enqueued in the same queue, thus granting the processing order by
 *   which they are requested.
 *
 * @param max_events
 *   When kevent() or epoll_wait() is used (when supported), this parameter
 *   will set the maximum number of events that these calls are allowed to
 *   notify each time they return.
 *
 * @param nmul
 *   A multiplier that will set the limit of notifier threads. This value
 *   is multiplied by nrq.
 *
 * @param nthreshold
 *   When nthreshold elements are present in the notification queue, a new
 *   notifier thread is created.
 *
 * @param ntimeout
 *   The timeout value for a notifier thread will way for work. If this
 *   value (in seconds) is exceeded and the notifier thread performed
 *   no notifications within this period, it is killed.
 *
 * @return
 *   On success, zero is returned. On error, -1 is returned and errno is set
 *   appropriately.
 *   \n\n
 *   Errors: EINVAL, ENOMEM, EAGAIN and EPERM
 * 
 * @see rtsaio_init()
 * @see rtsaio_destroy()
 * @see rtsaio_write()
 * @see rtsaio_write_eager()
 * @see rtsaio_read()
 * @see rtsaio_read_eager()
 * @see rtsaio_status()
 * @see rtsaio_error()
 * @see rtsaio_count()
 * @see async_op
 *
 */
int rtsaio_init1(int nrq,
		 int sched_policy,
		 int opt,
		 void (*write_notify) (struct async_op *aop),
		 void (*read_notify) (struct async_op *aop),
		 int flags,
		 int max_events,
		 int nmul,
		 int nthreshold,
		 int ntimeout);

/**
 * @brief
 *   Uninitializes librtsaio. This function should not be called while
 *   operations are still in progress.
 *
 * @see rtsaio_init()
 * @see rtsaio_cancel()
 * @see rtsaio_suspend()
 *
 */
void rtsaio_destroy(void);

#endif

