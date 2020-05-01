/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "ae.h"
#include "zmalloc.h"
#include "config.h"
// 使用#ifdef 来实现多态
/* Include the best multiplexing layer supported by this system.
 * The following should be ordered by performances, descending. */

/**
 * 1、Redis 不仅支持 Linux 下的 epoll，还支持其他的 IO 复用方式，目前支持如下四种：
 *      epoll：支持 Linux 系统
 *      kqueue：支持 FreeBSD 系统 (如 macOS)
 *      select
 *      evport: 支持 Solaris
 *
 *   优先级: evport > epoll > kqueue > select
 *   注：优先级顺序其实也代表了四种 IO 复用方式的性能高低
 * 2、对于每种 IO 复用方式，只要实现以下 8 个接口就可以正常对接 Redis 了：
 *      int aeApiCreate(aeEventLoop *eventLoop);
 *      void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask);
 *      void aeApiResize(aeEventLoop *eventLoop, int setsize);
 *      void aeApiFree(aeEventLoop *eventLoop);
 *      int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask);
 *      void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask);
 *      int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp);
 *      char *aeApiName(void);
 *
 * 3、虽然每个c文件对应的poll机制不同，但都定义了自己的aeApiState以及实现的都是相同的api:
*        aeApiState,
*        　　每种poll机制内部使用的相关结构体，例如：select用到的fdset, epoll用到的epoll_fd以及events数组
*        aeApiCreate,
*            poll机制的初始化，每个poll机制都会在这里分配一个新的aeApiState结构，并做一些特定的初始化操作
*            例如：对于select来说应该是就是初始化fdset，用于select的相关调用；对于epoll来说，需要创建epoll的fd以及epoll使用的events数组
*        aeApiResize,
*            调整poll机制中能处理的事件数目，例如：对于select来说，其实只要不超过fdset的最大值(一般系统默认是1024)它就什么都不做，否则返回错误；对于epoll来说，就是重新分配events数组
*            这个函数只在config阶段会被调用
*        aeApiFree,
*          对于select来说，主要就是释放aeApiState的空间
*        　对于epoll来说，主要就是关闭epoll的fd, 释放aeApiState以及events的空间
*        aeApiAddEvent,
*            对于select来说，就是往某个fd_set里面增加fd
*            对于epoll来说，就是在events中增加/修改感兴趣的事件
*        aeApiDelEvent,
*            对于select来说，就是从某个fd_set里面删除fd
*            对于epoll来说，就是在events中删除/修改感兴趣的事件
*        aeApiPoll,
*        　主要的poll入口，比如select或者epoll_wait
*        aeApiName
*        　返回poll机制的名字，比如select或者epoll*
 * */
#ifdef HAVE_EVPORT
#include "ae_evport.c"
#else
    #ifdef HAVE_EPOLL
    #include "ae_epoll.c"
    #else
        #ifdef HAVE_KQUEUE
        #include "ae_kqueue.c"
        #else
        #include "ae_select.c"
        #endif
    #endif
#endif

/**
 * 创建事件循环对象: setsize为最大事件的的个数，对于epoll来说也是epoll_event的个数
 *
 * eventLoop->events 数组特点：
 *  1、 Linux 内核会给每个进程维护一个文件描述符表。而 POSIX 标准对于文件描述符进行了以下约束：
 *      fd 为 0、1、2 分别表示标准输入、标准输出和错误输出，每次新打开的 fd，必须使用当前进程中最小可用的文件描述符。
 *      Redis 充分利用了文件描述符的这些特点，来存储每个 fd 对应的事件。
 *      在 Redis 的 eventloop 中，直接用了一个连续数组来存储事件信息:
 *  2、在linux环境下，fd 就是这个数组的下标。
    例如，当程序刚刚启动时候，创建监听套接字，按照标准规定，该 fd 的值为 3。此
    时就直接在 eventLoop->events 下标为 3 的元素中存放相应 event 数据。
    过也基于文件描述符的这些特点，意味着 events 数组的前三位一定不会有相应的 fd 赋值。
    那么，Redis 是如何指定 eventloop 的 setsize 的呢？
    server.el = aeCreateEventLoop(server.maxclients+CONFIG_FDSET_INCR);

    maxclients 代表用户配置的最大连接数，可在启动时由 --maxclients 指定，默认为 10000。
    CONFIG_FDSET_INCR 大小为 128。给 Redis 预留一些安全空间。
    也正是因为 Redis 利用了 fd 的这个特点，Redis 只能在完全符合 POSIX 标准的系统中工作。
    其他的例如 Windows 系统，生成的 fd 或者说 HANDLE 更像是个指针，并不符合 POSIX 标准
 * @param setsize
 * @return
 */
