/**
 * @file mm.h
 * @brief Real-Time Scalable Asynchronous Input/Output Library (librtsaio)
 *        Memory Management Interface Header
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


#ifndef RTSAIO_MM_H
#define RTSAIO_MM_H

#ifdef USE_LIBFSMA
 #include <fsma/fsma.h>
#endif

void *mm_alloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);
void *mm_calloc(size_t nmemb, size_t size);

#endif
