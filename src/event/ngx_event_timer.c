
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#if (NGX_THREADS)
ngx_mutex_t  *ngx_event_timer_mutex;//开启多线程才有效
#endif


ngx_thread_volatile ngx_rbtree_t  ngx_event_timer_rbtree;
static ngx_rbtree_node_t          ngx_event_timer_sentinel;
//sentinel 哨兵，复数，岗哨
/*
 * the event timer rbtree may contain the duplicate keys, however,
 * it should not be a problem, because we use the rbtree to find
 * a minimum timer value only
 */

ngx_int_t
ngx_event_timer_init(ngx_log_t *log)
{
    ngx_rbtree_init(&ngx_event_timer_rbtree, &ngx_event_timer_sentinel,
                    ngx_rbtree_insert_timer_value);

#if (NGX_THREADS)

    if (ngx_event_timer_mutex) {
        ngx_event_timer_mutex->log = log;
        return NGX_OK;
    }

    ngx_event_timer_mutex = ngx_mutex_init(log, 0);
    if (ngx_event_timer_mutex == NULL) {
        return NGX_ERROR;
    }

#endif

    return NGX_OK;
}


ngx_msec_t
ngx_event_find_timer(void)
{//返回红黑树中最小的节点的超时时间跟现在的时间的差，大于0表示还要这么久才超时
    ngx_msec_int_t      timer;
    ngx_rbtree_node_t  *node, *root, *sentinel;

    if (ngx_event_timer_rbtree.root == &ngx_event_timer_sentinel) {
        return NGX_TIMER_INFINITE;
    }

    ngx_mutex_lock(ngx_event_timer_mutex);//多进程模式下，该函数为空操作
    root = ngx_event_timer_rbtree.root;
    sentinel = ngx_event_timer_rbtree.sentinel;
    node = ngx_rbtree_min(root, sentinel);//找到这颗红黑树中最小的节点
    ngx_mutex_unlock(ngx_event_timer_mutex);
    timer = (ngx_msec_int_t) node->key - (ngx_msec_int_t) ngx_current_msec;//得到时间差，跟现在的缓存时间相减
    return (ngx_msec_t) (timer > 0 ? timer : 0);//如果大于0，表示还没有超时，返回剩余的时间。
}


void
ngx_event_expire_timers(void)
{
    ngx_event_t        *ev;
    ngx_rbtree_node_t  *node, *root, *sentinel;

    sentinel = ngx_event_timer_rbtree.sentinel;

    for ( ;; ) {

        ngx_mutex_lock(ngx_event_timer_mutex);

        root = ngx_event_timer_rbtree.root;

        if (root == sentinel) {
            return;
        }

        node = ngx_rbtree_min(root, sentinel);

        /* node->key <= ngx_current_time */

        if ((ngx_msec_int_t) node->key - (ngx_msec_int_t) ngx_current_msec <= 0)//超时了
        {
            ev = (ngx_event_t *) ((char *) node - offsetof(ngx_event_t, timer));

#if (NGX_THREADS)

            if (ngx_threaded && ngx_trylock(ev->lock) == 0) {
                /*
                 * We can not change the timer of the event that is been
                 * handling by another thread.  And we can not easy walk
                 * the rbtree to find a next expired timer so we exit the loop.
                 * However it should be rare case when the event that is
                 * been handling has expired timer.
                 */
                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0, "event %p is busy in expire timers", ev);
                break;
            }
#endif
            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, ev->log, 0,  "event timer del: %d: %M", ngx_event_ident(ev->data), ev->timer.key);
            ngx_rbtree_delete(&ngx_event_timer_rbtree, &ev->timer);
            ngx_mutex_unlock(ngx_event_timer_mutex);

#if (NGX_DEBUG)
            ev->timer.left = NULL;
            ev->timer.right = NULL;
            ev->timer.parent = NULL;
#endif

            ev->timer_set = 0;

#if (NGX_THREADS)
            if (ngx_threaded) {
                ev->posted_timedout = 1;
                ngx_post_event(ev, &ngx_posted_events);
                ngx_unlock(ev->lock);
                continue;
            }
#endif
            ev->timedout = 1;//标记为超时，然后调用一下它的回调函数，相当于告诉对方，你超时了，自己看着办
            ev->handler(ev);//调用回调
            continue;
        }
        break;//最小的都没有超时，直接退出循环吧
    }
    ngx_mutex_unlock(ngx_event_timer_mutex);
}
