/* Rax -- A radix tree implementation.
 *
 * Copyright (c) 2017-2018, Salvatore Sanfilippo <antirez at gmail dot com>
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

#ifndef RAX_H
#define RAX_H

#include <stdint.h>

/* Representation of a radix tree(基数树) as implemented in this file, that contains
 * the strings "foo", "foobar" and "footer" after the insertion of each
 * word. When the node represents a key inside the radix tree, we write it
 * between [], otherwise it is written between ().
 * 当那个节点代表一个Key时，使用[]表示，具体key值是当前节点所在的路径。否则使用(）表示。
 *
 * This is the vanilla(简朴，平常) representation:
 *
 *              (f) ""
 *                \
 *                (o) "f"
 *                  \
 *                  (o) "fo"
 *                    \
 *                  [t   b] "foo"
 *                  /     \
 *         "foot" (e)     (a) "foob"
 *                /         \
 *      "foote" (r)         (r) "fooba"
 *              /             \
 *    "footer" []             [] "foobar"
 *
 * However, this implementation implements a very common optimization where
 * successive nodes having a single child are "compressed" into the node
 * itself as a string of characters, each representing a next-level child,
 * and only the link to the node representing the last character node is
 * provided inside the representation. So the above representation is turend
 * into:
 * 只有一个孩子节点时，可以对前面的节点进行压缩。比如f-o-o可以压缩为“foo”
 *
 *                  ["foo"] ""
 *                     |
 *                  [t   b] "foo"
 *                  /     \
 *        "foot" ("er")    ("ar") "foob"
 *                 /          \
 *       "footer" []          [] "foobar"
 *
 * However this optimization makes the implementation a bit more complex.
 * For instance if a key "first" is added in the above radix tree, a
 * "node splitting" operation is needed, since the "foo" prefix is no longer
 * composed of nodes having a single child one after the other. This is the
 * above tree and the resulting node splitting after this event happens:
 * 压缩带来了复杂度：如果新加入元素可以会需要“解压”这个操作。比如之前"foobar",再插入一个"first"，那么foobar需要解压。
 *
 *                    (f) ""
 *                    /
 *                 (i o) "f"
 *                 /   \
 *    "firs"  ("rst")  (o) "fo"
 *              /        \
 *    "first" []       [t   b] "foo"
 *                     /     \
 *           "foot" ("er")    ("ar") "foob"
 *                    /          \
 *          "footer" []          [] "foobar"
 *
 * Similarly after deletion, if a new chain of nodes having a single child
 * is created (the chain must also not include nodes that represent keys),
 * it must be compressed back into a single node.
 *
 * 1、radix Tree(基数树) 其实就差不多是传统的二叉树，只是在寻找方式上，利用比如一个unsigned int的类型的每一个比特位作为树节点的判断。
 * 可以这样说，比如一个数1000101010101010010101010010101010，那么按照Radix 树的插入就是在根节点，
 * 如果遇到0，就指向左节点，如果遇到1就指向右节点，在插入过程中构造树节点，在删除过程中删除树节点
 * （使用一个比特位判断，会使树的高度过高，非叶节点过多。故在实际应用中，我们一般是使用多个比特位作为树节点的判断，
 * 但多比特位会使节点的子节点槽变多，增大节点的体积，一般选用2个或4个比特位作为树节点即可） 
 * 2、作用
 *      基数树（radix tree）是将long整数与指针键值相关联的机制，它存储有效率，并且可快速查询，用于整数值与指针的映射，对于长整型数据的映射，
 *      如何解决Hash冲突和Hash表大小的设计是一个很头疼的问题，利用radix树可以根据一个长整型（比如一个长ID）快速查找到其对应的对象指针。
 *      这比用hash映射来的简单，也更节省空间，使用hash映射hash函数难以设计，不恰当的hash函数可能增大冲突，或浪费空间。
 * 3、对比
 *      基数树和字典树的实现机制很像，都是将key拆分成一部分映射到树形结构中，基数树是将key按指针地址bit位拆分，而字典树是将key的字符串值按字符拆分。
 *      但是基数树的层高是相对固定的(因为指针地址的bit位是固定的)，而字典树的高度与字符串的长度相关，而树的高度决定了树的空间占用情况，所以基数树明显更节约空间。
 */

