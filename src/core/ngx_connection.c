
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


ngx_os_io_t  ngx_io;//比如ngx_os_io


ngx_listening_t *
ngx_create_listening(ngx_conf_t *cf, void *sockaddr, socklen_t socklen)
{//初始化并返回ngx_listening_t结构。
    size_t            len;
    ngx_listening_t  *ls;
    struct sockaddr  *sa;
    u_char            text[NGX_SOCKADDR_STRLEN];
    ls = ngx_array_push(&cf->cycle->listening);
    if (ls == NULL) {
        return NULL;
    }
    ngx_memzero(ls, sizeof(ngx_listening_t));
    sa = ngx_palloc(cf->pool, socklen);
    if (sa == NULL) {
        return NULL;
    }
    ngx_memcpy(sa, sockaddr, socklen);
    ls->sockaddr = sa;
    ls->socklen = socklen;
    len = ngx_sock_ntop(sa, text, NGX_SOCKADDR_STRLEN, 1);
    ls->addr_text.len = len;
    switch (ls->sockaddr->sa_family) {
#if (NGX_HAVE_INET6)
    case AF_INET6:
         ls->addr_text_max_len = NGX_INET6_ADDRSTRLEN;
         break;
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
    case AF_UNIX:
         ls->addr_text_max_len = NGX_UNIX_ADDRSTRLEN;
         len++;
         break;
#endif
    case AF_INET:
         ls->addr_text_max_len = NGX_INET_ADDRSTRLEN;
         break;
    default:
         ls->addr_text_max_len = NGX_SOCKADDR_STRLEN;
         break;
    }
    ls->addr_text.data = ngx_pnalloc(cf->pool, len);
    if (ls->addr_text.data == NULL) {
        return NULL;
    }
    ngx_memcpy(ls->addr_text.data, text, len);
    ls->fd = (ngx_socket_t) -1;
    ls->type = SOCK_STREAM;
    ls->backlog = NGX_LISTEN_BACKLOG;
    ls->rcvbuf = -1;
    ls->sndbuf = -1;
#if (NGX_HAVE_SETFIB)
    ls->setfib = -1;
#endif

    return ls;
}


ngx_int_t
ngx_set_inherited_sockets(ngx_cycle_t *cycle)
{//主要是对数组中的每一个元素进行判断是否有效，然后进行初始化操作。
    size_t                     len;
    ngx_uint_t                 i;
    ngx_listening_t           *ls;
    socklen_t                  olen;
#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
    ngx_err_t                  err;
    struct accept_filter_arg   af;
#endif
#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
    int                        timeout;
#endif

    ls = cycle->listening.elts;//当前只记录了一个fd
    for (i = 0; i < cycle->listening.nelts; i++) {
        ls[i].sockaddr = ngx_palloc(cycle->pool, NGX_SOCKADDRLEN);
        if (ls[i].sockaddr == NULL) {
            return NGX_ERROR;
        }
        ls[i].socklen = NGX_SOCKADDRLEN;
        if (getsockname(ls[i].fd, ls[i].sockaddr, &ls[i].socklen) == -1) {
            ngx_log_error(NGX_LOG_CRIT, cycle->log, ngx_socket_errno, "getsockname() of the inherited " "socket #%d failed", ls[i].fd);
            ls[i].ignore = 1;
            continue;
        }
        switch (ls[i].sockaddr->sa_family) {
#if (NGX_HAVE_INET6)
        case AF_INET6:
             ls[i].addr_text_max_len = NGX_INET6_ADDRSTRLEN;
             len = NGX_INET6_ADDRSTRLEN + sizeof(":65535") - 1;
             break;
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
        case AF_UNIX:
             ls[i].addr_text_max_len = NGX_UNIX_ADDRSTRLEN;
             len = NGX_UNIX_ADDRSTRLEN;
             break;
#endif
        case AF_INET:
             ls[i].addr_text_max_len = NGX_INET_ADDRSTRLEN;
             len = NGX_INET_ADDRSTRLEN + sizeof(":65535") - 1;
             break;

        default:
            ngx_log_error(NGX_LOG_CRIT, cycle->log, ngx_socket_errno, "the inherited socket #%d has " "an unsupported protocol family", ls[i].fd);
            ls[i].ignore = 1;
            continue;
        }

        ls[i].addr_text.data = ngx_pnalloc(cycle->pool, len);//len为127.0.0.1:8008的长度
        if (ls[i].addr_text.data == NULL) {
            return NGX_ERROR;
        }

        len = ngx_sock_ntop(ls[i].sockaddr, ls[i].addr_text.data, len, 1);
        if (len == 0) {
            return NGX_ERROR;
        }

        ls[i].addr_text.len = len;

        ls[i].backlog = NGX_LISTEN_BACKLOG;//511大小

        olen = sizeof(int);
//得到接收缓冲区的大小
        if (getsockopt(ls[i].fd, SOL_SOCKET, SO_RCVBUF, (void *) &ls[i].rcvbuf, &olen) == -1)
        {//k The SO_RCVBUF socket option determines the size of a socket's receive buffer that is used by the underlying transport. 
        //SO_RCVBUF表示服务器端的用于接收数据的缓冲区的大小，以字节为单位。一般说来，
        //传输大的连续的数据块（基于HTTP或FTP协议的数据传输）可以使用较大的缓冲区，这可以减少传输数据的次数，
        //从而提高传输数据的效率。而对于交互式的通信（Telnet和网络游戏），则应该采用小的缓冲区，确保能及时把小批量的数据发送给对方。
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_socket_errno, "getsockopt(SO_RCVBUF) %V failed, ignored", &ls[i].addr_text);
            ls[i].rcvbuf = -1;
        }
        olen = sizeof(int);
//得到发送缓冲区的大小
        if (getsockopt(ls[i].fd, SOL_SOCKET, SO_SNDBUF, (void *) &ls[i].sndbuf,  &olen) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_socket_errno,  "getsockopt(SO_SNDBUF) %V failed, ignored", &ls[i].addr_text);

            ls[i].sndbuf = -1;
        }

