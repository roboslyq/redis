/* zmalloc - total amount of allocated memory aware version of malloc()
 *  1、malloc()本身能够保证所分配的内存是8字节对齐的：如果你要分配的内存不是8的倍数，那么malloc就会多分配一点，来凑成8的倍数。
 *      所以update_zmalloc_stat_alloc函数（或者说zmalloc()相对malloc()而言）真正要实现的功能并不是进行8字节对齐（malloc已经保证了），
 *      它的真正目的是使变量used_memory精确的维护实际已分配内存的大小。
 *  2、感知由malloc()分配的内存数量：即可以精确的统计由Malloc分配的内存
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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
 * 1、先判断是否使用tcmalloc，如果是，会用tcmalloc对应的函数替换掉标准的libc中的malloc和free函数实现。
 * 2、其次会判断jemalloc是否可用
 * 3、最后如果都没有使用才会用标准的libc中的内存管理函数。
 * 注意：redis2.4.4及以上版本中，jemalloc已经作为源码包的一部分包含在源码包中，路径是antirez/redis/deps/jemalloc，
 *      所以可以直接被使用。而如果你要使用tcmalloc的话，是需要自己安装的。
 */
#ifndef __ZMALLOC_H
#define __ZMALLOC_H

/* Double expansion needed for stringification of macro values. */
#define __xstr(s) __str(s)
#define __str(s) #s
/**
 * 1、编译时选择malloc： 默认先判断TCMALLOC
 * 2、由USE_TCMALLOC、USE_JEMALLOC和__APPLE__控制使用哪种malloc。而这三个常量在Makefile中定义。
 *     以redis-server为例，使用TCMalloc优化redis只需：make USE_TCMALLOC=yes
 * 3、操作系统不同，提供的内存分配接口不同(主要是系统有没有记录已经分配内存大小，如果有就直接使用系统接口，如果没有就需要redis自己实现记录)
 *   具体情况是：jemalloc ，tcmalloc 或者apple系统下，都提供了检测内存块大小的函数，因此 zmalloc_size就使用相应的库函数。
 *             如果默认使用libc的话则 zmalloc_size函数有以下的定义
 * */
#if defined(USE_TCMALLOC)
#define ZMALLOC_LIB ("tcmalloc-" __xstr(TC_VERSION_MAJOR) "." __xstr(TC_VERSION_MINOR))
#include <google/tcmalloc.h>
#if (TC_VERSION_MAJOR == 1 && TC_VERSION_MINOR >= 6) || (TC_VERSION_MAJOR > 1)
#define HAVE_MALLOC_SIZE 1
#define zmalloc_size(p) tc_malloc_size(p)
#else
#error "Newer version of tcmalloc required"
#endif
#elif defined(USE_JEMALLOC) /**  其次JEMALLOC */
#define ZMALLOC_LIB ("jemalloc-" __xstr(JEMALLOC_VERSION_MAJOR) "." __xstr(JEMALLOC_VERSION_MINOR) "." __xstr(JEMALLOC_VERSION_BUGFIX))
#include <jemalloc/jemalloc.h>
#if (JEMALLOC_VERSION_MAJOR == 2 && JEMALLOC_VERSION_MINOR >= 1) || (JEMALLOC_VERSION_MAJOR > 2)
#define HAVE_MALLOC_SIZE 1
#define zmalloc_size(p) je_malloc_usable_size(p)
#else
#error "Newer version of jemalloc required"
#endif

#elif defined(__APPLE__)
#include <malloc/malloc.h>
#define HAVE_MALLOC_SIZE 1
#define zmalloc_size(p) malloc_size(p)
#endif

#ifndef ZMALLOC_LIB
#define ZMALLOC_LIB "libc" /** 最后使用libc */
#ifdef __GLIBC__
#include <malloc.h>
#define HAVE_MALLOC_SIZE 1
#define zmalloc_size(p) malloc_usable_size(p)
#endif
#endif

/* We can enable the Redis defrag capabilities only if we are using Jemalloc
 * and the version used is our special version modified for Redis having
 * the ability to return per-allocation fragmentation hints. */
/**
 * 当我们使用Jemalloc进行内存分配时，我们就有能力进行内存的碎片整理，
 * 并且关于Jemalloc的版本，我们已经专门为redis作了修改，使其可以对每次分配进行碎片提示。
 * */
#if defined(USE_JEMALLOC) && defined(JEMALLOC_FRAG_HINT)
#define HAVE_DEFRAG
#endif

void *zmalloc(size_t size); /* 调用zmalloc申请size个大小的空间 */
void *zcalloc(size_t size); /* 调用系统函数calloc函数申请空间 */
void *zrealloc(void *ptr, size_t size);/* 原内存重新调整空间为size的大小 */
void zfree(void *ptr); /* 释放空间方法，并更新used_memory的值 */
char *zstrdup(const char *s);  /* 字符串复制方法 */
size_t zmalloc_used_memory(void);/* 获取当前已经占用的内存大小 */
void zmalloc_set_oom_handler(void (*oom_handler)(size_t));/* 可自定义设置内存溢出的处理方法 */
size_t zmalloc_get_rss(void);/* Resident Set Size 实际使用物理内存（包含共享库占用的内存） */
int zmalloc_get_allocator_info(size_t *allocated, size_t *active, size_t *resident);
void set_jemalloc_bg_thread(int enable);
int jemalloc_purge();
size_t zmalloc_get_private_dirty(long pid);/* 获取私有的脏数据大小 */
size_t zmalloc_get_smap_bytes_by_field(char *field, long pid);
size_t zmalloc_get_memory_size(void);
void zlibc_free(void *ptr); /* 原始系统free释放方法 */

#ifdef HAVE_DEFRAG
void zfree_no_tcache(void *ptr);
void *zmalloc_no_tcache(size_t size);
#endif

#ifndef HAVE_MALLOC_SIZE
size_t zmalloc_size(void *ptr);
size_t zmalloc_usable(void *ptr);
#else
#define zmalloc_usable(p) zmalloc_size(p)
#endif

#ifdef REDIS_TEST
int zmalloc_test(int argc, char **argv);
#endif

#endif /* __ZMALLOC_H */
