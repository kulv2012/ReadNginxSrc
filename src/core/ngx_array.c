
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


ngx_array_t *
ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size)
{
    ngx_array_t *a;

    a = ngx_palloc(p, sizeof(ngx_array_t));
    if (a == NULL) {
        return NULL;
    }

    a->elts = ngx_palloc(p, n * size);
    if (a->elts == NULL) {
        return NULL;
    }

    a->nelts = 0;
    a->size = size;
    a->nalloc = n;
    a->pool = p;

    return a;
}


void
ngx_array_destroy(ngx_array_t *a)
{
    ngx_pool_t  *p;

    p = a->pool;
//if it happens that destory the last buf unit , we return it for reuse 
    if ((u_char *) a->elts + a->size * a->nalloc == p->d.last) {
        p->d.last -= a->size * a->nalloc;
    }
//if the array struct is at the end of poll.return it 
    if ((u_char *) a + sizeof(ngx_array_t) == p->d.last) {
        p->d.last = (u_char *) a;
    }
}


void *
ngx_array_push(ngx_array_t *a)
{
    void        *elt, *new;
    size_t       size;
    ngx_pool_t  *p;

    if (a->nelts == a->nalloc) {//悲剧，用完了
        /* the array is full */
        size = a->size * a->nalloc;//计算一下现在的总大小
        p = a->pool;
        if ((u_char *) a->elts + size == p->d.last && p->d.last + a->size <= p->d.end)
        {//妈呀，如果我是这个pool的最后一个成员，并且我后面的空地足够容得下一个元素的大小，直接扩展之!真是无所不用其极啊
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */

            p->d.last += a->size;//pool，你直接给我划拨吧，我订了
            a->nalloc++;//申请量增加1

        } else {
            /* allocate a new array */

            new = ngx_palloc(p, 2 * size);//没办法了，从内存池申请个2倍大小的。
            if (new == NULL) {
                return NULL;
            }

            ngx_memcpy(new, a->elts, size);
            a->elts = new;//就这么指向了新的内存，旧的内存就不释放了吗，这就是nginx内存管理的妙处，通过内存池申请的，不显示释放，统一释放。
            a->nalloc *= 2;
        }
    }

    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts++;
//return the address of the pushed element
    return elt;
}


void *
ngx_array_push_n(ngx_array_t *a, ngx_uint_t n)
{
    void        *elt, *new;
    size_t       size;
    ngx_uint_t   nalloc;
    ngx_pool_t  *p;

    size = n * a->size;
    if (a->nelts + n > a->nalloc) {
        /* the array is full */
        p = a->pool;
        if ((u_char *) a->elts + a->size * a->nalloc == p->d.last && p->d.last + size <= p->d.end) {
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */
            p->d.last += size;
            a->nalloc += n;
        } else {
            /* allocate a new array */
            nalloc = 2 * ((n >= a->nalloc) ? n : a->nalloc);//否则申请2倍的数目
            new = ngx_palloc(p, nalloc * a->size);
            if (new == NULL) {
                return NULL;
            }

            ngx_memcpy(new, a->elts, a->nelts * a->size);
            a->elts = new;
            a->nalloc = nalloc;
        }
    }

    elt = (u_char *) a->elts + a->size * a->nelts;
    a->nelts += n;

    return elt;
}
