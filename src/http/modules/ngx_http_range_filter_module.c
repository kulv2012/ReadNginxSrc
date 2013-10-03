
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * the single part format:
 *
 * "HTTP/1.0 206 Partial Content" CRLF
 * ... header ...
 * "Content-Type: image/jpeg" CRLF
 * "Content-Length: SIZE" CRLF
 * "Content-Range: bytes START-END/SIZE" CRLF
 * CRLF
 * ... data ...
 *
 *
 * the mutlipart format:
 *
 * "HTTP/1.0 206 Partial Content" CRLF
 * ... header ...
 * "Content-Type: multipart/byteranges; boundary=0123456789" CRLF
 * CRLF
 * CRLF
 * "--0123456789" CRLF
 * "Content-Type: image/jpeg" CRLF
 * "Content-Range: bytes START0-END0/SIZE" CRLF
 * CRLF
 * ... data ...
 * CRLF
 * "--0123456789" CRLF
 * "Content-Type: image/jpeg" CRLF
 * "Content-Range: bytes START1-END1/SIZE" CRLF
 * CRLF
 * ... data ...
 * CRLF
 * "--0123456789--" CRLF
 */


typedef struct {
    off_t        start;
    off_t        end;
    ngx_str_t    content_range;
} ngx_http_range_t;


typedef struct {
    off_t        offset;
    ngx_str_t    boundary_header;
    ngx_array_t  ranges;//客户端要求的ranges，ngx_http_range_parse函数填充，一个range一个数组成员
} ngx_http_range_filter_ctx_t;


ngx_int_t ngx_http_range_parse(ngx_http_request_t *r,
    ngx_http_range_filter_ctx_t *ctx);
static ngx_int_t ngx_http_range_singlepart_header(ngx_http_request_t *r,
    ngx_http_range_filter_ctx_t *ctx);
static ngx_int_t ngx_http_range_multipart_header(ngx_http_request_t *r,
    ngx_http_range_filter_ctx_t *ctx);
static ngx_int_t ngx_http_range_not_satisfiable(ngx_http_request_t *r);
static ngx_int_t ngx_http_range_test_overlapped(ngx_http_request_t *r,
    ngx_http_range_filter_ctx_t *ctx, ngx_chain_t *in);
static ngx_int_t ngx_http_range_singlepart_body(ngx_http_request_t *r,
    ngx_http_range_filter_ctx_t *ctx, ngx_chain_t *in);
static ngx_int_t ngx_http_range_multipart_body(ngx_http_request_t *r,
    ngx_http_range_filter_ctx_t *ctx, ngx_chain_t *in);

static ngx_int_t ngx_http_range_header_filter_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_range_body_filter_init(ngx_conf_t *cf);


static ngx_http_module_t  ngx_http_range_header_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_range_header_filter_init,     /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL,                                  /* merge location configuration */
};


ngx_module_t  ngx_http_range_header_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_range_header_filter_module_ctx, /* module context */
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


static ngx_http_module_t  ngx_http_range_body_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_range_body_filter_init,       /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL,                                  /* merge location configuration */
};


ngx_module_t  ngx_http_range_body_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_range_body_filter_module_ctx, /* module context */
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

//静态局部变量，用来保存下一个过滤器的函数指针。nginx通常做法，用这种方式来建立一调filter链
static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;