aeEventLoop *aeCreateEventLoop(int setsize) {
    aeEventLoop *eventLoop;
    int i;
    //分配该结构体的内存空间
    if ((eventLoop = zmalloc(sizeof(*eventLoop))) == NULL) goto err;
    //给监听fd分配空间，空间大小为setsize
    eventLoop->events = zmalloc(sizeof(aeFileEvent)*setsize);
    //给事件就绪fd分配空间，空间大小为setsize
    eventLoop->fired = zmalloc(sizeof(aeFiredEvent)*setsize);
    if (eventLoop->events == NULL || eventLoop->fired == NULL) goto err;
    //初始化
    eventLoop->setsize = setsize; //最多setsize个事件
    eventLoop->lastTime = time(NULL);
    eventLoop->timeEventHead = NULL;
    eventLoop->timeEventNextId = 0;
    eventLoop->stop = 0;
    eventLoop->maxfd = -1;
    eventLoop->beforesleep = NULL;
    eventLoop->aftersleep = NULL;
    eventLoop->flags = 0;
    // poll初始化：
    // 根据系统不同，选择不同的实现，C里面的多态自然是用 #ifdef 来实现了
    // 具体实现有如下四种：ae_kqueue.c  ae_epoll.c ae_avport.c  ae_select.c
    // 这一步为创建底层IO处理的数据，如epoll，创建epoll_event,和epfd。

    if (aeApiCreate(eventLoop) == -1) goto err;
    /* Events with mask == AE_NONE are not set. So let's initialize the
     * vector with it. */
   /**linux 内核会给每个进程维护一个文件描述符表。而 POSIX 标准对于文件描述符进行了以下约束： fd 为 0、1、2 分别表示标准输入、标准输出和错误输出
    每次新打开的 fd，必须使用当前进程中最小可用的文件描述符。
    Redis 充分利用了文件描述符的这些特点，来存储每个 fd 对应的事件。
    在 Redis 的 eventloop 中，直接用了一个连续数组来存储事件信息
    */
    for (i = 0; i < setsize; i++)
        //数组长度就是 setsize，同时创建之后将每一个 event 的 mask 属性置为 AE_NONE(即是 0)，mask 代表该 fd 注册了哪些事件。
        eventLoop->events[i].mask = AE_NONE;
    return eventLoop;

err:
    if (eventLoop) {
        zfree(eventLoop->events);
        zfree(eventLoop->fired);
        zfree(eventLoop);
    }
    return NULL;
}

/* Return the current set size. */
int aeGetSetSize(aeEventLoop *eventLoop) {
    return eventLoop->setsize;
}

/* Tells the next iteration/s of the event processing to set timeout of 0. */
void aeSetDontWait(aeEventLoop *eventLoop, int noWait) {
    if (noWait)
        eventLoop->flags |= AE_DONT_WAIT;
    else
        eventLoop->flags &= ~AE_DONT_WAIT;
}

