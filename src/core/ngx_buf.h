
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_BUF_H_INCLUDED_
#define _NGX_BUF_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef void *            ngx_buf_tag_t;

typedef struct ngx_buf_s  ngx_buf_t;

struct ngx_buf_s {//内存的描述结构/管理结构，其start/end指向一块真正存放数据的内存。
    u_char          *pos;//当前数据读到了这里
    u_char          *last;//所有数据的末尾
    off_t            file_pos;//如果在文件中，那就表示为偏移
    off_t            file_last;///如果在文件中，那就表示为偏移

    u_char          *start;         /* start of buffer */
    u_char          *end;           /* end of buffer */
    ngx_buf_tag_t    tag;
    ngx_file_t      *file;
    ngx_buf_t       *shadow;//shadow会将buf组成一条链表。用last_shadow标记表明是否是某个大裸FCGI数据块中的最后一个。通过p->in等指向头部。
    //这里容易混淆，以为last_shadow指的是整个链表的最后一个，其实不是，这个链表中可能是属于几个大FCGI数据块，就有几个为1的。具体参考ngx_event_pipe_drain_chains


    /* the buf's content could be changed */
    unsigned         temporary:1;
    /*
     * the buf's content is in a memory cache or in a read only memory
     * and must not be changed
     */
    unsigned         memory:1;

    /* the buf's content is mmap()ed and must not be changed */
    unsigned         mmap:1;

    unsigned         recycled:1;//这个域表示我们当前的buf是需要被回收的。调用output_filter之前会设置为0.代表我这个buf是否需要回收重复利用。
    unsigned         in_file:1;//这个BUFFER存放在文件中。
    unsigned         flush:1;//这块内存是不是需要尽快flush给客户端，也就是是否需要尽快发送出去，ngx_http_write_filter会利用这个标志做判断。
    unsigned         sync:1;
    unsigned         last_buf:1;//是否是最后一块内存。
    unsigned         last_in_chain:1;

    unsigned         last_shadow:1;//这个代表的是，我这个data数据块是不是属于裸FCGI数据块的最后一个，如果是，那我的shadow指向这个大数据块。否则指向下一个data节点。
    unsigned         temp_file:1;
    /* STUB */ int   num;
};


struct ngx_chain_s {
    ngx_buf_t    *buf;
    ngx_chain_t  *next;
};


typedef struct {
    ngx_int_t    num;
    size_t       size;
} ngx_bufs_t;


typedef struct ngx_output_chain_ctx_s  ngx_output_chain_ctx_t;

typedef ngx_int_t (*ngx_output_chain_filter_pt)(void *ctx, ngx_chain_t *in);

#if (NGX_HAVE_FILE_AIO)
typedef void (*ngx_output_chain_aio_pt)(ngx_output_chain_ctx_t *ctx,
    ngx_file_t *file);
#endif

struct ngx_output_chain_ctx_s {
    ngx_buf_t                   *buf;
    ngx_chain_t                 *in;//所有待发送的数据放在这里，是个链表。
    ngx_chain_t                 *free;//已经发送完毕的内存
    ngx_chain_t                 *busy;//发送了一部分的

    unsigned                     sendfile:1;
    unsigned                     directio:1;
#if (NGX_HAVE_ALIGNED_DIRECTIO)
    unsigned                     unaligned:1;
#endif
    unsigned                     need_in_memory:1;
    unsigned                     need_in_temp:1;
#if (NGX_HAVE_FILE_AIO)
    unsigned                     aio:1;

    ngx_output_chain_aio_pt      aio_handler;
#endif

    off_t                        alignment;

    ngx_pool_t                  *pool;
    ngx_int_t                    allocated;
    ngx_bufs_t                   bufs;
    ngx_buf_tag_t                tag; //&ngx_http_fastcgi_module;

    ngx_output_chain_filter_pt   output_filter;//为ngx_chain_writer , ngx_http_output_filter ,ngx_http_next_filter
    void                        *filter_ctx;//为 u->output.filter_ctx = &u->writer;
};


typedef struct {
    ngx_chain_t                 *out;//还没有发送出去的待发送数据的头部
    ngx_chain_t                **last;//永远指向最优一个ngx_chain_t的next字段的地址。这样可以通过这个地址不断的在后面增加元素。
    ngx_connection_t            *connection;//我这个输出链表对应的连接
    ngx_pool_t                  *pool;
    off_t                        limit;
} ngx_chain_writer_ctx_t;


#define NGX_CHAIN_ERROR     (ngx_chain_t *) NGX_ERROR


#define ngx_buf_in_memory(b)        (b->temporary || b->memory || b->mmap)
#define ngx_buf_in_memory_only(b)   (ngx_buf_in_memory(b) && !b->in_file)

//如果这个必须发送给客户端或者是最后一块 && 并且不在内存中且不是文件。那是什么?
#define ngx_buf_special(b)                                                   \
    ((b->flush || b->last_buf || b->sync)                                    \
     && !ngx_buf_in_memory(b) && !b->in_file)

#define ngx_buf_sync_only(b)                                                 \
    (b->sync                                                                 \
     && !ngx_buf_in_memory(b) && !b->in_file && !b->flush && !b->last_buf)

#define ngx_buf_size(b)                                                      \
    (ngx_buf_in_memory(b) ? (off_t) (b->last - b->pos):                      \
                            (b->file_last - b->file_pos))

ngx_buf_t *ngx_create_temp_buf(ngx_pool_t *pool, size_t size);
ngx_chain_t *ngx_create_chain_of_bufs(ngx_pool_t *pool, ngx_bufs_t *bufs);


#define ngx_alloc_buf(pool)  ngx_palloc(pool, sizeof(ngx_buf_t))
#define ngx_calloc_buf(pool) ngx_pcalloc(pool, sizeof(ngx_buf_t))

ngx_chain_t *ngx_alloc_chain_link(ngx_pool_t *pool);
#define ngx_free_chain(pool, cl)                                             \
    cl->next = pool->chain;                                                  \
    pool->chain = cl



ngx_int_t ngx_output_chain(ngx_output_chain_ctx_t *ctx, ngx_chain_t *in);
ngx_int_t ngx_chain_writer(void *ctx, ngx_chain_t *in);

ngx_int_t ngx_chain_add_copy(ngx_pool_t *pool, ngx_chain_t **chain,
    ngx_chain_t *in);
ngx_chain_t *ngx_chain_get_free_buf(ngx_pool_t *p, ngx_chain_t **free);
void ngx_chain_update_chains(ngx_chain_t **free, ngx_chain_t **busy,
    ngx_chain_t **out, ngx_buf_tag_t tag);


#endif /* _NGX_BUF_H_INCLUDED_ */