static ngx_int_t
ngx_http_range_header_filter(ngx_http_request_t *r)
{//ngx_http_top_header_filter链的函数指针，用来过滤发送给客户端的头部数据。
//ngx_http_send_header函数会调用这里。
    time_t                        if_range;
    ngx_int_t                     rc;
    ngx_http_range_filter_ctx_t  *ctx;

    if (r->http_version < NGX_HTTP_VERSION_10
        || r->headers_out.status != NGX_HTTP_OK
        || r != r->main
        || r->headers_out.content_length_n == -1
        || !r->allow_ranges)
    {//不处理非200的请求，否则直接调用下一个。
        return ngx_http_next_header_filter(r);
    }

    if (r->headers_in.range == NULL
        || r->headers_in.range->value.len < 7
        || ngx_strncasecmp(r->headers_in.range->value.data,
                           (u_char *) "bytes=", 6)
           != 0)
    {//如果客户端发送过来的头部数据中没有range字段，则我们也不需要处理range。直接返回即可
        goto next_filter;
    }

    if (r->headers_in.if_range && r->headers_out.last_modified_time != -1) {
		//语法为If-Range = "If-Range" ":" ( entity-tag | HTTP-date )  后面带的是时间，也就如果修改时间等于XX的话，就给我我没有的，否则给我所有的。
        if_range = ngx_http_parse_time(r->headers_in.if_range->value.data, r->headers_in.if_range->value.len);
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http ir:%d lm:%d", if_range, r->headers_out.last_modified_time);
//If-Range的意思是：“如果entity没有发生变化，那么把我缺失的部分发送给我。
//如果entity发生了变化，那么把整个entity发送给我”。
        if (if_range != r->headers_out.last_modified_time) {
            goto next_filter;//时间跟服务器上的时间不相等，需要返回所有的。所以略过
        }
    }
//到这里说明需要老老实实发送range部分了。先申请数据结构
    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_range_filter_ctx_t));
    if (ctx == NULL) {
        return NGX_ERROR;
    }
    if (ngx_array_init(&ctx->ranges, r->pool, 1, sizeof(ngx_http_range_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }
//解析range的语法，在本文件开头有介绍。解析出开始，结束等。结果range放入ctx->ranges里面
    rc = ngx_http_range_parse(r, ctx);

    if (rc == NGX_OK) {
        ngx_http_set_ctx(r, ctx, ngx_http_range_body_filter_module);
		//修改发送的头部字段为206
        r->headers_out.status = NGX_HTTP_PARTIAL_CONTENT;
        r->headers_out.status_line.len = 0;

        if (ctx->ranges.nelts == 1) {//单个RANGE
            return ngx_http_range_singlepart_header(r, ctx);
        }
		//多个range，比如"Range: bytes=500-600,601-999"
        return ngx_http_range_multipart_header(r, ctx);
    }

    if (rc == NGX_HTTP_RANGE_NOT_SATISFIABLE) {
        return ngx_http_range_not_satisfiable(r);
    }

    /* rc == NGX_ERROR */

    return rc;

next_filter:
//顺便加上一个"Accept-Ranges: bytes",比如curl -I http://chenzhenianqing.cn/wp-content/uploads/2013/07/kulvRss.jpg
    r->headers_out.accept_ranges = ngx_list_push(&r->headers_out.headers);
    if (r->headers_out.accept_ranges == NULL) {
        return NGX_ERROR;
    }
    r->headers_out.accept_ranges->hash = 1;
    ngx_str_set(&r->headers_out.accept_ranges->key, "Accept-Ranges");
    ngx_str_set(&r->headers_out.accept_ranges->value, "bytes");

    return ngx_http_next_header_filter(r);
}


ngx_int_t
ngx_http_range_parse(ngx_http_request_t *r, ngx_http_range_filter_ctx_t *ctx)
{//解析客户端发送过来的Range: bytes=后面的部分，也就是字节开始结束字符串。结果放入ctx->ranges数组内。
    u_char            *p;
    off_t              start, end;
    ngx_uint_t         suffix;
    ngx_http_range_t  *range;

    p = r->headers_in.range->value.data + 6;//跳过"bytes="前缀

    for ( ;; ) {//客户端发送过来的格式为Range: bytes=500-600,601-999，一次一个range的来。
        start = 0;
        end = 0;
        suffix = 0;

        while (*p == ' ') { p++; }//去空格

        if (*p != '-') {
            if (*p < '0' || *p > '9') {
                return NGX_HTTP_RANGE_NOT_SATISFIABLE;
            }

            while (*p >= '0' && *p <= '9') {
                start = start * 10 + *p++ - '0';
            }

            while (*p == ' ') { p++; }

            if (*p++ != '-') {
                return NGX_HTTP_RANGE_NOT_SATISFIABLE;
            }

            if (start >= r->headers_out.content_length_n) {
                return NGX_HTTP_RANGE_NOT_SATISFIABLE;
            }

            while (*p == ' ') { p++; }

            if (*p == ',' || *p == '\0') {//后面还有一个range ,或者说这个range结束了。但是没有收到end，那就是到最后了
                range = ngx_array_push(&ctx->ranges);//正式增加一个range
                if (range == NULL) {
                    return NGX_ERROR;
                }
                range->start = start;
                range->end = r->headers_out.content_length_n;//到最后
                if (*p++ != ',') {//没有了，结束
                    return NGX_OK;
                }
                continue;
            }
        } else {
            suffix = 1;//没有开始，只有-end格式
            p++;
        }

        if (*p < '0' || *p > '9') {
            return NGX_HTTP_RANGE_NOT_SATISFIABLE;
        }
		//接下来是结束部分了。
        while (*p >= '0' && *p <= '9') {
            end = end * 10 + *p++ - '0';
        }

        while (*p == ' ') { p++; }

        if (*p != ',' && *p != '\0') {
            return NGX_HTTP_RANGE_NOT_SATISFIABLE;
        }
        if (suffix) {//-end的意思是，我要最后end个字节。
           start = r->headers_out.content_length_n - end;
           end = r->headers_out.content_length_n - 1;
        }
        if (start > end) {
            return NGX_HTTP_RANGE_NOT_SATISFIABLE;
        }
        range = ngx_array_push(&ctx->ranges);//增加一个正式的。
        if (range == NULL) {
            return NGX_ERROR;
        }

        range->start = start;
        if (end >= r->headers_out.content_length_n) {
            /*
             * Download Accelerator sends the last byte position
             * that equals to the file length
             */
            range->end = r->headers_out.content_length_n;

        } else {
            range->end = end + 1;
        }
        if (*p++ != ',') {
            return NGX_OK;
        }
    }
}


static ngx_int_t
ngx_http_range_singlepart_header(ngx_http_request_t *r, ngx_http_range_filter_ctx_t *ctx)
{//客户端只请求了一个range,那么我们返回的数据格式为: Content-Range: bytes START-END/SIZE" CRLF
    ngx_table_elt_t   *content_range;
    ngx_http_range_t  *range;

    content_range = ngx_list_push(&r->headers_out.headers);//在输出头里面申请一个header line
    if (content_range == NULL) {
        return NGX_ERROR;
    }
    r->headers_out.content_range = content_range;
    content_range->hash = 1;
    ngx_str_set(&content_range->key, "Content-Range");

    content_range->value.data = ngx_pnalloc(r->pool, sizeof("bytes -/") - 1 + 3 * NGX_OFF_T_LEN);//申请三个足够长度的数字的字符串
    if (content_range->value.data == NULL) {
        return NGX_ERROR;
    }
    /* "Content-Range: bytes SSSS-EEEE/TTTT" header */
    range = ctx->ranges.elts;
//设置START-END/SIZE格式。
    content_range->value.len = ngx_sprintf(content_range->value.data,
                                           "bytes %O-%O/%O",
                                           range->start, range->end - 1, r->headers_out.content_length_n) - content_range->value.data;
    r->headers_out.content_length_n = range->end - range->start;//本次返回的数据长度。总长度在SIZE上面
    if (r->headers_out.content_length) {
        r->headers_out.content_length->hash = 0;
        r->headers_out.content_length = NULL;
    }
    return ngx_http_next_header_filter(r);
}


static ngx_int_t
ngx_http_range_multipart_header(ngx_http_request_t *r,
    ngx_http_range_filter_ctx_t *ctx)
{
    size_t              len;
    ngx_uint_t          i;
    ngx_http_range_t   *range;
    ngx_atomic_uint_t   boundary;

    len = sizeof(CRLF "--") - 1 + NGX_ATOMIC_T_LEN
          + sizeof(CRLF "Content-Type: ") - 1
          + r->headers_out.content_type.len
          + sizeof(CRLF "Content-Range: bytes ") - 1;

    if (r->headers_out.charset.len) {
        len += sizeof("; charset=") - 1 + r->headers_out.charset.len;
    }

    ctx->boundary_header.data = ngx_pnalloc(r->pool, len);
    if (ctx->boundary_header.data == NULL) {
        return NGX_ERROR;
    }
    boundary = ngx_next_temp_number(0);
    /*
     * The boundary header of the range:
     * CRLF
     * "--0123456789" CRLF
     * "Content-Type: image/jpeg" CRLF
     * "Content-Range: bytes "
     */
//拼接bondery头，也就是--xxxxx以及后面的那几行头部数据。头部数据之后就是bonder的数据部分了。
    if (r->headers_out.charset.len) {
        ctx->boundary_header.len = ngx_sprintf(ctx->boundary_header.data,
                                           CRLF "--%0muA" CRLF
                                           "Content-Type: %V; charset=%V" CRLF
                                           "Content-Range: bytes ",
                                           boundary,
                                           &r->headers_out.content_type,
                                           &r->headers_out.charset)
                                   - ctx->boundary_header.data;

        r->headers_out.charset.len = 0;

    } else if (r->headers_out.content_type.len) {
        ctx->boundary_header.len = ngx_sprintf(ctx->boundary_header.data,
                                           CRLF "--%0muA" CRLF
                                           "Content-Type: %V" CRLF
                                           "Content-Range: bytes ",
                                           boundary,
                                           &r->headers_out.content_type)
                                   - ctx->boundary_header.data;

    } else {
        ctx->boundary_header.len = ngx_sprintf(ctx->boundary_header.data,
                                           CRLF "--%0muA" CRLF
                                           "Content-Range: bytes ",
                                           boundary)
                                   - ctx->boundary_header.data;
    }
//这是最开始那一行声明下面是mutipart 格式数据的行:"Content-Type: multipart/byteranges; boundary=0123456789" CRLF
//下面的sizeof其实没必要用全部字符串，Content-Type:不需要。
    r->headers_out.content_type.data =
        ngx_pnalloc(r->pool, sizeof("Content-Type: multipart/byteranges; boundary=") - 1 + NGX_ATOMIC_T_LEN);

    if (r->headers_out.content_type.data == NULL) {
        return NGX_ERROR;
    }
    r->headers_out.content_type_lowcase = NULL;
    /* "Content-Type: multipart/byteranges; boundary=0123456789" */
    r->headers_out.content_type.len =
                           ngx_sprintf(r->headers_out.content_type.data,
                                       "multipart/byteranges; boundary=%0muA", boundary)
                           - r->headers_out.content_type.data;

    r->headers_out.content_type_len = r->headers_out.content_type.len;

    /* the size of the last boundary CRLF "--0123456789--" CRLF */
	//len用来统计数据部分的长度。
    len = sizeof(CRLF "--") - 1 + NGX_ATOMIC_T_LEN + sizeof("--" CRLF) - 1;
	
    range = ctx->ranges.elts;
    for (i = 0; i < ctx->ranges.nelts; i++) {
//一个个range的将其放入发送缓冲区中。
        /* the size of the range: "SSSS-EEEE/TTTT" CRLF CRLF */

        range[i].content_range.data = ngx_pnalloc(r->pool, 3 * NGX_OFF_T_LEN + 2 + 4);

        if (range[i].content_range.data == NULL) {
            return NGX_ERROR;
        }

        range[i].content_range.len = ngx_sprintf(range[i].content_range.data,
                                               "%O-%O/%O" CRLF CRLF,
                                               range[i].start, range[i].end - 1,
                                               r->headers_out.content_length_n)
                                     - range[i].content_range.data;
//递增长度。
        len += ctx->boundary_header.len + range[i].content_range.len
                                    + (size_t) (range[i].end - range[i].start);
    }

    r->headers_out.content_length_n = len;//本次返回的数据部分长度。

    if (r->headers_out.content_length) {
        r->headers_out.content_length->hash = 0;
        r->headers_out.content_length = NULL;
    }
    return ngx_http_next_header_filter(r);
}


static ngx_int_t
ngx_http_range_not_satisfiable(ngx_http_request_t *r)
{
    ngx_table_elt_t  *content_range;

    r->headers_out.status = NGX_HTTP_RANGE_NOT_SATISFIABLE;

    content_range = ngx_list_push(&r->headers_out.headers);
    if (content_range == NULL) {
        return NGX_ERROR;
    }

    r->headers_out.content_range = content_range;

    content_range->hash = 1;
    ngx_str_set(&content_range->key, "Content-Range");

    content_range->value.data = ngx_pnalloc(r->pool,
                                       sizeof("bytes */") - 1 + NGX_OFF_T_LEN);
    if (content_range->value.data == NULL) {
        return NGX_ERROR;
    }

    content_range->value.len = ngx_sprintf(content_range->value.data,
                                           "bytes */%O",
                                           r->headers_out.content_length_n)
                               - content_range->value.data;

    ngx_http_clear_content_length(r);

    return NGX_HTTP_RANGE_NOT_SATISFIABLE;
}


static ngx_int_t
ngx_http_range_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{//这是第一个BODY过滤函数
    ngx_http_range_filter_ctx_t  *ctx;

    if (in == NULL) {
        return ngx_http_next_body_filter(r, in);
    }
    ctx = ngx_http_get_module_ctx(r, ngx_http_range_body_filter_module);
    if (ctx == NULL) {
        return ngx_http_next_body_filter(r, in);
    }
    if (ctx->ranges.nelts == 1) {//如果只有一个range
        return ngx_http_range_singlepart_body(r, ctx, in);
    }
    /*下面说明，NGINX只支持整个数据都在一个buffer里面的情况也就是多个body它不好处理
     * multipart ranges are supported only if whole body is in a single buffer
     */
    if (ngx_buf_special(in->buf)) {
        return ngx_http_next_body_filter(r, in);
    }
	//检查是否所有的RANGE都在第一块buf里面。那么在后面的buf里面不行么，不行，nginx不支持
    if (ngx_http_range_test_overlapped(r, ctx, in) != NGX_OK) {
        return NGX_ERROR;
    }

    return ngx_http_range_multipart_body(r, ctx, in);
}


static ngx_int_t
ngx_http_range_test_overlapped(ngx_http_request_t *r,
    ngx_http_range_filter_ctx_t *ctx, ngx_chain_t *in)
{//这个函数判断in参数是否是第一块buf，并且所有的客户端请求的ranges是否全部在第一块
    off_t              start, last;
    ngx_buf_t         *buf;
    ngx_uint_t         i;
    ngx_http_range_t  *range;

    if (ctx->offset) {//如果之前发送过一些数据，那么也就是客户端请求的数据跨越了多块 buf，不支持
        goto overlapped;
    }
    buf = in->buf;
    if (!buf->last_buf) {//只对前面或者中介的buff进行
        if (buf->in_file) {//得到这坨数据的起始，结束位置。
            start = buf->file_pos + ctx->offset;
            last = buf->file_last + ctx->offset;
        } else {
            start = buf->pos - buf->start + ctx->offset;
            last = buf->last - buf->start + ctx->offset;
        }
        range = ctx->ranges.elts;
		//看看客户端请求的数据是否都在第一块buf里面
        for (i = 0; i < ctx->ranges.nelts; i++) {
            if (start > range[i].start || last < range[i].end) {
		//如果客户端要求的开始位置在buf前面，或者结束位置在buf后面，则超出了本buffer的范围
                 goto overlapped;//跳过，客户端要求的buf超过位置了
            }
        }
    }
    ctx->offset = ngx_buf_size(buf);
    return NGX_OK;
overlapped:
     ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "range in overlapped buffers");
    return NGX_ERROR;
}


