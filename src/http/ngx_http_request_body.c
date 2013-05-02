
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static void ngx_http_read_client_request_body_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_do_read_client_request_body(ngx_http_request_t *r);
static ngx_int_t ngx_http_write_request_body(ngx_http_request_t *r,
    ngx_chain_t *body);
static ngx_int_t ngx_http_read_discarded_request_body(ngx_http_request_t *r);
static ngx_int_t ngx_http_test_expect(ngx_http_request_t *r);


/*
 * on completion ngx_http_read_client_request_body() adds to
 * r->request_body->bufs one or two bufs:
 *    *) one memory buf that was preread in r->header_in;如果在读取请求头的时候已经读入了一部分数据，则放入这里
 *    *) one memory or file buf that contains the rest of the body 没有预读的数据部分放入这里
 */
ngx_int_t ngx_http_read_client_request_body(ngx_http_request_t *r, ngx_http_client_body_handler_pt post_handler)
{//post_handler = ngx_http_upstream_init。NGINX会等到请求的BODY全部读取完毕后才进行upstream的初始化，GOOD
    size_t                     preread;
    ssize_t                    size;
    ngx_buf_t                 *b;
    ngx_chain_t               *cl, **next;
    ngx_temp_file_t           *tf;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    r->main->count++;
    if (r->request_body || r->discard_body) {//discard_body是否需要丢弃请求内容部分。或者已经有请求体了。则直接回调
        post_handler(r);//不需要请求体，直接调用ngx_http_upstream_init
        return NGX_OK;
    }

    if (ngx_http_test_expect(r) != NGX_OK) {//检查是否需要发送HTTP/1.1 100 Continue
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    rb = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));
    if (rb == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->request_body = rb;//分配请求体结构，下面进行按需填充。
    if (r->headers_in.content_length_n < 0) {
        post_handler(r);//如果不需要读取body部分，长度小于0
        return NGX_OK;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (r->headers_in.content_length_n == 0) {//如果没有设置content_length_n
        if (r->request_body_in_file_only) {//client_body_in_file_only这个指令始终存储一个连接请求实体到一个文件即使它只有0字节。
            tf = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
            if (tf == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            tf->file.fd = NGX_INVALID_FILE;
            tf->file.log = r->connection->log;
            tf->path = clcf->client_body_temp_path;
            tf->pool = r->pool;
            tf->warn = "a client request body is buffered to a temporary file";
            tf->log_level = r->request_body_file_log_level;
            tf->persistent = r->request_body_in_persistent_file;
            tf->clean = r->request_body_in_clean_file;
            if (r->request_body_file_group_access) {
                tf->access = 0660;
            }
            rb->temp_file = tf;//创建一个临时文件用来存储POST过来的body。虽然这个只有0字节，啥东西都没有。
            if (ngx_create_temp_file(&tf->file, tf->path, tf->pool, tf->persistent, tf->clean, tf->access) != NGX_OK) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
        }
		//由于实际的content_length_n长度为0，也就不需要进行读取了。直接到init
        post_handler(r);//一般GET请求直接到这里了
        return NGX_OK;
    }
	//好吧，这回content_length_n大于0 了，也就是个POST请求。这里先记录一下，待会POST数据读取完毕后，需要调用到这个ngx_http_upstream_init
    rb->post_handler = post_handler;

    /*
     * set by ngx_pcalloc():
     *     rb->bufs = NULL;
     *     rb->buf = NULL;
     *     rb->rest = 0;
     */
    preread = r->header_in->last - r->header_in->pos;//使用之前读入的剩余数据，如果之前预读了数据的话。
    if (preread) {//如果之前预读了多余的请求体
        /* there is the pre-read part of the request body */
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http client request body preread %uz", preread);

        b = ngx_calloc_buf(r->pool);//分配ngx_buf_t结构，用于存储预读的数据
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        b->temporary = 1;
        b->start = r->header_in->pos;//直接指向已经预读的数据的开头。这个POS已经在外面就设置好了的。读取请求头，HEADERS后就移位了。
        b->pos = r->header_in->pos;
        b->last = r->header_in->last;
        b->end = r->header_in->end;

        rb->bufs = ngx_alloc_chain_link(r->pool);//申请一个buf链接表。用来存储2个BODY部分
        if (rb->bufs == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        rb->bufs->buf = b;//预读的BODY部分放在这里
        rb->bufs->next = NULL;//其余部分待会读取的时候放在这里
        rb->buf = b;//ngx_http_request_body_t 的buf指向这块新的buf

        if ((off_t) preread >= r->headers_in.content_length_n) {//OK，我已经读了足够的BODY了，可以想到，下面可以直接去init了
            /* the whole request body was pre-read */
            r->header_in->pos += (size_t) r->headers_in.content_length_n;
            r->request_length += r->headers_in.content_length_n;//统计请求的总长度

            if (r->request_body_in_file_only) {//如果需要记录到文件，则写入文件
                if (ngx_http_write_request_body(r, rb->bufs) != NGX_OK) {
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
            }
            post_handler(r);//进行处理
            return NGX_OK;
        }
		//如果预读的数据还不够，还有部分数据没有读入进来。
        /*
         * to not consider the body as pipelined request in
         * ngx_http_set_keepalive()
         */
        r->header_in->pos = r->header_in->last;//后移，待会从这里写入，也就是追加吧
        r->request_length += preread;//统计总长度

        rb->rest = r->headers_in.content_length_n - preread;//计算还有多少数据需要读取，减去刚才预读的部分。
        if (rb->rest <= (off_t) (b->end - b->last)) {//如果还要读取的数据大小足够容纳到现在的预读BUFFER里面，那就干脆放入其中吧。
            /* the whole request body may be placed in r->header_in */
            rb->to_write = rb->bufs;//可以写入第一个位置rb->bufs->buf = b;
            r->read_event_handler = ngx_http_read_client_request_body_handler;//设置为读取客户端的请求体
            return ngx_http_do_read_client_request_body(r);//果断的去开始读取剩余数据了
        }
		//如果预读的BUFFER容不下所有的。那就需要分配一个新的了。
        next = &rb->bufs->next;//设置要读取的数据为第二个buf

    } else {
        b = NULL;//没有预读数据
        rb->rest = r->headers_in.content_length_n;//设置所需读取的数据为所有的。
        next = &rb->bufs;//然后设置要读取的数据所存放的位置为bufs的开头
    }

    size = clcf->client_body_buffer_size;//配置的最大缓冲区大小。
    size += size >> 2;//设置大小为size + 1/4*size,剩余的内容不超过缓冲区大小的1.25倍，一次读完（1.25可能是经验值吧），否则，按缓冲区大小读取。

    if (rb->rest < size) {//如果剩下的比1.25倍最大缓冲区大小要小的话
        size = (ssize_t) rb->rest;//记录所需剩余读入字节数
        if (r->request_body_in_single_buf) {//如果指定只用一个buffer则要加上预读的。
            size += preread;
        }

    } else {//如果1.25倍最大缓冲区大小不足以容纳POST数据，那我们也只读取最大POST数据了。
        size = clcf->client_body_buffer_size;
        /* disable copying buffer for r->request_body_in_single_buf */
        b = NULL;
    }

    rb->buf = ngx_create_temp_buf(r->pool, size);//分配这么多临时内存
    if (rb->buf == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    cl = ngx_alloc_chain_link(r->pool);//分配一个链接表
    if (cl == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    cl->buf = rb->buf;//记录一下刚才申请的内存，待会数据就存放在这了。
    cl->next = NULL;//没有下一个了。

    if (b && r->request_body_in_single_buf) {//如果指定只用一个buffer则要加上预读的,那就需要把之前的数据拷贝过来
        size = b->last - b->pos;
        ngx_memcpy(rb->buf->pos, b->pos, size);
        rb->buf->last += size;
        next = &rb->bufs;//待会链接在头部。
    }

    *next = cl;//GOOD，链接起来。如果有预读数据，且可以放多个buffer,就链接在第二个位置，否则链接在第一个位置。
    if (r->request_body_in_file_only || r->request_body_in_single_buf) {
        rb->to_write = rb->bufs;//设置一下待会需要写入的位置。如果一个buffer，就头部

    } else {//否则如果已经设置了第二个位置，也就是有预读数据且有2份BUFFER，那就存在第二个里面，否则头部。
        rb->to_write = rb->bufs->next ? rb->bufs->next : rb->bufs;
    }
    r->read_event_handler = ngx_http_read_client_request_body_handler;//设置为读取客户端的请求体读取函数，其实就等于下面的，只是进行了超时判断

    return ngx_http_do_read_client_request_body(r);//果断的去开始读取剩余数据了
}


static void
ngx_http_read_client_request_body_handler(ngx_http_request_t *r)
{
    ngx_int_t  rc;
    if (r->connection->read->timedout) {//老规矩，超时判断。
        r->connection->timedout = 1;
        ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }
    rc = ngx_http_do_read_client_request_body(r);//真正去读取数据了
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        ngx_http_finalize_request(r, rc);
    }
}


static ngx_int_t
ngx_http_do_read_client_request_body(ngx_http_request_t *r)
{//开始读取剩余的POST数据，存放在r->request_body里面，如果读完了，回调post_handler，其实就是ngx_http_upstream_init。
    size_t                     size;
    ssize_t                    n;
    ngx_buf_t                 *b;
    ngx_connection_t          *c;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;//拿到这个连接的ngx_connection_t结构
    rb = r->request_body;//拿到数据存放位置
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,"http read client request body");
    for ( ;; ) {
        for ( ;; ) {
            if (rb->buf->last == rb->buf->end) {//如果数据缓冲区不够了。
                if (ngx_http_write_request_body(r, rb->to_write) != NGX_OK) {//不行了，地方不够，果断写到文件里面去吧。
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
                rb->to_write = rb->bufs->next ? rb->bufs->next : rb->bufs;//如果还有第二个缓冲区，则写入第二个，否则写第一个。
                rb->buf->last = rb->buf->start;
            }
            size = rb->buf->end - rb->buf->last;//计算这个缓冲区的剩余大小
            if ((off_t) size > rb->rest) {//擦，够了，size就等于我要读取的大小。否则的话就读剩余的容量大小。
                size = (size_t) rb->rest;
            }
            n = c->recv(c, rb->buf->last, size);//使劲读数据。等于ngx_unix_recv，就光读数据，不改变epoll属性。
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http client request body recv %z", n);

            if (n == NGX_AGAIN) {//没有了
                break;
            }
            if (n == 0) {//返回0表示客户端关闭连接了
                ngx_log_error(NGX_LOG_INFO, c->log, 0,"client closed prematurely connection");
            }
            if (n == 0 || n == NGX_ERROR) {
                c->error = 1;//标记为错误。这样外部会关闭连接的。
                return NGX_HTTP_BAD_REQUEST;
            }
            rb->buf->last += n;//读了n字节。
            rb->rest -= n;//还有这么多字节要读取
            r->request_length += n;//统计总传输字节数
            if (rb->rest == 0) {
                break;
            }

            if (rb->buf->last < rb->buf->end) {
                break;//这个肯定了吧。
            }
        }

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http client request body rest %O", rb->rest);
        if (rb->rest == 0) {
            break;//不剩下了，全读完了。
        }

        if (!c->read->ready) {//如果这个连接在ngx_unix_recv里面标记为没有足够数据可以读取了，那我们就需要加入epool可读监听事件里面去。
            clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
            ngx_add_timer(c->read, clcf->client_body_timeout);//设个超时吧。
//将连接设置到可读事件监控中，有可读事件就会调用ngx_http_request_handler->r->read_event_handler = ngx_http_read_client_request_body_handler; 
            if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            return NGX_AGAIN;//返回
        }
    }
//如果全部读完了，那就会到这里来。
    if (c->read->timer_set) {
        ngx_del_timer(c->read);
    }
    if (rb->temp_file || r->request_body_in_file_only) {//改写文件的写文件
        /* save the last part */
        if (ngx_http_write_request_body(r, rb->to_write) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        b->in_file = 1;
        b->file_pos = 0;
        b->file_last = rb->temp_file->file.offset;
        b->file = &rb->temp_file->file;
        if (rb->bufs->next) {//有第二个就放第二个
            rb->bufs->next->buf = b;

        } else {
            rb->bufs->buf = b;//否则放第一个缓冲区
        }
    }

    if (r->request_body_in_file_only && rb->bufs->next) {//如果POST数据必须存放在文件中，并且有2个缓冲区，下面是啥意思?
        rb->bufs = rb->bufs->next;//指向第二个缓冲区，其实就是上面的文件。也就是bufs指向文件
    }

    rb->post_handler(r);

    return NGX_OK;
}


static ngx_int_t
ngx_http_write_request_body(ngx_http_request_t *r, ngx_chain_t *body)
{
    ssize_t                    n;
    ngx_temp_file_t           *tf;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    rb = r->request_body;

    if (rb->temp_file == NULL) {
        tf = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
        if (tf == NULL) {
            return NGX_ERROR;
        }

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        tf->file.fd = NGX_INVALID_FILE;
        tf->file.log = r->connection->log;
        tf->path = clcf->client_body_temp_path;
        tf->pool = r->pool;
        tf->warn = "a client request body is buffered to a temporary file";
        tf->log_level = r->request_body_file_log_level;
        tf->persistent = r->request_body_in_persistent_file;
        tf->clean = r->request_body_in_clean_file;

        if (r->request_body_file_group_access) {
            tf->access = 0660;
        }

        rb->temp_file = tf;
    }

    n = ngx_write_chain_to_temp_file(rb->temp_file, body);
    /* TODO: n == 0 or not complete and level event */
    if (n == NGX_ERROR) {
        return NGX_ERROR;
    }

    rb->temp_file->offset += n;

    return NGX_OK;
}


ngx_int_t
ngx_http_discard_request_body(ngx_http_request_t *r)
{
    ssize_t       size;
    ngx_event_t  *rev;

    if (r != r->main || r->discard_body) {
        return NGX_OK;
    }

    if (ngx_http_test_expect(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rev = r->connection->read;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0, "http set discard body");

    if (rev->timer_set) {
        ngx_del_timer(rev);
    }

    if (r->headers_in.content_length_n <= 0 || r->request_body) {
        return NGX_OK;
    }

    size = r->header_in->last - r->header_in->pos;

    if (size) {
        if (r->headers_in.content_length_n > size) {
            r->header_in->pos += size;
            r->headers_in.content_length_n -= size;

        } else {
            r->header_in->pos += (size_t) r->headers_in.content_length_n;
            r->headers_in.content_length_n = 0;
            return NGX_OK;
        }
    }

    r->read_event_handler = ngx_http_discarded_request_body_handler;

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (ngx_http_read_discarded_request_body(r) == NGX_OK) {
        r->lingering_close = 0;

    } else {
        r->count++;
        r->discard_body = 1;
    }

    return NGX_OK;
}


void
ngx_http_discarded_request_body_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_msec_t                 timer;
    ngx_event_t               *rev;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
    rev = c->read;

    if (rev->timedout) {
        c->timedout = 1;
        c->error = 1;
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (r->lingering_time) {
        timer = (ngx_msec_t) (r->lingering_time - ngx_time());

        if (timer <= 0) {
            r->discard_body = 0;
            r->lingering_close = 0;
            ngx_http_finalize_request(r, NGX_ERROR);
            return;
        }

    } else {
        timer = 0;
    }

    rc = ngx_http_read_discarded_request_body(r);

    if (rc == NGX_OK) {
        r->discard_body = 0;
        r->lingering_close = 0;
        ngx_http_finalize_request(r, NGX_DONE);
        return;
    }

    /* rc == NGX_AGAIN */

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        c->error = 1;
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (timer) {

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        timer *= 1000;

        if (timer > clcf->lingering_timeout) {
            timer = clcf->lingering_timeout;
        }

        ngx_add_timer(rev, timer);
    }
}


static ngx_int_t
ngx_http_read_discarded_request_body(ngx_http_request_t *r)
{
    size_t   size;
    ssize_t  n;
    u_char   buffer[NGX_HTTP_DISCARD_BUFFER_SIZE];

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http read discarded body");

    for ( ;; ) {
        if (r->headers_in.content_length_n == 0) {
            r->read_event_handler = ngx_http_block_reading;
            return NGX_OK;
        }

        if (!r->connection->read->ready) {
            return NGX_AGAIN;
        }

        size = (r->headers_in.content_length_n > NGX_HTTP_DISCARD_BUFFER_SIZE) ?
                   NGX_HTTP_DISCARD_BUFFER_SIZE:
                   (size_t) r->headers_in.content_length_n;

        n = r->connection->recv(r->connection, buffer, size);

        if (n == NGX_ERROR) {
            r->connection->error = 1;
            return NGX_OK;
        }

        if (n == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        if (n == 0) {
            return NGX_OK;
        }

        r->headers_in.content_length_n -= n;
    }
}


static ngx_int_t
ngx_http_test_expect(ngx_http_request_t *r)
{//如果需要进行100-continue的反馈，则调用ngx_unix_send发送反馈回去
    ngx_int_t   n;
    ngx_str_t  *expect;

    if (r->expect_tested
        || r->headers_in.expect == NULL
        || r->http_version < NGX_HTTP_VERSION_11)
    {
        return NGX_OK;
    }
    r->expect_tested = 1;
    expect = &r->headers_in.expect->value;
    if (expect->len != sizeof("100-continue") - 1 || ngx_strncasecmp(expect->data, (u_char *) "100-continue", sizeof("100-continue") - 1) != 0)
    {
        return NGX_OK;
    }
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "send 100 Continue");
    n = r->connection->send(r->connection, (u_char *) "HTTP/1.1 100 Continue" CRLF CRLF, sizeof("HTTP/1.1 100 Continue" CRLF CRLF) - 1);

    if (n == sizeof("HTTP/1.1 100 Continue" CRLF CRLF) - 1) {
        return NGX_OK;
    }
    /* we assume that such small packet should be send successfully */
    return NGX_ERROR;
}
