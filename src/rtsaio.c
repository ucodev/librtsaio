/**
 * @file rtsaio.c
 * @brief Real-Time Scalable Asynchronous Input/Output Library (librtsaio)
 *        RTSAIO Library Interface
 *
 * Date: 14-05-2014
 * 
 * Copyright 2012-2014 Pedro A. Hortas (pah@ucodev.org)
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
#include <stdlib.h>
#include <errno.h>

#include "mm.h"
#include "asyncop.h"
#include "rtsaio.h"

static struct rtsaio_handler *handler = NULL;

static void _saio_write_notify(union async_arg arg) {
	struct async_op *aop = arg.argp;

	handler->_write_notify(aop);
}

static void _saio_read_notify(union async_arg arg) {
	struct async_op *aop = arg.argp;

	handler->_read_notify(aop);
}

int rtsaio_write(struct async_op *aop) {
	if (!aop->notify)
		aop->notify = &_saio_write_notify;

	aop->arg.argp = aop;

	return handler->async->write(handler->async, aop);
}

int rtsaio_write_eager(struct async_op *aop) {
	if (!aop->notify)
		aop->notify = &_saio_write_notify;

	aop->arg.argp = aop;

	return handler->async->write_eager(handler->async, aop);
}

int rtsaio_read(struct async_op *aop) {
	if (!aop->notify)
		aop->notify = &_saio_read_notify;

	aop->arg.argp = aop;

	return handler->async->read(handler->async, aop);
}

int rtsaio_read_eager(struct async_op *aop) {
	if (!aop->notify)
		aop->notify = &_saio_read_notify;

	aop->arg.argp = aop;

	return handler->async->read_eager(handler->async, aop);
}

int rtsaio_cancel(struct async_op *aop) {
	return handler->async->cancel(handler->async, aop);
}

int rtsaio_suspend(struct async_op *aop) {
	return handler->async->suspend(handler->async, aop);
}

int rtsaio_error(struct async_op *aop) {
	return handler->async->error(handler->async, aop);
}

int rtsaio_status(struct async_op *aop) {
	return handler->async->status(handler->async, aop);
}

size_t rtsaio_count(struct async_op *aop) {
	return handler->async->count(handler->async, aop);
}

struct async_stat *rtsaio_stat(void) {
	return handler->async->stat(handler->async);
}

void rtsaio_stat_reset(void) {
	handler->async->stat_reset(handler->async);
}

int rtsaio_init(int nrq,
		int sched_policy,
		int opt,
		void (*write_notify) (struct async_op *aop),
		void (*read_notify) (struct async_op *aop))
{
	return rtsaio_init1(nrq, sched_policy, opt, write_notify, read_notify, 0, 0, 0, 0, 0);
}

int rtsaio_init1(int nrq,
		 int sched_policy,
		 int opt,
		 void (*write_notify) (struct async_op *aop),
		 void (*read_notify) (struct async_op *aop),
		 int flags,
		 int max_events,
		 int nmul,
		 int nthreshold,
		 int ntimeout)
{
	int errsv = 0;

	if (!write_notify || !read_notify) {
		errno = EINVAL;
		return -1;
	}

	if (!(handler = mm_alloc(sizeof(struct rtsaio_handler))))
		return -1;

	memset(handler, 0, sizeof(struct rtsaio_handler));

	if (!(handler->async = async_init(nrq, sched_policy, opt, flags, max_events, nmul, nthreshold, ntimeout))) {
		errsv = errno;
		mm_free(handler);
		errno = errsv;
		return -1;
	}

	handler->_write_notify = write_notify;
	handler->_read_notify = read_notify;

	return 0;
}


void rtsaio_destroy(void) {
	async_destroy(handler->async);
	mm_free(handler);
}