static ngx_int_t ngx_http_range_singlepart_body(ngx_http_request_t *r, ngx_http_range_filter_ctx_t *ctx, ngx_chain_t *in)
{//处理客户端只发送一个range过来的情况
    off_t              start, last;
    ngx_buf_t         *buf;
    ngx_chain_t       *out, *cl, **ll;
    ngx_http_range_t  *range;

    out = NULL;
    ll = &out;
    range = ctx->ranges.elts;
//对in参数的要发送出去的缓冲数据链表，一一检查其内容是否在range的start-end之间，
//不在的就丢弃，只剪裁出区间之内的，发送给客户端。
    for (cl = in; cl; cl = cl->next) {

        buf = cl->buf;

        start = ctx->offset;//在所有数据中的位置。
        last = ctx->offset + ngx_buf_size(buf);

        ctx->offset = last;
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http range body buf: %O-%O", start, last);

        if (ngx_buf_special(buf)) {
            *ll = cl;//将out指向这个节点，从而循环建立链表。
            ll = &cl->next;
            continue;
        }

        if (range->end <= start || range->start >= last) {//丢弃这个。将其pos指向last从而略过。
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http range body skip");
            if (buf->in_file) {
                buf->file_pos = buf->file_last;
            }
            buf->pos = buf->last;
            buf->sync = 1;
            continue;
        }
        if (range->start > start) {//开头重合，去掉重合部分
            if (buf->in_file) {
                buf->file_pos += range->start - start;//向后移动开始部分
            }
            if (ngx_buf_in_memory(buf)) {//如果在内存的话就移动指针
                buf->pos += (size_t) (range->start - start);
            }
        }

        if (range->end <= last) {//尾部重合，去掉尾部
            if (buf->in_file) {
                buf->file_last -= last - range->end;
            }
            if (ngx_buf_in_memory(buf)) {
                buf->last -= (size_t) (last - range->end);
            }
            buf->last_buf = 1;//标记为最后一块内存，后面的都不需要了。
            *ll = cl;
            cl->next = NULL;//直接剪断，后面可能还有输出链的，但是没事，等这个连接关闭后，数据都回收了的。
            break;
        }
        *ll = cl;//后移动。
        ll = &cl->next;
    }
	//到这里后，out变量所指向的链表里面的数据都是range之间的，需要发送给客户端的数据。于是调用下一个filter进行发送。
    if (out == NULL) {
        return NGX_OK;
    }
    return ngx_http_next_body_filter(r, out);
}


