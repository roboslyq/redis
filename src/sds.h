/* SDSLib 2.0 -- A C dynamic strings library
 *
 * Copyright (c) 2006-2015, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2015, Oran Agra
 * Copyright (c) 2015, Redis Labs, Inc
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

#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)
extern const char *SDS_NOINIT;

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>
/* 1、类型定义：从这里可以看出，本质上sds类型就是char*类型: 实际指向下面sdshdrXX结构中的buf
 * 2、主要区别就是：sds一定有一个所属的结构(sdshdr)，这个header结构在每次创建sds时被创建，用来存储sds以及sds的相关信息。
 * 3、优点：
 *       1)想用O(1)的时间复杂度获取字符串长度(利用sdshdr)。
 *       2)sds实现了部分自己的字符串处理函数，能够存储二进制字符串 保证二进制安全，而所有C语言str前缀的字符串处理函数不保证二进制安全(遇到'0'就停下，认为它是字符串的结尾，不能存二进制数据)。
 *       3)制定内存重分配方法，减少 因修改字符串而导致的 内存分配和释放 的次数。
 * */
typedef char *sds;

/*
 * 1、sdshdr和sds是一一对应的关系，一个sds一定会有一个sdshdr用来记录sds的信息。在redis3.2分支出现之前sdshdr只有一个类型，定义如下：
        struct sdshdr {
            unsigned int len;//表示sds当前的长度
            unsigned int free;//已为sds分配的长度-sds当前的长度
            char buf[];//sds实际存放的位置
        };
        所在，在小于3.2之前版本的redis每次创建一个sds 不管sds实际有多长，都会分配一个大小固定的sdshdr。根据成员len的类型可知，sds最多能存长度为2^(8*sizeof(unsigned int))的字符串
   2、3.2分支引入了五种sdshdr类型，每次在创建一个sds时根据sds的实际长度判断应该选择什么类型的sdshdr，不同类型的sdshdr占用的内存空间不同。这样细分一下可以省去很多不必要的内存开销。
   以下是五种类型的sdshdr类型定义：
        长度在0和2^5-1之间，选用SDS_TYPE_5类型的header。
        长度在2^5和2^8-1之间，选用SDS_TYPE_8类型的header。
        长度在2^8和2^16-1之间，选用SDS_TYPE_16类型的header。
        长度在2^16和2^32-1之间，选用SDS_TYPE_32类型的header。
        长度大于2^32的，选用SDS_TYPE_64类型的header。能表示的最大长度为2^64-1。
 * /

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */
// sdshdr5未被使用(可以不看)
// __attribute__ ((__packed__))语法: 不存在于任何C语言标准，是GCC的一个extension，用来告诉编译器使用最小的内存来存储sdshd
// packed: ...,This attribute, attached to struct or union type definition, specifies that each member of the structure or union is placed to minimize the memory required.
struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */
    char buf[];
};
// sdshdr8:
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len; /* used  表示当前sds的长度(单位是字节),包括'0'终止符，通过len直接获取字符串长度，不需要扫一遍string*/
    uint8_t alloc; /* excluding the header and null terminator 表示已为sds分配的内存大小(单位是字节),(3.2以前的版本用的free是表示还剩free字节可用空间)，不包括'0'终止符*/
    unsigned char flags; /* 3 lsb of type, 5 unused bits 用一个字节表示当前sdshdr的类型，因为有sdshdr有五种类型，所以至少需要3位来
                            表示000:sdshdr5，001:sdshdr8，010:sdshdr16，011:sdshdr32，100:sdshdr64。高5位用不到所以都为0。
                         */
    char buf[]; //sds实际存放的位置(本质是一个char数组)
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len; /* used */
    uint16_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len; /* used */
    uint32_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len; /* used */
    uint64_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};

#define SDS_TYPE_5  0 //00000000
#define SDS_TYPE_8  1 //00000001
#define SDS_TYPE_16 2 //00000010
#define SDS_TYPE_32 3 //00000011
#define SDS_TYPE_64 4 //00000100
#define SDS_TYPE_MASK 7 // //00000111 (低位掩码，与操作可以实现高位过滤)
#define SDS_TYPE_BITS 3

//redis提供助手宏或者函数，可以更方便的操作sds

/*
 * 双井号##的意思是在一个宏(macro)定义里连接两个子串(token)，连接之后这##号两边的子串就被编译器识别为一个。
 * sdslen函数里第一行出现了s[-1]，看起来感觉会是一个undefined behavior，其实不是，这是一种正常又正确的使用方式，它就等同于*(s-1)。
 * he deﬁnition of the subscript operator [] is that E1[E2] is identical to (*((E1)+(E2))). --C99。
 * 又因为s是一个sds(char*)所以s指向的类型是char，-1就是-1*sizeof(char)，由于sdshdr结构体内禁用了内存对齐，所以这也刚好是一个
 * flags(unsigned char)的地址，所以通过s[-1]我们可以获得sds所属的sdshdr的成员变量flags
 */

