//  Copyright (C) 2015-2019 Virgil Security, Inc.
//
//  All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      (1) Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//      (2) Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in
//      the documentation and/or other materials provided with the
//      distribution.
//
//      (3) Neither the name of the copyright holder nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY EXPRESS OR
//  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
//  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//  DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
//  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
//  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
//  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
//  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
//  IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//  POSSIBILITY OF SUCH DAMAGE.
//
//  Lead Maintainer: Virgil Security Inc. <support@virgilsecurity.com>

#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <stdio.h>
#include <virgil/iot/protocols/sdmp/PRVS.h>

// For the simplest implementation of os_event
static int _wait_flag = 0;
static pthread_mutex_t _wait_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _wait_cond = PTHREAD_COND_INITIALIZER;

/******************************************************************************/
static int
vs_prvs_stop_wait_func(void) {
    if (0 != pthread_mutex_lock(&_wait_mutex)) {
        //        fprintf(stderr, "pthread_mutex_lock 1 %s %d\n", strerror(errno), errno);
    }

    if (0 != pthread_cond_signal(&_wait_cond)) {
        fprintf(stderr, "pthread_cond_signal %s\n", strerror(errno));
    }

    _wait_flag = 1;

    if (0 != pthread_mutex_unlock(&_wait_mutex)) {
        fprintf(stderr, "pthread_mutex_unlock 1 %s\n", strerror(errno));
    }

    return 0;
}

/******************************************************************************/
static bool
_is_greater_timespec(struct timespec a, struct timespec b) {
    if (a.tv_sec == b.tv_sec) {
        return a.tv_nsec > b.tv_nsec;
    }

    return a.tv_sec > b.tv_sec;
}

/******************************************************************************/
static int
vs_prvs_wait_func(size_t wait_ms) {
    struct timespec time_to_wait;
    struct timespec ts_now;
    struct timeval now;

    _wait_flag = 0;
    if (0 != pthread_mutex_init(&_wait_mutex, NULL)) {
        fprintf(stderr, "pthread_mutex_init %s\n", strerror(errno));
    }

    if (0 != pthread_cond_init(&_wait_cond, NULL)) {
        fprintf(stderr, "pthread_cond_init %s\n", strerror(errno));
    }

    gettimeofday(&now, NULL);

    time_to_wait.tv_sec = now.tv_sec + wait_ms / 1000UL;
    time_to_wait.tv_nsec = (now.tv_usec + 1000UL * (wait_ms % 1000UL)) * 1000UL;

    //    fprintf(stderr, "pthread_cond_timedwait ts_now(%lld.%.9ld)  time_to_wait(%lld.%.9ld)\n", (long
    //    long)now.tv_sec, now.tv_usec * 1000UL, (long long)time_to_wait.tv_sec, time_to_wait.tv_nsec);

    if (0 != pthread_mutex_lock(&_wait_mutex)) {
        fprintf(stderr, "pthread_mutex_lock %s\n", strerror(errno));
    }

    do {
        if (0 != pthread_cond_timedwait(&_wait_cond, &_wait_mutex, &time_to_wait)) {
            fprintf(stderr, "pthread_cond_timedwait %s _wait_flag = %d\n", strerror(errno), _wait_flag);
        }
        gettimeofday(&now, NULL);
        ts_now.tv_sec = now.tv_sec;
        ts_now.tv_nsec = now.tv_usec * 1000UL;
        //        fprintf(stderr, "ts_now(%lld.%.9ld) > time_to_wait(%lld.%.9ld) : %s\n", (long long)ts_now.tv_sec,
        //        ts_now.tv_nsec, (long long)time_to_wait.tv_sec, time_to_wait.tv_nsec, _is_greater_timespec(ts_now,
        //        time_to_wait) ? "YES" : "NO");
    } while (!_wait_flag && !_is_greater_timespec(ts_now, time_to_wait));

    pthread_mutex_unlock(&_wait_mutex);

    pthread_cond_destroy(&_wait_cond);
    pthread_mutex_destroy(&_wait_mutex);

    return 0;
}

/******************************************************************************/
vs_sdmp_prvs_impl_t
vs_prvs_impl() {
    vs_sdmp_prvs_impl_t res;

    memset(&res, 0, sizeof(res));

    res.stop_wait_func = vs_prvs_stop_wait_func;
    res.wait_func = vs_prvs_wait_func;

    return res;
}