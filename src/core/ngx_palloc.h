
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_PALLOC_H_INCLUDED_
#define _NGX_PALLOC_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * NGX_MAX_ALLOC_FROM_POOL should be (ngx_pagesize - 1), i.e. 4095 on x86.
 * On Windows NT it decreases a number of locked pages in a kernel.
 */
#define NGX_MAX_ALLOC_FROM_POOL  (ngx_pagesize - 1)
/*ngx_pagesize 等于getpagesize()，nginx_posix_init.c里面设置*/
#define NGX_DEFAULT_POOL_SIZE    (16 * 1024)

#define NGX_POOL_ALIGNMENT       16
#define NGX_MIN_POOL_SIZE                                                     \
    ngx_align((sizeof(ngx_pool_t) + 2 * sizeof(ngx_pool_large_t)),            \
              NGX_POOL_ALIGNMENT)


typedef void (*ngx_pool_cleanup_pt)(void *data);

typedef struct ngx_pool_cleanup_s  ngx_pool_cleanup_t;

struct ngx_pool_cleanup_s {
    ngx_pool_cleanup_pt   handler;
    void                 *data;
    ngx_pool_cleanup_t   *next;//k 形成链表
};


typedef struct ngx_pool_large_s  ngx_pool_large_t;

struct ngx_pool_large_s {
    ngx_pool_large_t     *next;//k 形成链表
    void                 *alloc;
};


typedef struct {
    u_char               *last;//当前使用到last
    u_char               *end;//缓冲区尾部
    ngx_pool_t           *next;//貌似是指向下一个ngx_pool_s的开始
    ngx_uint_t            failed;//内存申请在这个内存块上没有被满足的次数，一般为空闲大小不合适，太小了。超过4次后，ngx_pool_s的current指针就会越过这里
} ngx_pool_data_t;

/*每个pool都有一块小内存，然后是大内存链表，cleanup内存链表*/
struct ngx_pool_s {
    ngx_pool_data_t       d;//存放内存位置，首尾等，链表结构
    size_t                max;//这是指一次能分配的最大大小?对，最大NGX_MAX_ALLOC_FROM_POOL
    ngx_pool_t           *current;//当前的池
    ngx_chain_t          *chain;//组成列表，指向头部，每次分配chain的时候把头部的链表返回，然后pool->chain = cl->next;
    ngx_pool_large_t     *large;//大块内存分配
    ngx_pool_cleanup_t   *cleanup;
    ngx_log_t            *log;
};


typedef struct {
    ngx_fd_t              fd;
    u_char               *name;
    ngx_log_t            *log;
} ngx_pool_cleanup_file_t;


void *ngx_alloc(size_t size, ngx_log_t *log);
void *ngx_calloc(size_t size, ngx_log_t *log);

ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log);
void ngx_destroy_pool(ngx_pool_t *pool);
void ngx_reset_pool(ngx_pool_t *pool);

void *ngx_palloc(ngx_pool_t *pool, size_t size);
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);
void *ngx_pcalloc(ngx_pool_t *pool, size_t size);
void *ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment);
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p);


ngx_pool_cleanup_t *ngx_pool_cleanup_add(ngx_pool_t *p, size_t size);
void ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd);
void ngx_pool_cleanup_file(void *data);
void ngx_pool_delete_file(void *data);


#endif /* _NGX_PALLOC_H_INCLUDED_ */