// 获取header指针
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (void*)((s)-(sizeof(struct sdshdr##T)));

// 获取字符串指针: 返回一个类型为T包含s字符串的sdshdr的指针
#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))

// 用于计算SDS_TYPE_5的实际长度: 用sdshdr5的flags成员变量做参数返回sds的长度，这其实是一个没办法的hack
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)

//获取sds字符串长度
static inline size_t sdslen(const sds s) {
    //sdshdr的flags成员变量
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->len; //取出sdshdr的len成
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->len;
    }
    return 0;
}
//获取sds字符串空余空间（即alloc - len）。
static inline size_t sdsavail(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            return 0;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}
// 设置sds字符串长度
static inline void sdssetlen(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}
//增加sds字符串长度
static inline void sdsinclen(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}

/* sdsalloc() = sdsavail() + sdslen() */
//获取sds字符串容量。
static inline size_t sdsalloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->alloc;
    }
    return 0;
}
//设置sds字符串容量。
static inline void sdssetalloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->alloc = newlen;
            break;
    }
}

sds sdsnewlen(const void *init, size_t initlen); // 根据传入的init指针，和initlen创建一个合适的sds，为了兼容c字符串函数，sds总是会以''结尾
sds sdsnew(const char *init);                    // 直接根据传入的init字符串指针，创建合适的sds(里面通过调用sdsnewlen(const void *init, size_t initlen)实现)
sds sdsempty(void);// 创建一个空的sds
sds sdsdup(const sds s);// 复制一个sds
void sdsfree(sds s);// 释放sds内存
sds sdsgrowzero(sds s, size_t len);// 增长s到能容纳len长度，增长的空间初始化为0，并且更新s长度为len，如果s实际已经比len长，则不进行任何操作
sds sdscatlen(sds s, const void *t, size_t len);// 拼接函数
sds sdscat(sds s, const char *t);
sds sdscatsds(sds s, const sds t);
sds sdscpylen(sds s, const char *t, size_t len);// 拷贝函数
sds sdscpy(sds s, const char *t);
// 格式化fmt，并拼接到s中
sds sdscatvprintf(sds s, const char *fmt, va_list ap);
#ifdef __GNUC__
sds sdscatprintf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sdscatprintf(sds s, const char *fmt, ...);
#endif

sds sdscatfmt(sds s, char const *fmt, ...); // redis自定义的简化版格式化fmt并拼接到s，速度会比*printf快很多
sds sdstrim(sds s, const char *cset);// 去除头尾指定的字符集合
void sdsrange(sds s, ssize_t start, ssize_t end);// 用s的start到end重新赋值s，start和end可以为负值
void sdsupdatelen(sds s);// 使用strlen(s)更新s长度
void sdsclear(sds s);// 清空s，只设置了下长度为0，不会释放内存
int sdscmp(const sds s1, const sds s2);// 比较两个sds
sds *sdssplitlen(const char *s, ssize_t len, const char *sep, int seplen, int *count);// 分割字符串到返回的sds数组中，数组长度在count中返回。二进制安全
void sdsfreesplitres(sds *tokens, int count); // 释放sdssplitlen和sdssplitargs返回的sds*数组
void sdstolower(sds s);// 转为小写
void sdstoupper(sds s);// 转为大写
sds sdsfromlonglong(long long value);// 从long long类型初始化sds
sds sdscatrepr(sds s, const char *p, size_t len);// 将p对应的字符串转义后用'"'包裹后，连接到s中
sds *sdssplitargs(const char *line, int *argc);// 分割命令行到返回的sds数组中，数组长度在argc参数返回
sds sdsmapchars(sds s, const char *from, const char *to, size_t setlen);// 替换sds中的字符，如果s[i] = from[j] 则将s[i]替换为to[j]
sds sdsjoin(char **argv, int argc, char *sep); // 拼接C风格字符串数组到新的sds中
sds sdsjoinsds(sds *argv, int argc, const char *sep, size_t seplen);// 拼接sds数组到新的sds中

/* Low level functions exposed to the user API */
sds sdsMakeRoomFor(sds s, size_t addlen);// 扩容s，使能再容纳addlen长度，只会修改alloc参数，不会改变len
void sdsIncrLen(sds s, ssize_t incr);// 增加s长度，需要跟sdsMakeRoomFor配合使用
sds sdsRemoveFreeSpace(sds s);// 移除s所有分配的预留空间
size_t sdsAllocSize(sds s);// s实际占用的内存大小
void *sdsAllocPtr(sds s); // 返回s header指针

/* Export the allocator used by SDS to the program using SDS.
 * Sometimes the program SDS is linked to, may use a different set of
 * allocators, but may want to allocate or free things that SDS will
 * respectively free or allocate. */
void *sds_malloc(size_t size);
void *sds_realloc(void *ptr, size_t size);
void sds_free(void *ptr);

#ifdef REDIS_TEST
int sdsTest(int argc, char *argv[]);
#endif

#endif