#define RAX_NODE_MAX_SIZE ((1<<29)-1)
/** rax节点，sizeof(raxNode) = 4 */
typedef struct raxNode {
    uint32_t iskey:1;     /* Does this node contain a key?  当前节点是不是包含key?没有key的是根节点*/
    uint32_t isnull:1;    /* Associated value is NULL (don't store it). 当前节点是不是空？中间节点都是空的*/
    uint32_t iscompr:1;   /* Node is compressed. 当前节点是不是压缩节点？ */
    uint32_t size:29;     /* Number of children, or compressed string len. 如果当前节点不是压缩节点，则保存子节点的数量，如果是压缩节点，则保存压缩字符串的长度 (isCompressed) byte[] */
    /* Data layout is as follows:
     *
     * If node is not compressed we have 'size' bytes, one for each children
     * character, and 'size' raxNode pointers, point to each child node.
     * Note how the character is not stored in the children but in the
     * edge of the parents:
     *
     * [header iscompr=0][abc][a-ptr][b-ptr][c-ptr](value-ptr?)
     *
     * if node is compressed (iscompr bit is 1) the node has 1 children.
     * In that case the 'size' bytes of the string stored immediately at
     * the start of the data section, represent a sequence of successive
     * nodes linked one after the other, for which only the last one in
     * the sequence is actually represented as a node, and pointed to by
     * the current compressed node.
     *
     * [header iscompr=1][xyz][z-ptr](value-ptr?)
     *
     * Both compressed and not compressed nodes can represent a key
     * with associated data in the radix tree at any level (not just terminal
     * nodes).
     *
     * If the node has an associated key (iskey=1) and is not NULL
     * (isnull=0), then after the raxNode pointers poiting to the
     * children, an additional value pointer is present (as you can see
     * in the representation above as "value-ptr" field).
     */
    /**  路由键、子节点指针、value */
    unsigned char data[];
} raxNode;
/** rax结构体*/
typedef struct rax {
    raxNode *head;      //根节点
    uint64_t numele;    //元素数量
    uint64_t numnodes;  //节点数量(节点不一定是元素，但元素一定是节点)
} rax;

/* Stack data structure used by raxLowWalk() in order to, optionally, return
 * a list of parent nodes to the caller. The nodes do not have a "parent"
 * field for space concerns, so we use the auxiliary stack when needed. */
#define RAX_STACK_STATIC_ITEMS 32
/** 对树进行遍历 */
typedef struct raxStack {
    void **stack; /* Points to static_items or an heap allocated array. */
    size_t items, maxitems; /* Number of items contained and total space. */
    /* Up to RAXSTACK_STACK_ITEMS items we avoid to allocate on the heap
     * and use this static array of pointers instead. */
    void *static_items[RAX_STACK_STATIC_ITEMS];
    int oom; /* True if pushing into this stack failed for OOM at some point. */
} raxStack;

/* Optional callback used for iterators and be notified on each rax node,
 * including nodes not representing keys. If the callback returns true
 * the callback changed the node pointer in the iterator structure, and the
 * iterator implementation will have to replace the pointer in the radix tree
 * internals. This allows the callback to reallocate the node to perform
 * very special operations, normally not needed by normal applications.
 *
 * This callback is used to perform very low level analysis of the radix tree
 * structure, scanning each possible node (but the root node), or in order to
 * reallocate the nodes to reduce the allocation fragmentation (this is the
 * Redis application for this callback).
 *
 * This is currently only supported in forward iterations (raxNext) */
typedef int (*raxNodeCallback)(raxNode **noderef);

/* Radix tree iterator state is encapsulated into this data structure. */
#define RAX_ITER_STATIC_LEN 128
#define RAX_ITER_JUST_SEEKED (1<<0) /* Iterator was just seeked. Return current
                                       element for the first iteration and
                                       clear the flag. */
#define RAX_ITER_EOF (1<<1)    /* End of iteration reached. */
#define RAX_ITER_SAFE (1<<2)   /* Safe iterator, allows operations while
                                  iterating. But it is slower. */
typedef struct raxIterator {
    int flags;
    rax *rt;                /* Radix tree we are iterating. */
    unsigned char *key;     /* The current string. */
    void *data;             /* Data associated to this key. */
    size_t key_len;         /* Current key length. */
    size_t key_max;         /* Max key len the current key buffer can hold. */
    unsigned char key_static_string[RAX_ITER_STATIC_LEN];
    raxNode *node;          /* Current node. Only for unsafe iteration. */
    raxStack stack;         /* Stack used for unsafe iteration. */
    raxNodeCallback node_cb; /* Optional node callback. Normally set to NULL. */
} raxIterator;

/* A special pointer returned for not found items. */
extern void *raxNotFound;

/* Exported API. */
rax *raxNew(void);
int raxInsert(rax *rax, unsigned char *s, size_t len, void *data, void **old);
int raxTryInsert(rax *rax, unsigned char *s, size_t len, void *data, void **old);
int raxRemove(rax *rax, unsigned char *s, size_t len, void **old);
void *raxFind(rax *rax, unsigned char *s, size_t len);
void raxFree(rax *rax);
void raxFreeWithCallback(rax *rax, void (*free_callback)(void*));
void raxStart(raxIterator *it, rax *rt);
int raxSeek(raxIterator *it, const char *op, unsigned char *ele, size_t len);
int raxNext(raxIterator *it);
int raxPrev(raxIterator *it);
int raxRandomWalk(raxIterator *it, size_t steps);
int raxCompare(raxIterator *iter, const char *op, unsigned char *key, size_t key_len);
void raxStop(raxIterator *it);
int raxEOF(raxIterator *it);
void raxShow(rax *rax);
uint64_t raxSize(rax *rax);
unsigned long raxTouch(raxNode *n);
void raxSetDebugMsg(int onoff);

/* Internal API. May be used by the node callback in order to access rax nodes
 * in a low level way, so this function is exported as well. */
void raxSetData(raxNode *n, void *data);

#endif
