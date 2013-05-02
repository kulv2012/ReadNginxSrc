
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


static ngx_int_t ngx_enable_accept_events(ngx_cycle_t *cycle);
static ngx_int_t ngx_disable_accept_events(ngx_cycle_t *cycle);
static void ngx_close_accepted_connection(ngx_connection_t *c);

//当有新连接后，会调用读ngx_event_t结构read的handler回调，监听socket会设置为这个函数。
//工作进程初始化的时候会调用ngx_event_process_init模块初始化函数设置为ngx_event_accept，当做accept钩子
//有新连接的时候会调用这里进行accept.
//这里会将新连接放入epoll，监听可读可写事件，然后调用ngx_http_init_connection
void ngx_event_accept(ngx_event_t *ev)
{
    socklen_t          socklen;
    ngx_err_t          err;
    ngx_log_t         *log;
    ngx_socket_t       s;
    ngx_event_t       *rev, *wev;
    ngx_listening_t   *ls;
    ngx_connection_t  *c, *lc;
    ngx_event_conf_t  *ecf;
    u_char             sa[NGX_SOCKADDRLEN];

    ecf = ngx_event_get_conf(ngx_cycle->conf_ctx, ngx_event_core_module);//先得到ngx_events_module，然后再得到里面的core模块
    if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {
        ev->available = 1;
    } else if (!(ngx_event_flags & NGX_USE_KQUEUE_EVENT)) {
        ev->available = ecf->multi_accept;//一次尽量接完，默认为0的
    }
    lc = ev->data;//得到这个事件所属的连接
    ls = lc->listening;//从而得到这个连接所指的listening 结构
    ev->ready = 0;
    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, ev->log, 0,"accept on %V, ready: %d", &ls->addr_text, ev->available);
    do {//这个连接有可读事件了，那可能可以读很多了，所以得有循环
        socklen = NGX_SOCKADDRLEN;
        s = accept(lc->fd, (struct sockaddr *) sa, &socklen);//接一个新连接
        if (s == -1) {//失败
            err = ngx_socket_errno;
            if (err == NGX_EAGAIN) {//没有了这回
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, err, "accept() not ready");
                return;
            }
            ngx_log_error((ngx_uint_t) ((err == NGX_ECONNABORTED) ? NGX_LOG_ERR : NGX_LOG_ALERT), ev->log, err, "accept() failed");
            if (err == NGX_ECONNABORTED) {
                if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
                    ev->available--;//kqueue的话不能接多个
                }
                if (ev->available) {
                    continue;
                }
            }
            return;
        }
//accept成功
#if (NGX_STAT_STUB)
        (void) ngx_atomic_fetch_add(ngx_stat_accepted, 1);
#endif
        ngx_accept_disabled = ngx_cycle->connection_n / 8 - ngx_cycle->free_connection_n;
//当已使用的连接数占到在nginx.conf里配置的worker_connections总数的7/8以上时，ngx_accept_disabled为大于0，
//此后在主循环里面就不会再进行accept，而是递减1，这样相当于让我这个进程丢掉一点accept的机会吧。
//不过这个只在accept_mutex on 配置打开时才有效，否则的话是默认会不断监听的

        c = ngx_get_connection(s, ev->log);//拿到一个空闲的连接
        if (c == NULL) {
            if (ngx_close_socket(s) == -1) {
                ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_socket_errno,
                              ngx_close_socket_n " failed");
            }
            return;
        }

#if (NGX_STAT_STUB)
        (void) ngx_atomic_fetch_add(ngx_stat_active, 1);
#endif

        c->pool = ngx_create_pool(ls->pool_size, ev->log);
//为这个连接新建一个pool，这样那个连接关闭后，这个内存池也可以释放了，这样大大减少内存泄露
        if (c->pool == NULL) {//内存申请失败
            ngx_close_accepted_connection(c);
            return;
        }

        c->sockaddr = ngx_palloc(c->pool, socklen);
        if (c->sockaddr == NULL) {
            ngx_close_accepted_connection(c);
            return;
        }

        ngx_memcpy(c->sockaddr, sa, socklen);
        log = ngx_palloc(c->pool, sizeof(ngx_log_t));
        if (log == NULL) {
            ngx_close_accepted_connection(c);
            return;
        }
        /* set a blocking mode for aio and non-blocking mode for others */
        if (ngx_inherited_nonblocking) {
            if (ngx_event_flags & NGX_USE_AIO_EVENT) {
                if (ngx_blocking(s) == -1) {
                    ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_socket_errno,
                                  ngx_blocking_n " failed");
                    ngx_close_accepted_connection(c);
                    return;
                }
            }
        } else {//设置为非阻塞的
            if (!(ngx_event_flags & (NGX_USE_AIO_EVENT|NGX_USE_RTSIG_EVENT))) {
                if (ngx_nonblocking(s) == -1) {
                    ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_socket_errno,
                                  ngx_nonblocking_n " failed");
                    ngx_close_accepted_connection(c);
                    return;
                }
            }
        }
        *log = ls->log;
        c->recv = ngx_recv;//k ngx_unix_recv  ，其实还有ngx_ssl_recv
        c->send = ngx_send;//k ngx_unix_send , 其实还有ngx_ssl_write
        c->recv_chain = ngx_recv_chain;//k ngx_readv_chain
        c->send_chain = ngx_send_chain;//k ngx_writev_chain
