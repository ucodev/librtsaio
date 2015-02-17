/**
 * @file timespec.c
 * @brief Real-Time Scalable Asynchronous Input/Output Library (librtsaio)
 *        RTSAIO Library timespec interface
 *
 * Date: 17-02-2015
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
#include <time.h>

#if !defined(CLOCK_REALTIME) && !defined(CLOCK_MONOTONIC)
#include <sys/time.h>
#endif


void timespec_get(struct timespec *ts, int realtime) {
#if defined(CLOCK_REALTIME) && defined(CLOCK_MONOTONIC)
	if (realtime) {
		clock_gettime(CLOCK_REALTIME, ts);
	} else {
		clock_gettime(CLOCK_MONOTONIC, ts);
	}
#else
	struct timeval tv;

	gettimeofday(&tv, NULL);

	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = tv.tv_usec * 1000;
#endif
}

int timespec_compare(const struct timespec *ts1, const struct timespec *ts2) {
	if (ts1->tv_sec > ts2->tv_sec)
		return 1;

	if (ts1->tv_sec < ts2->tv_sec)
		return -1;

	if (ts1->tv_nsec > ts2->tv_nsec)
		return 1;

	if (ts1->tv_nsec < ts2->tv_nsec)
		return -1;

	return 0;
}

void timespec_sub(struct timespec *dest, const struct timespec *src) {
	long tmp = dest->tv_nsec - src->tv_nsec;

	dest->tv_sec = dest->tv_sec - src->tv_sec - (tmp < 0);
	dest->tv_nsec = (tmp < 0) ? 1000000000 + tmp : tmp;
}

void timespec_add(struct timespec *dest, const struct timespec *src) {
	long tmp = src->tv_nsec + dest->tv_nsec;

	dest->tv_sec += src->tv_sec + (tmp > 999999999);
	dest->tv_nsec = (tmp > 999999999) ? tmp - 1000000000 : tmp;
}

