
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>


ngx_int_t
ngx_event_connect_peer(ngx_peer_connection_t *pc)
{//��ȡһ�����õ�peer,Ȼ��������.��ע��ɶ�����д�¼�
    int                rc;
    ngx_int_t          event;
    ngx_err_t          err;
    ngx_uint_t         level;
    ngx_socket_t       s;
    ngx_event_t       *rev, *wev;
    ngx_connection_t  *c;

    rc = pc->get(pc, pc->data);//ngx_http_upstream_get_round_robin_peer��ȡһ��peer
    if (rc != NGX_OK) {
        return rc;
    }
    s = ngx_socket(pc->sockaddr->sa_family, SOCK_STREAM, 0);//�½�һ��SOCK
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, pc->log, 0, "socket %d", s);
    if (s == -1) {
        ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno, ngx_socket_n " failed");
        return NGX_ERROR;
    }
    c = ngx_get_connection(s, pc->log);//��ngx_cycle->free_connections������һ�����е�λ�ô��������ӣ�Ȼ���ʼ����س�Ա
    if (c == NULL) {//�õ�һ�����ӽṹ��
        if (ngx_close_socket(s) == -1) {
            ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno,  ngx_close_socket_n "failed");
        }
        return NGX_ERROR;
    }

    if (pc->rcvbuf) {//rcvbuf�Ǹ�ָ��
        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const void *) &pc->rcvbuf, sizeof(int)) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno, "setsockopt(SO_RCVBUF) failed");
            goto failed;
        }
    }
	//����Ϊ������ģʽ��
    if (ngx_nonblocking(s) == -1) {
        ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno, ngx_nonblocking_n " failed");
        goto failed;
    }
    if (pc->local) {//����ɶ��˼�������?
        if (bind(s, pc->local->sockaddr, pc->local->socklen) == -1) {
            ngx_log_error(NGX_LOG_CRIT, pc->log, ngx_socket_errno, "bind(%V) failed", &pc->local->name);
            goto failed;
        }
    }

	//��������������ӵĶ�д�ص�������
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
    rev = c->read;//����������ӵĶ�д�¼��ṹ
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

    if (ngx_add_conn) {//����ngx_epoll_add_connectionע��epool�ĸ����¼���������д�¼��ṹ�������á�
        if (ngx_add_conn(c) == NGX_ERROR) {
            goto failed;
        }
    }
    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, pc->log, 0, "connect to %V, fd:%d #%d", pc->name, s, c->number);
    rc = connect(s, pc->sockaddr, pc->socklen);//Ȼ��������

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
    if (ngx_add_event(rev, NGX_READ_EVENT, event) != NGX_OK) {//���ö��¼�
        goto failed;
    }
    if (rc == -1) {
        /* NGX_EINPROGRESS */
        if (ngx_add_event(wev, NGX_WRITE_EVENT, event) != NGX_OK) {//����д�¼�
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