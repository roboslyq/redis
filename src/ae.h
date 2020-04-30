/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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
/**
 * 1、 Redis没有选择libevent以及libev为其事件模型库，而是自己写了一个异步事件库模型库-AE:“A simple event-driven programming library”。
 *
 * 2、ae模块封装了 select、epoll、avport 以及 kqueue 这些 I/O 多路复用函数，为上层提供了相同的接口。
 *   主要支持了epoll、select、kqueue、以及基于Solaris的event ports。主要提供了对两种类型的事件驱动：
 *      1）、 IO事件（文件事件），包括有IO的读事件和写事件。
 *      2）、 定时器事件，包括有一次性定时器和循环定时器。
 *      3)、 Reactor模式，串行处理事件
 *      4)、 具有定时事件功能（但是不能过多，因为是使用链表实现的）
 *      5)、 优先处理读事件
 *
 * 3、ae库源文件如下：
 *      文件	            用途
 *      ae.h	        AE事件库接口定义
 *      ae.c	        AE事件库实现
 *      ae_epoll.c	    epoll绑定
 *      ae_evport.c 	evport绑定
 *      ae_kqueue.c	    kqueue绑定
 *      ae_select.c	    select绑定
 */
#ifndef __AE_H__
#define __AE_H__

#include <time.h>

#define AE_OK 0
#define AE_ERR -1
/** ae框架处理两类事件，file event和time event */

#define AE_NONE 0       /* No events registered. 没有事件注册 */
#define AE_READABLE 1   /* Fire when descriptor is readable. 有可读事件*/
#define AE_WRITABLE 2   /* Fire when descriptor is writable. 有可写事件*/
#define AE_BARRIER 4    /* With WRITABLE, never fire the event if the
                           READABLE event already fired in the same event
                           loop iteration. Useful when you want to persist
                           things to disk before sending replies, and want
                           to do that in a group fashion. 对于WRITABLE，如果可读事件已经在同一个事件循环迭代中触发，则不要触发该事件。
                           当您希望在发送回复之前将内容持久化到磁盘，并且希望以组的方式执行此操作时，此选项非常有用*/

#define AE_FILE_EVENTS 1
#define AE_TIME_EVENTS 2
#define AE_ALL_EVENTS (AE_FILE_EVENTS|AE_TIME_EVENTS)
#define AE_DONT_WAIT 4
#define AE_CALL_AFTER_SLEEP 8

#define AE_NOMORE -1
#define AE_DELETED_EVENT_ID -1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

//前置声明，避免了编译出错，因为aeEventLoop需要用到aeFileEvent结构体
struct aeEventLoop;

//为方便使用定义的函数指针别名
/* Types and data structures */
//定义文件事件处理接口（函数指针）
typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
//时间事件处理接口（函数指针），该函数返回定时的时长
typedef int aeTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData);
typedef void aeEventFinalizerProc(struct aeEventLoop *eventLoop, void *clientData);
//aeMain中使用，在调用处理事件前调用
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

/* File event structure */
/** 定义一个文件事件结构，本质是一个数组*/
typedef struct aeFileEvent {
    //文件事件结构体
    int mask; /* one of AE_(READABLE|WRITABLE|BARRIER)  */
    //读事件的处理函数
    aeFileProc *rfileProc;
    //写事件的处理函数
    aeFileProc *wfileProc;
    //传递给上述两个函数的数据
    void *clientData;
} aeFileEvent;

/* Time event structure */
/** 定义一个时间事件结构：本质是一个链表*/
typedef struct aeTimeEvent {
    //时间事件标识符，用于唯一标识该时间事件，并且用于删除时间事件
    long long id; /* time event identifier. */
    long when_sec; /* seconds */
    long when_ms; /* milliseconds */
    //该事件对应的处理程序
    aeTimeProc *timeProc;
    //时间事件的最后一次处理程序，若已设置，则删除时间事件时会被调用
    aeEventFinalizerProc *finalizerProc;
    void *clientData;
    struct aeTimeEvent *prev;
    struct aeTimeEvent *next;
} aeTimeEvent;

/* A fired event */
/**  触发，表示即将执行的事件*/
typedef struct aeFiredEvent {
    int fd;
    int mask;
} aeFiredEvent;

/* State of an event based program */
/* 事件对象  进行任务处理的重要数据结构。*/
typedef struct aeEventLoop {
    //最大文件描述符的值
    int maxfd;   /* highest file descriptor currently registered */
    //文件描述符的最大监听数
    int setsize; /* max number of file descriptors tracked  同时支持的连接数*/
    //用于生成时间事件的唯一标识id
    long long timeEventNextId;
    //用于检测系统时间是否变更（判断标准 now<lastTime）
    time_t lastTime;     /* Used to detect system clock skew */
    //注册要使用的文件事件，这里的分离表实现为直接索引，即通过fd来访问，实现事件的分离
    aeFileEvent *events; /* Registered events */
    //已触发的事件
    aeFiredEvent *fired; /* Fired events */
    aeTimeEvent *timeEventHead;
    //停止标志，1表示停止
    int stop;
    //这个是处理底层特定API的数据，对于epoll来说，该结构体包含了epoll fd和epoll_event
    void *apidata; /* This is used for polling API specific data */
    //在调用processEvent前（即如果没有事件则睡眠），调用该处理函数
    aeBeforeSleepProc *beforesleep;
    aeBeforeSleepProc *aftersleep;
    int flags;
} aeEventLoop;

/* Prototypes */
/** 创建一个新的循环事件*/
aeEventLoop *aeCreateEventLoop(int setsize);
/** 删除一个新的循环事件*/
void aeDeleteEventLoop(aeEventLoop *eventLoop);
/** 停止AE*/
void aeStop(aeEventLoop *eventLoop);
/**
 *  创建一个File事件
 * @param eventLoop
 * @param fd
 * @param mask
 * @param proc
 * @param clientData
 * @return
 */
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData);
/** 删除一个File事件*/
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask);
/** 获取所有的File事件*/
int aeGetFileEvents(aeEventLoop *eventLoop, int fd);
/** 创建一个时间事件*/
long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc);
/** 删除一个时间事件*/
int aeDeleteTimeEvent(aeEventLoop *eventLoop, long long id);
/** 获取所有的Time事件*/
int aeProcessEvents(aeEventLoop *eventLoop, int flags);
/** 等待 */
int aeWait(int fd, int mask, long long milliseconds);
void aeMain(aeEventLoop *eventLoop);
char *aeGetApiName(void);
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep);
void aeSetAfterSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *aftersleep);
int aeGetSetSize(aeEventLoop *eventLoop);
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);
void aeSetDontWait(aeEventLoop *eventLoop, int noWait);

#endif