#if 0
        /* SO_SETFIB is currently a set only option */

#if (NGX_HAVE_SETFIB)

        if (getsockopt(ls[i].setfib, SOL_SOCKET, SO_SETFIB,
                       (void *) &ls[i].setfib, &olen)
            == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_socket_errno,
                          "getsockopt(SO_SETFIB) %V failed, ignored",
                          &ls[i].addr_text);

            ls[i].setfib = -1;
        }

#endif
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
//Linux（以及其他的一些操作系统）在其TCP实现中包括了TCP_DEFER_ACCEPT选项。
//它们设置在侦听套接字的服务器方，该选项命令内核不等待最后的ACK包而且在第1个
//真正有数据的包到达才初始化侦听进程。在发送SYN/ACK包之后，服务器就会等待客户程序发送含数据的IP包。
//SO_ACCEPTFILTER在FreeBSD上叫做“接受过滤器”，而且具有多种用法。不过，在几乎所有的情况下其效果与TCP_DEFER_ACCEPT是一样的：
//服务器不等待最后的ACK包而仅仅等待携带数据负载的包。

        ngx_memzero(&af, sizeof(struct accept_filter_arg));
        olen = sizeof(struct accept_filter_arg);
        if (getsockopt(ls[i].fd, SOL_SOCKET, SO_ACCEPTFILTER, &af, &olen) == -1) {
            err = ngx_errno;
            if (err == NGX_EINVAL) {
                continue;
            }
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, err,  "getsockopt(SO_ACCEPTFILTER) for %V failed, ignored", &ls[i].addr_text);
            continue;
        }
        if (olen < sizeof(struct accept_filter_arg) || af.af_name[0] == '\0') {
            continue;
        }
        ls[i].accept_filter = ngx_palloc(cycle->pool, 16);
        if (ls[i].accept_filter == NULL) {
            return NGX_ERROR;
        }
        (void) ngx_cpystrn((u_char *) ls[i].accept_filter,  (u_char *) af.af_name, 16);
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)

        timeout = 0;
        olen = sizeof(int);
        if (getsockopt(ls[i].fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &timeout, &olen) == -1) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, ngx_errno, "getsockopt(TCP_DEFER_ACCEPT) for %V failed, ignored",  &ls[i].addr_text);
            continue;
        }
        if (olen < sizeof(int) || timeout == 0) {
            continue;
        }
        ls[i].deferred_accept = 1;