static ngx_int_t
ngx_http_range_multipart_body(ngx_http_request_t *r,
    ngx_http_range_filter_ctx_t *ctx, ngx_chain_t *in)
{
    off_t              body_start;
    ngx_buf_t         *b, *buf;
    ngx_uint_t         i;
    ngx_chain_t       *out, *hcl, *rcl, *dcl, **ll;
    ngx_http_range_t  *range;

    ll = &out;
    buf = in->buf;
    range = ctx->ranges.elts;

#if (NGX_HTTP_CACHE)
    body_start = r->cached ? r->cache->body_start : 0;
#else
    body_start = 0;
#endif
//循环遍历range数组，将in里面的数据一块块放到out链表后面，然后发送out链表的数据给客户端(调用下一个filter)。
    for (i = 0; i < ctx->ranges.nelts; i++) {
        /*
         * The boundary header of the range:
         * CRLF
         * "--0123456789" CRLF
         * "Content-Type: image/jpeg" CRLF
         * "Content-Range: bytes "
         */

        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return NGX_ERROR;
        }
		//预先分配一个boundary头部，也就是上面的每一块都需要的头部。注意不是申明mutipart的那个头部。
        b->memory = 1;
        b->pos = ctx->boundary_header.data;
        b->last = ctx->boundary_header.data + ctx->boundary_header.len;
        hcl = ngx_alloc_chain_link(r->pool);
        if (hcl == NULL) {
            return NGX_ERROR;
        }
        hcl->buf = b;
		
		//拼接下面的区块数字字符串，放在boundary_header的后面。
        /* "SSSS-EEEE/TTTT" CRLF CRLF */
        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return NGX_ERROR;
        }
        b->temporary = 1;
        b->pos = range[i].content_range.data;
        b->last = range[i].content_range.data + range[i].content_range.len;
        rcl = ngx_alloc_chain_link(r->pool);
        if (rcl == NULL) {
            return NGX_ERROR;
        }
        rcl->buf = b;
		//下面就是数据部分了。
        /* the range data */
        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return NGX_ERROR;
        }
        b->in_file = buf->in_file;
        b->temporary = buf->temporary;
        b->memory = buf->memory;
        b->mmap = buf->mmap;
        b->file = buf->file;

        if (buf->in_file) {
            b->file_pos = body_start + range[i].start;
            b->file_last = body_start + range[i].end;
        }
        if (ngx_buf_in_memory(buf)) {
            b->pos = buf->start + (size_t) range[i].start;
            b->last = buf->start + (size_t) range[i].end;
        }
        dcl = ngx_alloc_chain_link(r->pool);
        if (dcl == NULL) {
            return NGX_ERROR;
        }
        dcl->buf = b;

        *ll = hcl;//头部
        hcl->next = rcl;//区域数据
        rcl->next = dcl;//真正的数据
        ll = &dcl->next;//这样就形成了一个链表。
    }