/* Resize the maximum set size of the event loop.
 * If the requested set size is smaller than the current set size, but
 * there is already a file descriptor in use that is >= the requested
 * set size minus one, AE_ERR is returned and the operation is not
 * performed at all.
 *
 * Otherwise AE_OK is returned and the operation is successful. */
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize) {
    int i;

    if (setsize == eventLoop->setsize) return AE_OK;
    if (eventLoop->maxfd >= setsize) return AE_ERR;
    if (aeApiResize(eventLoop,setsize) == -1) return AE_ERR;

    eventLoop->events = zrealloc(eventLoop->events,sizeof(aeFileEvent)*setsize);
    eventLoop->fired = zrealloc(eventLoop->fired,sizeof(aeFiredEvent)*setsize);
    eventLoop->setsize = setsize;

    /* Make sure that if we created new slots, they are initialized with
     * an AE_NONE mask. */
    for (i = eventLoop->maxfd+1; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
    return AE_OK;
}

void aeDeleteEventLoop(aeEventLoop *eventLoop) {
    aeApiFree(eventLoop);
    zfree(eventLoop->events);
    zfree(eventLoop->fired);

    /* Free the time events list. */
    aeTimeEvent *next_te, *te = eventLoop->timeEventHead;
    while (te) {
        next_te = te->next;
        zfree(te);
        te = next_te;
    }
    zfree(eventLoop);
}

void aeStop(aeEventLoop *eventLoop) {
    eventLoop->stop = 1;
}
/**
 * 网络 IO 事件注册到eventLoop中：
 * 1、对于创建文件事件，需要传入一个该事件对应的处理程序，当事件发生时，会调用对应的回调函数。
 *  这里设计的aeFileEvent结构体就是将事件源（FD），事件，事件处理程序关联起来。
 * 2、目前可注册的事件有三种:
 *      AE_READABLE 可读事件
 *      AE_WRITABLE 可写事件
 *      AE_BARRIER
 *
 * @param eventLoop  事件循环监听器，功能类似NIO编程中是selector
 * @param fd          socket文件描述符
 * @param mask        掩码
 * @param proc        事件处理器：
 *                     ACCEPT 对应 networking->acceptTcpHandler
 *
 * @param clientData   客户端传过来的数据，默认为空
 * @return
 */
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData)
{
    if (fd >= eventLoop->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }
    aeFileEvent *fe = &eventLoop->events[fd];// 获取events中的对应fd位置了aeFileEvent（因为linux的fd生成规则，所以直接使用fd作为数据下标）

    if (aeApiAddEvent(eventLoop, fd, mask) == -1)
        return AE_ERR;
    fe->mask |= mask;// 更新fe中的事件标识符
    if (mask & AE_READABLE) fe->rfileProc = proc; // 添加读事件处理器
    if (mask & AE_WRITABLE) fe->wfileProc = proc; //添加写事件处理器
    fe->clientData = clientData;
    if (fd > eventLoop->maxfd)//更新eventLoop中最在fd。因为linux规则，与epoll实现函数中的fd句柄类似
        eventLoop->maxfd = fd;
    return AE_OK;
}
/**
 * 网络 IO 事件从eventLoop中删除：
 * @param eventLoop
 * @param fd
 * @param mask
 */
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
    if (fd >= eventLoop->setsize) return;
    aeFileEvent *fe = &eventLoop->events[fd];
    if (fe->mask == AE_NONE) return;

    /* We want to always remove AE_BARRIER if set when AE_WRITABLE
     * is removed. */
    if (mask & AE_WRITABLE) mask |= AE_BARRIER;

    aeApiDelEvent(eventLoop, fd, mask);
    fe->mask = fe->mask & (~mask);
    if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
        /* Update the max fd */
        int j;

        for (j = eventLoop->maxfd-1; j >= 0; j--)
            if (eventLoop->events[j].mask != AE_NONE) break;
        eventLoop->maxfd = j;
    }
}

int aeGetFileEvents(aeEventLoop *eventLoop, int fd) {
    if (fd >= eventLoop->setsize) return 0;
    aeFileEvent *fe = &eventLoop->events[fd];

    return fe->mask;
}

static void aeGetTime(long *seconds, long *milliseconds)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec/1000;
}

static void aeAddMillisecondsToNow(long long milliseconds, long *sec, long *ms) {
    long cur_sec, cur_ms, when_sec, when_ms;

    aeGetTime(&cur_sec, &cur_ms);
    when_sec = cur_sec + milliseconds/1000;
    when_ms = cur_ms + milliseconds%1000;
    if (when_ms >= 1000) {
        when_sec ++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
}

long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc)
{
    long long id = eventLoop->timeEventNextId++;
    aeTimeEvent *te;

    te = zmalloc(sizeof(*te));
    if (te == NULL) return AE_ERR;
    te->id = id;
    aeAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;
    te->prev = NULL;
    te->next = eventLoop->timeEventHead;
    if (te->next)
        te->next->prev = te;
    eventLoop->timeEventHead = te;
    return id;
}