#endif
    }
    return NGX_OK;
}


ngx_int_t
ngx_open_listening_sockets(ngx_cycle_t *cycle)
{
    int               reuseaddr;
    ngx_uint_t        i, tries, failed;
    ngx_err_t         err;
    ngx_log_t        *log;
    ngx_socket_t      s;
    ngx_listening_t  *ls;

    reuseaddr = 1;
#if (NGX_SUPPRESS_WARN)
    failed = 0;
#endif
    log = cycle->log;
    /* TODO: configurable try number */
    for (tries = 5; tries; tries--) {
        failed = 0;//一共重试5次，比如端口打不开的时候
        /* for each listening socket */
		//k:ngx_listening_s cycle->listening.elts[]
        ls = cycle->listening.elts;
        for (i = 0; i < cycle->listening.nelts; i++) {
            if (ls[i].ignore) {
                continue;
            }
            if (ls[i].fd != -1) {
                continue;//已经打开了
            }
            if (ls[i].inherited) {
                /* TODO: close on exit */
                /* TODO: nonblocking */
                /* TODO: deferred accept */
                continue;
            }
//新建SOCKET
            s = ngx_socket(ls[i].sockaddr->sa_family, ls[i].type, 0);

            if (s == -1) {
                ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno, ngx_socket_n " %V failed", &ls[i].addr_text);
                return NGX_ERROR;
            }
//SO_REUSEADDR，这个套接字选项通知内核，如果端口忙，但TCP状态位于 TIME_WAIT ，可以重用端口。
            if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,(const void *) &reuseaddr, sizeof(int)) == -1)
            {
                ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,    "setsockopt(SO_REUSEADDR) %V failed",  &ls[i].addr_text);
                if (ngx_close_socket(s) == -1) {
                    ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,  ngx_close_socket_n " %V failed",   &ls[i].addr_text);
                }
                return NGX_ERROR;
            }

#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)

            if (ls[i].sockaddr->sa_family == AF_INET6 && ls[i].ipv6only) {
                int  ipv6only;
                ipv6only = (ls[i].ipv6only == 1);
                if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const void *) &ipv6only, sizeof(int))  == -1)
                {
                    ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno, "setsockopt(IPV6_V6ONLY) %V failed, ignored",  &ls[i].addr_text);
                }
            }
#endif
            /* TODO: close on exit */
//设置非阻塞模式
            if (!(ngx_event_flags & NGX_USE_AIO_EVENT)) {
                if (ngx_nonblocking(s) == -1) {
                    ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,  ngx_nonblocking_n " %V failed",  &ls[i].addr_text);
                    if (ngx_close_socket(s) == -1) {
                        ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,  ngx_close_socket_n " %V failed", &ls[i].addr_text);
                    }
                    return NGX_ERROR;
                }
            }

            ngx_log_debug2(NGX_LOG_DEBUG_CORE, log, 0,  "bind() %V #%d ", &ls[i].addr_text, s);
//绑定地址
            if (bind(s, ls[i].sockaddr, ls[i].socklen) == -1) {
                err = ngx_socket_errno;
                if (err == NGX_EADDRINUSE && ngx_test_config) {
                    continue;
                }
                ngx_log_error(NGX_LOG_EMERG, log, err,  "bind() to %V failed", &ls[i].addr_text);
                if (ngx_close_socket(s) == -1) {
                    ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,  ngx_close_socket_n " %V failed",  &ls[i].addr_text);
                }
                if (err != NGX_EADDRINUSE) {
                    return NGX_ERROR;
                }
                failed = 1;
                continue;
            }

#if (NGX_HAVE_UNIX_DOMAIN)

            if (ls[i].sockaddr->sa_family == AF_UNIX) {
                mode_t   mode;
                u_char  *name;
                name = ls[i].addr_text.data + sizeof("unix:") - 1;
                mode = (S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
                if (chmod((char *) name, mode) == -1) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,  "chmod() \"%s\" failed", name);
                }

                if (ngx_test_config) {
                    if (ngx_delete_file(name) == -1) {
                        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, ngx_delete_file_n " %s failed", name);
                    }
                }
            }
