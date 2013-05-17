
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#define NGX_IOVS  16


#if (NGX_HAVE_KQUEUE)

ssize_t
ngx_readv_chain(ngx_connection_t *c, ngx_chain_t *chain)
{
    u_char        *prev;
    ssize_t        n, size;
    ngx_err_t      err;
    ngx_array_t    vec;
    ngx_event_t   *rev;
    struct iovec  *iov, iovs[NGX_IOVS];

    rev = c->read;

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, c->log, 0,
                       "readv: eof:%d, avail:%d, err:%d",
                       rev->pending_eof, rev->available, rev->kq_errno);

        if (rev->available == 0) {
            if (rev->pending_eof) {
                rev->ready = 0;
                rev->eof = 1;

                ngx_log_error(NGX_LOG_INFO, c->log, rev->kq_errno,
                              "kevent() reported about an closed connection");

                if (rev->kq_errno) {
                    rev->error = 1;
                    ngx_set_socket_errno(rev->kq_errno);
                    return NGX_ERROR;
                }

                return 0;

            } else {
                return NGX_AGAIN;
            }
        }
    }

    prev = NULL;
    iov = NULL;
    size = 0;

    vec.elts = iovs;
    vec.nelts = 0;
    vec.size = sizeof(struct iovec);
    vec.nalloc = NGX_IOVS;
    vec.pool = c->pool;

    /* coalesce the neighbouring bufs */

    while (chain) {
        if (prev == chain->buf->last) {
            iov->iov_len += chain->buf->end - chain->buf->last;

        } else {
            iov = ngx_array_push(&vec);
            if (iov == NULL) {
                return NGX_ERROR;
            }

            iov->iov_base = (void *) chain->buf->last;
            iov->iov_len = chain->buf->end - chain->buf->last;
        }

        size += chain->buf->end - chain->buf->last;
        prev = chain->buf->end;
        chain = chain->next;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "readv: %d, last:%d", vec.nelts, iov->iov_len);

    rev = c->read;

    do {
        n = readv(c->fd, (struct iovec *) vec.elts, vec.nelts);

        if (n >= 0) {
            if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
                rev->available -= n;

                /*
                 * rev->available may be negative here because some additional
                 * bytes may be received between kevent() and recv()
                 */

                if (rev->available <= 0) {
                    if (!rev->pending_eof) {
                        rev->ready = 0;
                    }

                    if (rev->available < 0) {
                        rev->available = 0;
                    }
                }

                if (n == 0) {

                    /*
                     * on FreeBSD recv() may return 0 on closed socket
                     * even if kqueue reported about available data
                     */

#if 0
                    ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                                  "readv() returned 0 while kevent() reported "
                                  "%d available bytes", rev->available);
#endif

                    rev->eof = 1;
                    rev->available = 0;
                }

                return n;
            }

            if (n < size) {
                rev->ready = 0;
            }

            if (n == 0) {
                rev->eof = 1;
            }

            return n;
        }

        err = ngx_socket_errno;

        if (err == NGX_EAGAIN || err == NGX_EINTR) {
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err,
                           "readv() not ready");
            n = NGX_AGAIN;

        } else {
            n = ngx_connection_error(c, err, "readv() failed");
            break;
        }

    } while (err == NGX_EINTR);

    rev->ready = 0;

    if (n == NGX_ERROR) {
        c->read->error = 1;
    }

    return n;
}

#else /* ! NGX_HAVE_KQUEUE */

ssize_t ngx_readv_chain(ngx_connection_t *c, ngx_chain_t *chain) {
//这个函数用readv将将连接的数据读取放到chain的链表里面，如果有错标记error或者eof。
//返回读取到的字节数。
    u_char        *prev;
    ssize_t        n, size;
    ngx_err_t      err;
    ngx_array_t    vec;
    ngx_event_t   *rev;
    struct iovec  *iov, iovs[NGX_IOVS];//16个块

    prev = NULL;
    iov = NULL;
    size = 0;

	//创建一个vec的数组
    vec.elts = iovs;
    vec.nelts = 0;
    vec.size = sizeof(struct iovec);
    vec.nalloc = NGX_IOVS;
    vec.pool = c->pool;

    /* coalesce the neighbouring bufs */
    while (chain) {//遍历chain缓冲链表，不断的申请struct iovec结构为待会的readv做准备，碰到临近2块内存如果正好接在一起，就公用之。
        if (prev == chain->buf->last) {
            iov->iov_len += chain->buf->end - chain->buf->last;
        } else {
            iov = ngx_array_push(&vec);
            if (iov == NULL) {
                return NGX_ERROR;
            }
			//指向这块内存起始位置，其实之前可能还有数据，注意这不是内存块的开始，而是数据的末尾。有数据是因为上次没有填满一块内存块的数据。
            iov->iov_base = (void *) chain->buf->last;
            iov->iov_len = chain->buf->end - chain->buf->last;//赋值这块内存的最大大小。
        }
        size += chain->buf->end - chain->buf->last;//统计总大小。
        prev = chain->buf->end;//记着这块内存的最后一个位置，待会看看是不是跟下一块内存接起来了。
        chain = chain->next;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0, "readv: %d:%d", vec.nelts, iov->iov_len);
    rev = c->read;//得到这个SOCK的读事件结构，不断的调用readv进行数据读取。
    do {
		//read系列函数返回0表示对端发送了FIN包
		//If any portion of a regular file prior to the end-of-file has not been written, read() shall return bytes with value 0.
		//如果是没有数据可读了，会返回-1，然后errno为EAGAIN表示暂时没有数据。
        n = readv(c->fd, (struct iovec *) vec.elts, vec.nelts);
        if (n == 0) {
            rev->ready = 0;
            rev->eof = 1;//readv返回0表示对端已经关闭连接，没有数据了。
            return n;
        } else if (n > 0) {
            if (n < size && !(ngx_event_flags & NGX_USE_GREEDY_EVENT)) {
                rev->ready = 0;//看名字应该是贪心的意思，比如我读取了一次后，我想再试试看运气，在网络条件好的时候应该有用。
            }
            return n;
        }
		//readv返回-1，如果不是EAGAIN就有问题。
        err = ngx_socket_errno;
        if (err == NGX_EAGAIN || err == NGX_EINTR) {
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err, "readv() not ready");
            n = NGX_AGAIN;
        } else {
            n = ngx_connection_error(c, err, "readv() failed");
            break;
        }
    } while (err == NGX_EINTR);
    rev->ready = 0;//不可读了。
    if (n == NGX_ERROR) {
        c->read->error = 1;//连接有错误发生。
    }
    return n;
}

#endif /* NGX_HAVE_KQUEUE */
