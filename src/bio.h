/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
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
 * redis的正常指令处理都是单线程的，没有创建子线程。指令谁先到就先处理谁。
 * 但其实在后台还是创建出了一些子线程，处理一些可以异步但比较耗时的任务，主要用于close(2)，fsync(2)的操作
 */
#ifndef __BIO_H
#define __BIO_H

/* Exported API */
void bioInit(void);
void bioCreateBackgroundJob(int type, void *arg1, void *arg2, void *arg3);
unsigned long long bioPendingJobsOfType(int type);
unsigned long long bioWaitStepOfType(int type);
time_t bioOlderJobOfType(int type);
void bioKillThreads(void);

/* Background job opcodes */
/* 后台任务的定义，目前定义了3个后台任务 */

#define BIO_CLOSE_FILE    0 /* Deferred close(2) syscall. 系统调用close(2)(其中2是fd)，主要是进行大文件释放，比如replication.c ->bg_unlink() */
#define BIO_AOF_FSYNC     1 /* Deferred AOF fsync. aof文件的追加刷盘(fsync函数)同步：延迟刷盘时，调用fsync()函数，直接等待磁盘数据刷好(落盘)才返回*/
#define BIO_LAZY_FREE     2 /* Deferred objects freeing. (4.0后新增,说明之后优化redis后台任务也可能新增 )就是后台释放内存操作，比如 unlink命令， 或者大对象的渐进式释放 dbAsyncDelete()方法*/
#define BIO_NUM_OPS       3 /** 限定了当前任务类型最大值不能大于3，当大于3时即认为是不正常的任务*/

#endif
