/* Background I/O service for Redis.
 *
 * This file implements operations that we need to perform in the background.
 * Currently there is only a single operation, that is a background close(2)
 * system call. This is needed as when the process is the last owner of a
 * reference to a file closing it means unlinking it, and the deletion of the
 * file is slow, blocking the server.
 *
 * In the future we'll either continue implementing new things we need or
 * we'll switch to libeio. However there are probably long term uses for this
 * file as we may want to put here Redis specific background tasks (for instance
 * it is not impossible that we'll need a non blocking FLUSHDB/FLUSHALL
 * implementation).
 *
 * DESIGN
 * ------
 *
 * The design is trivial, we have a structure representing a job to perform
 * and a different thread and job queue for every job type.
 * Every thread waits for new jobs in its queue, and process every job
 * sequentially.
 *
 * Jobs of the same type are guaranteed to be processed from the least
 * recently inserted to the most recently inserted (older jobs processed
 * first).
 *
 * Currently there is no way for the creator of the job to be notified about
 * the completion of the operation, this will only be added when/if needed.
 *
 * ----------------------------------------------------------------------------
 *
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


#include "server.h"
#include "bio.h"
static pthread_t bio_threads[BIO_NUM_OPS];          //保存pthread,数组大小为3
static pthread_mutex_t bio_mutex[BIO_NUM_OPS];      //保存mutex
static pthread_cond_t bio_newjob_cond[BIO_NUM_OPS]; //保存新任务条件变量
static pthread_cond_t bio_step_cond[BIO_NUM_OPS];   //保存step条件变量
// 存放工作的队列就一个list数组.即每种任务类型均是一个List
static list *bio_jobs[BIO_NUM_OPS];
/* The following array is used to hold the number of pending jobs for every
 * OP type. This allows us to export the bioPendingJobsOfType() API that is
 * useful when the main thread wants to perform some operation that may involve
 * objects shared with the background thread. The main thread will just wait
 * that there are no longer jobs of this type to be executed before performing
 * the sensible operation. This data is also useful for reporting. */
// 记录每种类型 job 队列里有多少 job 等待执行
static unsigned long long bio_pending[BIO_NUM_OPS];

/* This structure represents a background Job. It is only used locally to this
 * file as the API does not expose the internals at all. */
struct bio_job {
    time_t time; /* Time at which the job was created.  任务创建时间*/
    /* Job specific arguments pointers. If we need to pass more than three
     * arguments we can just pass a pointer to a structure or alike. */
    void *arg1, *arg2, *arg3; //三个保存的参数
};

void *bioProcessBackgroundJobs(void *arg);
void lazyfreeFreeObjectFromBioThread(robj *o);
void lazyfreeFreeDatabaseFromBioThread(dict *ht1, dict *ht2);
void lazyfreeFreeSlotsMapFromBioThread(zskiplist *sl);

/* Make sure we have enough stack to perform all the things we do in the
 * main thread. */
#define REDIS_THREAD_STACK_SIZE (1024*1024*4)

/* Initialize the background system, spawning the thread. */
/**
 * background I/O service，后台I/O服务，是redis的aof持久化后台服务
 * 在服务启动时,由server.c -> InitServerLast()中被调用,完成初始化.
 * */