int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id)
{
    aeTimeEvent *te = eventLoop->timeEventHead;
    while(te) {
        if (te->id == id) {
            te->id = AE_DELETED_EVENT_ID;
            return AE_OK;
        }
        te = te->next;
    }
    return AE_ERR; /* NO event with the specified ID found */
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */
static aeTimeEvent *aeSearchNearestTimer(aeEventLoop *eventLoop)
{
    aeTimeEvent *te = eventLoop->timeEventHead;
    aeTimeEvent *nearest = NULL;

    while(te) {
        if (!nearest || te->when_sec < nearest->when_sec ||
                (te->when_sec == nearest->when_sec &&
                 te->when_ms < nearest->when_ms))
            nearest = te;
        te = te->next;
    }
    return nearest;
}

/* Process time events */
static int processTimeEvents(aeEventLoop *eventLoop) {
    int processed = 0;
    aeTimeEvent *te;
    long long maxId;
    time_t now = time(NULL);

    /* If the system clock is moved to the future, and then set back to the
     * right value, time events may be delayed in a random way. Often this
     * means that scheduled operations will not be performed soon enough.
     *
     * Here we try to detect system clock skews, and force all the time
     * events to be processed ASAP when this happens: the idea is that
     * processing events earlier is less dangerous than delaying them
     * indefinitely, and practice suggests it is. */
    if (now < eventLoop->lastTime) {
        te = eventLoop->timeEventHead;
        while(te) {
            te->when_sec = 0;
            te = te->next;
        }
    }
    eventLoop->lastTime = now;

    te = eventLoop->timeEventHead;
    maxId = eventLoop->timeEventNextId-1;
    while(te) {
        long now_sec, now_ms;
        long long id;

        /* Remove events scheduled for deletion. */
        if (te->id == AE_DELETED_EVENT_ID) {
            aeTimeEvent *next = te->next;
            if (te->prev)
                te->prev->next = te->next;
            else
                eventLoop->timeEventHead = te->next;
            if (te->next)
                te->next->prev = te->prev;
            if (te->finalizerProc)
                te->finalizerProc(eventLoop, te->clientData);
            zfree(te);
            te = next;
            continue;
        }

        /* Make sure we don't process time events created by time events in
         * this iteration. Note that this check is currently useless: we always
         * add new timers on the head, however if we change the implementation
         * detail, this check may be useful again: we keep it here for future
         * defense. */
        if (te->id > maxId) {
            te = te->next;
            continue;
        }
        aeGetTime(&now_sec, &now_ms);
        if (now_sec > te->when_sec ||
            (now_sec == te->when_sec && now_ms >= te->when_ms))
        {
            int retval;

            id = te->id;
            retval = te->timeProc(eventLoop, id, te->clientData);
            processed++;
            if (retval != AE_NOMORE) {
                aeAddMillisecondsToNow(retval,&te->when_sec,&te->when_ms);
            } else {
                te->id = AE_DELETED_EVENT_ID;
            }
        }
        te = te->next;
    }
    return processed;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 * if flags has AE_CALL_AFTER_SLEEP set, the aftersleep callback is called.
 *
 * The function returns the number of events processed. */
/** 事件处理*/
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
    int processed = 0, numevents;

    /* Nothing to do? return ASAP */
    /** 没有事件发生时,直接 返回0*/
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
    if (eventLoop->maxfd != -1 ||
        ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        int j;
        aeTimeEvent *shortest = NULL;
        struct timeval tv, *tvp;

        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
            shortest = aeSearchNearestTimer(eventLoop);
        if (shortest) {
            long now_sec, now_ms;

            aeGetTime(&now_sec, &now_ms);
            tvp = &tv;

            /* How many milliseconds we need to wait for the next
             * time event to fire? */
            long long ms =
                (shortest->when_sec - now_sec)*1000 +
                shortest->when_ms - now_ms;

            if (ms > 0) {
                tvp->tv_sec = ms/1000;
                tvp->tv_usec = (ms % 1000)*1000;
            } else {
                tvp->tv_sec = 0;
                tvp->tv_usec = 0;
            }
        } else {
            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to set the timeout
             * to zero */
            if (flags & AE_DONT_WAIT) {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* Otherwise we can block */
                tvp = NULL; /* wait forever */
            }
        }

        if (eventLoop->flags & AE_DONT_WAIT) {
            tv.tv_sec = tv.tv_usec = 0;
            tvp = &tv;
        }

        /* Call the multiplexing API, will return only on timeout or when
         * some event fires. */
        /** 多路利用监听：阻塞直到有数据返回或者超时。windowns默认使用select实现 */
        numevents = aeApiPoll(eventLoop, tvp);

        /* After sleep callback. */
        if (eventLoop->aftersleep != NULL && flags & AE_CALL_AFTER_SLEEP)
            eventLoop->aftersleep(eventLoop);
        /** 当有事件来时，进行事件处理，包括 ACCEPT，READ,WRITE等事件*/
        for (j = 0; j < numevents; j++) {
            aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];
            int mask = eventLoop->fired[j].mask;
            int fd = eventLoop->fired[j].fd;
            int fired = 0; /* Number of events fired for current fd. */

            /* Normally we execute the readable event first, and the writable
             * event laster. This is useful as sometimes we may be able
             * to serve the reply of a query immediately after processing the
             * query.
             *
             * However if AE_BARRIER is set in the mask, our application is
             * asking us to do the reverse: never fire the writable event
             * after the readable. In such a case, we invert the calls.
             * This is useful when, for instance, we want to do things
             * in the beforeSleep() hook, like fsynching a file to disk,
             * before replying to a client. */
            int invert = fe->mask & AE_BARRIER;

            /* Note the "fe->mask & mask & ..." code: maybe an already
             * processed event removed an element that fired and we still
             * didn't processed, so we check if the event is still valid.
             *
             * Fire the readable event if the call sequence is not
             * inverted. */
            /** 读事件处理 */
            if (!invert && fe->mask & mask & AE_READABLE) {
                fe->rfileProc(eventLoop,fd,fe->clientData,mask);//读事件处理器
                fired++;
                fe = &eventLoop->events[fd]; /* Refresh in case of resize. */
            }

            /* Fire the writable event. */
            /** 写事件处理 */
            if (fe->mask & mask & AE_WRITABLE) {
                if (!fired || fe->wfileProc != fe->rfileProc) {
                    fe->wfileProc(eventLoop,fd,fe->clientData,mask);
                    fired++;
                }
            }

            /* If we have to invert the call, fire the readable event now
             * after the writable one. */
            /** 事件转换：当我们读完事件后，要转换为监听写事件。或者写完要转换为读监听 */
            if (invert) {
                fe = &eventLoop->events[fd]; /* Refresh in case of resize. */
                if ((fe->mask & mask & AE_READABLE) &&
                    (!fired || fe->wfileProc != fe->rfileProc))
                {
                    fe->rfileProc(eventLoop,fd,fe->clientData,mask);
                    fired++;
                }
            }
            //事件处理数加+
            processed++;
        }
    }
    /* Check time events */
    if (flags & AE_TIME_EVENTS)
        processed += processTimeEvents(eventLoop);

    return processed; /* return the number of processed file/time events */
}