#endif
//设置为LISTENG的SOCKET，负责监听请求
            if (listen(s, ls[i].backlog) == -1) {
                ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno, "listen() to %V, backlog %d failed", &ls[i].addr_text, ls[i].backlog);
                if (ngx_close_socket(s) == -1) {
                    ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,  ngx_close_socket_n " %V failed", &ls[i].addr_text);
                }
                return NGX_ERROR;
            }
            ls[i].listen = 1;
            ls[i].fd = s;
        }
        if (!failed) {//如果没有失败的，则OK，跳出结束
            break;
        }
        /* TODO: delay configurable */
        ngx_log_error(NGX_LOG_NOTICE, log, 0, "try again to bind() after 500ms");
//否则500毫秒后再次尝试失败的。
        ngx_msleep(500);
    }
    if (failed) {
        ngx_log_error(NGX_LOG_EMERG, log, 0, "still could not bind()");
        return NGX_ERROR;
    }

    return NGX_OK;
}


void
ngx_configure_listening_sockets(ngx_cycle_t *cycle)
{
    ngx_uint_t                 i;
    ngx_listening_t           *ls;

#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
    struct accept_filter_arg   af;
#endif
#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
    int                        timeout;
#endif

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {

        ls[i].log = *ls[i].logp;

        if (ls[i].rcvbuf != -1) {
            if (setsockopt(ls[i].fd, SOL_SOCKET, SO_RCVBUF,
                           (const void *) &ls[i].rcvbuf, sizeof(int))
                == -1)
            {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_socket_errno,
                              "setsockopt(SO_RCVBUF, %d) %V failed, ignored",
                              ls[i].rcvbuf, &ls[i].addr_text);
            }
        }

        if (ls[i].sndbuf != -1) {
            if (setsockopt(ls[i].fd, SOL_SOCKET, SO_SNDBUF,
                           (const void *) &ls[i].sndbuf, sizeof(int))
                == -1)
            {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_socket_errno,
                              "setsockopt(SO_SNDBUF, %d) %V failed, ignored",
                              ls[i].sndbuf, &ls[i].addr_text);
            }
        }

#if (NGX_HAVE_SETFIB)
        if (ls[i].setfib != -1) {
            if (setsockopt(ls[i].fd, SOL_SOCKET, SO_SETFIB,
                           (const void *) &ls[i].setfib, sizeof(int))
                == -1)
            {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_socket_errno,
                              "setsockopt(SO_SETFIB, %d) %V failed, ignored",
                              ls[i].setfib, &ls[i].addr_text);
            }
        }
#endif

#if 0
        if (1) {
            int tcp_nodelay = 1;

            if (setsockopt(ls[i].fd, IPPROTO_TCP, TCP_NODELAY,
                       (const void *) &tcp_nodelay, sizeof(int))
                == -1)
            {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_socket_errno,
                              "setsockopt(TCP_NODELAY) %V failed, ignored",
                              &ls[i].addr_text);
            }
        }
#endif

        if (ls[i].listen) {

            /* change backlog via listen() */

            if (listen(ls[i].fd, ls[i].backlog) == -1) {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_socket_errno,
                              "listen() to %V, backlog %d failed, ignored",
                              &ls[i].addr_text, ls[i].backlog);
            }
        }

        /*
         * setting deferred mode should be last operation on socket,
         * because code may prematurely continue cycle on failure
         */

#if (NGX_HAVE_DEFERRED_ACCEPT)

#ifdef SO_ACCEPTFILTER

        if (ls[i].delete_deferred) {
            if (setsockopt(ls[i].fd, SOL_SOCKET, SO_ACCEPTFILTER, NULL, 0)
                == -1)
            {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                              "setsockopt(SO_ACCEPTFILTER, NULL) "
                              "for %V failed, ignored",
                              &ls[i].addr_text);

                if (ls[i].accept_filter) {
                    ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                                  "could not change the accept filter "
                                  "to \"%s\" for %V, ignored",
                                  ls[i].accept_filter, &ls[i].addr_text);
                }

                continue;
            }

            ls[i].deferred_accept = 0;
        }

        if (ls[i].add_deferred) {
            ngx_memzero(&af, sizeof(struct accept_filter_arg));
            (void) ngx_cpystrn((u_char *) af.af_name,
                               (u_char *) ls[i].accept_filter, 16);

            if (setsockopt(ls[i].fd, SOL_SOCKET, SO_ACCEPTFILTER,
                           &af, sizeof(struct accept_filter_arg))
                == -1)
            {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                              "setsockopt(SO_ACCEPTFILTER, \"%s\") "
                              " for %V failed, ignored",
                              ls[i].accept_filter, &ls[i].addr_text);
                continue;
            }

            ls[i].deferred_accept = 1;
        }

