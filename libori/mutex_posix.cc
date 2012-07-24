/*
 * Copyright (c) 2012 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Mutex Class
 * Copyright (c) 2005 Ali Mashtizadeh
 * All rights reserved.
 */

#include "mutex.h"

Mutex::Mutex()
{
    pthread_mutex_init(&lockHandle, NULL);
}

Mutex::~Mutex()
{
    pthread_mutex_destroy(&lockHandle);
}

void Mutex::lock()
{
    pthread_mutex_lock(&lockHandle);
}

void Mutex::unlock()
{
    pthread_mutex_unlock(&lockHandle);
}

bool Mutex::tryLock()
{
    return (pthread_mutex_trylock(&lockHandle) == 0);
}

