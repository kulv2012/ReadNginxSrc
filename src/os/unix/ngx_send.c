
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


ssize_t
ngx_unix_send(ngx_connection_t *c, u_char *buf, size_t size)
{//当一个连接SOCK可写的时候，会不断的调用这里进行数据的发送。
//返回最后一次调用send发送的数据长度，并不是i总长度。
    ssize_t       n;
    ngx_err_t     err;
    ngx_event_t  *wev;

    wev = c->write;//取得其写事件指针
#if (NGX_HAVE_KQUEUE)
    if ((ngx_event_flags & NGX_USE_KQUEUE_EVENT) && wev->pending_eof) {
        (void) ngx_connection_error(c, wev->kq_errno,  "kevent() reported about an closed connection");
        wev->error = 1;
        return NGX_ERROR;
    }
#endif

    for ( ;; ) {//循环不断的写入。
        n = send(c->fd, buf, size, 0);

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, c->log, 0,  "send: fd:%d %d of %d", c->fd, n, size);
        if (n > 0) {
            if (n < (ssize_t) size) {//么有写完所有的，就返回非0了，表示不可写了。
                wev->ready = 0;
            }
            c->sent += n;//计数已经发送的。那么这个n代表的是最后一次发送的大小。并不是本函数此次发送的总大小。
            return n;
        }
        err = ngx_socket_errno;
        if (n == 0) {
            ngx_log_error(NGX_LOG_ALERT, c->log, err, "send() returned zero");
            wev->ready = 0;
            return n;
        }

        if (err == NGX_EAGAIN || err == NGX_EINTR) {
            wev->ready = 0;
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err,  "send() not ready");
            if (err == NGX_EAGAIN) {
                return NGX_AGAIN;
            }

        } else {
            wev->error = 1;
            (void) ngx_connection_error(c, err, "send() failed");
            return NGX_ERROR;
        }
    }
}
