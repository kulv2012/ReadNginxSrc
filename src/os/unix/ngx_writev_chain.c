
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#if (IOV_MAX > 64)
#define NGX_IOVS  64
#else
#define NGX_IOVS  IOV_MAX
#endif


ngx_chain_t *
ngx_writev_chain(ngx_connection_t *c, ngx_chain_t *in, off_t limit)
{//µ÷ÓÃwritevÒ»´Î·¢ËÍ¶à¸ö»º³åÇø£¬Èç¹ûÃ»ÓĞ·¢ËÍÍê±Ï£¬Ôò·µ»ØÊ£ÏÂµÄÁ´½Ó½á¹¹Í·²¿¡£
    u_char        *prev;
    ssize_t        n, size, sent;
    off_t          send, prev_send;
    ngx_uint_t     eintr, complete;
    ngx_err_t      err;
    ngx_array_t    vec;
    ngx_chain_t   *cl;
    ngx_event_t   *wev;
    struct iovec  *iov, iovs[NGX_IOVS];
    wev = c->write;//ÄÃµ½Õâ¸öÁ¬½ÓµÄĞ´ÊÂ¼ş½á¹¹
    if (!wev->ready) {//Á¬½Ó»¹Ã»×¼±¸ºÃ£¬·µ»Øµ±Ç°µÄ½Úµã¡£
        return in;
    }
#if (NGX_HAVE_KQUEUE)
    if ((ngx_event_flags & NGX_USE_KQUEUE_EVENT) && wev->pending_eof) {
        (void) ngx_connection_error(c, wev->kq_errno,  "kevent() reported about an closed connection");
        wev->error = 1;
        return NGX_CHAIN_ERROR;
    }
#endif
    /* the maximum limit size is the maximum size_t value - the page size */
    if (limit == 0 || limit > (off_t) (NGX_MAX_SIZE_T_VALUE - ngx_pagesize)) {
        limit = NGX_MAX_SIZE_T_VALUE - ngx_pagesize;//¹»´óÁË£¬×î´óµÄÕûÊı
    }
    send = 0;
    complete = 0;
    vec.elts = iovs;//Êı×é
    vec.size = sizeof(struct iovec);
    vec.nalloc = NGX_IOVS;//ÉêÇëÁËÕâÃ´¶à¡£
    vec.pool = c->pool;

    for ( ;; ) {
        prev = NULL;
        iov = NULL;
        eintr = 0;
        prev_send = send;//Ö®Ç°ÒÑ¾­·¢ËÍÁËÕâÃ´¶à
        vec.nelts = 0;
        /* create the iovec and coalesce the neighbouring bufs */
		//Ñ­»··¢ËÍÊı¾İ£¬Ò»´ÎÒ»¿éIOV_MAXÊıÄ¿µÄ»º³åÇø¡£
        for (cl = in; cl && vec.nelts < IOV_MAX && send < limit; cl = cl->next)
        {
            if (ngx_buf_special(cl->buf)) {
                continue;
            }
#if 1
            if (!ngx_buf_in_memory(cl->buf)) {
                ngx_debug_point();
            }
#endif
            size = cl->buf->last - cl->buf->pos;//¼ÆËãÕâ¸ö½ÚµãµÄ´óĞ¡
            if (send + size > limit) {//³¬¹ı×î´ó·¢ËÍ´óĞ¡¡£½Ø¶Ï
                size = (ssize_t) (limit - send);
            }
            if (prev == cl->buf->pos) {//Èç¹û»¹ÊÇµÈÓÚ¸Õ²ÅµÄÎ»ÖÃ£¬ÄÇ¾Í¸´ÓÃ
                iov->iov_len += size;

            } else {//·ñÔòÒªĞÂÔöÒ»¸ö½Úµã¡£·µ»ØÖ®
                iov = ngx_array_push(&vec);
                if (iov == NULL) {
                    return NGX_CHAIN_ERROR;
                }
                iov->iov_base = (void *) cl->buf->pos;//´ÓÕâÀï¿ªÊ¼
                iov->iov_len = size;//ÓĞÕâÃ´¶àÎÒÒª·¢ËÍ
            }
            prev = cl->buf->pos + size;//¼ÇÂ¼¸Õ²Å·¢µ½ÁËÕâ¸öÎ»ÖÃ£¬ÎªÖ¸Õë¹ş¡£
            send += size;//Ôö¼ÓÒÑ¾­¼ÇÂ¼µÄÊı¾İ³¤¶È¡£
        }

        n = writev(c->fd, vec.elts, vec.nelts);//µ÷ÓÃwritev·¢ËÍÕâĞ©Êı¾İ£¬·µ»Ø·¢ËÍµÄÊı¾İ´óĞ¡
        if (n == -1) {
            err = ngx_errno;
            switch (err) {
            case NGX_EAGAIN:
                break;
            case NGX_EINTR:
                eintr = 1;
                break;
            default:
                wev->error = 1;
                (void) ngx_connection_error(c, err, "writev() failed");
                return NGX_CHAIN_ERROR;
            }
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, c->log, err,  "writev() not ready");
        }
        sent = n > 0 ? n : 0;//¼ÇÂ¼·¢ËÍµÄÊı¾İ´óĞ¡¡£
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0, "writev: %z", sent);
        if (send - prev_send == sent) {//É¶ÒâË¼å?Ö®Ç°Ã»ÓĞ·¢ËÍÈÎºÎÊı¾İÂğ
            complete = 1;
        }
        c->sent += sent;//µİÔöÍ³¼ÆÊı¾İ£¬Õâ¸öÁ´½ÓÉÏ·¢ËÍµÄÊı¾İ´óĞ¡

        for (cl = in; cl; cl = cl->next) {//ÓÖ±éÀúÒ»´ÎÕâ¸öÁ´½Ó£¬ÎªÁËÕÒµ½ÄÇ¿éÖ»³É¹¦·¢ËÍÁËÒ»²¿·ÖÊı¾İµÄÄÚ´æ¿é£¬´ÓËü¼ÌĞø¿ªÊ¼·¢ËÍ¡£
            if (ngx_buf_special(cl->buf)) {
                continue;
            }
            if (sent == 0) {
                break;
            }
            size = cl->buf->last - cl->buf->pos;
            if (sent >= size) {
                sent -= size;//±ê¼ÇºóÃæ»¹ÓĞ¶àÉÙÊı¾İÊÇÎÒ·¢ËÍ¹ıµÄ
                cl->buf->pos = cl->buf->last;//Çå¿ÕÕâ¶ÎÄÚ´æ¡£¼ÌĞøÕÒÏÂÒ»¸ö
                continue;
            }
            cl->buf->pos += sent;//Õâ¿éÄÚ´æÃ»ÓĞÍêÈ«·¢ËÍÍê±Ï£¬±¯¾ç£¬ÏÂ»ØµÃ´ÓÕâÀï¿ªÊ¼¡£

            break;
        }
        if (eintr) {
            continue;
        }
        if (!complete) {
            wev->ready = 0;
            return cl;
        }
        if (send >= limit || cl == NULL) {
            return cl;
        }
        in = cl;//¼ÌĞø¸Õ²ÅÃ»ÓĞ·¢ËÍÍê±ÏµÄÄÚ´æ¡£¼ÌĞø·¢ËÍ
    }
}