void bioInit(void) {
    pthread_attr_t attr;
    pthread_t thread;
    size_t stacksize;
    int j;

    /* Initialization of state vars and objects */
    /** 初始化3种任务类型相关状态及参数 */
    for (j = 0; j < BIO_NUM_OPS; j++) {
        pthread_mutex_init(&bio_mutex[j],NULL);
        pthread_cond_init(&bio_newjob_cond[j],NULL);
        pthread_cond_init(&bio_step_cond[j],NULL);
        bio_jobs[j] = listCreate(); //创建任务队列
        bio_pending[j] = 0;         //等待数量是0
    }

    /* Set the stack size as by default it may be small in some system */
    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr,&stacksize);
    if (!stacksize) stacksize = 1; /* The world is full of Solaris Fixes */
    while (stacksize < REDIS_THREAD_STACK_SIZE) stacksize *= 2;
    pthread_attr_setstacksize(&attr, stacksize);

    /* Ready to spawn our threads. We use the single argument the thread
     * function accepts in order to pass the job ID the thread is
     * responsible of. */
    for (j = 0; j < BIO_NUM_OPS; j++) {
        void *arg = (void*)(unsigned long) j;
        //创建一个线程，来执行相应的任务，具体的任务定义在bio.h中。目前有，0，1，2三种类型。而BI_NUM_OPS等于3，因此刚
        //好创建且个三线，来执行三种后台任务
        if (pthread_create(&thread,&attr,bioProcessBackgroundJobs,arg) != 0) {//创建线程,线程创建完成后，会调用bioProcessBackgroundJobs
            serverLog(LL_WARNING,"Fatal: Can't initialize Background Jobs.");
            exit(1);
        }
        bio_threads[j] = thread;
    }
}
/**
 * 主线程创建后台任务，唤醒后台任务线程执行任务,供其它模块调用。
 * 目前已经有如下模块调用了此方法：
 *   1、aof.c
 *      aof_background_fsync(int fd) -> bioCreateBackgroundJob(BIO_AOF_FSYNC,(void*)(long)fd,NULL,NULL);
 *      backgroundRewriteDoneHandler(int exitcode, int bysignal) -> bioCreateBackgroundJob(BIO_CLOSE_FILE,(void*)(long)oldfd,NULL,NULL);
 *   2.lazyfree.c
 *       dbAsyncDelete(redisDb *db, robj *key) -> bioCreateBackgroundJob(BIO_LAZY_FREE,val,NULL,NULL);
 *   3.replication.c
 *       bg_unlink(const char *filename)->bioCreateBackgroundJob(BIO_CLOSE_FILE,(void*)(long)fd,NULL,NULL);
 *       readSyncBulkPayload(connection *conn)->bioCreateBackgroundJob(BIO_CLOSE_FILE,(void*)(long)fd,NULL,NULL);
 *
 * @param type  任务类型，目前只有3种BIO_CLOSE_FILE BIO_AOF_FSYNC BIO_LAZY_FREE
 * @param arg1  通常为要处理的fd
 * @param arg2
 * @param arg3
 * @return
 */
void bioCreateBackgroundJob(int type, void *arg1, void *arg2, void *arg3) {
    struct bio_job *job = zmalloc(sizeof(*job));

    job->time = time(NULL);
    job->arg1 = arg1;
    job->arg2 = arg2;
    job->arg3 = arg3;
    //互斥锁加锁
    pthread_mutex_lock(&bio_mutex[type]);
    // 将任务添加到对应的任务类型列表中
    listAddNodeTail(bio_jobs[type],job);
    //对应任务类型的任务数量+1
    bio_pending[type]++;
    //条件变量生产
    pthread_cond_signal(&bio_newjob_cond[type]);
    //互斥锁解锁
    pthread_mutex_unlock(&bio_mutex[type]);
//   以下代码为本人添加(因为不太熟悉pthread),所以添加记录pthread常用锁的4个函数:条件变量消费
//    int pthread_cond_wait(pthread_cond_t *cptr,pthread_mutex_t *mptr);
}

/**
 * 处理后台任务，后台线程启动函数
 * 整理流程：
 *      1获取锁，判断任务是否存在，不存在则进入cond_wait,存在就提取出一个任务，然后根据任务类型，进行操作。
 *      2.执行结束后唤醒所有的step_cond，再次获取锁。删除完成的任务。进入循环
 * @param arg  任务类型
 * @return
 */
