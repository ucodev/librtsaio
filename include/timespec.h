/**
 * @file timespec.h
 * @brief Real-Time Scalable Asynchronous Input/Output Library (librtsaio)
 *        RTSAIO Library timespec interface header
 *
 * Date: 31-08-2012
 * 
 * Copyright 2012 Pedro A. Hortas (pah@ucodev.org)
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

#ifndef RTSAIO_TIMESPEC_H
#define RTSAIO_TIMESPEC_H

#include <time.h>

void timespec_get(struct timespec *ts, int realtime);
int timespec_compare(const struct timespec *ts1, const struct timespec *ts2);
void timespec_sub(struct timespec *dest, const struct timespec *src);
void timespec_add(struct timespec *dest, const struct timespec *src);

#endif