/*ngx_io = ngx_os_io ;//相当于这个IO是跟os相关的。
ngx_os_io_t ngx_os_io = {
    ngx_unix_recv,
    ngx_readv_chain,
    ngx_udp_unix_recv,
    ngx_unix_send,
    ngx_writev_chain,
    0
};*/
        c->log = log;
        c->pool->log = log;
        c->socklen = socklen;
        c->listening = ls;//刚申请的连接，回指一下这个连接所属的listening结构。指向我是从哪个listenSOCK accept出来的
        c->local_sockaddr = ls->sockaddr;
        c->unexpected_eof = 1;
#if (NGX_HAVE_UNIX_DOMAIN)
        if (c->sockaddr->sa_family == AF_UNIX) {
            c->tcp_nopush = NGX_TCP_NOPUSH_DISABLED;
            c->tcp_nodelay = NGX_TCP_NODELAY_DISABLED;
        }
#endif
        rev = c->read;//这个新连接的读写事件
        wev = c->write;
        wev->ready = 1;// 写事件，表示已经accept了 ?
        if (ngx_event_flags & (NGX_USE_AIO_EVENT|NGX_USE_RTSIG_EVENT)) {
            /* rtsig, aio, iocp */
            rev->ready = 1;
        }
        if (ev->deferred_accept) {
//如果采用deferred模式，内核在三次握手建立连接后，不会立即通知程序监听连接可读，而是等待到第一个可读数据包才通知,因此，此时是有可读事件的
            rev->ready = 1;//这回可以读的
        }
        rev->log = log;
        wev->log = log;
        /*
         * TODO: MT: - ngx_atomic_fetch_add()
         *             or protection by critical section or light mutex
         *
         * TODO: MP: - allocated in a shared memory
         *           - ngx_atomic_fetch_add()
         *             or protection by critical section or light mutex
         */
        c->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);
#if (NGX_STAT_STUB)
        (void) ngx_atomic_fetch_add(ngx_stat_handled, 1);
#endif
#if (NGX_THREADS)
        rev->lock = &c->lock;//读写事件锁等于连接上的锁，对于多线程
        wev->lock = &c->lock;
        rev->own_lock = &c->lock;
        wev->own_lock = &c->lock;
#endif
        if (ls->addr_ntop) {
            c->addr_text.data = ngx_pnalloc(c->pool, ls->addr_text_max_len);
            if (c->addr_text.data == NULL) {
                ngx_close_accepted_connection(c);
                return;
            }
            c->addr_text.len = ngx_sock_ntop(c->sockaddr, c->addr_text.data, ls->addr_text_max_len, 0);
            if (c->addr_text.len == 0) {
                ngx_close_accepted_connection(c);
                return;
            }
        }
        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, log, 0, "*%d accept: %V fd:%d", c->number, &c->addr_text, s);
        if (ngx_add_conn && (ngx_event_flags & NGX_USE_EPOLL_EVENT) == 0) {
            if (ngx_add_conn(c) == NGX_ERROR) {//现在加入了，但还没设置回调呢，不过没事，反正单进程，不会有事的。待会就加
//如果使用epoll，我喜欢.ngx_epoll_add_connection 采用边缘触发，注册EPOLLIN|EPOLLOUT|EPOLLET
                ngx_close_accepted_connection(c);
                return;
            }
        }
        log->data = NULL;
        log->handler = NULL;
