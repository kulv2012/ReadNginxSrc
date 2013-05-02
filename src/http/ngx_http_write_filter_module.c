
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static ngx_int_t ngx_http_write_filter_init(ngx_conf_t *cf);

//ngx_http_write_filter_module.c是一个特别的module，负责进行实际的发送。这个模块是最初的ngx_http_top_body_filter，
//通常会在next_header_filter和next_body_filter的最后进行调用。

static ngx_http_module_t  ngx_http_write_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_write_filter_init,            /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL,                                  /* merge location configuration */
};


ngx_module_t  ngx_http_write_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_write_filter_module_ctx,     /* module context */
    NULL,                                  /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


ngx_int_t ngx_http_write_filter(ngx_http_request_t *r, ngx_chain_t *in)
{//将r->out里面的数据，和参数里面的数据一并以writev的机制发送给客户端，如果没有发送完所有的，则将剩下的放在r->out
    off_t                      size, sent, nsent, limit;
    ngx_uint_t                 last, flush;
    ngx_msec_t                 delay;
    ngx_chain_t               *cl, *ln, **ll, *chain;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;//得到这个连接的结构
    if (c->error) {
        return NGX_ERROR;
    }
    size = 0;
    flush = 0;
    last = 0;
    ll = &r->out;//计算一下已经放进去的输出链的数据大小。这部分数据是之前没有发送完毕的数据。
    /* find the size, the flush point and the last link of the saved chain */
    for (cl = r->out; cl; cl = cl->next) {
        ll = &cl->next;
        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, c->log, 0, "write old buf t:%d f:%d %p, pos %p, size: %z file: %O, size: %z",
                       cl->buf->temporary, cl->buf->in_file, cl->buf->start, cl->buf->pos, cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos, cl->buf->file_last - cl->buf->file_pos);

        if (ngx_buf_size(cl->buf) == 0 && !ngx_buf_special(cl->buf)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,"zero size buf in writer t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary, cl->buf->recycled, cl->buf->in_file,cl->buf->start, cl->buf->pos,
                          cl->buf->last, cl->buf->file,cl->buf->file_pos, cl->buf->file_last);
            ngx_debug_point();
            return NGX_ERROR;
        }
        size += ngx_buf_size(cl->buf);
        if (cl->buf->flush || cl->buf->recycled) {
            flush = 1;
        }
        if (cl->buf->last_buf) {
            last = 1;
        }
    }
    /* add the new chain to the existent one */
    for (ln = in; ln; ln = ln->next) {//将新的数据，也就是参数的in，放入到链接结构尾部。
        cl = ngx_alloc_chain_link(r->pool);
        if (cl == NULL) {
            return NGX_ERROR;
        }
        cl->buf = ln->buf;
        *ll = cl;
        ll = &cl->next;
        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, c->log, 0,"write new buf t:%d f:%d %p, pos %p, size: %z file: %O, size: %z",
                       cl->buf->temporary, cl->buf->in_file, cl->buf->start, cl->buf->pos,cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,cl->buf->file_last - cl->buf->file_pos);
        if (ngx_buf_size(cl->buf) == 0 && !ngx_buf_special(cl->buf)) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,  "zero size buf in writer t:%d r:%d f:%d %p %p-%p %p %O-%O",
                          cl->buf->temporary, cl->buf->recycled, cl->buf->in_file, cl->buf->start, cl->buf->pos,
                          cl->buf->last, cl->buf->file, cl->buf->file_pos, cl->buf->file_last);
            ngx_debug_point();
            return NGX_ERROR;
        }
        size += ngx_buf_size(cl->buf);//统计总大小。
        if (cl->buf->flush || cl->buf->recycled) {
            flush = 1;
        }
        if (cl->buf->last_buf) {
            last = 1;
        }
    }
    *ll = NULL;//扫尾，标记链表尾部结束
    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, c->log, 0, "http write filter: l:%d f:%d s:%O", last, flush, size);
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    /*
     * avoid the output if there are no last buf, no flush point,
     * there are the incoming bufs and the size of all bufs
     * is smaller than "postpone_output" directive
     */
    if (!last && !flush && in && size < (off_t) clcf->postpone_output) {
        return NGX_OK;
    }
    if (c->write->delayed) {
        c->buffered |= NGX_HTTP_WRITE_BUFFERED;
        return NGX_AGAIN;
    }
    if (size == 0 && !(c->buffered & NGX_LOWLEVEL_BUFFERED)) {
        if (last) {
            r->out = NULL;//啥也么有
            c->buffered &= ~NGX_HTTP_WRITE_BUFFERED;
            return NGX_OK;
        }
        if (flush) {
            do {
                r->out = r->out->next;
            } while (r->out);
            c->buffered &= ~NGX_HTTP_WRITE_BUFFERED;
            return NGX_OK;
        }
        ngx_log_error(NGX_LOG_ALERT, c->log, 0,  "the http output chain is empty");
        ngx_debug_point();
        return NGX_ERROR;
    }
    if (r->limit_rate) {//如果有限速
        limit = r->limit_rate * (ngx_time() - r->start_sec + 1) - (c->sent - clcf->limit_rate_after);
        if (limit <= 0) {
            c->write->delayed = 1;
            ngx_add_timer(c->write, (ngx_msec_t) (- limit * 1000 / r->limit_rate + 1));
            c->buffered |= NGX_HTTP_WRITE_BUFFERED;
            return NGX_AGAIN;
        }
    } else if (clcf->sendfile_max_chunk) {
        limit = clcf->sendfile_max_chunk;
    } else {
        limit = 0;
    }
    sent = c->sent;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http write filter limit %O", limit);
	//调用writev一次发送多个缓冲区，如果没有发送完毕，则返回剩下的链接结构头部。
    chain = c->send_chain(c, r->out, limit);//放入到发送链接结构里面.也就是ngx_writev_chain函数
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http write filter %p", chain);
    if (chain == NGX_CHAIN_ERROR) {
        c->error = 1;
        return NGX_ERROR;
    }
    if (r->limit_rate) {//如果设置了速度限制
        nsent = c->sent;
        if (clcf->limit_rate_after) {
            sent -= clcf->limit_rate_after;
            if (sent < 0) {
                sent = 0;
            }
            nsent -= clcf->limit_rate_after;
            if (nsent < 0) {
                nsent = 0;
            }
        }
        delay = (ngx_msec_t) ((nsent - sent) * 1000 / r->limit_rate + 1);
        if (delay > 0) {
            c->write->delayed = 1;
            ngx_add_timer(c->write, delay);//速度太快了，则设置定时器，然后设置写事件结构。
        }
    } else if (c->write->ready && clcf->sendfile_max_chunk && (size_t) (c->sent - sent) >= clcf->sendfile_max_chunk - 2 * ngx_pagesize) {
        c->write->delayed = 1;//
        ngx_add_timer(c->write, 1);
    }

    for (cl = r->out; cl && cl != chain; /* void */) {//释放前面已经发送出去的缓冲区
        ln = cl;
        cl = cl->next;
        ngx_free_chain(r->pool, ln);
    }
    r->out = chain;//记录还没有发送出去的连接结构。
    if (chain) {
        c->buffered |= NGX_HTTP_WRITE_BUFFERED;
        return NGX_AGAIN;
    }
    c->buffered &= ~NGX_HTTP_WRITE_BUFFERED;
    if ((c->buffered & NGX_LOWLEVEL_BUFFERED) && r->postponed == NULL) {
        return NGX_AGAIN;
    }
    return NGX_OK;
}


static ngx_int_t
ngx_http_write_filter_init(ngx_conf_t *cf)
{//最后一个BODY FILTER，负责数据发送
    ngx_http_top_body_filter = ngx_http_write_filter;

    return NGX_OK;
}
