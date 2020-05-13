# Redis之数据结构与编码实现

> 在介绍Redis数据结构之前，我觉得有必要先介绍一下SDS(Simple Dynamic String)。因为SDS是Redis底层数据结构实现，是对字符串的封装。Redis中的字符串默认都是使用SDS保存的。

## 对C语言char[]扩展

那么为什么要对C语言的原生char进行扩展呢？因为c语言中没有String(字符串)这个概念，对String的操作均是通过字符数组来完成的。通常，c的字符数组定义如下：

```c
char str[] = "hello，world"; 
strlen(str); 
```

上述原生C语言的字符数组存在以下缺陷：

1、求字符数组的长度，时间复杂度是O(n)

2、不能动态扩容和预分配空间

因为字符串使用实在是太频繁了，所以有必要对其进行优化一下。

### sdshdr结构体

SDS (Simple Dynamic String)，Simple的意思是简单，Dynamic即动态。String是字符串的意思。是Redis对C语言原生的字符数组一种扩展包装。由Redis作者antirez创建，目前已经有一个独立[项目]( https://github.com/antirez/sds)。

sds 有两个版本，在Redis 3.2之前使用的是第一个版本，其数据结构如下所示：

```c
typedef char sds;      
struct sdshdr {
    unsigned int len;   //buf中已经使用的长度
    unsigned int free;  //buf中未使用的长度
    char buf[];         //柔性数组buf
};

```

在3.2之后数据结构如下：

```c
struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags; 
    char buf[];
};
// sdshdr8:
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len; 
    uint8_t alloc;
    unsigned char flags;  
    char buf[];  
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len;  
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
```

上述源码用图表示如下：



![sds](./images/struct/1.jpg)

- **len**:  表示当前sds的长度(单位是字节),包括'0'终止符，通过len直接获取字符串长度，不需要扫一遍string。

- **alloc**： 表示已为sds分配的内存大小(单位是字节),(3.2以前的版本用的free是表示还剩free字节可用空间)，不包括'0'终止符

- **flags**:用一个字节表示当前sdshdr的类型，因为有sdshdr有五种类型，所以至少需要3位来表示000:sdshdr5 ， 001:sdshdr8，010:sdshdr16，011:sdshdr32，100:sdshdr64。高5位用不到所以都为0。

- **buf[]**: sds实际存放的位置(本质是一个char数组)。**关键：** 这个字符数组也是暴露给用户的地址。但是，在创建sds时，已经把完整的`sdshdr`结构创建出来了，因此，可以通过向前(左)偏移，得到len,alloc,flags这些类型。

即通过给char[] 添加了三种重要的标识，使得各种操作十分方便快捷，起到了空间换时间的作用。

1、求字符数组的长度，时间复杂度是O(1)，因为有字段记录了数组的长度

2、可以实现动态扩容和预分配空间，因为有len和alloc两属性，因此很方便的求出空闲属性alloc - len

3、因为有了len与alloc属性，什么预分配空间，惰性释放等均可以实现。

4、一个神奇的实现是SDS实现返回的不是sdshdr结构体，而是sdshdr->buf?为何这样实现？因为这些实现就可以完全兼容标准C语言的常见字符串方法。因为sdshdr->buf就是一样char[]数组。

## redisObject对象

redis对所有的对象的操作，不是直接的，而是通过redisObject对象进行包装。

```c
typedef struct redisObject {
    unsigned type:4;// 对象的类型，也就是我们说的 string、list、hash、set、zset中的一种，可以使用命令 TYPE key 来查看。
    unsigned encoding:4;/* 1、encoding属性记录了队形所使用的编码(底层数据结构)，即这个对象底层使用哪种数据结构实现。详情见下面定义的OBJ_ENCODING_XXX
                        * 2、encoding可以根据不同的使用场景来为一个对象设置不同的编码，从而优化在某一场景下的效率，极大的提升了 Redis 的灵活性和效率。
                        * 3、可以通过函数strEncoding()来获取具体的编码方式
                        * */
    // 对象最后一次被访问的时间
    unsigned lru:LRU_BITS; /* LRU time (relative to global lru_clock) or
                            * LFU data (least significant 8 bits frequency
                            * and most significant 16 bits access time). */
    // 键值对对象的引用统计。当此值为 0 时，回收对象。
    int refcount;
    // 指向底层实现数据结构的指针,就是实际存放数据的地址。具体实现由 type + encoding组合实现
    void *ptr;
} robj;
```

通过redisObject包装，可以精确的对编码实现控制，从而对内存的使用达到最优化。

编码方式有如下几种：

```c
#define OBJ_ENCODING_RAW 0        /* Raw representation */
#define OBJ_ENCODING_INT 1        /* Encoded as integer */
#define OBJ_ENCODING_HT 2         /* Encoded as hash table */
#define OBJ_ENCODING_ZIPMAP 3     /* Encoded as zipmap */
#define OBJ_ENCODING_LINKEDLIST 4 /* No longer used: old list encoding. */
#define OBJ_ENCODING_ZIPLIST 5    /* Encoded as ziplist */
#define OBJ_ENCODING_INTSET 6     /* Encoded as intset */
#define OBJ_ENCODING_SKIPLIST 7   /* Encoded as skiplist */
#define OBJ_ENCODING_EMBSTR 8     /* Embedded sds string encoding */
#define OBJ_ENCODING_QUICKLIST 9  /* Encoded as linked list of ziplists */
```



## type=String 

###　encoding=OBJ_ENCODING_INT

###  raw

在redisObject基础之上，如果type是一个string并且长度大于44,则采用Raw编码。

此时ptr与redisObject内存不连续。此时*ptr指向一个sds结构体

### embstr 

在redisObject基础之上，如果type是一个string并且长度小于44,则采用embstr 编码。

此时*ptr与redisObject内存连续，此时ptr指向一个sds结构体

### type = List

### type = Hash

### type = SET

### type = ZSET

## 4大扩展结构

### bitmap

### GeoHash

### HyperLogLog

### Stream