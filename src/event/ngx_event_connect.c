
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>


ngx_int_t
ngx_event_connect_peer(ngx_peer_connection_t *pc)
{//获取一个可用的peer,然后连接它.并注册可读，可写事件
    int                rc;
    ngx_int_t          event;
    ngx_err_t          err;
    ngx_uint_t         level;
    ngx_socket_t       s;
    ngx_event_t       *rev, *wev;
    ngx_connection_t  *c;

    rc = pc->get(pc, pc->data);//ngx_http_upstream_get_round_robin_peer获取一个peer
    if (rc != NGX_OK) {
        return rc;
    }
    s = ngx_socket(pc->sockaddr->sa_family, SOCK_STREAM, 0);//新建一个SOCK
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, pc->log, 0, "socket %d", s);
    if (s == -1) {
        ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno, ngx_socket_n " failed");
        return NGX_ERROR;
    }
    c = ngx_get_connection(s, pc->log);//在ngx_cycle->free_connections里面找一个空闲的位置存放这个连接，然后初始化相关成员
    if (c == NULL) {//得到一个连接结构。
        if (ngx_close_socket(s) == -1) {
            ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno,  ngx_close_socket_n "failed");
        }
        return NGX_ERROR;
    }

    if (pc->rcvbuf) {//rcvbuf是个指针
        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const void *) &pc->rcvbuf, sizeof(int)) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno, "setsockopt(SO_RCVBUF) failed");
            goto failed;
        }
    }
	//设置为非阻塞模式。
    if (ngx_nonblocking(s) == -1) {
        ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno, ngx_nonblocking_n " failed");
        goto failed;
    }
    if (pc->local) {//这是啥意思，绑定它�?
        if (bind(s, pc->local->sockaddr, pc->local->socklen) == -1) {
            ngx_log_error(NGX_LOG_CRIT, pc->log, ngx_socket_errno, "bind(%V) failed", &pc->local->name);
            goto failed;
        }
    }

	//下面设置这个连接的读写回调函数。
    c->recv = ngx_recv;
    c->send = ngx_send;
    c->recv_chain = ngx_recv_chain;
    c->send_chain = ngx_send_chain;

    c->sendfile = 1;
    c->log_error = pc->log_error;
    if (pc->sockaddr->sa_family != AF_INET) {
        c->tcp_nopush = NGX_TCP_NOPUSH_DISABLED;
        c->tcp_nodelay = NGX_TCP_NODELAY_DISABLED;
#if (NGX_SOLARIS)
        /* Solaris's sendfilev() supports AF_NCA, AF_INET, and AF_INET6 */
        c->sendfile = 0;
#endif
    }
    rev = c->read;//设置这个连接的读写事件结构
    wev = c->write;

    rev->log = pc->log;
    wev->log = pc->log;
    pc->connection = c;
    c->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);

#if (NGX_THREADS)
    /* TODO: lock event when call completion handler */
    rev->lock = pc->lock;
    wev->lock = pc->lock;
    rev->own_lock = &c->lock;
    wev->own_lock = &c->lock;
#endif

    if (ngx_add_conn) {//调用ngx_epoll_add_connection注册epool的各个事件，不过读写事件结构待会设置。
        if (ngx_add_conn(c) == NGX_ERROR) {
            goto failed;
        }
    }
    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, pc->log, 0, "connect to %V, fd:%d #%d", pc->name, s, c->number);
    rc = connect(s, pc->sockaddr, pc->socklen);//然后连接它

    if (rc == -1) {
        err = ngx_socket_errno;
        if (err != NGX_EINPROGRESS
#if (NGX_WIN32)
            /* Winsock returns WSAEWOULDBLOCK (NGX_EAGAIN) */
            && err != NGX_EAGAIN
#endif
            )
        {
            if (err == NGX_ECONNREFUSED
#if (NGX_LINUX)
                /*
                 * Linux returns EAGAIN instead of ECONNREFUSED
                 * for unix sockets if listen queue is full
                 */
                || err == NGX_EAGAIN
#endif
                || err == NGX_ECONNRESET
                || err == NGX_ENETDOWN
                || err == NGX_ENETUNREACH
                || err == NGX_EHOSTDOWN
                || err == NGX_EHOSTUNREACH)
            {
                level = NGX_LOG_ERR;

            } else {
                level = NGX_LOG_CRIT;
            }
            ngx_log_error(level, c->log, err, "connect() to %V failed", pc->name);
            return NGX_DECLINED;
        }
    }

    if (ngx_add_conn) {
        if (rc == -1) {
            /* NGX_EINPROGRESS */
            return NGX_AGAIN;
        }
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, pc->log, 0, "connected");
        wev->ready = 1;
        return NGX_OK;
    }
    if (ngx_event_flags & NGX_USE_AIO_EVENT) {
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, pc->log, ngx_socket_errno, "connect(): %d", rc);
        /* aio, iocp */
        if (ngx_blocking(s) == -1) {
            ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno, ngx_blocking_n " failed");
            goto failed;
        }
        /*
         * FreeBSD's aio allows to post an operation on non-connected socket.
         * NT does not support it.
         *
         * TODO: check in Win32, etc. As workaround we can use NGX_ONESHOT_EVENT
         */
        rev->ready = 1;
        wev->ready = 1;
        return NGX_OK;
    }
    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {
        /* kqueue */
        event = NGX_CLEAR_EVENT;
    } else {
        /* select, poll, /dev/poll */
        event = NGX_LEVEL_EVENT;
    }
    if (ngx_add_event(rev, NGX_READ_EVENT, event) != NGX_OK) {//设置读事件
        goto failed;
    }
    if (rc == -1) {
        /* NGX_EINPROGRESS */
        if (ngx_add_event(wev, NGX_WRITE_EVENT, event) != NGX_OK) {//设置写事件
            goto failed;
        }
        return NGX_AGAIN;
    }
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, pc->log, 0, "connected");
    wev->ready = 1;
    return NGX_OK;

failed:
    ngx_free_connection(c);
    if (ngx_close_socket(s) == -1) {
        ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno,  ngx_close_socket_n " failed");
    }
    return NGX_ERROR;
}


ngx_int_t
ngx_event_get_peer(ngx_peer_connection_t *pc, void *data)
{
    return NGX_OK;
}