void *bioProcessBackgroundJobs(void *arg) {
    struct bio_job *job;
    unsigned long type = (unsigned long) arg;
    sigset_t sigset;

    /* Check that the type is within the right interval. */
    /* 检查任务类型是否合法 */
    if (type >= BIO_NUM_OPS) {
        serverLog(LL_WARNING,
            "Warning: bio thread started with wrong type %lu",type);
        return NULL;
    }
    //根据type来定义thread的名称
    switch (type) {
    case BIO_CLOSE_FILE:
        redis_set_thread_title("bio_close_file");
        break;
    case BIO_AOF_FSYNC:
        redis_set_thread_title("bio_aof_fsync");
        break;
    case BIO_LAZY_FREE:
        redis_set_thread_title("bio_lazy_free");
        break;
    }

    /* Make the thread killable at any time, so that bioKillThreads()
     * can work reliably. */
    /** 设置BIO线程能随时被kill掉，通常通过bioKillThreads()方法触发Kill操作*/
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);            //设置响应cancel
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);       //设置立即结束

    pthread_mutex_lock(&bio_mutex[type]);   //锁住互斥
    /* Block SIGALRM so we are sure that only the main thread will
     * receive the watchdog signal. */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    //只让主线程处理 SIGALRM
    if (pthread_sigmask(SIG_BLOCK, &sigset, NULL))
        serverLog(LL_WARNING,
            "Warning: can't mask SIGALRM in bio.c thread: %s", strerror(errno));
    //死循环，一直在后台运行
    while(1) {
        listNode *ln;

        /* The loop always starts with the lock hold. */
        /** 如果没有任务，则进入wait状态，如果被唤醒则continue（因为可能意外被唤醒，故再次循环检查有没有可执行的任务） */
        if (listLength(bio_jobs[type]) == 0) {
            pthread_cond_wait(&bio_newjob_cond[type],&bio_mutex[type]);
            continue;//防止意外唤醒
        }
        /* Pop the job from the queue. */
        /** 从任务队列中获取一个任务*/
        ln = listFirst(bio_jobs[type]);// 获取新的任务
        job = ln->value;
        /* It is now possible to unlock the background system as we know have
         * a stand alone job structure to process.*/
        // 释放锁可以加入新的任务
        pthread_mutex_unlock(&bio_mutex[type]);

        /* Process the job accordingly to its type. */
        //判断自己的类型
        if (type == BIO_CLOSE_FILE) {
            close((long)job->arg1);
        } else if (type == BIO_AOF_FSYNC) {
            redis_fsync((long)job->arg1);
        } else if (type == BIO_LAZY_FREE) {
            /* What we free changes depending on what arguments are set:
             * arg1 -> free the object at pointer.
             * arg2 & arg3 -> free two dictionaries (a Redis DB).
             * only arg3 -> free the skiplist. */
            if (job->arg1)
                lazyfreeFreeObjectFromBioThread(job->arg1);
            else if (job->arg2 && job->arg3)
                lazyfreeFreeDatabaseFromBioThread(job->arg2,job->arg3);
            else if (job->arg3)
                lazyfreeFreeSlotsMapFromBioThread(job->arg3);
        } else {
            serverPanic("Wrong job type in bioProcessBackgroundJobs().");
        }
        zfree(job);

        /* Lock again before reiterating the loop, if there are no longer
         * jobs to process we'll block again in pthread_cond_wait(). */
        pthread_mutex_lock(&bio_mutex[type]); //锁住
        listDelNode(bio_jobs[type],ln); //删除完成的任务
        bio_pending[type]--;

        /* Unblock threads blocked on bioWaitStepOfType() if any. */
        pthread_cond_broadcast(&bio_step_cond[type]);//唤醒所有的 setp_cond
    }
}

/* Return the number of pending jobs of the specified type. */
unsigned long long bioPendingJobsOfType(int type) {
    unsigned long long val;
    pthread_mutex_lock(&bio_mutex[type]);
    val = bio_pending[type];
    pthread_mutex_unlock(&bio_mutex[type]);
    return val;
}

/* If there are pending jobs for the specified type, the function blocks
 * and waits that the next job was processed. Otherwise the function
 * does not block and returns ASAP.
 *
 * The function returns the number of jobs still to process of the
 * requested type.
 *
 * This function is useful when from another thread, we want to wait
 * a bio.c thread to do more work in a blocking way.
 */
unsigned long long bioWaitStepOfType(int type) {
    unsigned long long val;
    pthread_mutex_lock(&bio_mutex[type]);
    val = bio_pending[type];
    if (val != 0) {
        pthread_cond_wait(&bio_step_cond[type],&bio_mutex[type]);
        val = bio_pending[type];
    }
    pthread_mutex_unlock(&bio_mutex[type]);
    return val;
}

/* Kill the running bio threads in an unclean way. This function should be
 * used only when it's critical to stop the threads for some reason.
 * Currently Redis does this only on crash (for instance on SIGSEGV) in order
 * to perform a fast memory check without other threads messing with memory. */
void bioKillThreads(void) {
    int err, j;

    for (j = 0; j < BIO_NUM_OPS; j++) {
        if (pthread_cancel(bio_threads[j]) == 0) {
            if ((err = pthread_join(bio_threads[j],NULL)) != 0) {
                serverLog(LL_WARNING,
                    "Bio thread for job type #%d can be joined: %s",
                        j, strerror(err));
            } else {
                serverLog(LL_WARNING,
                    "Bio thread for job type #%d terminated",j);
            }
        }
    }
}
