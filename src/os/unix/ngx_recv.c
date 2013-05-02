
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


ssize_t
ngx_unix_recv(ngx_connection_t *c, u_char *buf, size_t size)
{//我要在c上读数据，最多读size大小，存放在buf开头的地方。不改变epool属性
//本函数为党一个连接可读的时候调用。返回最后一次调用recv时读取的数据长度。
    ssize_t       n;
    ngx_err_t     err;
    ngx_event_t  *rev;

    rev = c->read;
    do {
        n = recv(c->fd, buf, size, 0);
         ngx_log_debug3(NGX_LOG_DEBUG_EVENT, c->log, 0, "recv: fd:%d %d of %d", c->fd, n, size);
//These calls return the number of bytes received, or -1 if an error occurred. 
//The return value will be 0 when the peer has performed an orderly shutdown.
        if (n == 0) {//返回0表示连接已断开。
            rev->ready = 0;//设置为0表示没有数据可以读了，有可读事件的时候会设置进来的,ngx_epoll_process_events这个里面，有可读事件就标记为1
            rev->eof = 1;//不过这回不行了
            return n;
        } else if (n > 0) {
            if ((size_t) n < size&& !(ngx_event_flags & NGX_USE_GREEDY_EVENT))
            {//如果没有设置为贪婪策略，就是一次没有读取完毕，马上想接着读。正常的话设置为0表示没有数据了
                rev->ready = 0;
            }
            return n;
        }
//如果小于2，肯定悲剧了
        err = ngx_socket_errno;

        if (err == NGX_EAGAIN || err == NGX_EINTR) {//收到中断影响
             ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err,"recv() not ready");
            n = NGX_AGAIN;
        } else {
            n = ngx_connection_error(c, err, "recv() failed");
            break;
        }

    } while (err == NGX_EINTR);

    rev->ready = 0;
    if (n == NGX_ERROR) {
        rev->error = 1;
    }

    return n;
}