#endif

#ifdef TCP_DEFER_ACCEPT

        if (ls[i].add_deferred || ls[i].delete_deferred) {

            if (ls[i].add_deferred) {
                timeout = (int) (ls[i].post_accept_timeout / 1000);

            } else {
                timeout = 0;
            }

            if (setsockopt(ls[i].fd, IPPROTO_TCP, TCP_DEFER_ACCEPT,
                           &timeout, sizeof(int))
                == -1)
            {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                              "setsockopt(TCP_DEFER_ACCEPT, %d) for %V failed, "
                              "ignored",
                              timeout, &ls[i].addr_text);

                continue;
            }
        }

        if (ls[i].add_deferred) {
            ls[i].deferred_accept = 1;
        }

#endif

#endif /* NGX_HAVE_DEFERRED_ACCEPT */
    }

    return;
}

/*
1.对已经加入到epoll中的监听socket，删除注册的事件。
2.close掉所有的监听端口；
3.将这个链接放回到free_connections链表头部，并置空files对应结构
*/
void ngx_close_listening_sockets(ngx_cycle_t *cycle)
{
    ngx_uint_t         i;
    ngx_listening_t   *ls;
    ngx_connection_t  *c;

    if (ngx_event_flags & NGX_USE_IOCP_EVENT) {
        return;
    }
    ngx_accept_mutex_held = 0;
    ngx_use_accept_mutex = 0;
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        c = ls[i].connection;//拿到这个监听socket
        if (c) {
            if (c->read->active) {//c->read实际上为cycle->read_events[i]对应的位置。如果这个连接设置到了epoll，需要删除之
                if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {//传说中的rtsig，内核现在已经丢弃了
                    ngx_del_conn(c, NGX_CLOSE_EVENT);
                } else if (ngx_event_flags & NGX_USE_EPOLL_EVENT) {
                    /*
                     * it seems that Linux-2.6.x OpenVZ sends events
                     * for closed shared listening sockets unless
                     * the events was explicity deleted
                     */
                    ngx_del_event(c->read, NGX_READ_EVENT, 0);
                } else {
                    ngx_del_event(c->read, NGX_READ_EVENT, NGX_CLOSE_EVENT);
                }
            }
//将这个链接放回到free_connections链表头部，并置空files对应结构
            ngx_free_connection(c);
            c->fd = (ngx_socket_t) -1;//置空，ngx_free_connection要用fd所以这里置空
        }
//关闭这个链接
        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0, "close listening %V #%d ", &ls[i].addr_text, ls[i].fd);
        if (ngx_close_socket(ls[i].fd) == -1) {//使用ngx_listening_s里面保存的fd进行关闭
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_socket_errno,
                          ngx_close_socket_n " %V failed", &ls[i].addr_text);
        }
//如果是使用的sock文件
#if (NGX_HAVE_UNIX_DOMAIN)
        if (ls[i].sockaddr->sa_family == AF_UNIX
            && ngx_process <= NGX_PROCESS_MASTER //只有NGX_PROCESS_SINGLE小于master,就是单进程模型
            && ngx_new_binary == 0) {
            u_char *name = ls[i].addr_text.data + sizeof("unix:") - 1;
            if (ngx_delete_file(name) == -1) {//删除sock文件
                 ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_socket_errno,  ngx_delete_file_n " %s failed", name);
            }
        }
#endif
        ls[i].fd = (ngx_socket_t) -1;//吧ngx_listening_s里面的也置空
    }
}


