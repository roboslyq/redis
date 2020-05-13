# Redis之数据结构与编码实现

## 底层扩展

在讲Redis数据结构与编码实现之前，先讲一个SDS的概念。

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

3.2之后的定义，更细节，更节省空间

#### sdshdr

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

> **len**:  表示当前sds的长度(单位是字节),包括'0'终止符，通过len直接获取字符串长度，不需要扫一遍string。
>
> **alloc**： 表示已为sds分配的内存大小(单位是字节),(3.2以前的版本用的free是表示还剩free字节可用空间)，不包括'0'终止符
>
> **flags**:用一个字节表示当前sdshdr的类型，因为有sdshdr有五种类型，所以至少需要3位来表示000:sdshdr5 ， 001:sdshdr8，010:sdshdr16，011:sdshdr32，100:sdshdr64。高5位用不到所以都为0。
>
> **buf[]**: sds实际存放的位置(本质是一个char数组)

## 5大数据结构

### String

### List

### Hash

### SET

### ZSET

## 4大扩展结构

### bitmap

### GeoHash

### HyperLogLog

### Stream

## 编码实现

### raw

### embstr 

### linkedlist 

### ziplist

### hashtable 

### intset 

### skiplist 

> 