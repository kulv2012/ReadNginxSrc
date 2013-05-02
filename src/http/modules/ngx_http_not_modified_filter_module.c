
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>



static ngx_int_t ngx_http_not_modified_filter_init(ngx_conf_t *cf);


static ngx_http_module_t  ngx_http_not_modified_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_not_modified_filter_init,     /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_not_modified_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_not_modified_filter_module_ctx, /* module context */
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


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;


static ngx_int_t
ngx_http_not_modified_header_filter(ngx_http_request_t *r)
{
    time_t                     ims;
    ngx_http_core_loc_conf_t  *clcf;
    if (r->headers_out.status != NGX_HTTP_OK || r != r->main  || r->headers_in.if_modified_since == NULL
        || r->headers_out.last_modified_time == -1)
    {//give it to next one
        return ngx_http_next_header_filter(r);
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (clcf->if_modified_since == NGX_HTTP_IMS_OFF) {//关闭了if_modified_since
        return ngx_http_next_header_filter(r);
    }
	//如果客户端传过来的时间
    ims = ngx_http_parse_time(r->headers_in.if_modified_since->value.data,  r->headers_in.if_modified_since->value.len);
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http ims:%d lm:%d", ims, r->headers_out.last_modified_time);
    if (ims != r->headers_out.last_modified_time) {
        if (clcf->if_modified_since == NGX_HTTP_IMS_EXACT || ims < r->headers_out.last_modified_time)  {//如果客户端的时间旧，资源有更新。
            return ngx_http_next_header_filter(r);
        }
    }
//如果资源没有更新，那就直接返回304给客户端就行。这样减少很多带宽。
    r->headers_out.status = NGX_HTTP_NOT_MODIFIED;
    r->headers_out.status_line.len = 0;
    r->headers_out.content_type.len = 0;
    ngx_http_clear_content_length(r);
    ngx_http_clear_accept_ranges(r);

    if (r->headers_out.content_encoding) {
        r->headers_out.content_encoding->hash = 0;
        r->headers_out.content_encoding = NULL;
    }

    return ngx_http_next_header_filter(r);
}


static ngx_int_t
ngx_http_not_modified_filter_init(ngx_conf_t *cf)
{
//insert my header filter into the frist of filter list
    ngx_http_next_header_filter = ngx_http_top_header_filter;//保存当前的头部。
    ngx_http_top_header_filter = ngx_http_not_modified_header_filter;//将自己设置为当前头部。后面就会调用之前的头部。连接起来的。

    return NGX_OK;
}