ngx_connection_t *
ngx_get_connection(ngx_socket_t s, ngx_log_t *log)
{//在ngx_cycle->free_connections里面找一个空闲的位置存放这个连接，然后初始化相关成员
//free_connections一开始是指向connections的，connections是所有的连接。

    ngx_uint_t         instance;
    ngx_event_t       *rev, *wev;
    ngx_connection_t  *c;

    /* disable warning: Win32 SOCKET is u_int while UNIX socket is int */
    if (ngx_cycle->files && (ngx_uint_t) s >= ngx_cycle->files_n) {
        ngx_log_error(NGX_LOG_ALERT, log, 0, "the new socket has number %d, but only %ui files are available", s, ngx_cycle->files_n);
        return NULL;
    }
    /* ngx_mutex_lock */
    c = ngx_cycle->free_connections;
    if (c == NULL) {
        ngx_log_error(NGX_LOG_ALERT, log, 0,
                      "%ui worker_connections are not enough",
                      ngx_cycle->connection_n);
        /* ngx_mutex_unlock */
        return NULL;
    }

    ngx_cycle->free_connections = c->data;//减少一个空闲连接，指向下一个
    ngx_cycle->free_connection_n--;
    /* ngx_mutex_unlock */

    if (ngx_cycle->files) {//记录进数组
        ngx_cycle->files[s] = c;
    }

    rev = c->read;
    wev = c->write;
//清理一下数据结构，重新赋值
    ngx_memzero(c, sizeof(ngx_connection_t));

    c->read = rev;
    c->write = wev;
    c->fd = s;//记录fd
    c->log = log;

    instance = rev->instance;

    ngx_memzero(rev, sizeof(ngx_event_t));//清空
    ngx_memzero(wev, sizeof(ngx_event_t));

    rev->instance = !instance;//这个连接要被使用了，反一下，以便防止stale event
    wev->instance = !instance;

    rev->index = NGX_INVALID_INDEX;
    wev->index = NGX_INVALID_INDEX;

    rev->data = c;//回指。一个连接的读写event结构的data部分会回指向这个连接。
    wev->data = c;

    wev->write = 1;

    return c;
}


void
ngx_free_connection(ngx_connection_t *c)
{
    /* ngx_mutex_lock */

    c->data = ngx_cycle->free_connections;//返回到头部
    ngx_cycle->free_connections = c;
    ngx_cycle->free_connection_n++;//增加计数

    /* ngx_mutex_unlock */

    if (ngx_cycle->files) {
        ngx_cycle->files[c->fd] = NULL;//去掉槽位，设置为空
    }
}


void
ngx_close_connection(ngx_connection_t *c)
{
    ngx_err_t     err;
    ngx_uint_t    log_error, level;
    ngx_socket_t  fd;

    if (c->fd == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, 0, "connection already closed");
        return;
    }

    if (c->read->timer_set) {
        ngx_del_timer(c->read);
    }

    if (c->write->timer_set) {
        ngx_del_timer(c->write);
    }

    if (ngx_del_conn) {
        ngx_del_conn(c, NGX_CLOSE_EVENT);

    } else {
        if (c->read->active || c->read->disabled) {
            ngx_del_event(c->read, NGX_READ_EVENT, NGX_CLOSE_EVENT);
        }

        if (c->write->active || c->write->disabled) {
            ngx_del_event(c->write, NGX_WRITE_EVENT, NGX_CLOSE_EVENT);
        }
    }

#if (NGX_THREADS)

    /*
     * we have to clean the connection information before the closing
     * because another thread may reopen the same file descriptor
     * before we clean the connection
     */

    ngx_mutex_lock(ngx_posted_events_mutex);

    if (c->read->prev) {
        ngx_delete_posted_event(c->read);
    }

    if (c->write->prev) {
        ngx_delete_posted_event(c->write);
    }

    c->read->closed = 1;
    c->write->closed = 1;

    if (c->single_connection) {
        ngx_unlock(&c->lock);
        c->read->locked = 0;
        c->write->locked = 0;
    }

    ngx_mutex_unlock(ngx_posted_events_mutex);

#else

    if (c->read->prev) {
        ngx_delete_posted_event(c->read);
    }

    if (c->write->prev) {
        ngx_delete_posted_event(c->write);
    }

    c->read->closed = 1;
    c->write->closed = 1;