/* Wait for milliseconds until the given file descriptor becomes
 * writable/readable/exception */
int aeWait(int fd, int mask, long long milliseconds) {
    struct pollfd pfd;
    int retmask = 0, retval;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    if (mask & AE_READABLE) pfd.events |= POLLIN;
    if (mask & AE_WRITABLE) pfd.events |= POLLOUT;

    if ((retval = poll(&pfd, 1, milliseconds))== 1) {
        if (pfd.revents & POLLIN) retmask |= AE_READABLE;
        if (pfd.revents & POLLOUT) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLERR) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLHUP) retmask |= AE_WRITABLE;
        return retmask;
    } else {
        return retval;
    }
}
/**
 * 监听事件主循环器
 * 1、取出最近的一次超时事件。
 * 2、计算该超时事件还有多久才可以触发。
 * 3、等待网络事件触发或者超时(epoll_wait必须要指定一个超时时间。)。
 * 4、处理触发的各个事件，包括网络事件和超时事件
 * @param eventLoop
 */
void aeMain(aeEventLoop *eventLoop) {
    eventLoop->stop = 0;
    while (!eventLoop->stop) {
        if (eventLoop->beforesleep != NULL)
            eventLoop->beforesleep(eventLoop);
        aeProcessEvents(eventLoop, AE_ALL_EVENTS|AE_CALL_AFTER_SLEEP);
    }
}

char *aeGetApiName(void) {
    return aeApiName();
}

void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep) {
    eventLoop->beforesleep = beforesleep;
}

void aeSetAfterSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *aftersleep) {
    eventLoop->aftersleep = aftersleep;
}