//注意，这个链接的读写事件回调句柄暂时还没有设置，为什么呢? 因为此处是通用的，
//我只负责接受连接，加入epoll，具体句柄，看具体的类型了，是http还是ftp还是https啥的。具体的就得看这个listen sock是用于什么了，比如http,ftp啥的。
//比如说: 接收一个连接后，应该怎么办呢，应该进行对应的初始化。那怎么初始化? 解析时碰到什么，就怎么初始化吧
        ls->handler(c);//指向ngx_http_init_connection，最开头是在ngx_http_commands -> ngx_http_block设置的
// ngx_http_block 里面调用了 ngx_http_optimize_servers ，这个函数对listening和connection相关的变量进行了初始化和调优，
//并最终在 ngx_http_add_listening （被ngx_http_init_listening调用） 中注册了listening 的 handler 为 ngx_http_init_connection
        if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
            ev->available--;
        }

    } while (ev->available);//一次可以接多个，直到没有可读的了
}

/*
获得accept锁，多个worker仅有一个可以得到这把锁。
获得锁不是阻塞过程，都是立刻返回，获取成功的话ngx_accept_mutex_held被置为1。
拿到锁,那么监听句柄会被放到本进程的epoll中了，否则，则监听句柄会被从epoll中取出。  

*/
ngx_int_t ngx_trylock_accept_mutex(ngx_cycle_t *cycle)
{
    if (ngx_shmtx_trylock(&ngx_accept_mutex)) {//文件锁或者spinlock
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "accept mutex locked");
        if (ngx_accept_mutex_held && ngx_accept_events == 0 && !(ngx_event_flags & NGX_USE_RTSIG_EVENT)) {//注意后面有个非字
            return NGX_OK;
        }
//将监听的SOCK 的读事件加入到epoll，因为我们获得了锁，所以我们可以进行accept了，于是将将accept事件加入epoll
        if (ngx_enable_accept_events(cycle) == NGX_ERROR) {
            ngx_shmtx_unlock(&ngx_accept_mutex);
            return NGX_ERROR;
        }
        ngx_accept_events = 0;
        ngx_accept_mutex_held = 1;//我拿到了，这里可以返回了
        return NGX_OK;
    }
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "accept mutex lock failed: %ui", ngx_accept_mutex_held);
    if (ngx_accept_mutex_held) {
//ngx_accept_mutex_held在外面是不会被改变的，因此这里表示，如果刚才我获得过一次锁了，这回我没有拿到锁，那我得删除epoll注册才行。
//这里主要避免一点，开始的时候是没有加入epoll的，如果第一次没有拿到锁，那么这里就不需要删除，如果连续几次都没有拿到锁，那也不需要重复删除
        if (ngx_disable_accept_events(cycle) == NGX_ERROR) {
            return NGX_ERROR;
        }

        ngx_accept_mutex_held = 0;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_enable_accept_events(ngx_cycle_t *cycle)
{//将cycle->listening的每个可读事件都加入到epoll
    ngx_uint_t         i;
    ngx_listening_t   *ls;
    ngx_connection_t  *c;

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        c = ls[i].connection;
        if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {
            if (ngx_add_conn(c) == NGX_ERROR) {
                return NGX_ERROR;
            }
        } else {//将这个读事件加入进去
            if (ngx_add_event(c->read, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_disable_accept_events(ngx_cycle_t *cycle)
{//删除监听SOCK的读事件，一般在没有获得锁的时候，得先删除这个事件才行，不然越位了
    ngx_uint_t         i;
    ngx_listening_t   *ls;
    ngx_connection_t  *c;

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        c = ls[i].connection;
        if (!c->read->active) {
            continue;
        }
        if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {
            if (ngx_del_conn(c, NGX_DISABLE_EVENT) == NGX_ERROR) {
                return NGX_ERROR;
            }

        } else {
            if (ngx_del_event(c->read, NGX_READ_EVENT, NGX_DISABLE_EVENT)//删除读事件，不过会放一个写事件。这里好像很变扭
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }
    }

    return NGX_OK;
}


static void
ngx_close_accepted_connection(ngx_connection_t *c)
{
    ngx_socket_t  fd;

    ngx_free_connection(c);

    fd = c->fd;
    c->fd = (ngx_socket_t) -1;

    if (ngx_close_socket(fd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_socket_errno,
                      ngx_close_socket_n " failed");
    }

    if (c->pool) {
        ngx_destroy_pool(c->pool);
    }

#if (NGX_STAT_STUB)
    (void) ngx_atomic_fetch_add(ngx_stat_active, -1);
#endif
}


u_char *
ngx_accept_log_error(ngx_log_t *log, u_char *buf, size_t len)
{
    return ngx_snprintf(buf, len, " while accepting new connection on %V",
                        log->data);
}