#endif

    log_error = c->log_error;

    ngx_free_connection(c);

    fd = c->fd;
    c->fd = (ngx_socket_t) -1;

    if (ngx_close_socket(fd) == -1) {

        err = ngx_socket_errno;

        if (err == NGX_ECONNRESET || err == NGX_ENOTCONN) {

            switch (log_error) {

            case NGX_ERROR_INFO:
                level = NGX_LOG_INFO;
                break;

            case NGX_ERROR_ERR:
                level = NGX_LOG_ERR;
                break;

            default:
                level = NGX_LOG_CRIT;
            }

        } else {
            level = NGX_LOG_CRIT;
        }

        /* we use ngx_cycle->log because c->log was in c->pool */

        ngx_log_error(level, ngx_cycle->log, err,
                      ngx_close_socket_n " %d failed", fd);
    }
}


ngx_int_t
ngx_connection_local_sockaddr(ngx_connection_t *c, ngx_str_t *s, ngx_uint_t port)
{
    socklen_t             len;
    ngx_uint_t            addr;
    u_char                sa[NGX_SOCKADDRLEN];
    struct sockaddr_in   *sin;
#if (NGX_HAVE_INET6)
    ngx_uint_t            i;
    struct sockaddr_in6  *sin6;
#endif

    switch (c->local_sockaddr->sa_family) {//本机的地址，c->local_sockaddr = ls->sockaddr;

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = (struct sockaddr_in6 *) c->local_sockaddr;

        for (addr = 0, i = 0; addr == 0 && i < 16; i++) {
            addr |= sin6->sin6_addr.s6_addr[i];
        }

        break;
#endif

    default: /* AF_INET */
        sin = (struct sockaddr_in *) c->local_sockaddr;
        addr = sin->sin_addr.s_addr;
        break;
    }

    if (addr == 0) {
        len = NGX_SOCKADDRLEN;
        if (getsockname(c->fd, (struct sockaddr *) &sa, &len) == -1) {
            ngx_connection_error(c, ngx_socket_errno, "getsockname() failed");
            return NGX_ERROR;
        }

        c->local_sockaddr = ngx_palloc(c->pool, len);
        if (c->local_sockaddr == NULL) {
            return NGX_ERROR;
        }

        ngx_memcpy(c->local_sockaddr, &sa, len);// 通过这个函数，实质上就是调用了getsockname，获得该连接的服务器端ip 
    }

    if (s == NULL) {
        return NGX_OK;
    }

    s->len = ngx_sock_ntop(c->local_sockaddr, s->data, s->len, port);

    return NGX_OK;
}


ngx_int_t
ngx_connection_error(ngx_connection_t *c, ngx_err_t err, char *text)
{
    ngx_uint_t  level;
    /* Winsock may return NGX_ECONNABORTED instead of NGX_ECONNRESET */
    if ((err == NGX_ECONNRESET
#if (NGX_WIN32)
         || err == NGX_ECONNABORTED
#endif
        ) && c->log_error == NGX_ERROR_IGNORE_ECONNRESET)
    {
        return 0;
    }

#if (NGX_SOLARIS)
    if (err == NGX_EINVAL && c->log_error == NGX_ERROR_IGNORE_EINVAL) {
        return 0;
    }
#endif

    if (err == 0
        || err == NGX_ECONNRESET
#if (NGX_WIN32)
        || err == NGX_ECONNABORTED
#else
        || err == NGX_EPIPE
#endif
        || err == NGX_ENOTCONN
        || err == NGX_ETIMEDOUT
        || err == NGX_ECONNREFUSED
        || err == NGX_ENETDOWN
        || err == NGX_ENETUNREACH
        || err == NGX_EHOSTDOWN
        || err == NGX_EHOSTUNREACH)
    {
        switch (c->log_error) {

        case NGX_ERROR_IGNORE_EINVAL:
        case NGX_ERROR_IGNORE_ECONNRESET:
        case NGX_ERROR_INFO:
            level = NGX_LOG_INFO;
            break;

        default:
            level = NGX_LOG_ERR;
        }

    } else {
        level = NGX_LOG_ALERT;
    }

    ngx_log_error(level, c->log, err, text);

    return NGX_ERROR;
}
