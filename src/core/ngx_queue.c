
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * find the middle queue element if the queue has odd number of elements
 * or the first element of the queue's second part otherwise
 */
ngx_queue_t *
ngx_queue_middle(ngx_queue_t *queue)
{//得到链表的中部位置
    ngx_queue_t  *middle, *next;

    middle = ngx_queue_head(queue);
    if (middle == ngx_queue_last(queue)) {
        return middle;//就一个节点。
    }
    next = ngx_queue_head(queue);
    for ( ;; ) {//middle指针每次循环走一步，next每次走2步，那就等next到达终点的时候，middle指针正好到达中间的地方。
        middle = 	ngx_queue_next(middle);
        next = 		ngx_queue_next(next);
        if (next == ngx_queue_last(queue)) {
            return middle;
        }
        next = ngx_queue_next(next);
        if (next == ngx_queue_last(queue)) {
            return middle;
        }
    }
}


/* the stable insertion sort */
void
ngx_queue_sort(ngx_queue_t *queue, ngx_int_t (*cmp)(const ngx_queue_t *, const ngx_queue_t *))
{//从小到大插入排序。
    ngx_queue_t  *q, *prev, *next;

    q = ngx_queue_head(queue);
    if (q == ngx_queue_last(queue)) {
        return;//就一个节点。
    }
    for (q = ngx_queue_next(q); q != ngx_queue_sentinel(queue); q = next) {
        prev = ngx_queue_prev(q);
        next = ngx_queue_next(q);
        ngx_queue_remove(q);
        do {
            if (cmp(prev, q) <= 0) {
                break;//如果前面的小，那就没事了。
            }
            prev = ngx_queue_prev(prev);
        } while (prev != ngx_queue_sentinel(queue));
        ngx_queue_insert_after(prev, q);//插入后面。
    }
}