//最后还得拼接一个"	CRLF
//					--0123456789--" CRLF 结尾
    /* the last boundary CRLF "--0123456789--" CRLF  */
    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        return NGX_ERROR;
    }
    b->temporary = 1;
    b->last_buf = 1;//这真的是最后一块了。
    b->pos = ngx_pnalloc(r->pool, sizeof(CRLF "--") - 1 + NGX_ATOMIC_T_LEN + sizeof("--" CRLF) - 1);
    if (b->pos == NULL) {
        return NGX_ERROR;
    }
	//这个结尾数据正好可以从ctx->boundary_header.data头部开始取，后面的不要就行了。
    b->last = ngx_cpymem(b->pos, ctx->boundary_header.data,
                         sizeof(CRLF "--") - 1 + NGX_ATOMIC_T_LEN);
    *b->last++ = '-'; *b->last++ = '-';//接上--2个字符
    *b->last++ = CR; *b->last++ = LF;//结尾符号。

    hcl = ngx_alloc_chain_link(r->pool);
    if (hcl == NULL) {
        return NGX_ERROR;
    }

    hcl->buf = b;
    hcl->next = NULL;
    *ll = hcl;//将这块数据放到最后面
//继续下面的。
    return ngx_http_next_body_filter(r, out);
}


static ngx_int_t
ngx_http_range_header_filter_init(ngx_conf_t *cf)
{//组成filter链表
    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_range_header_filter;
    return NGX_OK;
}


static ngx_int_t
ngx_http_range_body_filter_init(ngx_conf_t *cf)
{//这是第一个BODY过滤函数、在给客户端发送BODY部分数据的时候会先调用这里
    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_range_body_filter;

    return NGX_OK;
}
