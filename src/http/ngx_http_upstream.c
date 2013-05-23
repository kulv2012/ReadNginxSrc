
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#if (NGX_HTTP_CACHE)
static ngx_int_t ngx_http_upstream_cache(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_cache_send(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_cache_status(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
#endif

static void ngx_http_upstream_init_request(ngx_http_request_t *r);
static void ngx_http_upstream_resolve_handler(ngx_resolver_ctx_t *ctx);
static void ngx_http_upstream_rd_check_broken_connection(ngx_http_request_t *r);
static void ngx_http_upstream_wr_check_broken_connection(ngx_http_request_t *r);
static void ngx_http_upstream_check_broken_connection(ngx_http_request_t *r,
    ngx_event_t *ev);
static void ngx_http_upstream_connect(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_reinit(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_send_request(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_send_request_handler(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_process_header(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_test_next(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_intercept_errors(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static ngx_int_t ngx_http_upstream_test_connect(ngx_connection_t *c);
static ngx_int_t ngx_http_upstream_process_headers(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_process_body_in_memory(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_send_response(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void
    ngx_http_upstream_process_non_buffered_downstream(ngx_http_request_t *r);
static void
    ngx_http_upstream_process_non_buffered_upstream(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void
    ngx_http_upstream_process_non_buffered_request(ngx_http_request_t *r,
    ngx_uint_t do_write);
static ngx_int_t ngx_http_upstream_non_buffered_filter_init(void *data);
static ngx_int_t ngx_http_upstream_non_buffered_filter(void *data,
    ssize_t bytes);
static void ngx_http_upstream_process_downstream(ngx_http_request_t *r);
static void ngx_http_upstream_process_upstream(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_process_request(ngx_http_request_t *r);
static void ngx_http_upstream_store(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_dummy_handler(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_upstream_next(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_uint_t ft_type);
static void ngx_http_upstream_cleanup(void *data);
static void ngx_http_upstream_finalize_request(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_int_t rc);

static ngx_int_t ngx_http_upstream_process_header_line(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_set_cookie(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t
    ngx_http_upstream_process_cache_control(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_ignore_header_line(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_expires(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_accel_expires(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_limit_rate(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_buffering(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_process_charset(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_copy_header_line(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t
    ngx_http_upstream_copy_multi_header_lines(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_copy_content_type(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_copy_content_length(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_copy_last_modified(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_rewrite_location(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_rewrite_refresh(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
static ngx_int_t ngx_http_upstream_copy_allow_ranges(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);

#if (NGX_HTTP_GZIP)
static ngx_int_t ngx_http_upstream_copy_content_encoding(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset);
#endif

static ngx_int_t ngx_http_upstream_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_http_upstream_addr_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_upstream_status_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_upstream_response_time_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_upstream_response_length_variable(
    ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data);

static char *ngx_http_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy);
static char *ngx_http_upstream_server(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static void *ngx_http_upstream_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_upstream_init_main_conf(ngx_conf_t *cf, void *conf);

#if (NGX_HTTP_SSL)
static void ngx_http_upstream_ssl_init_connection(ngx_http_request_t *,
    ngx_http_upstream_t *u, ngx_connection_t *c);
static void ngx_http_upstream_ssl_handshake(ngx_connection_t *c);
#endif


ngx_http_upstream_header_t  ngx_http_upstream_headers_in[] = {

    { ngx_string("Status"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, status),
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("Content-Type"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, content_type),
                 ngx_http_upstream_copy_content_type, 0, 1 },

    { ngx_string("Content-Length"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, content_length),
                 ngx_http_upstream_copy_content_length, 0, 0 },

    { ngx_string("Date"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, date),
                 ngx_http_upstream_copy_header_line,
                 offsetof(ngx_http_headers_out_t, date), 0 },

    { ngx_string("Last-Modified"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, last_modified),
                 ngx_http_upstream_copy_last_modified, 0, 0 },

    { ngx_string("ETag"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, etag),
                 ngx_http_upstream_copy_header_line,
                 offsetof(ngx_http_headers_out_t, etag), 0 },

    { ngx_string("Server"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, server),
                 ngx_http_upstream_copy_header_line,
                 offsetof(ngx_http_headers_out_t, server), 0 },

    { ngx_string("WWW-Authenticate"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, www_authenticate),
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("Location"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, location),
                 ngx_http_upstream_rewrite_location, 0, 0 },

    { ngx_string("Refresh"),
                 ngx_http_upstream_ignore_header_line, 0,
                 ngx_http_upstream_rewrite_refresh, 0, 0 },

    { ngx_string("Set-Cookie"),
                 ngx_http_upstream_process_set_cookie, 0,
                 ngx_http_upstream_copy_header_line, 0, 1 },

    { ngx_string("Content-Disposition"),
                 ngx_http_upstream_ignore_header_line, 0,
                 ngx_http_upstream_copy_header_line, 0, 1 },

    { ngx_string("Cache-Control"),
                 ngx_http_upstream_process_cache_control, 0,
                 ngx_http_upstream_copy_multi_header_lines,
                 offsetof(ngx_http_headers_out_t, cache_control), 1 },

    { ngx_string("Expires"),
                 ngx_http_upstream_process_expires, 0,
                 ngx_http_upstream_copy_header_line,
                 offsetof(ngx_http_headers_out_t, expires), 1 },

    { ngx_string("Accept-Ranges"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, accept_ranges),
                 ngx_http_upstream_copy_allow_ranges,
                 offsetof(ngx_http_headers_out_t, accept_ranges), 1 },

    { ngx_string("Connection"),
                 ngx_http_upstream_ignore_header_line, 0,
                 ngx_http_upstream_ignore_header_line, 0, 0 },

    { ngx_string("Keep-Alive"),
                 ngx_http_upstream_ignore_header_line, 0,
                 ngx_http_upstream_ignore_header_line, 0, 0 },

    { ngx_string("X-Powered-By"),
                 ngx_http_upstream_ignore_header_line, 0,
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("X-Accel-Expires"),
                 ngx_http_upstream_process_accel_expires, 0,
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("X-Accel-Redirect"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, x_accel_redirect),
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("X-Accel-Limit-Rate"),
                 ngx_http_upstream_process_limit_rate, 0,
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("X-Accel-Buffering"),
                 ngx_http_upstream_process_buffering, 0,
                 ngx_http_upstream_copy_header_line, 0, 0 },

    { ngx_string("X-Accel-Charset"),
                 ngx_http_upstream_process_charset, 0,
                 ngx_http_upstream_copy_header_line, 0, 0 },

#if (NGX_HTTP_GZIP)
    { ngx_string("Content-Encoding"),
                 ngx_http_upstream_process_header_line,
                 offsetof(ngx_http_upstream_headers_in_t, content_encoding),
                 ngx_http_upstream_copy_content_encoding, 0, 0 },
#endif

    { ngx_null_string, NULL, 0, NULL, 0, 0 }
};


static ngx_command_t  ngx_http_upstream_commands[] = {

    { ngx_string("upstream"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_TAKE1,
      ngx_http_upstream,
      0,
      0,
      NULL },

    { ngx_string("server"),
      NGX_HTTP_UPS_CONF|NGX_CONF_1MORE,
      ngx_http_upstream_server,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_upstream_module_ctx = {
    ngx_http_upstream_add_variables,       /* preconfiguration */
    NULL,                                  /* postconfiguration */

    ngx_http_upstream_create_main_conf,    /* create main configuration */
    ngx_http_upstream_init_main_conf,      /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_upstream_module = {
    NGX_MODULE_V1,
    &ngx_http_upstream_module_ctx,         /* module context */
    ngx_http_upstream_commands,            /* module directives */
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


static ngx_http_variable_t  ngx_http_upstream_vars[] = {

    { ngx_string("upstream_addr"), NULL,
      ngx_http_upstream_addr_variable, 0,
      NGX_HTTP_VAR_NOHASH|NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("upstream_status"), NULL,
      ngx_http_upstream_status_variable, 0,
      NGX_HTTP_VAR_NOHASH|NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("upstream_response_time"), NULL,
      ngx_http_upstream_response_time_variable, 0,
      NGX_HTTP_VAR_NOHASH|NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("upstream_response_length"), NULL,
      ngx_http_upstream_response_length_variable, 0,
      NGX_HTTP_VAR_NOHASH|NGX_HTTP_VAR_NOCACHEABLE, 0 },

#if (NGX_HTTP_CACHE)

    { ngx_string("upstream_cache_status"), NULL,
      ngx_http_upstream_cache_status, 0,
      NGX_HTTP_VAR_NOHASH|NGX_HTTP_VAR_NOCACHEABLE, 0 },

#endif

    { ngx_null_string, NULL, NULL, 0, 0, 0 }
};


static ngx_http_upstream_next_t  ngx_http_upstream_next_errors[] = {
    { 500, NGX_HTTP_UPSTREAM_FT_HTTP_500 },
    { 502, NGX_HTTP_UPSTREAM_FT_HTTP_502 },
    { 503, NGX_HTTP_UPSTREAM_FT_HTTP_503 },
    { 504, NGX_HTTP_UPSTREAM_FT_HTTP_504 },
    { 404, NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { 0, 0 }
};


ngx_conf_bitmask_t  ngx_http_upstream_cache_method_mask[] = {
   { ngx_string("GET"),  NGX_HTTP_GET},
   { ngx_string("HEAD"), NGX_HTTP_HEAD },
   { ngx_string("POST"), NGX_HTTP_POST },
   { ngx_null_string, 0 }
};


ngx_conf_bitmask_t  ngx_http_upstream_ignore_headers_masks[] = {
    { ngx_string("X-Accel-Redirect"), NGX_HTTP_UPSTREAM_IGN_XA_REDIRECT },
    { ngx_string("X-Accel-Expires"), NGX_HTTP_UPSTREAM_IGN_XA_EXPIRES },
    { ngx_string("Expires"), NGX_HTTP_UPSTREAM_IGN_EXPIRES },
    { ngx_string("Cache-Control"), NGX_HTTP_UPSTREAM_IGN_CACHE_CONTROL },
    { ngx_string("Set-Cookie"), NGX_HTTP_UPSTREAM_IGN_SET_COOKIE },
    { ngx_null_string, 0 }
};

//Ò»°á»áÔÚ´¦Àí¹ı³ÌµÄ»Øµ÷º¯ÊıÖĞµ÷ÓÃ£¬±ÈÈçngx_http_proxy_handler£¬ngx_http_fastcgi_handlerµÈ£¬ÓÃÀ´ÉêÇëupstream´ó½á¹¹Ìå
ngx_int_t ngx_http_upstream_create(ngx_http_request_t *r)
{//´´½¨Ò»¸öÉÏÓÎÄ£¿éĞèÒªµÄ½á¹¹£¬ÉèÖÃµ½r²ÎÊıµÄ¿Í»§¶ËÇëÇó½á¹¹ÉÏÃæÈ¥¡£
    ngx_http_upstream_t  *u;

    u = r->upstream;//ÄÃµ½Õâ¸öÇëÇóµÄupstream½á¹¹,Èç¹ûÆäcleanup³ÉÔ±·Ç¿Õ£¬¾ÍÖ´ĞĞÇåÀí¡£ÎªÉ¶¿
    if (u && u->cleanup) {
        r->main->count++;
        ngx_http_upstream_cleanup(r);//Èç¹ûÒÑ¾­ÓĞÁËupstream½á¹¹£¬¾Í¸´ÓÃ¾ÉµÄ¡£
    }

    u = ngx_pcalloc(r->pool, sizeof(ngx_http_upstream_t));//È»ºóÉêÇëÒ»¸öĞÂµÄngx_http_upstream_t
    if (u == NULL) {
        return NGX_ERROR;
    }
    r->upstream = u;
    u->peer.log = r->connection->log;//Ê¹ÓÃÕâ¸öÁ¬½ÓµÄÈÕÖ¾½á¹¹
    u->peer.log_error = NGX_ERROR_ERR;
#if (NGX_THREADS)
    u->peer.lock = &r->connection->lock;
#endif
#if (NGX_HTTP_CACHE)
    r->cache = NULL;
#endif
    return NGX_OK;
}

//ÏÂÃæº¯ÊıÒ»°ãÊÇÕâÃ´±»µ÷ÓÃµÄ: ngx_http_read_client_request_body(r, ngx_http_upstream_init);Ò²¾ÍÊÇ¶ÁÈ¡Íê¿Í»§¶ËÇëÇóµÄbodyºóµ÷ÓÃÕâÀï¡£
void ngx_http_upstream_init(ngx_http_request_t *r)
{//ngx_http_read_client_request_body¶ÁÈ¡Íê±Ï¿Í»§¶ËµÄÊı¾İºó£¬¾Í»áµ÷ÓÃÕâÀï½øĞĞ³õÊ¼»¯Ò»¸öupstream
    ngx_connection_t     *c;
    c = r->connection;//µÃµ½ÆäÁ¬½Ó½á¹¹¡£
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http init upstream, client timer: %d", c->read->timer_set);

    if (c->read->timer_set) {//¶ÁÍêÁË£¬½«¶ÁÊÂ¼ş½á¹¹¶¨Ê±Æ÷É¾³ı¡£
        ngx_del_timer(c->read);
    }
    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {//Èç¹ûepollÊ¹ÓÃ±ßÔµ´¥·¢
        if (!c->write->active) {//ÒªÔö¼Ó¿ÉĞ´ÊÂ¼şÍ¨Öª£¬ÎªÉ¶?ÒòÎª´ı»á¿ÉÄÜ¾ÍÄÜĞ´ÁË
            if (ngx_add_event(c->write, NGX_WRITE_EVENT, NGX_CLEAR_EVENT)  == NGX_ERROR) {
                ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
        }
    }
    ngx_http_upstream_init_request(r);//½øĞĞÊµÖÊµÄ³õÊ¼»¯¡£
}


static void
ngx_http_upstream_init_request(ngx_http_request_t *r)
{//ngx_http_upstream_initµ÷ÓÃÕâÀï£¬´ËÊ±¿Í»§¶Ë·¢ËÍµÄÊı¾İ¶¼ÒÑ¾­½ÓÊÕÍê±ÏÁË¡£
/*1. µ÷ÓÃcreate_request´´½¨fcgi»òÕßproxyµÄÊı¾İ½á¹¹¡£
  2. µ÷ÓÃngx_http_upstream_connectÁ¬½ÓÏÂÓÎ·şÎñÆ÷¡£
  */
    ngx_str_t                      *host;
    ngx_uint_t                      i;
    ngx_resolver_ctx_t             *ctx, temp;
    ngx_http_cleanup_t             *cln;
    ngx_http_upstream_t            *u;
    ngx_http_core_loc_conf_t       *clcf;
    ngx_http_upstream_srv_conf_t   *uscf, **uscfp;
    ngx_http_upstream_main_conf_t  *umcf;

    if (r->aio) {//Ê²Ã´¶«Î÷
        return;
    }
    u = r->upstream;//ngx_http_upstream_createÀïÃæÉèÖÃµÄ
#if (NGX_HTTP_CACHE)
    if (u->conf->cache) {
        ngx_int_t  rc;
        rc = ngx_http_upstream_cache(r, u);
        if (rc == NGX_BUSY) {
            r->write_event_handler = ngx_http_upstream_init_request;
            return;
        }
        r->write_event_handler = ngx_http_request_empty_handler;
        if (rc == NGX_DONE) {
            return;
        }
        if (rc != NGX_DECLINED) {
            ngx_http_finalize_request(r, rc);
            return;
        }
    }
#endif

    u->store = (u->conf->store || u->conf->store_lengths);
    if (!u->store && !r->post_action && !u->conf->ignore_client_abort) {//ignore_client_abortºöÂÔ¿Í»§¶ËÌáÇ°¶Ï¿ªÁ¬½Ó¡£ÕâÀïÖ¸²»ºöÂÔ¿Í»§¶ËÌáÇ°¶Ï¿ª¡£
        r->read_event_handler = ngx_http_upstream_rd_check_broken_connection;//ÉèÖÃ»Øµ÷ĞèÒª¼ì²âÁ¬½ÓÊÇ·ñÓĞÎÊÌâ¡£
        r->write_event_handler = ngx_http_upstream_wr_check_broken_connection;
    }
    if (r->request_body) {//¿Í»§¶Ë·¢ËÍ¹ıÀ´µÄPOSTÊı¾İ´æ·ÅÔÚ´Ë,ngx_http_read_client_request_body·ÅµÄ
        u->request_bufs = r->request_body->bufs;//¼ÇÂ¼¿Í»§¶Ë·¢ËÍµÄÊı¾İ£¬ÏÂÃæÔÚcreate_requestµÄÊ±ºò¿½±´µ½·¢ËÍ»º³åÁ´½Ó±íÀïÃæµÄ¡£
    }
	//Èç¹ûÊÇFCGI¡£ÏÂÃæ×é½¨ºÃFCGIµÄ¸÷ÖÖÍ·²¿£¬°üÀ¨ÇëÇó¿ªÊ¼Í·£¬ÇëÇó²ÎÊıÍ·£¬ÇëÇóSTDINÍ·¡£´æ·ÅÔÚu->request_bufsÁ´½Ó±íÀïÃæ¡£
	//Èç¹ûÊÇProxyÄ£¿é£¬ngx_http_proxy_create_request×é¼ş·´Ïò´úÀíµÄÍ·²¿É¶µÄ,·Åµ½u->request_bufsÀïÃæ
    if (u->create_request(r) != NGX_OK) {//ngx_http_fastcgi_create_request
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    u->peer.local = u->conf->local;
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    u->output.alignment = clcf->directio_alignment;
    u->output.pool = r->pool;
    u->output.bufs.num = 1;
    u->output.bufs.size = clcf->client_body_buffer_size;
	//ÉèÖÃ¹ıÂËÄ£¿éµÄ¿ªÊ¼¹ıÂËº¯ÊıÎªwriter¡£Ò²¾ÍÊÇoutput_filter¡£ÔÚngx_output_chain±»µ÷ÓÃÒÑ½øĞĞÊı¾İµÄ¹ıÂË
    u->output.output_filter = ngx_chain_writer;
    u->output.filter_ctx = &u->writer;//²Î¿¼ngx_chain_writer£¬ÀïÃæ»á½«Êä³öbufÒ»¸ö¸öÁ¬½Óµ½ÕâÀï¡£

    u->writer.pool = r->pool;
    if (r->upstream_states == NULL) {//Êı×éupstream_states£¬±£ÁôupstreamµÄ×´Ì¬ĞÅÏ¢¡£
        r->upstream_states = ngx_array_create(r->pool, 1, sizeof(ngx_http_upstream_state_t));
        if (r->upstream_states == NULL) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
    } else {//Èç¹ûÒÑ¾­ÓĞÁË£¬ĞÂ¼ÓÒ»¸ö¡£
        u->state = ngx_array_push(r->upstream_states);
        if (u->state == NULL) {
            ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        ngx_memzero(u->state, sizeof(ngx_http_upstream_state_t));
    }
	//¹ÒÔÚÇåÀí»Øµ÷º¯Êı£¬¸ÉÂïµÄÔİ²»Çå³ş
    cln = ngx_http_cleanup_add(r, 0);//»·ĞÎÁ´±í£¬ÉêÇëÒ»¸öĞÂµÄÔªËØ¡£
    if (cln == NULL) {
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    cln->handler = ngx_http_upstream_cleanup;//¸ÉÂïµÄ
    cln->data = r;//Ö¸ÏòËùÖ¸µÄÇëÇó½á¹¹Ìå¡£
    u->cleanup = &cln->handler;
/*È»ºó¾ÍÊÇÕâ¸öº¯Êı×îºËĞÄµÄ´¦Àí²¿·Ö£¬ÄÇ¾ÍÊÇ¸ù¾İupstreamµÄÀàĞÍÀ´½øĞĞ²»Í¬µÄ²Ù×÷£¬ÕâÀïµÄupstream¾ÍÊÇÎÒÃÇÍ¨¹ıXXX_pass´«µİ½øÀ´µÄÖµ£¬
ÕâÀïµÄupstreamÓĞ¿ÉÄÜÏÂÃæ¼¸ÖÖÇé¿ö¡£
1 XXX_passÖĞ²»°üº¬±äÁ¿¡£
2 XXX_pass´«µİµÄÖµ°üº¬ÁËÒ»¸ö±äÁ¿($¿ªÊ¼).ÕâÖÖÇé¿öÒ²¾ÍÊÇËµupstreamµÄurlÊÇ¶¯Ì¬±ä»¯µÄ£¬Òò´ËĞèÒªÃ¿´Î¶¼½âÎöÒ»±é.
¶øµÚ¶şÖÖÇé¿öÓÖ·ÖÎª2ÖÖ£¬Ò»ÖÖÊÇÔÚ½øÈëupstreamÖ®Ç°£¬Ò²¾ÍÊÇ upstreamÄ£¿éµÄhandlerÖ®ÖĞÒÑ¾­±»resolveµÄµØÖ·(Çë¿´ngx_http_XXX_evalº¯Êı)£¬
Ò»ÖÖÊÇÃ»ÓĞ±»resolve£¬´ËÊ±¾ÍĞèÒªupstreamÄ£¿éÀ´½øĞĞresolve¡£½ÓÏÂÀ´µÄ´úÂë¾ÍÊÇ´¦ÀíÕâ²¿·ÖµÄ¶«Î÷¡£*/
    if (u->resolved == NULL) {//ÉÏÓÎµÄIPµØÖ·ÊÇ·ñ±»½âÎö¹ı£¬ngx_http_fastcgi_handlerµ÷ÓÃngx_http_fastcgi_eval»á½âÎö¡£
        uscf = u->conf->upstream;
    } else {
    //ngx_http_fastcgi_handler »áµ÷ÓÃ ngx_http_fastcgi_evalº¯Êı£¬½øĞĞfastcgi_pass ºóÃæµÄURLµÄ¼òÎö£¬½âÎö³öunixÓò£¬»òÕßsocket.
        if (u->resolved->sockaddr) {//Èç¹ûµØÖ·ÒÑ¾­±»resolve¹ıÁË£¬ÎÒIPµØÖ·£¬´ËÊ±´´½¨round robin peer¾ÍĞĞ
            if (ngx_http_upstream_create_round_robin_peer(r, u->resolved) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
            ngx_http_upstream_connect(r, u);//Á¬½ÓÖ®
            return;
        }
		//ÏÂÃæ¿ªÊ¼²éÕÒÓòÃû£¬ÒòÎªfcgi_passºóÃæ²»ÊÇip:port£¬¶øÊÇurl£»
        host = &u->resolved->host;//»ñÈ¡hostĞÅÏ¢¡£
        umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
        uscfp = umcf->upstreams.elts;//±éÀúËùÓĞµÄÉÏÓÎÄ£¿é£¬¸ù¾İÆähost½øĞĞ²éÕÒ£¬ÕÒµ½host,portÏàÍ¬µÄ¡£
        for (i = 0; i < umcf->upstreams.nelts; i++) {
            uscf = uscfp[i];//ÕÒÒ»¸öIPÒ»ÑùµÄÉÏÁ÷Ä£¿é
            if (uscf->host.len == host->len && ((uscf->port == 0 && u->resolved->no_port) || uscf->port == u->resolved->port)
                && ngx_memcmp(uscf->host.data, host->data, host->len) == 0) {
                goto found;//Õâ¸öhostÕıºÃÏàµÈ
            }
        }
		//Ã»°ì·¨ÁË£¬url²»ÔÚupstreamsÊı×éÀïÃæ£¬Ò²¾ÍÊÇ²»ÊÇÎÒÃÇÅäÖÃµÄ£¬ÄÇÃ´³õÊ¼»¯ÓòÃû½âÎöÆ÷
        temp.name = *host;
        ctx = ngx_resolve_start(clcf->resolver, &temp);//½øĞĞÓòÃû½âÎö£¬´ø»º´æµÄ¡£ÉêÇëÏà¹ØµÄ½á¹¹£¬·µ»ØÉÏÏÂÎÄµØÖ·¡£
        if (ctx == NULL) {
            ngx_http_upstream_finalize_request(r, u,NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        if (ctx == NGX_NO_RESOLVER) {//Èç·¨½øĞĞÓòÃû½âÎö¡£
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,  "no resolver defined to resolve %V", host);
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_BAD_GATEWAY);
            return;
        }
		
        ctx->name = *host;
        ctx->type = NGX_RESOLVE_A;
        ctx->handler = ngx_http_upstream_resolve_handler;//ÉèÖÃÓòÃû½âÎöÍê³ÉºóµÄ»Øµ÷º¯Êı¡£
        ctx->data = r;
        ctx->timeout = clcf->resolver_timeout;
        u->resolved->ctx = ctx;
		//¿ªÊ¼ÓòÃû½âÎö£¬Ã»ÓĞÍê³ÉÒ²»á·µ»ØµÄ¡£
        if (ngx_resolve_name(ctx) != NGX_OK) {
            u->resolved->ctx = NULL;
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        return;
    }

found:
	//ÔÚngx_http_upstream_init_main_confµÄÊ±ºò£¬»áµ÷ÓÃ¸÷¸öupstreamµÄinit·½·¨£¬È»ºóµ÷ÓÃngx_http_upstream_init_round_robin»òÕßÆäËû¡£
    if (uscf->peer.init(r, uscf) != NGX_OK) {//Îªngx_http_upstream_init_round_robinÉèÖÃµÄ£¬Îªngx_http_upstream_init_round_robin_peer
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    ngx_http_upstream_connect(r, u);
}


#if (NGX_HTTP_CACHE)

static ngx_int_t
ngx_http_upstream_cache(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_int_t          rc;
    ngx_http_cache_t  *c;

    c = r->cache;

    if (c == NULL) {

        switch (ngx_http_test_predicates(r, u->conf->cache_bypass)) {

        case NGX_ERROR:
            return NGX_ERROR;

        case NGX_DECLINED:
            u->cache_status = NGX_HTTP_CACHE_BYPASS;
            return NGX_DECLINED;

        default: /* NGX_OK */
            break;
        }

        if (!(r->method & u->conf->cache_methods)) {
            return NGX_DECLINED;
        }

        if (r->method & NGX_HTTP_HEAD) {
            u->method = ngx_http_core_get_method;
        }

        if (ngx_http_file_cache_new(r) != NGX_OK) {
            return NGX_ERROR;
        }

        if (u->create_key(r) != NGX_OK) {
            return NGX_ERROR;
        }

        /* TODO: add keys */

        ngx_http_file_cache_create_key(r);

        if (r->cache->header_start >= u->conf->buffer_size) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "cache key too large, increase upstream buffer size %uz",
                u->conf->buffer_size);

            r->cache = NULL;
            return NGX_DECLINED;
        }

        u->cacheable = 1;

        c = r->cache;

        c->min_uses = u->conf->cache_min_uses;
        c->body_start = u->conf->buffer_size;
        c->file_cache = u->conf->cache->data;

        u->cache_status = NGX_HTTP_CACHE_MISS;
    }

    rc = ngx_http_file_cache_open(r);

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http upstream cache: %i", rc);

    switch (rc) {

    case NGX_HTTP_CACHE_UPDATING:

        if (u->conf->cache_use_stale & NGX_HTTP_UPSTREAM_FT_UPDATING) {
            u->cache_status = rc;
            rc = NGX_OK;

        } else {
            rc = NGX_HTTP_CACHE_STALE;
        }

        break;

    case NGX_OK:
        u->cache_status = NGX_HTTP_CACHE_HIT;
    }

    switch (rc) {

    case NGX_OK:

        rc = ngx_http_upstream_cache_send(r, u);

        if (rc != NGX_HTTP_UPSTREAM_INVALID_HEADER) {
            return rc;
        }

        break;

    case NGX_HTTP_CACHE_STALE:

        c->valid_sec = 0;
        u->buffer.start = NULL;
        u->cache_status = NGX_HTTP_CACHE_EXPIRED;

        break;

    case NGX_DECLINED:

        if ((size_t) (u->buffer.end - u->buffer.start) < u->conf->buffer_size) {
            u->buffer.start = NULL;

        } else {
            u->buffer.pos = u->buffer.start + c->header_start;
            u->buffer.last = u->buffer.pos;
        }

        break;

    case NGX_HTTP_CACHE_SCARCE:

        u->cacheable = 0;

        break;

    case NGX_AGAIN:

        return NGX_BUSY;

    case NGX_ERROR:

        return NGX_ERROR;

    default:

        /* cached NGX_HTTP_BAD_GATEWAY, NGX_HTTP_GATEWAY_TIME_OUT, etc. */

        u->cache_status = NGX_HTTP_CACHE_HIT;

        return rc;
    }

    r->cached = 0;

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_upstream_cache_send(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_int_t          rc;
    ngx_http_cache_t  *c;

    r->cached = 1;
    c = r->cache;

    if (c->header_start == c->body_start) {
        r->http_version = NGX_HTTP_VERSION_9;
        return ngx_http_cache_send(r);
    }

    /* TODO: cache stack */

    u->buffer = *c->buf;
    u->buffer.pos += c->header_start;

    ngx_memzero(&u->headers_in, sizeof(ngx_http_upstream_headers_in_t));

    if (ngx_list_init(&u->headers_in.headers, r->pool, 8,
                      sizeof(ngx_table_elt_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    rc = u->process_header(r);

    if (rc == NGX_OK) {

        if (ngx_http_upstream_process_headers(r, u) != NGX_OK) {
            return NGX_DONE;
        }

        return ngx_http_cache_send(r);
    }

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    /* rc == NGX_HTTP_UPSTREAM_INVALID_HEADER */

    /* TODO: delete file */

    return rc;
}

#endif


static void
ngx_http_upstream_resolve_handler(ngx_resolver_ctx_t *ctx)
{//nginx ÓòÃû½âÎöÍêºó»áµ÷ÓÃÕâÀï¡£
    ngx_http_request_t            *r;
    ngx_http_upstream_t           *u;
    ngx_http_upstream_resolved_t  *ur;

    r = ctx->data;
    u = r->upstream;
    ur = u->resolved;
    if (ctx->state) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
			"%V could not be resolved (%i: %s)", &ctx->name, ctx->state,ngx_resolver_strerror(ctx->state));
        ngx_http_upstream_finalize_request(r, u, NGX_HTTP_BAD_GATEWAY);
        return;
    }

    ur->naddrs = ctx->naddrs;
    ur->addrs = ctx->addrs;
#if (NGX_DEBUG)
    {
    in_addr_t   addr;
    ngx_uint_t  i;

    for (i = 0; i < ctx->naddrs; i++) {
        addr = ntohl(ur->addrs[i]);

        ngx_log_debug4(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "name was resolved to %ud.%ud.%ud.%ud", (addr >> 24) & 0xff, (addr >> 16) & 0xff,(addr >> 8) & 0xff, addr & 0xff);
    }
    }
#endif

    if (ngx_http_upstream_create_round_robin_peer(r, ur) != NGX_OK) {
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    ngx_resolve_name_done(ctx);
    ur->ctx = NULL;

    ngx_http_upstream_connect(r, u);
}


static void
ngx_http_upstream_handler(ngx_event_t *ev)
{//Õâ¸öÊÇ¶ÁĞ´ÊÂ¼şµÄÍ³Ò»»Øµ÷º¯Êı£¬²»¹ı×Ô¼º»á¸ù¾İ¶Á»¹ÊÇĞ´µ÷ÓÃ¶ÔÓ¦µÄwrite_event_handlerµÈ
//ngx_http_upstream_connectÉèÖÃµÄ¶ÁĞ´¾ä±ú¡£
    ngx_connection_t     *c;
    ngx_http_request_t   *r;
    ngx_http_log_ctx_t   *ctx;
    ngx_http_upstream_t  *u;

    c = ev->data;
    r = c->data;

    u = r->upstream;
    c = r->connection;

    ctx = c->log->data;
    ctx->current_request = r;
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0, "http upstream request: \"%V?%V\"", &r->uri, &r->args);

    if (ev->write) {
        u->write_event_handler(r, u);//ngx_http_upstream_send_request_handler
    } else {
        u->read_event_handler(r, u);//ngx_http_upstream_process_header
    }
    ngx_http_run_posted_requests(c);
}


static void
ngx_http_upstream_rd_check_broken_connection(ngx_http_request_t *r)
{
    ngx_http_upstream_check_broken_connection(r, r->connection->read);
}


static void
ngx_http_upstream_wr_check_broken_connection(ngx_http_request_t *r)
{
    ngx_http_upstream_check_broken_connection(r, r->connection->write);
}


static void ngx_http_upstream_check_broken_connection(ngx_http_request_t *r, ngx_event_t *ev)
{
    int                  n;
    char                 buf[1];
    ngx_err_t            err;
    ngx_int_t            event;
    ngx_connection_t     *c;
    ngx_http_upstream_t  *u;

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, ev->log, 0,  "http upstream check client, write event:%d, \"%V\"", ev->write, &r->uri);
    c = r->connection;
    u = r->upstream;
    if (c->error) {//Èç¹ûÒÑ¾­±»±ê¼ÇÎªÁ¬½Ó´íÎó£¬ÔòÖ±½Ó½áÊøÁ¬½Ó¼´¿É¡£
        if ((ngx_event_flags & NGX_USE_LEVEL_EVENT) && ev->active) {
            event = ev->write ? NGX_WRITE_EVENT : NGX_READ_EVENT;
            if (ngx_del_event(ev, event, 0) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
        }
        if (!u->cacheable) {
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_CLIENT_CLOSED_REQUEST);
        }
        return;
    }
#if (NGX_HAVE_KQUEUE)
    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
        if (!ev->pending_eof) {
            return;
        }
        ev->eof = 1;
        c->error = 1;

        if (ev->kq_errno) {
            ev->error = 1;
        }

        if (!u->cacheable && u->peer.connection) {
            ngx_log_error(NGX_LOG_INFO, ev->log, ev->kq_errno, "kevent() reported that client closed prematurely "
                          "connection, so upstream connection is closed too");
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_CLIENT_CLOSED_REQUEST);
            return;
        }
        ngx_log_error(NGX_LOG_INFO, ev->log, ev->kq_errno, "kevent() reported that client closed prematurely connection");
        if (u->peer.connection == NULL) {
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_CLIENT_CLOSED_REQUEST);
        }
        return;
    }
#endif
    n = recv(c->fd, buf, 1, MSG_PEEK);//MSG_PEEKÊÔÌ½Ò»¸ö×Ô¼º£¬¿´¿´ÊÇ·ñÓĞÎÊÌâ¡£
    err = ngx_socket_errno;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, ev->log, err,  "http upstream recv(): %d", n);
    if (ev->write && (n >= 0 || err == NGX_EAGAIN)) {
        return;//Ã»ÎÊÌâ¡£
    }
    if ((ngx_event_flags & NGX_USE_LEVEL_EVENT) && ev->active) {
        event = ev->write ? NGX_WRITE_EVENT : NGX_READ_EVENT;
        if (ngx_del_event(ev, event, 0) != NGX_OK) {
            ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
    }
    if (n > 0) {
        return;
    }
    if (n == -1) {
        if (err == NGX_EAGAIN) {
            return;
        }
        ev->error = 1;
    } else { /* n == 0 */
        err = 0;
    }

    ev->eof = 1;
    c->error = 1;
    if (!u->cacheable && u->peer.connection) {
        ngx_log_error(NGX_LOG_INFO, ev->log, err,  "client closed prematurely connection, so upstream connection is closed too");
        ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_CLIENT_CLOSED_REQUEST);
        return;
    }
    ngx_log_error(NGX_LOG_INFO, ev->log, err, "client closed prematurely connection");
    if (u->peer.connection == NULL) {
        ngx_http_upstream_finalize_request(r, u, NGX_HTTP_CLIENT_CLOSED_REQUEST);
    }
}


static void ngx_http_upstream_connect(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//µ÷ÓÃsocket,connectÁ¬½ÓÒ»¸öºó¶ËµÄpeer,È»ºóÉèÖÃ¶ÁĞ´ÊÂ¼ş»Øµ÷º¯Êı£¬½øÈë·¢ËÍÊı¾İµÄngx_http_upstream_send_requestÀïÃæ
//ÕâÀï¸ºÔğÁ¬½Óºó¶Ë·şÎñ£¬È»ºóÉèÖÃ¸÷¸ö¶ÁĞ´ÊÂ¼ş»Øµ÷¡£×îºóÈç¹ûÁ¬½Ó½¨Á¢³É¹¦£¬»áµ÷ÓÃngx_http_upstream_send_request½øĞĞÊı¾İ·¢ËÍ¡£
    ngx_int_t          rc;
    ngx_time_t        *tp;
    ngx_connection_t  *c;

    r->connection->log->action = "connecting to upstream";
    r->connection->single_connection = 0;
    if (u->state && u->state->response_sec) {
        tp = ngx_timeofday();//»ñÈ¡»º´æµÄÊ±¼ä
        u->state->response_sec = tp->sec - u->state->response_sec;//¼ÇÂ¼Ê±¼ä×´Ì¬¡£
        u->state->response_msec = tp->msec - u->state->response_msec;
    }
	//¸üĞÂÒ»ÏÂ×´Ì¬Êı¾İ
    u->state = ngx_array_push(r->upstream_states);//Ôö¼ÓÒ»¸öÉÏÓÎÄ£¿éµÄ×´Ì¬
    if (u->state == NULL) {
        ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    ngx_memzero(u->state, sizeof(ngx_http_upstream_state_t));
    tp = ngx_timeofday();
    u->state->response_sec = tp->sec;
    u->state->response_msec = tp->msec;
	//ÏÂÃæÁ¬½ÓºóµÄÄÇÄ£¿é£¬È»ºóÉèÖÃ¶ÁĞ´»Øµ÷¡£
    rc = ngx_event_connect_peer(&u->peer);//»ñÈ¡Ò»¸öpeer£¬È»ºósocket(),connect(),add_eventÖ®×¢²áÏà¹ØÊÂ¼ş½á¹¹
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http upstream connect: %i", rc);
    if (rc == NGX_ERROR) {
        ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    u->state->peer = u->peer.name;
    if (rc == NGX_BUSY) {//Èç¹ûÕâ¸öpeer±»ÉèÖÃÎªÃ¦Âµ×´Ì¬£¬Ôò³¢ÊÔÏÂÒ»¸ö£¬»áµİ¹é»ØÀ´µÄ¡£
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "no live upstreams");
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_NOLIVE);//³¢ÊÔ»Ö¸´Ò»Ğ©ÉÏÓÎÄ£¿é£¬È»ºóµİ¹éµ÷ÓÃngx_http_upstream_connect½øĞĞÁ¬½Ó¡£
        return;
    }

    if (rc == NGX_DECLINED) {
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);//ÊÔÊÔÆäËûµÄ¡£
        return;
    }

    /* rc == NGX_OK || rc == NGX_AGAIN */
    c = u->peer.connection;//µÃµ½Õâ¸öpeerĞÂ½¨Á¢µÄÁ¬½Ó½á¹¹¡£
    c->data = r;//¼Ç×¡ÎÒÕâ¸öÁ¬½ÓÊôÓÚÄÄ¸öÇëÇó¡£
    c->write->handler = ngx_http_upstream_handler;//ÉèÖÃÕâ¸öÁ¬½ÓµÄ¶ÁĞ´ÊÂ¼ş½á¹¹¡£ÕâÊÇÕæÕıµÄ¶ÁĞ´ÊÂ¼ş»Øµ÷¡£ÀïÃæ»áµ÷ÓÃwrite_event_handler¡£
    c->read->handler = ngx_http_upstream_handler;//Õâ¸öÊÇ¶ÁĞ´ÊÂ¼şµÄÍ³Ò»»Øµ÷º¯Êı£¬²»¹ı×Ô¼º»á¸ù¾İ¶Á»¹ÊÇĞ´µ÷ÓÃ¶ÔÓ¦µÄwrite_event_handlerµÈ

	//Ò»¸öupstreamµÄ¶ÁĞ´»Øµ÷£¬×¨ÃÅ×ö¸úupstreamÓĞ¹ØµÄÊÂÇé¡£ÉÏÃæµÄ»ù±¾¶ÁĞ´ÊÂ¼ş»Øµ÷ngx_http_upstream_handler»áµ÷ÓÃÏÂÃæµÄº¯ÊıÍê³Éupstream¶ÔÓ¦µÄÊÂÇé¡£
	u->write_event_handler = ngx_http_upstream_send_request_handler;//ÉèÖÃĞ´ÊÂ¼şµÄ´¦Àíº¯Êı¡£
    u->read_event_handler = ngx_http_upstream_process_header;//¶Á»Øµ÷

    c->sendfile &= r->connection->sendfile;
    u->output.sendfile = c->sendfile;

    c->pool = r->pool;
    c->log = r->connection->log;
    c->read->log = c->log;
    c->write->log = c->log;
    /* init or reinit the ngx_output_chain() and ngx_chain_writer() contexts */
    u->writer.out = NULL;//ÕâÊÇiÓÃÀ´¸øngx_chain_writeº¯Êı¼ÇÂ¼·¢ËÍ»º³åÇøµÄ¡£
    u->writer.last = &u->writer.out;//Ö¸Ïò×Ô¼ºµÄÍ·²¿¡£ĞÎ³ÉÑ­»·µÄ½á¹¹¡£
    u->writer.connection = c;
    u->writer.limit = 0;

    if (u->request_sent) {//Èç¹ûÊÇÒÑ¾­·¢ËÍÁËÇëÇó¡£È´»¹ĞèÒªÁ¬½Ó£¬µÃÖØĞÂ³õÊ¼»¯Ò»ÏÂ
        if (ngx_http_upstream_reinit(r, u) != NGX_OK) {
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
    }

    if (r->request_body //¿Í»§¶Ë·¢ËÍ¹ıÀ´µÄPOSTÊı¾İ´æ·ÅÔÚ´Ë,ngx_http_read_client_request_body·ÅµÄ
		&& r->request_body->buf && r->request_body->temp_file && r == r->main)
    {//request_bodyÊÇFCGI½á¹¹µÄÊı¾İ¡£
        /*
         * the r->request_body->buf can be reused for one request only,
         * the subrequests should allocate their own temporay bufs
         */
        u->output.free = ngx_alloc_chain_link(r->pool);
        if (u->output.free == NULL) {
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }//´ı»áĞèÒªÊÍ·ÅµÄ¶«Î÷¡£
        //Ö¸ÏòÇëÇóµÄBODYÊı¾İ²¿·Ö£¬É¶ÒâË¼?°ÑÊı¾İ·Åµ½output±äÁ¿ÀïÃæ£¬´ı»áµ½send_requestÀïÃæ»á¿½±´µ½Êä³öÁ´±í½øĞĞ·¢ËÍ
        u->output.free->buf = r->request_body->buf;
        u->output.free->next = NULL;
        u->output.allocated = 1;
		//Çå¿ÕÕâ¿éÄÚ´æ£¬¸ÉÂïÄØ?ÒòÎªÇëÇóµÄFCGIÊı¾İÒÑ¾­¿½±´µ½ÁËngx_http_upstream_sµÄrequest_bufsÁ´½Ó±íÀïÃæ
        r->request_body->buf->pos = r->request_body->buf->start;
        r->request_body->buf->last = r->request_body->buf->start;
        r->request_body->buf->tag = u->output.tag;
    }

    u->request_sent = 0;//»¹Ã»·¢ËÍÇëÇóÌåÄØ¡£
    if (rc == NGX_AGAIN) {//Èç¹û¸Õ²ÅµÄrc±íÊ¾Á¬½ÓÉĞÎ´½¨Á¢£¬ÔòÉèÖÃÁ¬½Ó³¬Ê±Ê±¼ä¡£
        ngx_add_timer(c->write, u->conf->connect_timeout);
        return;
    }
#if (NGX_HTTP_SSL)
    if (u->ssl && c->ssl == NULL) {
        ngx_http_upstream_ssl_init_connection(r, u, c);
        return;
    }
#endif
    ngx_http_upstream_send_request(r, u);//ÒÑ¾­Á¬½Ó³É¹¦ºó¶Ë£¬ÏÂÃæ½øĞĞÊı¾İ·¢ËÍ¡£
}


#if (NGX_HTTP_SSL)

static void
ngx_http_upstream_ssl_init_connection(ngx_http_request_t *r,
    ngx_http_upstream_t *u, ngx_connection_t *c)
{
    ngx_int_t   rc;

    if (ngx_ssl_create_connection(u->conf->ssl, c,
                                  NGX_SSL_BUFFER|NGX_SSL_CLIENT)
        != NGX_OK)
    {
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    c->sendfile = 0;
    u->output.sendfile = 0;

    if (u->conf->ssl_session_reuse) {
        if (u->peer.set_session(&u->peer, u->peer.data) != NGX_OK) {
            ngx_http_upstream_finalize_request(r, u,
                                               NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
    }

    r->connection->log->action = "SSL handshaking to upstream";

    rc = ngx_ssl_handshake(c);

    if (rc == NGX_AGAIN) {
        c->ssl->handler = ngx_http_upstream_ssl_handshake;
        return;
    }

    ngx_http_upstream_ssl_handshake(c);
}


static void
ngx_http_upstream_ssl_handshake(ngx_connection_t *c)
{
    ngx_http_request_t   *r;
    ngx_http_upstream_t  *u;

    r = c->data;
    u = r->upstream;

    if (c->ssl->handshaked) {

        if (u->conf->ssl_session_reuse) {
            u->peer.save_session(&u->peer, u->peer.data);
        }

        c->write->handler = ngx_http_upstream_handler;
        c->read->handler = ngx_http_upstream_handler;

        ngx_http_upstream_send_request(r, u);

        return;
    }

    ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);

}

#endif


static ngx_int_t
ngx_http_upstream_reinit(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_chain_t  *cl;

    if (u->reinit_request(r) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_memzero(&u->headers_in, sizeof(ngx_http_upstream_headers_in_t));

    if (ngx_list_init(&u->headers_in.headers, r->pool, 8,
                      sizeof(ngx_table_elt_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    /* reinit the request chain */

    for (cl = u->request_bufs; cl; cl = cl->next) {
        cl->buf->pos = cl->buf->start;
        cl->buf->file_pos = 0;
    }

    /* reinit the subrequest's ngx_output_chain() context */

    if (r->request_body && r->request_body->temp_file
        && r != r->main && u->output.buf)
    {
        u->output.free = ngx_alloc_chain_link(r->pool);
        if (u->output.free == NULL) {
            return NGX_ERROR;
        }

        u->output.free->buf = u->output.buf;
        u->output.free->next = NULL;

        u->output.buf->pos = u->output.buf->start;
        u->output.buf->last = u->output.buf->start;
    }

    u->output.buf = NULL;
    u->output.in = NULL;
    u->output.busy = NULL;

    /* reinit u->buffer */

    u->buffer.pos = u->buffer.start;

#if (NGX_HTTP_CACHE)

    if (r->cache) {
        u->buffer.pos += r->cache->header_start;
    }

#endif

    u->buffer.last = u->buffer.pos;

    return NGX_OK;
}


static void
ngx_http_upstream_send_request(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//µ÷ÓÃÊä³öµÄ¹ıÂËÆ÷£¬·¢ËÍÊı¾İµ½ºó¶Ë
    ngx_int_t          rc;
    ngx_connection_t  *c;
    c = u->peer.connection;//ÄÃµ½Õâ¸öpeerµÄÁ¬½Ó
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http upstream send request");
	//²âÊÔÒ»¸öÁ¬½Ó×´Ì¬£¬Èç¹ûÁ¬½ÓËğ»µ£¬ÔòÖØÊÔ
    if (!u->request_sent && ngx_http_upstream_test_connect(c) != NGX_OK) {
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);
        return;
    }
    c->log->action = "sending request to upstream";
	//ÏÂÃæ¿ªÊ¼¹ıÂËÄ£¿éµÄ¹ı³Ì¡£¶ÔÇëÇóµÄFCGIÊı¾İ½øĞĞ¹ıÂË£¬ÀïÃæ»áµ÷ÓÃngx_chain_writer£¬½«Êı¾İÓÃwritev·¢ËÍ³öÈ¥
	//ngx_http_upstream_connect½«¿Í»§¶Ë·¢ËÍµÄÊı¾İ¿½±´µ½ÕâÀï£¬Èç¹ûÊÇ´Ó¶ÁĞ´ÊÂ¼ş»Øµ÷½øÈëµÄ£¬ÔòÕâÀïµÄrequest_sentÓ¦¸ÃÎª1£¬
	//±íÊ¾Êı¾İÒÑ¾­¿½±´µ½Êä³öÁ´ÁË¡£Õâ·İÊı¾İÊÇÔÚngx_http_upstream_init_requestÀïÃæµ÷ÓÃ´¦ÀíÄ£¿é±ÈÈçFCGIµÄcreate_request´¦ÀíµÄ£¬½âÎöÎªFCGIµÄ½á¹¹Êı¾İ¡£
    rc = ngx_output_chain(&u->output, u->request_sent ? NULL : u->request_bufs);
    u->request_sent = 1;//±êÖ¾Î»Êı¾İÒÑ¾­·¢ËÍÍê±Ï,Ö¸µÄÊÇ·ÅÈëÊä³öÁĞ±íÀïÃæ£¬²»Ò»¶¨·¢ËÍ³öÈ¥ÁË¡£

    if (rc == NGX_ERROR) {//Èç¹û³ö´í£¬¼ÌĞø
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);
        return;
    }
    if (c->write->timer_set) {//ÒÑ¾­²»ĞèÒªĞ´Êı¾İÁË¡£
        ngx_del_timer(c->write);
    }

    if (rc == NGX_AGAIN) {//Êı¾İ»¹Ã»ÓĞ·¢ËÍÍê±Ï£¬´ı»á»¹ĞèÒª·¢ËÍ¡£
        ngx_add_timer(c->write, u->conf->send_timeout);
        if (ngx_handle_write_event(c->write, u->conf->send_lowat) != NGX_OK) {//×¢²áÒ»ÏÂ¶ÁĞ´ÊÂ¼ş¡£
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        return;
    }
    /* rc == NGX_OK */
    if (c->tcp_nopush == NGX_TCP_NOPUSH_SET) {
        if (ngx_tcp_push(c->fd) == NGX_ERROR) {//ÉèÖÃPUSH±êÖ¾Î»£¬¾¡¿ì·¢ËÍÊı¾İ¡£
            ngx_log_error(NGX_LOG_CRIT, c->log, ngx_socket_errno, ngx_tcp_push_n " failed");
            ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        c->tcp_nopush = NGX_TCP_NOPUSH_UNSET;
    }
    ngx_add_timer(c->read, u->conf->read_timeout);//Õâ»ØÊı¾İÒÑ¾­·¢ËÍÁË£¬¿ÉÒÔ×¼±¸½ÓÊÕÁË£¬ÉèÖÃ½ÓÊÕ³¬Ê±¶¨Ê±Æ÷¡£
#if 1
    if (c->read->ready) {//Èç¹û¶ÁÒÑ¾­readyÁË£¬ÄÇÃ´£¬Äã¶®µÄ£¬È¥¶Á¸öÍ·°¡
        /* post aio operation */
        /* TODO comment
         * although we can post aio operation just in the end
         * of ngx_http_upstream_connect() CHECK IT !!!
         * it's better to do here because we postpone header buffer allocation
         */
        ngx_http_upstream_process_header(r, u);///´¦ÀíÉÏÓÎ·¢ËÍµÄÏìÓ¦Í·¡£
        return;
    }
#endif
    u->write_event_handler = ngx_http_upstream_dummy_handler;//²»ÓÃĞ´ÁË£¬Ö»ĞèÒª¶Á
    if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
	//ÏÂÃæ£¬ÓÉÓÚÕâ¸ö¾ä±ú»¹Ã»Êı¾İ¿ÉÒÔ¶ÁÈ¡£¬ÎÒÃÇ¿ÉÒÔÏÈ»ØÈ¥¸ÉÆäËûÊÂÇéÁË£¬ÒòÎªÔÚngx_http_upstream_connectÀïÃæÒÑ¾­ÉèÖÃÁË¶ÁÊı¾İµÄ»Øµ÷º¯ÊıÁËµÄ¡£
}


static void
ngx_http_upstream_send_request_handler(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//Èç¹û¾ä±ú¿ÉÒÔĞ´ÁË£¬Ôòµ÷ÓÃÕâÀï×¼±¸·¢ËÍÊı¾İµ½ºó¶ËÈ¥¡£´Ë´¦Ö»´¦ÀíÒ»ÏÂĞ´³¬Ê±µÈ¡£
    ngx_connection_t  *c;

    c = u->peer.connection;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http upstream send request handler");

    if (c->write->timedout) {
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_TIMEOUT);
        return;
    }

#if (NGX_HTTP_SSL)
    if (u->ssl && c->ssl == NULL) {
        ngx_http_upstream_ssl_init_connection(r, u, c);
        return;
    }
#endif
    if (u->header_sent) {
        u->write_event_handler = ngx_http_upstream_dummy_handler;
        (void) ngx_handle_write_event(c->write, 0);
        return;
    }
    ngx_http_upstream_send_request(r, u);//·¢ËÍÊı¾İµ½ºó¶Ë
}


static void ngx_http_upstream_process_header(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//¶ÁÈ¡FCGIÍ·²¿Êı¾İ£¬»òÕßproxyÍ·²¿Êı¾İ¡£ngx_http_upstream_send_request·¢ËÍÍêÊı¾İºó£¬
//»áµ÷ÓÃÕâÀï£¬»òÕßÓĞ¿ÉĞ´ÊÂ¼şµÄÊ±ºò»áµ÷ÓÃÕâÀï¡£
    ssize_t            n;
    ngx_int_t          rc;
    ngx_connection_t  *c;
    c = u->peer.connection;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http upstream process header");
    c->log->action = "reading response header from upstream";
    if (c->read->timedout) {//¶Á³¬Ê±ÁË£¬ÂÖÑ¯ÏÂÒ»¸ö¡£´íÎóĞÅÏ¢Ó¦¸ÃÒÑ¾­´òÓ¡ÁË
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_TIMEOUT);
        return;
    }
	//ÎÒÒÑ·¢ËÍÇëÇó£¬µ«Á¬½Ó³öÎÊÌâÁË
    if (!u->request_sent && ngx_http_upstream_test_connect(c) != NGX_OK) {
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);
        return;
    }
    if (u->buffer.start == NULL) {//·ÖÅäÒ»¿é»º´æ£¬ÓÃÀ´´æ·Å½ÓÊÜ»ØÀ´µÄÊı¾İ¡£
        u->buffer.start = ngx_palloc(r->pool, u->conf->buffer_size);
        if (u->buffer.start == NULL) {
            ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
        u->buffer.pos = u->buffer.start;
        u->buffer.last = u->buffer.start;
        u->buffer.end = u->buffer.start + u->conf->buffer_size;
        u->buffer.temporary = 1;
        u->buffer.tag = u->output.tag;
		//³õÊ¼»¯headers_in´æ·ÅÍ·²¿ĞÅÏ¢£¬ºó¶ËFCGI,proxy½âÎöºóµÄHTTPÍ·²¿½«·ÅÈëÕâÀï
        if (ngx_list_init(&u->headers_in.headers, r->pool, 8, sizeof(ngx_table_elt_t))
            != NGX_OK){
            ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
            return;
        }
#if (NGX_HTTP_CACHE)
        if (r->cache) {
            u->buffer.pos += r->cache->header_start;
            u->buffer.last = u->buffer.pos;
        }
#endif
    }

    for ( ;; ) {//²»¶Ïµ÷recv¶ÁÈ¡Êı¾İ£¬Èç¹ûÃ»ÓĞÁË£¬¾ÍÏÈ·µ»Ø
    //recv Îª ngx_unix_recv£¬¶ÁÈ¡Êı¾İ·ÅÔÚu->buffer.lastµÄÎ»ÖÃ£¬·µ»Ø¶Áµ½µÄ´óĞ¡¡£
        n = c->recv(c, u->buffer.last, u->buffer.end - u->buffer.last);
        if (n == NGX_AGAIN) {//»¹Ã»ÓĞ¶ÁÍê£¬»¹ĞèÒª¹Ø×¢Õâ¸öÊÂÇé¡£
            if (ngx_handle_read_event(c->read, 0) != NGX_OK) {//²ÎÊıÎª0£¬ÄÇ¾Í²»±ä£¬±£³ÖÔ­Ñù
                ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return;
            }
            return;
        }
        if (n == 0) {
            ngx_log_error(NGX_LOG_ERR, c->log, 0,  "upstream prematurely closed connection");
        }
        if (n == NGX_ERROR || n == 0) {//Ê§°ÜÖØÊÔ
            ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_ERROR);
            return;
        }
        u->buffer.last += n;//³É¹¦£¬ÒÆ¶¯¶ÁÈ¡µ½µÄ×Ö½ÚÊıÖ¸µ½×îºóÃæ
        rc = u->process_header(r);//ngx_http_fastcgi_process_headerµÈ£¬½øĞĞÊı¾İ´¦Àí£¬±ÈÈçºó¶Ë·µ»ØµÄÊı¾İÍ·²¿½âÎö£¬body¶ÁÈ¡µÈ¡£
        //×¢ÒâÕâ¸öº¯ÊıÖ´ĞĞÍê³Éºó£¬BODY²»Ò»¶¨È«²¿¶ÁÈ¡³É¹¦ÁË¡£Õâ¸öº¯ÊıÀàËÆ²å¼ş£¬ÓĞFCGI£¬proxy²å¼ş£¬½«ÆäFCGIµÄ°üÊı¾İ½âÎö£¬
        //½âÎö³öHTTPÍ·²¿£¬·ÅÈëheaders_inÀïÃæ£¬µ±Í·²¿½âÎöÅöµ½\r\n\r\nµÄÊ±ºò£¬Ò²¾ÍÊÇ¿ÕĞĞ£¬½áÊø·µ»Ø£¬ÔİÊ±²»¶ÁÈ¡BODYÁË¡£
        //ÒòÎªÎÒÃÇ±ØĞë´¦ÀíÍ·²¿²ÅÖªµÀµ½µ×ÓĞ¶àÉÙBODY£¬»¹ÓĞÃ»ÓĞFCGI_STDOUT¡£
        if (rc == NGX_AGAIN) {
            if (u->buffer.pos == u->buffer.end) {
                ngx_log_error(NGX_LOG_ERR, c->log, 0,"upstream sent too big header");
                ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_INVALID_HEADER);
                return;
            }
            continue;//¼ÌĞø¡£ÇëÇóµÄHTTP£¬FCGIÍ·²¿Ã»ÓĞ´¦ÀíÍê±Ï¡£
        }
        break;//µ½ÕâÀïËµÃ÷ÇëÇóµÄÍ·²¿ÒÑ¾­½âÎöÍê±ÏÁË¡£ÏÂÃæÖ»Ê£ÏÂbodyÁË£¬BODY²»¼±
    }
    if (rc == NGX_HTTP_UPSTREAM_INVALID_HEADER) {//Í·²¿¸ñÊ½´íÎó¡£³¢ÊÔÏÂÒ»¸ö·şÎñÆ÷¡£
        ngx_http_upstream_next(r, u, NGX_HTTP_UPSTREAM_FT_INVALID_HEADER);
        return;
    }
    if (rc == NGX_ERROR) {//³ö´í£¬½áÊøÇëÇó¡£
        ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    /* rc == NGX_OK */
    if (u->headers_in.status_n > NGX_HTTP_SPECIAL_RESPONSE) {//Èç¹û×´Ì¬Âë´óÓÚ300
        if (r->subrequest_in_memory) {
            u->buffer.last = u->buffer.pos;
        }
        if (ngx_http_upstream_test_next(r, u) == NGX_OK) {
            return;
        }
        if (ngx_http_upstream_intercept_errors(r, u) == NGX_OK) {
            return;
        }
    }
	//µ½ÕâÀï£¬FCGIµÈ¸ñÊ½µÄÊı¾İÒÑ¾­½âÎöÎª±ê×¼HTTPµÄ±íÊ¾ĞÎÊ½ÁË(³ıÁËBODY)£¬ËùÒÔ¿ÉÒÔ½øĞĞupstreamµÄprocess_headers¡£
	//ÉÏÃæµÄ u->process_header(r)ÒÑ¾­½øĞĞFCGIµÈ¸ñÊ½µÄ½âÎöÁË¡£ÏÂÃæ½«Í·²¿Êı¾İ¿½±´µ½headers_out.headersÊı×éÖĞ¡£
    if (ngx_http_upstream_process_headers(r, u) != NGX_OK) {
        return;//½âÎöÇëÇóµÄÍ·²¿×Ö¶Î¡£Ã¿ĞĞHEADER»Øµ÷Æäcopy_handler£¬È»ºó¿½±´Ò»ÏÂ×´Ì¬ÂëµÈ¡£
    }
    if (!r->subrequest_in_memory) {//Èç¹ûÃ»ÓĞ×ÓÇëÇóÁË£¬ÄÇ¾ÍÖ±½Ó·¢ËÍÏìÓ¦¸ø¿Í»§¶Ë°É¡£
        ngx_http_upstream_send_response(r, u);//¸ø¿Í»§¶Ë·¢ËÍÏìÓ¦£¬ÀïÃæ»á´¦Àíheader,body·Ö¿ª·¢ËÍµÄÇé¿öµÄ
        return;
    }
	//Èç¹û»¹ÓĞ×ÓÇëÇóµÄ»°¡£×ÓÇëÇó²»ÊÇ±ê×¼HTTP¡£
    /* subrequest content in memory */
    if (u->input_filter == NULL) {
        u->input_filter_init = ngx_http_upstream_non_buffered_filter_init;
        u->input_filter = ngx_http_upstream_non_buffered_filter;
        u->input_filter_ctx = r;
    }
    if (u->input_filter_init(u->input_filter_ctx) == NGX_ERROR) {
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }
    n = u->buffer.last - u->buffer.pos;
    if (n) {
        u->buffer.last -= n;
        u->state->response_length += n;
        if (u->input_filter(u->input_filter_ctx, n) == NGX_ERROR) {
            ngx_http_upstream_finalize_request(r, u, NGX_ERROR);
            return;
        }
        if (u->length == 0) {
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
    }
    u->read_event_handler = ngx_http_upstream_process_body_in_memory;//ÉèÖÃbody²¿·ÖµÄ¶ÁÊÂ¼ş»Øµ÷¡£
    ngx_http_upstream_process_body_in_memory(r, u);
}


static ngx_int_t
ngx_http_upstream_test_next(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_uint_t                 status;
    ngx_http_upstream_next_t  *un;

    status = u->headers_in.status_n;

    for (un = ngx_http_upstream_next_errors; un->status; un++) {

        if (status != un->status) {
            continue;
        }

        if (u->peer.tries > 1 && (u->conf->next_upstream & un->mask)) {
            ngx_http_upstream_next(r, u, un->mask);
            return NGX_OK;
        }

#if (NGX_HTTP_CACHE)

        if (u->cache_status == NGX_HTTP_CACHE_EXPIRED
            && (u->conf->cache_use_stale & un->mask))
        {
            ngx_int_t  rc;

            rc = u->reinit_request(r);

            if (rc == NGX_OK) {
                u->cache_status = NGX_HTTP_CACHE_STALE;
                rc = ngx_http_upstream_cache_send(r, u);
            }

            ngx_http_upstream_finalize_request(r, u, rc);
            return NGX_OK;
        }

#endif
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_upstream_intercept_errors(ngx_http_request_t *r,
    ngx_http_upstream_t *u)
{
    ngx_int_t                  status;
    ngx_uint_t                 i;
    ngx_table_elt_t           *h;
    ngx_http_err_page_t       *err_page;
    ngx_http_core_loc_conf_t  *clcf;

    status = u->headers_in.status_n;

    if (status == NGX_HTTP_NOT_FOUND && u->conf->intercept_404) {
        ngx_http_upstream_finalize_request(r, u, NGX_HTTP_NOT_FOUND);
        return NGX_OK;
    }

    if (!u->conf->intercept_errors) {
        return NGX_DECLINED;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (clcf->error_pages == NULL) {
        return NGX_DECLINED;
    }

    err_page = clcf->error_pages->elts;
    for (i = 0; i < clcf->error_pages->nelts; i++) {

        if (err_page[i].status == status) {

            if (status == NGX_HTTP_UNAUTHORIZED
                && u->headers_in.www_authenticate)
            {
                h = ngx_list_push(&r->headers_out.headers);

                if (h == NULL) {
                    ngx_http_upstream_finalize_request(r, u,
                                               NGX_HTTP_INTERNAL_SERVER_ERROR);
                    return NGX_OK;
                }

                *h = *u->headers_in.www_authenticate;

                r->headers_out.www_authenticate = h;
            }

#if (NGX_HTTP_CACHE)

            if (r->cache) {
                time_t  valid;

                valid = ngx_http_file_cache_valid(u->conf->cache_valid, status);

                if (valid) {
                    r->cache->valid_sec = ngx_time() + valid;
                    r->cache->error = status;
                }

                ngx_http_file_cache_free(r->cache, u->pipe->temp_file);
            }
#endif
            ngx_http_upstream_finalize_request(r, u, status);

            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_upstream_test_connect(ngx_connection_t *c)
{
    int        err;
    socklen_t  len;

#if (NGX_HAVE_KQUEUE)
    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT)  {
        if (c->write->pending_eof) {
            c->log->action = "connecting to upstream";
            (void) ngx_connection_error(c, c->write->kq_errno,
                                    "kevent() reported that connect() failed");
            return NGX_ERROR;
        }
    } else
#endif
    {
        err = 0;
        len = sizeof(int);
        /*
         * BSDs and Linux return 0 and set a pending error in err
         * Solaris returns -1 and sets errno
         */
        if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, (void *) &err, &len)
            == -1)
        {
            err = ngx_errno;
        }

        if (err) {
            c->log->action = "connecting to upstream";
            (void) ngx_connection_error(c, err, "connect() failed");
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_headers(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//½âÎöÇëÇóµÄÍ·²¿×Ö¶Î¡£Ã¿ĞĞHEADER»Øµ÷Æäcopy_handler£¬È»ºó¿½±´Ò»ÏÂ×´Ì¬ÂëµÈ¡£¿½±´Í·²¿×Ö¶Îµ½headers_out
    ngx_str_t                      *uri, args;
    ngx_uint_t                      i, flags;
    ngx_list_part_t                *part;
    ngx_table_elt_t                *h;
    ngx_http_upstream_header_t     *hh;
    ngx_http_upstream_main_conf_t  *umcf;

    umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);
    if (u->headers_in.x_accel_redirect && !(u->conf->ignore_headers & NGX_HTTP_UPSTREAM_IGN_XA_REDIRECT)) {
//Èç¹ûÍ·²¿ÖĞÊ¹ÓÃÁËX-Accel-RedirectÌØĞÔ£¬Ò²¾ÍÊÇÏÂÔØÎÄ¼şµÄÌØĞÔ£¬ÔòÔÚÕâÀï½øĞĞÎÄ¼şÏÂÔØ¡££¬ÖØ¶¨Ïò¡£
        ngx_http_upstream_finalize_request(r, u, NGX_DECLINED);
        part = &u->headers_in.headers.part;
        h = part->elts;
        for (i = 0; /* void */; i++) {
            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }
                part = part->next;
                h = part->elts;
                i = 0;
            }
            hh = ngx_hash_find(&umcf->headers_in_hash, h[i].hash, h[i].lowcase_key, h[i].key.len);
            if (hh && hh->redirect) {
                if (hh->copy_handler(r, &h[i], hh->conf) != NGX_OK) {
                    ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
                    return NGX_DONE;
                }
            }
        }
        uri = &u->headers_in.x_accel_redirect->value;
        ngx_str_null(&args);
        flags = NGX_HTTP_LOG_UNSAFE;
        if (ngx_http_parse_unsafe_uri(r, uri, &args, &flags) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_HTTP_NOT_FOUND);
            return NGX_DONE;
        }
        if (r->method != NGX_HTTP_HEAD) {
            r->method = NGX_HTTP_GET;
        }
        r->valid_unparsed_uri = 0;
        ngx_http_internal_redirect(r, uri, &args);//Ê¹ÓÃÄÚ²¿ÖØ¶¨Ïò£¬ÇÉÃîµÄÏÂÔØ¡£ÀïÃæÓÖ»á×ßµ½¸÷ÖÖÇëÇó´¦Àí½×¶Î¡£
        ngx_http_finalize_request(r, NGX_DONE);//Íê±Ï£¬¹Ø±ÕÇëÇó¡£
        return NGX_DONE;
    }//X-Accel-Redirect½áÊø

    part = &u->headers_in.headers.part;
    h = part->elts;
//´¦ÀíÃ¿Ò»¸öÍ·²¿HEADERĞĞ£¬»Øµ÷Æä¹ÒÔØµÄ¾ä±ú
    for (i = 0; /* void */; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            h = part->elts;
            i = 0;
        }
        if (ngx_hash_find(&u->conf->hide_headers_hash, h[i].hash, h[i].lowcase_key, h[i].key.len)){
            continue;
        }
        hh = ngx_hash_find(&umcf->headers_in_hash, h[i].hash, h[i].lowcase_key, h[i].key.len);
        if (hh) {//»Øµ÷ÇëÇóÍ·µÄ¾ä±ú£¬È«²¿×¢²áÔÚÕâ¸öÊı×éÀïÃængx_http_upstream_headers_in.
            if (hh->copy_handler(r, &h[i], hh->conf) != NGX_OK) {//Ò»¸ö¸ö¿½±´µ½ÇëÇóµÄheaders_outÀïÃæ
                ngx_http_upstream_finalize_request(r, u, NGX_HTTP_INTERNAL_SERVER_ERROR);
                return NGX_DONE;
            }
            continue;
        }
		///Èç¹ûÃ»ÓĞ×¢²á¾ä±ú£¬ÄÇ¾ÍÀÏÀÏÊµÊµµÄ¿½±´Ò»ÏÂÍ·²¿¾ÍĞĞÁË¡¢
        if (ngx_http_upstream_copy_header_line(r, &h[i], 0) != NGX_OK) {//½«Í·²¿Êı¾İ¿½±´µ½headers_out.headersÊı×éÖĞ¡£
            ngx_http_upstream_finalize_request(r, u,  NGX_HTTP_INTERNAL_SERVER_ERROR);
            return NGX_DONE;
        }
    }

    if (r->headers_out.server && r->headers_out.server->value.data == NULL) {
        r->headers_out.server->hash = 0;
    }
    if (r->headers_out.date && r->headers_out.date->value.data == NULL) {
        r->headers_out.date->hash = 0;
    }
	//¿½±´×´Ì¬ĞĞ£¬ÒòÎªÕâ¸ö²»ÊÇ´æÔÚheaders_inÀïÃæµÄ¡£
    r->headers_out.status = u->headers_in.status_n;
    r->headers_out.status_line = u->headers_in.status_line;
    u->headers_in.content_length_n = r->headers_out.content_length_n;
    if (r->headers_out.content_length_n != -1) {
        u->length = (size_t) r->headers_out.content_length_n;

    } else {
        u->length = NGX_MAX_SIZE_T_VALUE;
    }
    return NGX_OK;
}


static void
ngx_http_upstream_process_body_in_memory(ngx_http_request_t *r,
    ngx_http_upstream_t *u)
{
    size_t             size;
    ssize_t            n;
    ngx_buf_t         *b;
    ngx_event_t       *rev;
    ngx_connection_t  *c;

    c = u->peer.connection;
    rev = c->read;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http upstream process body on memory");
    if (rev->timedout) {
        ngx_connection_error(c, NGX_ETIMEDOUT, "upstream timed out");
        ngx_http_upstream_finalize_request(r, u, NGX_ETIMEDOUT);
        return;
    }

    b = &u->buffer;//µÃµ½»º³åÇø
    for ( ;; ) {
        size = b->end - b->last;//»º³åÇøÌ«Ğ¡ÁË¡£Õâ¸ö»º³åÇøÊÇ°üÀ¨ËùÓĞµÄ´Óºó¶Ë·¢¹ıÀ´µÄÊı¾İµÄ¡¢
        if (size == 0) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0, "upstream buffer is too small to read response");
            ngx_http_upstream_finalize_request(r, u, NGX_ERROR);
            return;
        }
        n = c->recv(c, b->last, size);//»¹Òª¶ÁÕâÃ´¶àÊı¾İ£¬ÊÇ¶¥¶à¶ÁÕâÃ´¶àÊı¾İ°É?
        if (n == NGX_AGAIN) {//Èç¹ûÃ»ÓĞ¶ÁÈ¡Íê±ÏÄÇÃ´¶à
            break;
        }
        if (n == 0 || n == NGX_ERROR) {
            ngx_http_upstream_finalize_request(r, u, n);
            return;
        }
        u->state->response_length += n;//³¤¶È²»¶Ï¼Ó
        if (u->input_filter(u->input_filter_ctx, n) == NGX_ERROR) {
            ngx_http_upstream_finalize_request(r, u, NGX_ERROR);
            return;
        }
        if (!rev->ready) {
            break;
        }
    }

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        ngx_http_upstream_finalize_request(r, u, NGX_ERROR);
        return;
    }

    if (rev->active) {
        ngx_add_timer(rev, u->conf->read_timeout);

    } else if (rev->timer_set) {
        ngx_del_timer(rev);
    }
}


static void ngx_http_upstream_send_response(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//·¢ËÍÇëÇóÊı¾İ¸ø¿Í»§¶Ë¡£ÀïÃæ»á´¦Àíheader,body·Ö¿ª·¢ËÍµÄÇé¿öµÄ 
    int                        tcp_nodelay;
    ssize_t                    n;
    ngx_int_t                  rc;
    ngx_event_pipe_t          *p;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;
	//ÏÈ·¢header£¬ÔÙ·¢body
    rc = ngx_http_send_header(r);//µ÷ÓÃÃ¿Ò»¸öfilter¹ıÂË£¬´¦ÀíÍ·²¿Êı¾İ¡£×îºó½«Êı¾İ·¢ËÍ¸ø¿Í»§¶Ë¡£µ÷ÓÃngx_http_top_header_filter
    if (rc == NGX_ERROR || rc > NGX_OK || r->post_action) {
        ngx_http_upstream_finalize_request(r, u, rc);
        return;
    }
    c = r->connection;
    if (r->header_only) {//Èç¹ûÖ»ĞèÒª·¢ËÍÍ·²¿Êı¾İ£¬±ÈÈç¿Í»§¶ËÓÃcurl -I ·ÃÎÊµÄ¡£·µ»Ø204×´Ì¬Âë¼´¿É¡£
        if (u->cacheable || u->store) {
            if (ngx_shutdown_socket(c->fd, NGX_WRITE_SHUTDOWN) == -1) {//¹Ø±ÕTCPµÄĞ´Êı¾İÁ÷
                ngx_connection_error(c, ngx_socket_errno, ngx_shutdown_socket_n " failed");
            }
            r->read_event_handler = ngx_http_request_empty_handler;//ÉèÖÃ¶ÁĞ´ÊÂ¼ş»Øµ÷Îª¿Õº¯Êı£¬ÕâÑù¾Í²»»ácareÊı¾İ¶ÁĞ´ÁË¡£
            r->write_event_handler = ngx_http_request_empty_handler;
            c->error = 1;//±ê¼ÇÎª1£¬Ò»»á¾Í»á¹Ø±ÕÁ¬½ÓµÄ¡£
        } else {
            ngx_http_upstream_finalize_request(r, u, rc);
            return;
        }
    }
    u->header_sent = 1;//±ê¼ÇÒÑ¾­·¢ËÍÁËÍ·²¿×Ö¶Î£¬ÖÁÉÙÊÇÒÑ¾­¹ÒÔØ³öÈ¥£¬¾­¹ıÁËfilterÁË¡£
    if (r->request_body && r->request_body->temp_file) {//É¾³ı¿Í»§¶Ë·¢ËÍµÄÊı¾İ¾ÖÌå
        ngx_pool_run_cleanup_file(r->pool, r->request_body->temp_file->file.fd);
        r->request_body->temp_file->file.fd = NGX_INVALID_FILE;
    }
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (!u->buffering) {//FCGIĞ´ËÀÎª1.ÒòÎªFCGIÊÇ°üÊ½µÄ´«Êä¡£·ÇÁ÷Ê½£¬²»ÄÜ½ÓÒ»µã£¬·¢Ò»µã¡£ÔÚngx_http_fastcgi_handlerÀïÃæÉèÖÃÎª1ÁË¡£
//bufferingÖ¸nginx »áÏÈbufferºó¶ËFCGI·¢¹ıÀ´µÄÊı¾İ£¬È»ºóÒ»´Î·¢ËÍ¸ø¿Í»§¶Ë¡£
//Ä¬ÈÏÕâ¸öÊÇ´ò¿ªµÄ¡£Ò²¾ÍÊÇnginx»ábuf×¡upstream·¢ËÍµÄÊı¾İ¡£ÕâÑùĞ§ÂÊ»á¸ü¸ß¡£
        if (u->input_filter == NULL) {//Èç¹ûinput_filterÎª¿Õ£¬ÔòÉèÖÃÄ¬ÈÏµÄfilter£¬È»ºó×¼±¸·¢ËÍÊı¾İµ½¿Í»§¶Ë¡£È»ºóÊÔ×Å¶Á¶ÁFCGI
            u->input_filter_init = ngx_http_upstream_non_buffered_filter_init;//Êµ¼ÊÉÏÎª¿Õº¯Êı
            //ngx_http_upstream_non_buffered_filter½«u->buffer.last - u->buffer.posÖ®¼äµÄÊı¾İ·Åµ½u->out_bufs·¢ËÍ»º³åÈ¥Á´±íÀïÃæ¡£
            u->input_filter = ngx_http_upstream_non_buffered_filter;//Ò»°ã¾ÍÉèÖÃÎªÕâ¸öÄ¬ÈÏµÄ£¬memcacheÎªngx_http_memcached_filter
            u->input_filter_ctx = r;
        }
		//ÉèÖÃupstreamµÄ¶ÁÊÂ¼ş»Øµ÷£¬ÉèÖÃ¿Í»§¶ËÁ¬½ÓµÄĞ´ÊÂ¼ş»Øµ÷¡£
        u->read_event_handler = ngx_http_upstream_process_non_buffered_upstream;
        r->write_event_handler = ngx_http_upstream_process_non_buffered_downstream;//µ÷ÓÃ¹ıÂËÄ£¿éÒ»¸ö¸ö¹ıÂËbody£¬×îÖÕ·¢ËÍ³öÈ¥¡£
        r->limit_rate = 0;
        if (u->input_filter_init(u->input_filter_ctx) == NGX_ERROR) {//µ÷ÓÃinput filter ³õÊ¼»¯º¯Êı£¬Ã»×öÊ²Ã´ÊÂÇé
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
        if (clcf->tcp_nodelay && c->tcp_nodelay == NGX_TCP_NODELAY_UNSET) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "tcp_nodelay");
            tcp_nodelay = 1;//´ò¿ªnodelay£¬×¼±¸½«Êı¾İÍêÈ«·¢ËÍ³öÈ¥
            if (setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, (const void *) &tcp_nodelay, sizeof(int)) == -1) {
                ngx_connection_error(c, ngx_socket_errno,  "setsockopt(TCP_NODELAY) failed");
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
            c->tcp_nodelay = NGX_TCP_NODELAY_SET;
        }
        n = u->buffer.last - u->buffer.pos;//µÃµ½½«Òª·¢ËÍµÄÊı¾İµÄ´óĞ¡£¬Ã¿´ÎÓĞ¶àÉÙ¾Í·¢ËÍ¶àÉÙ¡£²»µÈ´ıupstreamÁË
        if (n) {
            u->buffer.last = u->buffer.pos;//½«lastÖ¸ÏòÎªµ±Ç°µÄpos£¬ÄÇpost-lastÖ®Ç°µÄÊı¾İÃ»ÁË£¬²»¹ıÉÏÃæÓĞ¸ön¼Ç×ÅÁËµÄ¡£
            u->state->response_length += n;//Í³¼ÆÇëÇóµÄ·µ»ØÊı¾İ³¤¶È¡£
            //ÏÂÃæinput_filterÖ»ÊÇ¼òµ¥µÄ¿½±´bufferÉÏÃæµÄÊı¾İ×Ü¹²n³¤¶ÈµÄ£¬µ½u->out_bufsÀïÃæÈ¥£¬ÒÔ´ı·¢ËÍ¡£
            if (u->input_filter(u->input_filter_ctx, n) == NGX_ERROR) {//Ò»°ãÎªngx_http_upstream_non_buffered_filter
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
			//¿ªÊ¼·¢ËÍÊı¾İµ½downstream£¬È»ºó¶ÁÈ¡Êı¾İ£¬È»ºó·¢ËÍ£¬Èç´ËÑ­»·£¬ÖªµÀ²»¿É¶Á/²»¿ÉĞ´
            ngx_http_upstream_process_non_buffered_downstream(r);
        } else {//Ã»ÓĞÊı¾İ£¬³¤¶ÈÎª0£¬²»ĞèÒª·¢ËÍÁË°É¡£²»£¬Òªflush
            u->buffer.pos = u->buffer.start;
            u->buffer.last = u->buffer.start;
            if (ngx_http_send_special(r, NGX_HTTP_FLUSH) == NGX_ERROR) {
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
            if (u->peer.connection->read->ready) {//Èç¹ûºó¶ËFCGI¿É¶Á£¬Ôò¼ÌĞø¶ÁÈ¡upstreamµÄÊı¾İ.È»ºó·¢ËÍ
                ngx_http_upstream_process_non_buffered_upstream(r, u);
            }
        }
        return;
    }//!u->buffering½áÊø
//ÏÂÃæ¾ÍÊÇÒª½øĞĞºó¶ËÊı¾İ»º´æ´¦ÀíµÄ¹ı³ÌÁË¡£Ò²¾ÍÊÇÊ¹ÓÃÁËbuffering±ê¼ÇµÄÌõ¼şÏÂ
    /* TODO: preallocate event_pipe bufs, look "Content-Length" */
#if (NGX_HTTP_CACHE)//ÏÈ²»¹Ücache
    if (r->cache && r->cache->file.fd != NGX_INVALID_FILE) {
        ngx_pool_run_cleanup_file(r->pool, r->cache->file.fd);
        r->cache->file.fd = NGX_INVALID_FILE;
    }
    switch (ngx_http_test_predicates(r, u->conf->no_cache)) {
    case NGX_ERROR:
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    case NGX_DECLINED:
        u->cacheable = 0;
        break;
    default: /* NGX_OK */
        if (u->cache_status == NGX_HTTP_CACHE_BYPASS) {

            if (ngx_http_file_cache_new(r) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
            if (u->create_key(r) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
            /* TODO: add keys */
            r->cache->min_uses = u->conf->cache_min_uses;
            r->cache->body_start = u->conf->buffer_size;
            r->cache->file_cache = u->conf->cache->data;
            if (ngx_http_file_cache_create(r) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
            u->cacheable = 1;
        }
        break;
    }
    if (u->cacheable) {
        time_t  now, valid;
        now = ngx_time();
        valid = r->cache->valid_sec;
        if (valid == 0) {
            valid = ngx_http_file_cache_valid(u->conf->cache_valid, u->headers_in.status_n);
            if (valid) {
                r->cache->valid_sec = now + valid;
            }
        }
        if (valid) {
            r->cache->last_modified = r->headers_out.last_modified_time;
            r->cache->date = now;
            r->cache->body_start = (u_short) (u->buffer.pos - u->buffer.start);
            ngx_http_file_cache_set_header(r, u->buffer.start);
        } else {
            u->cacheable = 0;
            r->headers_out.last_modified_time = -1;
        }
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http cacheable: %d", u->cacheable);
    if (u->cacheable == 0 && r->cache) {
        ngx_http_file_cache_free(r->cache, u->pipe->temp_file);
    }
#endif //»º´æÎÄ¼ş´¦ÀíÍê³É¡£
//ÏÂÃæ½øÈëevent_pipe¹ı³Ì£¬pipe==Ë®±Ã£¬beng¡¤¡¤¡¤
    p = u->pipe;
    p->output_filter = (ngx_event_pipe_output_filter_pt) ngx_http_output_filter;//ÉèÖÃfilter£¬¿ÉÒÔ¿´µ½¾ÍÊÇhttpµÄÊä³öfilter
    p->output_ctx = r;
    p->tag = u->output.tag;
    p->bufs = u->conf->bufs;//ÉèÖÃbufs£¬Ëü¾ÍÊÇupstreamÖĞÉèÖÃµÄbufs.u == &flcf->upstream;
    p->busy_size = u->conf->busy_buffers_size;
    p->upstream = u->peer.connection;//¸³Öµ¸úºó¶ËupstreamµÄÁ¬½Ó¡£
    p->downstream = c;//¸³Öµ¸ú¿Í»§¶ËµÄÁ¬½Ó¡£
    p->pool = r->pool;
    p->log = c->log;

    p->cacheable = u->cacheable || u->store;
    p->temp_file = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
    if (p->temp_file == NULL) {
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    }
    p->temp_file->file.fd = NGX_INVALID_FILE;
    p->temp_file->file.log = c->log;
    p->temp_file->path = u->conf->temp_path;
    p->temp_file->pool = r->pool;
    if (p->cacheable) {
        p->temp_file->persistent = 1;
    } else {
        p->temp_file->log_level = NGX_LOG_WARN;
        p->temp_file->warn = "an upstream response is buffered to a temporary file";
    }
    p->max_temp_file_size = u->conf->max_temp_file_size;
    p->temp_file_write_size = u->conf->temp_file_write_size;

	//ÏÂÃæÉêÇëÒ»¸ö»º³åÁ´½Ó½Úµã£¬À´´æ´¢¸Õ²ÅÎÒÃÇÔÙ¶ÁÈ¡fcgiµÄ°ü£¬ÎªÁËµÃµ½HTTP headersµÄÊ±ºò²»Ğ¡ĞÄ¶à¶ÁÈ¡µ½µÄÊı¾İ¡£
	//ÆäÊµÖ»ÒªFCGI·¢¸øºó¶ËµÄ°üÖĞ£¬ÓĞÒ»¸ö°üµÄÇ°°ë²¿·ÖÊÇheader,ºóÒ»²¿·ÖÊÇbody£¬¾Í»áÓĞÔ¤¶ÁÊı¾İ¡£
    p->preread_bufs = ngx_alloc_chain_link(r->pool);
    if (p->preread_bufs == NULL) {
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    }
    p->preread_bufs->buf = &u->buffer;
    p->preread_bufs->next = NULL;
    u->buffer.recycled = 1;
    p->preread_size = u->buffer.last - u->buffer.pos;
    if (u->cacheable) {
        p->buf_to_file = ngx_calloc_buf(r->pool);
        if (p->buf_to_file == NULL) {
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
        p->buf_to_file->pos = u->buffer.start;
        p->buf_to_file->last = u->buffer.pos;
        p->buf_to_file->temporary = 1;
    }
    if (ngx_event_flags & NGX_USE_AIO_EVENT) {
        /* the posted aio operation may currupt a shadow buffer */
        p->single_buf = 1;
    }
    /* TODO: p->free_bufs = 0 if use ngx_create_chain_of_bufs() */
    p->free_bufs = 1;
    /*
     * event_pipe would do u->buffer.last += p->preread_size
     * as though these bytes were read
     */
    u->buffer.last = u->buffer.pos;
    if (u->conf->cyclic_temp_file) {
        /*
         * we need to disable the use of sendfile() if we use cyclic temp file
         * because the writing a new data may interfere with sendfile()
         * that uses the same kernel file pages (at least on FreeBSD)
         */
        p->cyclic_temp_file = 1;
        c->sendfile = 0;
    } else {
        p->cyclic_temp_file = 0;
    }
    p->read_timeout = u->conf->read_timeout;
    p->send_timeout = clcf->send_timeout;
    p->send_lowat = clcf->send_lowat;
	//ÏÂÃæµÄu->read***ÊÇÕâÑù±»µ÷ÓÃµÄ: c->read->handler = ngx_http_upstream_handler;ÉèÖÃÕâ¸öÁ¬½ÓÉÏµÄ¶ÁĞ´¾ä±úÊÇupstream_handler
	//u->read_event_handler = XXXX;//upstream×Ô¼º¼Ç×Åµ±Ç°×Ô¼ºÓĞÊÂ¼şÀ´µÄÊ±ºòÓ¦¸ÃÔõÃ´¶Á£¬¶ÁÊ²Ã´¡£
    u->read_event_handler = ngx_http_upstream_process_upstream;//ÉèÖÃ¶ÁÊÂ¼ş½á¹¹£¬ÊÇÓÃÀ´´¦Àí³¬Ê±£¬¹Ø±ÕÁ¬½ÓµÈÓÃµÄ¡£
    //ÏÂÃæµÄr->write***ÊÇÕâÑù±»µ÷ÓÃµÄ:c->write->handler = ngx_http_request_handler;r->write_event_handler = XXX;//
    r->write_event_handler = ngx_http_upstream_process_downstream;//ÉèÖÃ¿ÉĞ´ÊÂ¼ş½á¹¹¡£ÕâÑù¾Í¿ÉÒÔ¸ø¿Í»§¶Ë·¢ËÍÊı¾İÁË¡£
    //·¢¶¯Ò»ÏÂÊı¾İ¶ÁÈ¡°É¡£ÒÔºóÓĞÊı¾İ¿É¶ÁµÄÊ±ºòÒ²»áµ÷ÓÃÕâÀïµÄ¡£
    ngx_http_upstream_process_upstream(r, u);
}


static void
ngx_http_upstream_process_non_buffered_downstream(ngx_http_request_t *r)
{//ngx_http_upstream_send_response·¢ËÍÍêHERDERºó£¬Èç¹ûÊÇ·Ç»º³åÄ£Ê½£¬»áµ÷ÓÃÕâÀï½«Êı¾İ·¢ËÍ³öÈ¥µÄ¡£
//Õâ¸öº¯ÊıÊµ¼ÊÉÏÅĞ¶ÏÒ»ÏÂ³¬Ê±ºó£¬¾Íµ÷ÓÃngx_http_upstream_process_non_buffered_requestÁË¡£nginxÀÏ·½·¨¡£
    ngx_event_t          *wev;
    ngx_connection_t     *c;
    ngx_http_upstream_t  *u;

    c = r->connection;
    u = r->upstream;
    wev = c->write;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,"http upstream process non buffered downstream");
    c->log->action = "sending to client";
    if (wev->timedout) {
        c->timedout = 1;
        ngx_connection_error(c, NGX_ETIMEDOUT, "client timed out");
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    }
	//ÏÂÃæ¿ªÊ¼½«out_bufsÀïÃæµÄÊı¾İ·¢ËÍ³öÈ¥£¬È»ºó¶ÁÈ¡Êı¾İ£¬È»ºó·¢ËÍ£¬Èç´ËÑ­»·¡£
    ngx_http_upstream_process_non_buffered_request(r, 1);
}


static void
ngx_http_upstream_process_non_buffered_upstream(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//ngx_http_upstream_send_responseÉèÖÃºÍµ÷ÓÃÕâÀï£¬µ±ÉÏÓÎµÄPROXYÓĞÊı¾İµ½À´£¬¿ÉÒÔ¶ÁÈ¡µÄÊ±ºòµ÷ÓÃÕâÀï¡£
    ngx_connection_t  *c;

    c = u->peer.connection;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,"http upstream process non buffered upstream");
    c->log->action = "reading upstream";
    if (c->read->timedout) {
        ngx_connection_error(c, NGX_ETIMEDOUT, "upstream timed out");
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    }
	//ÕâÀï¸úngx_http_upstream_process_non_buffered_downstreamÆäÊµ¾ÍÒ»¸öÇø±ğ: ²ÎÊıÎª0£¬±íÊ¾²»ÓÃÁ¢¼´·¢ËÍÊı¾İ£¬ÒòÎªÃ»ÓĞÊı¾İ¿ÉÒÔ·¢ËÍ£¬µÃÏÈ¶ÁÈ¡²ÅĞĞ¡£
    ngx_http_upstream_process_non_buffered_request(r, 0);
}


static void
ngx_http_upstream_process_non_buffered_request(ngx_http_request_t *r, ngx_uint_t do_write)
{//µ÷ÓÃ¹ıÂËÄ£¿é£¬½«Êı¾İ·¢ËÍ³öÈ¥£¬do_writeÎªÊÇ·ñÒª¸ø¿Í»§¶Ë·¢ËÍÊı¾İ¡£
//1.Èç¹ûÒª·¢ËÍ£¬¾Íµ÷ÓÃngx_http_output_filter½«Êı¾İ·¢ËÍ³öÈ¥¡£
//2.È»ºóngx_unix_recv¶ÁÈ¡Êı¾İ£¬·ÅÈëout_bufsÀïÃæÈ¥¡£Èç´ËÑ­»·
    size_t                     size;
    ssize_t                    n;
    ngx_buf_t                 *b;
    ngx_int_t                  rc;
    ngx_connection_t          *downstream, *upstream;
    ngx_http_upstream_t       *u;
    ngx_http_core_loc_conf_t  *clcf;

    u = r->upstream;
    downstream = r->connection;//ÕÒµ½Õâ¸öÇëÇóµÄ¿Í»§¶ËÁ¬½Ó
    upstream = u->peer.connection;//ÕÒµ½ÉÏÓÎµÄÁ¬½Ó
    b = &u->buffer;//ÕÒµ½ÕâÛçÒª·¢ËÍµÄÊı¾İ£¬²»¹ı´ó²¿·Ö¶¼±»input filter·Åµ½out_bufsÀïÃæÈ¥ÁË¡£
    do_write = do_write || u->length == 0;//do_writeÎª1Ê±±íÊ¾ÒªÁ¢¼´·¢ËÍ¸ø¿Í»§¶Ë¡£
    for ( ;; ) {
        if (do_write) {//ÒªÁ¢¼´·¢ËÍ¡£
            if (u->out_bufs || u->busy_bufs) {
				//Èç¹ûu->out_bufs²»ÎªNULLÔòËµÃ÷ÓĞĞèÒª·¢ËÍµÄÊı¾İ£¬ÕâÊÇngx_http_upstream_non_buffered_filter¿½±´µ½ÕâÀïµÄ¡£
				//u->busy_bufs´ú±íÉÏ´ÎÎ´·¢ËÍÍê±ÏµÄÊı¾İ.
                rc = ngx_http_output_filter(r, u->out_bufs);//Ò»¸ö¸öµ÷ÓÃngx_http_top_body_filter¹ıÂËÄ£¿é£¬×îÖÕ·¢ËÍÊı¾İ¡£
                if (rc == NGX_ERROR) {
                    ngx_http_upstream_finalize_request(r, u, 0);
                    return;
                }
				//ÏÂÃæ½«out_bufsµÄÔªËØÒÆ¶¯µ½busy_bufsµÄºóÃæ£»½«ÒÑ¾­·¢ËÍÍê±ÏµÄbusy_bufsÁ´±íÔªËØÒÆ¶¯µ½free_bufsÀïÃæ
                ngx_chain_update_chains(&u->free_bufs, &u->busy_bufs, &u->out_bufs, u->output.tag);
            }
            if (u->busy_bufs == NULL) {//busy_bufsÃ»ÓĞÁË£¬¶¼·¢ÍêÁË¡£ÏëÒª·¢ËÍµÄÊı¾İ¶¼ÒÑ¾­·¢ËÍÍê±Ï
                if (u->length == 0 || upstream->read->eof || upstream->read->error) {
					//´ËÊ±finalize request£¬½áÊøÕâ´ÎÇëÇó
                    ngx_http_upstream_finalize_request(r, u, 0);
                    return;
                }
                b->pos = b->start;//ÖØÖÃu->buffer,ÒÔ±ãÓëÏÂ´ÎÊ¹ÓÃ£¬´Ó¿ªÊ¼Æğ
                b->last = b->start;
            }
        }//µ±Ç°»º´æÀïÃæµÄÊı¾İ·¢ËÍ¸ø¿Í»§¶Ë¸æÒ»¶ÎÂä
        size = b->end - b->last;//µÃµ½µ±Ç°bufµÄÊ£Óà¿Õ¼ä£¬ÆäÊµÈç¹ûdo_write=1£¬ºÜ¿ÉÄÜ¾ÍÎªÈ«¿ÕµÄ»º³åÇø
        if (size > u->length) {
            size = u->length;
        }
        if (size && upstream->read->ready) {//µ±Ç°µÄÕâ¿ébuffer»¹ÓĞÊ£Óà¿Õ¼ä£¬²¢ÇÒÅöÇÉ¸úUPSTREAMµÄÁ¬½ÓÊÇ¿É¶ÁµÄ£¬Ò²¾ÍÊÇFCGI·¢ËÍÁËÊı¾İ¡£
/*ÎªÊ²Ã´ÕâÀï»¹ÓĞ¿É¶ÁÊı¾İÄØ£¬²»ÊÇÒÑ¾­½Óµ½FCGIµÄ½áÊø°üÁËÂğ£¬ÒòÎªÖ®Ç°Ö»ÊÇ¶ÁÈ¡ÍêÁËHTTPÍ·²¿,Åöµ½\r\n\r\nºó¾ÍÍË³öÁË
   ÄÇÎÊÌâÀ´ÁË£¬upstreamÔõÃ´ÖªµÀ¸ÃÔõÃ´¶ÁÈ¡ÄØ£¬FCGI,PROXYÔõÃ´°ì£¬¿´µ½ÕâÀïÎÒÏëgdbÊÔÊÔ£¬ÓÚÊÇÕÒFCGIµÄbufferingÀàËÆµÄÑ¡Ïî£¬Î´¹û¡£²é¿´ÍøÉÏĞÅÏ¢:
   »ĞÈ»´óÎò£¬FCGIĞ­ÒéÊÇÎŞËùÎ½bufferingÁË£¬Ëü²»ÊÇÁ÷Ê½µÄÊı¾İ£¬¶øÊÇ°üÊ½µÄÒ»¸ö¸ö°ü£¬ËùÒÔÓÃµÄÊÇbufferingÄ£Ê½¡£
   http://www.ruby-forum.com/topic/197216
Yes. It's because of FastCGI protocol internals. It splits "stream"
into blocks max 32KB each. Each block has header info (how many bytes
it contains, etc). So nginx can't send content to the client until it
get the whole block from upstream.*/
            n = upstream->recv(upstream, b->last, size);//µ÷ÓÃngx_unix_recv¶ÁÈ¡Êı¾İµ½last³ÉÔ±£¬ºÜ¼òµ¥µÄrecv()¾ÍĞĞÁË¡£
            if (n == NGX_AGAIN) {//ÏÂ»ØÔÙÀ´£¬Õâ»ØÃ»Êı¾İÁË¡£
                break;
            }
            if (n > 0) {//¶ÁÁËÒ»Ğ©Êı¾İ£¬Á¢Âí½«Ëü·¢ËÍ³öÈ¥°É£¬Ò²¾ÍÊÇ·ÅÈëµ½out_bufsÁ´±íÀïÃæÈ¥£¬Ã»ÓĞÊµ¼Ê·¢ËÍµÄ£¬ÄÇÃ´£¬Ê²Ã´Ê±ºò·¢ËÍÄØ
                u->state->response_length += n;//ÔÙ´Îµ÷ÓÃinput_filter,ÕâÀïÃ»ÓĞreset u->buffer.last,ÕâÊÇÒòÎªÎÒÃÇÕâ¸öÖµ²¢Ã»ÓĞ¸üĞÂ.×¢ÒâÔÚÏÂÃæµÄinput_filterµÃ¸üĞÂÁË¡£
                if (u->input_filter(u->input_filter_ctx, n) == NGX_ERROR) {//¾ÍÊÇngx_http_upstream_non_buffered_filter
                    ngx_http_upstream_finalize_request(r, u, 0);
                    return;
                }
            }
            do_write = 1;//ÒòÎª¸Õ¸ÕÎŞÂÛÈçºÎn´óÓÚ0£¬ËùÒÔ¶ÁÈ¡ÁËÊı¾İ£¬ÄÇÃ´ÏÂÒ»¸öÑ­»·»á½«out_bufsµÄÊı¾İ·¢ËÍ³öÈ¥µÄ¡£
            continue;
        }
        break;
    }
	//ÏÂÃæ¾ÍÊÇ¸÷ÖÖ¶ÁĞ´ÊÂ¼ş½á¹¹
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (downstream->data == r) {
        if (ngx_handle_write_event(downstream->write, clcf->send_lowat) != NGX_OK) {
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
    }
    if (downstream->write->active && !downstream->write->ready) {
        ngx_add_timer(downstream->write, clcf->send_timeout);
    } else if (downstream->write->timer_set) {
        ngx_del_timer(downstream->write);
    }
    if (ngx_handle_read_event(upstream->read, 0) != NGX_OK) {
        ngx_http_upstream_finalize_request(r, u, 0);
        return;
    }
    if (upstream->read->active && !upstream->read->ready) {
        ngx_add_timer(upstream->read, u->conf->read_timeout);
    } else if (upstream->read->timer_set) {
        ngx_del_timer(upstream->read);
    }
}


static ngx_int_t
ngx_http_upstream_non_buffered_filter_init(void *data)
{
    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_non_buffered_filter(void *data, ssize_t bytes)
{//½«u->buffer.last - u->buffer.posÖ®¼äµÄÊı¾İ·Åµ½u->out_bufs·¢ËÍ»º³åÈ¥Á´±íÀïÃæ¡£ÕâÑù¿ÉĞ´µÄÊ±ºò¾Í»á·¢ËÍ¸ø¿Í»§¶Ë¡£
//ngx_http_upstream_process_non_buffered_requestº¯Êı»á¶ÁÈ¡out_bufsÀïÃæµÄÊı¾İ£¬È»ºóµ÷ÓÃÊä³ö¹ıÂËÁ´½Ó½øĞĞ·¢ËÍµÄ¡£
    ngx_http_request_t  *r = data;
    ngx_buf_t            *b;
    ngx_chain_t          *cl, **ll;
    ngx_http_upstream_t  *u;

    u = r->upstream;
    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {//±éÀúu->out_bufs
        ll = &cl->next;
    }
    cl = ngx_chain_get_free_buf(r->pool, &u->free_bufs);//·ÖÅäÒ»¸ö¿ÕÏĞµÄbuff
    if (cl == NULL) {
        return NGX_ERROR;
    }
    *ll = cl;//½«ĞÂÉêÇëµÄ»º´æÁ´½Ó½øÀ´¡£
    cl->buf->flush = 1;
    cl->buf->memory = 1;
    b = &u->buffer;//È¥³ı½«Òª·¢ËÍµÄÕâ¸öÊı¾İ£¬Ó¦¸ÃÊÇ¿Í»§¶ËµÄ·µ»ØÊı¾İÌå¡£½«Æä·ÅÈë
    cl->buf->pos = b->last;
    b->last += bytes;//ÍùºóÒÆ¶¯
    cl->buf->last = b->last;
    cl->buf->tag = u->output.tag;
//u->length±íÊ¾½«Òª·¢ËÍµÄÊı¾İ´óĞ¡Èç¹ûÎªNGX_MAX_SIZE_T_VALUE,ÔòËµÃ÷ºó¶ËĞ­Òé²¢Ã»ÓĞÖ¸¶¨ĞèÒª·¢ËÍµÄ´óĞ¡£¬´ËÊ±ÎÒÃÇÖ»ĞèÒª·¢ËÍÎÒÃÇ½ÓÊÕµ½µÄ.
    if (u->length == NGX_MAX_SIZE_T_VALUE) {
        return NGX_OK;
    }
    u->length -= bytes;//¸üĞÂ½«Òª·¢ËÍµÄÊı¾İ´óĞ¡
    return NGX_OK;
}


static void
ngx_http_upstream_process_downstream(ngx_http_request_t *r)
{//´¦Àí¿Í»§¶ËÁ¬½ÓµÄ¿É¶ÁÊÂ¼ş£¬ÀïÃæÖ»Òª´¥·¢ngx_event_pipe
    ngx_event_t          *wev;
    ngx_connection_t     *c;
    ngx_event_pipe_t     *p;
    ngx_http_upstream_t  *u;

    c = r->connection;
    u = r->upstream;
    p = u->pipe;
    wev = c->write;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,"http upstream process downstream");
    c->log->action = "sending to client";
    if (wev->timedout) {
        if (wev->delayed) {//¿´¿´Êı¾İÊÇ²»ÊÇÒòÎª±»ÏŞÁ÷µÈÔ­Òò¶şÑÓ³ÙÁË£¬Èç¹ûÕâÖÖÇé¿öÊÇ¿ÉÒÔÔÊĞíµÄ¡£
            wev->timedout = 0;
            wev->delayed = 0;
            if (!wev->ready) {
                ngx_add_timer(wev, p->send_timeout);
                if (ngx_handle_write_event(wev, p->send_lowat) != NGX_OK) {
                    ngx_http_upstream_finalize_request(r, u, 0);
                }
                return;
            }
			//delayÁË£¬Õı³£½øÈë´¦Àí¶ÁĞ´ÊÂ¼ş¾ÍĞĞÁË¡£
            if (ngx_event_pipe(p, wev->write) == NGX_ABORT) {
                ngx_http_upstream_finalize_request(r, u, 0);
                return;
            }
        } else {//²»ÊÇdelayedÁË£¬ÄÇ¾ÍÊÇÕæµÄ³¬Ê±ÁË¡£
            p->downstream_error = 1;
            c->timedout = 1;
            ngx_connection_error(c, NGX_ETIMEDOUT, "client timed out");
        }
    } else {
        if (wev->delayed) {
            ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,"http downstream delayed");
            if (ngx_handle_write_event(wev, p->send_lowat) != NGX_OK) {
                ngx_http_upstream_finalize_request(r, u, 0);
            }
            return;
        }
		//´ø×Å1µÄ±êÊ¶µ÷ÓÃ£¬ÀïÃæº¯Êı»áÁ¢¼´·¢ËÍÊı¾İµÄ£¬È»ºóÊÔ×Å¶ÁÈ¡Êı¾İ¡¢
        if (ngx_event_pipe(p, 1) == NGX_ABORT) {
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
    }
    ngx_http_upstream_process_request(r);
}


static void
ngx_http_upstream_process_upstream(ngx_http_request_t *r, ngx_http_upstream_t *u)
{//ÕâÊÇÔÚÓĞbufferingµÄÇé¿öÏÂÊ¹ÓÃµÄº¯Êı¡£
//ngx_http_upstream_send_responseµ÷ÓÃÕâÀï·¢¶¯Ò»ÏÂÊı¾İ¶ÁÈ¡¡£ÒÔºóÓĞÊı¾İ¿É¶ÁµÄÊ±ºòÒ²»áµ÷ÓÃÕâÀïµÄ¡£ÉèÖÃµ½ÁËu->read_event_handlerÁË¡£
    ngx_connection_t  *c;
    c = u->peer.connection;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0, "http upstream process upstream");
    c->log->action = "reading upstream";
    if (c->read->timedout) {//Èç¹û³¬Ê±ÁË
        u->pipe->upstream_error = 1;
        ngx_connection_error(c, NGX_ETIMEDOUT, "upstream timed out");
    } else {
    //ÇëÇóÃ»ÓĞ³¬Ê±£¬ÄÇÃ´¶Ôºó¶Ë£¬´¦ÀíÒ»ÏÂ¶ÁÊÂ¼ş¡£ngx_event_pipe¿ªÊ¼´¦Àí
        if (ngx_event_pipe(u->pipe, 0) == NGX_ABORT) {
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
    }
	//´¦ÀíÁËÒ»ÏÂÊÇ·ñĞèÒª°ÉÊı¾İĞ´µ½´ÅÅÌÉÏ¡£
    ngx_http_upstream_process_request(r);
}


static void
ngx_http_upstream_process_request(ngx_http_request_t *r)
{//ÀïÃæ¾Í´¦ÀíÁËÒ»ÏÂcache,storeµÄÇé¿ö£¬°ÉÊı¾İĞ´µ½´ÅÅÌÊ²Ã´µÄ£¬ºóĞøÔÙ¿´
    ngx_uint_t            del;
    ngx_temp_file_t      *tf;
    ngx_event_pipe_t     *p;
    ngx_http_upstream_t  *u;

    u = r->upstream;
    p = u->pipe;
    if (u->peer.connection) {
        if (u->store) {
            del = p->upstream_error;
            tf = u->pipe->temp_file;
            if (p->upstream_eof || p->upstream_done) {
                if (u->headers_in.status_n == NGX_HTTP_OK && (u->headers_in.content_length_n == -1
                        || (u->headers_in.content_length_n == tf->offset)))
                {
                    ngx_http_upstream_store(r, u);
                } else {
                    del = 1;
                }
            }
            if (del && tf->file.fd != NGX_INVALID_FILE) {
                if (ngx_delete_file(tf->file.name.data) == NGX_FILE_ERROR) {
                    ngx_log_error(NGX_LOG_CRIT, r->connection->log, ngx_errno,ngx_delete_file_n " \"%s\" failed", u->pipe->temp_file->file.name.data);
                }
            }
        }
#if (NGX_HTTP_CACHE)
        if (u->cacheable) {
            if (p->upstream_done) {
                ngx_http_file_cache_update(r, u->pipe->temp_file);
            } else if (p->upstream_eof) {
                /* TODO: check length & update cache */
                ngx_http_file_cache_update(r, u->pipe->temp_file);
            } else if (p->upstream_error) {
                ngx_http_file_cache_free(r->cache, u->pipe->temp_file);
            }
        }
#endif
        if (p->upstream_done || p->upstream_eof || p->upstream_error) {
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"http upstream exit: %p", p->out);
            ngx_http_upstream_finalize_request(r, u, 0);
            return;
        }
    }
    if (p->downstream_error) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http upstream downstream error");
        if (!u->cacheable && !u->store && u->peer.connection) {
            ngx_http_upstream_finalize_request(r, u, 0);
        }
    }
}


static void
ngx_http_upstream_store(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    size_t                  root;
    time_t                  lm;
    ngx_str_t               path;
    ngx_temp_file_t        *tf;
    ngx_ext_rename_file_t   ext;

    tf = u->pipe->temp_file;

    if (tf->file.fd == NGX_INVALID_FILE) {

        /* create file for empty 200 response */

        tf = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
        if (tf == NULL) {
            return;
        }

        tf->file.fd = NGX_INVALID_FILE;
        tf->file.log = r->connection->log;
        tf->path = u->conf->temp_path;
        tf->pool = r->pool;
        tf->persistent = 1;

        if (ngx_create_temp_file(&tf->file, tf->path, tf->pool,
                                 tf->persistent, tf->clean, tf->access)
            != NGX_OK)
        {
            return;
        }

        u->pipe->temp_file = tf;
    }

    ext.access = u->conf->store_access;
    ext.path_access = u->conf->store_access;
    ext.time = -1;
    ext.create_path = 1;
    ext.delete_file = 1;
    ext.log = r->connection->log;

    if (u->headers_in.last_modified) {
        lm = ngx_http_parse_time(u->headers_in.last_modified->value.data, u->headers_in.last_modified->value.len);
        if (lm != NGX_ERROR) {
            ext.time = lm;
            ext.fd = tf->file.fd;
        }
    }
    if (u->conf->store_lengths == NULL) {
        ngx_http_map_uri_to_path(r, &path, &root, 0);
    } else {
        if (ngx_http_script_run(r, &path, u->conf->store_lengths->elts, 0,  u->conf->store_values->elts) == NULL)
        {
            return;
        }
    }
    path.len--;
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "upstream stores \"%s\" to \"%s\"",
                   tf->file.name.data, path.data);
    (void) ngx_ext_rename_file(&tf->file.name, &path, &ext);
}


static void
ngx_http_upstream_dummy_handler(ngx_http_request_t *r, ngx_http_upstream_t *u)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http upstream dummy handler");
}


static void
ngx_http_upstream_next(ngx_http_request_t *r, ngx_http_upstream_t *u,
    ngx_uint_t ft_type)
{
    ngx_uint_t  status, state;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http next upstream, %xi", ft_type);

#if 0
    ngx_http_busy_unlock(u->conf->busy_lock, &u->busy_lock);
#endif

    if (ft_type == NGX_HTTP_UPSTREAM_FT_HTTP_404) {
        state = NGX_PEER_NEXT;
    } else {
        state = NGX_PEER_FAILED;
    }

    if (ft_type != NGX_HTTP_UPSTREAM_FT_NOLIVE) {
        u->peer.free(&u->peer, u->peer.data, state);
    }

    if (ft_type == NGX_HTTP_UPSTREAM_FT_TIMEOUT) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, NGX_ETIMEDOUT,
                      "upstream timed out");
    }

    if (u->peer.cached && ft_type == NGX_HTTP_UPSTREAM_FT_ERROR) {
        status = 0;

    } else {
        switch(ft_type) {

        case NGX_HTTP_UPSTREAM_FT_TIMEOUT:
            status = NGX_HTTP_GATEWAY_TIME_OUT;
            break;

        case NGX_HTTP_UPSTREAM_FT_HTTP_500:
            status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            break;

        case NGX_HTTP_UPSTREAM_FT_HTTP_404:
            status = NGX_HTTP_NOT_FOUND;
            break;

        /*
         * NGX_HTTP_UPSTREAM_FT_BUSY_LOCK and NGX_HTTP_UPSTREAM_FT_MAX_WAITING
         * never reach here
         */

        default:
            status = NGX_HTTP_BAD_GATEWAY;
        }
    }

    if (r->connection->error) {
        ngx_http_upstream_finalize_request(r, u,
                                           NGX_HTTP_CLIENT_CLOSED_REQUEST);
        return;
    }

    if (status) {
        u->state->status = status;

        if (u->peer.tries == 0 || !(u->conf->next_upstream & ft_type)) {

#if (NGX_HTTP_CACHE)

            if (u->cache_status == NGX_HTTP_CACHE_EXPIRED
                && (u->conf->cache_use_stale & ft_type))
            {
                ngx_int_t  rc;

                rc = u->reinit_request(r);

                if (rc == NGX_OK) {
                    u->cache_status = NGX_HTTP_CACHE_STALE;
                    rc = ngx_http_upstream_cache_send(r, u);
                }

                ngx_http_upstream_finalize_request(r, u, rc);
                return;
            }
#endif

            ngx_http_upstream_finalize_request(r, u, status);
            return;
        }
    }

    if (u->peer.connection) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "close http upstream connection: %d",
                       u->peer.connection->fd);
#if (NGX_HTTP_SSL)

        if (u->peer.connection->ssl) {
            u->peer.connection->ssl->no_wait_shutdown = 1;
            u->peer.connection->ssl->no_send_shutdown = 1;

            (void) ngx_ssl_shutdown(u->peer.connection);
        }
#endif

        ngx_close_connection(u->peer.connection);
    }

#if 0
    if (u->conf->busy_lock && !u->busy_locked) {
        ngx_http_upstream_busy_lock(p);
        return;
    }
#endif

    ngx_http_upstream_connect(r, u);
}


static void
ngx_http_upstream_cleanup(void *data)
{//½áÊøÒ»¸öÁ¬½Ó¡£Èç¹û½âÎö¹ıfcgi_pass url£» Ôòµ÷ÓÃngx_resolve_name_done
    ngx_http_request_t *r = data;
    ngx_http_upstream_t  *u;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "cleanup http upstream request: \"%V\"", &r->uri);
    u = r->upstream;
    if (u->resolved && u->resolved->ctx) {
        ngx_resolve_name_done(u->resolved->ctx);
        u->resolved->ctx = NULL;
    }
    ngx_http_upstream_finalize_request(r, u, NGX_DONE);
}


static void
ngx_http_upstream_finalize_request(ngx_http_request_t *r, ngx_http_upstream_t *u, ngx_int_t rc)
{
    ngx_time_t  *tp;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "finalize http upstream request: %i", rc);
    if (u->cleanup) {
        *u->cleanup = NULL;
        u->cleanup = NULL;
    }

    if (u->resolved && u->resolved->ctx) {
        ngx_resolve_name_done(u->resolved->ctx);
        u->resolved->ctx = NULL;
    }

    if (u->state && u->state->response_sec) {
        tp = ngx_timeofday();
        u->state->response_sec = tp->sec - u->state->response_sec;
        u->state->response_msec = tp->msec - u->state->response_msec;

        if (u->pipe) {
            u->state->response_length = u->pipe->read_length;
        }
    }

    u->finalize_request(r, rc);

    if (u->peer.free) {
        u->peer.free(&u->peer, u->peer.data, 0);
    }

    if (u->peer.connection) {

#if (NGX_HTTP_SSL)

        /* TODO: do not shutdown persistent connection */

        if (u->peer.connection->ssl) {

            /*
             * We send the "close notify" shutdown alert to the upstream only
             * and do not wait its "close notify" shutdown alert.
             * It is acceptable according to the TLS standard.
             */

            u->peer.connection->ssl->no_wait_shutdown = 1;

            (void) ngx_ssl_shutdown(u->peer.connection);
        }
#endif

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "close http upstream connection: %d",
                       u->peer.connection->fd);

        ngx_close_connection(u->peer.connection);
    }

    u->peer.connection = NULL;

    if (u->pipe && u->pipe->temp_file) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http upstream temp fd: %d",
                       u->pipe->temp_file->file.fd);
    }

#if (NGX_HTTP_CACHE)

    if (u->cacheable && r->cache) {
        time_t  valid;

        if (rc == NGX_HTTP_BAD_GATEWAY || rc == NGX_HTTP_GATEWAY_TIME_OUT) {

            valid = ngx_http_file_cache_valid(u->conf->cache_valid, rc);

            if (valid) {
                r->cache->valid_sec = ngx_time() + valid;
                r->cache->error = rc;
            }
        }

        ngx_http_file_cache_free(r->cache, u->pipe->temp_file);
    }

#endif

    if (u->header_sent
        && (rc == NGX_ERROR || rc >= NGX_HTTP_SPECIAL_RESPONSE))
    {
        rc = 0;
    }

    if (rc == NGX_DECLINED) {
        return;
    }

    r->connection->log->action = "sending to client";

    if (rc == 0) {
        rc = ngx_http_send_special(r, NGX_HTTP_LAST);
    }
    ngx_http_finalize_request(r, rc);
}


static ngx_int_t
ngx_http_upstream_process_header_line(ngx_http_request_t *r, ngx_table_elt_t *h, ngx_uint_t offset)
{//¾Í¿½±´ÁËÒ»ÏÂÍ·²¿Êı¾İ
    ngx_table_elt_t  **ph;
    ph = (ngx_table_elt_t **) ((char *) &r->upstream->headers_in + offset);
    if (*ph == NULL) {
        *ph = h;
    }
    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_ignore_header_line(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_set_cookie(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
#if (NGX_HTTP_CACHE)
    ngx_http_upstream_t  *u;

    u = r->upstream;

    if (!(u->conf->ignore_headers & NGX_HTTP_UPSTREAM_IGN_SET_COOKIE)) {
        u->cacheable = 0;
    }
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_cache_control(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset)
{
    ngx_array_t          *pa;
    ngx_table_elt_t     **ph;
    ngx_http_upstream_t  *u;

    u = r->upstream;
    pa = &u->headers_in.cache_control;

    if (pa->elts == NULL) {
       if (ngx_array_init(pa, r->pool, 2, sizeof(ngx_table_elt_t *)) != NGX_OK)
       {
           return NGX_ERROR;
       }
    }

    ph = ngx_array_push(pa);
    if (ph == NULL) {
        return NGX_ERROR;
    }

    *ph = h;

#if (NGX_HTTP_CACHE)
    {
    u_char     *p, *last;
    ngx_int_t   n;

    if (u->conf->ignore_headers & NGX_HTTP_UPSTREAM_IGN_CACHE_CONTROL) {
        return NGX_OK;
    }

    if (r->cache == NULL) {
        return NGX_OK;
    }

    if (r->cache->valid_sec != 0) {
        return NGX_OK;
    }

    p = h->value.data;
    last = p + h->value.len;

    if (ngx_strlcasestrn(p, last, (u_char *) "no-cache", 8 - 1) != NULL
        || ngx_strlcasestrn(p, last, (u_char *) "no-store", 8 - 1) != NULL
        || ngx_strlcasestrn(p, last, (u_char *) "private", 7 - 1) != NULL)
    {
        u->cacheable = 0;
        return NGX_OK;
    }

    p = ngx_strlcasestrn(p, last, (u_char *) "max-age=", 8 - 1);

    if (p == NULL) {
        return NGX_OK;
    }

    n = 0;

    for (p += 8; p < last; p++) {
        if (*p == ',' || *p == ';' || *p == ' ') {
            break;
        }

        if (*p >= '0' && *p <= '9') {
            n = n * 10 + *p - '0';
            continue;
        }

        u->cacheable = 0;
        return NGX_OK;
    }

    if (n == 0) {
        u->cacheable = 0;
        return NGX_OK;
    }

    r->cache->valid_sec = ngx_time() + n;
    }
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_expires(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_http_upstream_t  *u;

    u = r->upstream;
    u->headers_in.expires = h;

#if (NGX_HTTP_CACHE)
    {
    time_t  expires;

    if (u->conf->ignore_headers & NGX_HTTP_UPSTREAM_IGN_EXPIRES) {
        return NGX_OK;
    }

    if (r->cache == NULL) {
        return NGX_OK;
    }

    if (r->cache->valid_sec != 0) {
        return NGX_OK;
    }

    expires = ngx_http_parse_time(h->value.data, h->value.len);

    if (expires == NGX_ERROR || expires < ngx_time()) {
        u->cacheable = 0;
        return NGX_OK;
    }

    r->cache->valid_sec = expires;
    }
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_accel_expires(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset)
{
    ngx_http_upstream_t  *u;

    u = r->upstream;
    u->headers_in.x_accel_expires = h;

#if (NGX_HTTP_CACHE)
    {
    u_char     *p;
    size_t      len;
    ngx_int_t   n;

    if (u->conf->ignore_headers & NGX_HTTP_UPSTREAM_IGN_XA_EXPIRES) {
        return NGX_OK;
    }

    if (r->cache == NULL) {
        return NGX_OK;
    }

    len = h->value.len;
    p = h->value.data;

    if (p[0] != '@') {
        n = ngx_atoi(p, len);

        switch (n) {
        case 0:
            u->cacheable = 0;
        case NGX_ERROR:
            return NGX_OK;

        default:
            r->cache->valid_sec = ngx_time() + n;
            return NGX_OK;
        }
    }

    p++;
    len--;

    n = ngx_atoi(p, len);

    if (n != NGX_ERROR) {
        r->cache->valid_sec = n;
    }
    }
#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_limit_rate(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_int_t  n;

    r->upstream->headers_in.x_accel_limit_rate = h;

    n = ngx_atoi(h->value.data, h->value.len);

    if (n != NGX_ERROR) {
        r->limit_rate = (size_t) n;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_buffering(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    u_char  c0, c1, c2;

    if (r->upstream->conf->change_buffering) {

        if (h->value.len == 2) {
            c0 = ngx_tolower(h->value.data[0]);
            c1 = ngx_tolower(h->value.data[1]);

            if (c0 == 'n' && c1 == 'o') {
                r->upstream->buffering = 0;
            }

        } else if (h->value.len == 3) {
            c0 = ngx_tolower(h->value.data[0]);
            c1 = ngx_tolower(h->value.data[1]);
            c2 = ngx_tolower(h->value.data[2]);

            if (c0 == 'y' && c1 == 'e' && c2 == 's') {
                r->upstream->buffering = 1;
            }
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_process_charset(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    r->headers_out.override_charset = &h->value;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_header_line(ngx_http_request_t *r, ngx_table_elt_t *h, ngx_uint_t offset)
{//½«Í·²¿Êı¾İ¿½±´µ½headers_out.headersÊı×éÖĞ¡£
    ngx_table_elt_t  *ho, **ph;
    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }
    *ho = *h;//Ö¸ÏòĞÂÊı¾İµÄÎ»ÖÃ¡£
    if (offset) {
        ph = (ngx_table_elt_t **) ((char *) &r->headers_out + offset);
        *ph = ho;
    }
    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_multi_header_lines(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset)
{
    ngx_array_t      *pa;
    ngx_table_elt_t  *ho, **ph;

    pa = (ngx_array_t *) ((char *) &r->headers_out + offset);

    if (pa->elts == NULL) {
        if (ngx_array_init(pa, r->pool, 2, sizeof(ngx_table_elt_t *)) != NGX_OK)
        {
            return NGX_ERROR;
        }
    }

    ph = ngx_array_push(pa);
    if (ph == NULL) {
        return NGX_ERROR;
    }

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;
    *ph = ho;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_content_type(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    u_char  *p, *last;

    r->headers_out.content_type_len = h->value.len;
    r->headers_out.content_type = h->value;
    r->headers_out.content_type_lowcase = NULL;

    for (p = h->value.data; *p; p++) {

        if (*p != ';') {
            continue;
        }

        last = p;

        while (*++p == ' ') { /* void */ }

        if (*p == '\0') {
            return NGX_OK;
        }

        if (ngx_strncasecmp(p, (u_char *) "charset=", 8) != 0) {
            continue;
        }

        p += 8;

        r->headers_out.content_type_len = last - h->value.data;

        if (*p == '"') {
            p++;
        }

        last = h->value.data + h->value.len;

        if (*(last - 1) == '"') {
            last--;
        }

        r->headers_out.charset.len = last - p;
        r->headers_out.charset.data = p;

        return NGX_OK;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_content_length(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_table_elt_t  *ho;

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;

    r->headers_out.content_length = ho;
    r->headers_out.content_length_n = ngx_atoof(h->value.data, h->value.len);

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_last_modified(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_table_elt_t  *ho;

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;

    r->headers_out.last_modified = ho;

#if (NGX_HTTP_CACHE)

    if (r->upstream->cacheable) {
        r->headers_out.last_modified_time = ngx_http_parse_time(h->value.data,
                                                                h->value.len);
    }

#endif

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_rewrite_location(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    ngx_int_t         rc;
    ngx_table_elt_t  *ho;

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;

    if (r->upstream->rewrite_redirect) {
        rc = r->upstream->rewrite_redirect(r, ho, 0);

        if (rc == NGX_DECLINED) {
            return NGX_OK;
        }

        if (rc == NGX_OK) {
            r->headers_out.location = ho;

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "rewritten location: \"%V\"", &ho->value);
        }

        return rc;
    }

    if (ho->value.data[0] != '/') {
        r->headers_out.location = ho;
    }

    /*
     * we do not set r->headers_out.location here to avoid the handling
     * the local redirects without a host name by ngx_http_header_filter()
     */

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_rewrite_refresh(ngx_http_request_t *r, ngx_table_elt_t *h,
    ngx_uint_t offset)
{
    u_char           *p;
    ngx_int_t         rc;
    ngx_table_elt_t  *ho;

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;

    if (r->upstream->rewrite_redirect) {

        p = ngx_strcasestrn(ho->value.data, "url=", 4 - 1);

        if (p) {
            rc = r->upstream->rewrite_redirect(r, ho, p + 4 - ho->value.data);

        } else {
            return NGX_OK;
        }

        if (rc == NGX_DECLINED) {
            return NGX_OK;
        }

        if (rc == NGX_OK) {
            r->headers_out.refresh = ho;

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "rewritten refresh: \"%V\"", &ho->value);
        }

        return rc;
    }

    r->headers_out.refresh = ho;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_copy_allow_ranges(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset)
{
    ngx_table_elt_t  *ho;

#if (NGX_HTTP_CACHE)

    if (r->cached) {
        r->allow_ranges = 1;
        return NGX_OK;

    }

#endif

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }
    *ho = *h;
    r->headers_out.accept_ranges = ho;

    return NGX_OK;
}


#if (NGX_HTTP_GZIP)

static ngx_int_t
ngx_http_upstream_copy_content_encoding(ngx_http_request_t *r,
    ngx_table_elt_t *h, ngx_uint_t offset)
{
    ngx_table_elt_t  *ho;

    ho = ngx_list_push(&r->headers_out.headers);
    if (ho == NULL) {
        return NGX_ERROR;
    }

    *ho = *h;

    r->headers_out.content_encoding = ho;

    return NGX_OK;
}

#endif


static ngx_int_t
ngx_http_upstream_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var, *v;

    for (v = ngx_http_upstream_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_addr_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                     *p;
    size_t                      len;
    ngx_uint_t                  i;
    ngx_http_upstream_state_t  *state;

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    len = 0;
    state = r->upstream_states->elts;

    for (i = 0; i < r->upstream_states->nelts; i++) {
        if (state[i].peer) {
            len += state[i].peer->len + 2;

        } else {
            len += 3;
        }
    }

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->data = p;

    i = 0;

    for ( ;; ) {
        if (state[i].peer) {
            p = ngx_cpymem(p, state[i].peer->data, state[i].peer->len);
        }

        if (++i == r->upstream_states->nelts) {
            break;
        }

        if (state[i].peer) {
            *p++ = ',';
            *p++ = ' ';

        } else {
            *p++ = ' ';
            *p++ = ':';
            *p++ = ' ';

            if (++i == r->upstream_states->nelts) {
                break;
            }

            continue;
        }
    }

    v->len = p - v->data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_status_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                     *p;
    size_t                      len;
    ngx_uint_t                  i;
    ngx_http_upstream_state_t  *state;

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    len = r->upstream_states->nelts * (3 + 2);

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->data = p;

    i = 0;
    state = r->upstream_states->elts;

    for ( ;; ) {
        if (state[i].status) {
            p = ngx_sprintf(p, "%ui", state[i].status);

        } else {
            *p++ = '-';
        }

        if (++i == r->upstream_states->nelts) {
            break;
        }

        if (state[i].peer) {
            *p++ = ',';
            *p++ = ' ';

        } else {
            *p++ = ' ';
            *p++ = ':';
            *p++ = ' ';

            if (++i == r->upstream_states->nelts) {
                break;
            }

            continue;
        }
    }

    v->len = p - v->data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_response_time_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                     *p;
    size_t                      len;
    ngx_uint_t                  i;
    ngx_msec_int_t              ms;
    ngx_http_upstream_state_t  *state;

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    len = r->upstream_states->nelts * (NGX_TIME_T_LEN + 4 + 2);

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->data = p;

    i = 0;
    state = r->upstream_states->elts;

    for ( ;; ) {
        if (state[i].status) {
            ms = (ngx_msec_int_t)
                     (state[i].response_sec * 1000 + state[i].response_msec);
            ms = ngx_max(ms, 0);
            p = ngx_sprintf(p, "%d.%03d", ms / 1000, ms % 1000);

        } else {
            *p++ = '-';
        }

        if (++i == r->upstream_states->nelts) {
            break;
        }

        if (state[i].peer) {
            *p++ = ',';
            *p++ = ' ';

        } else {
            *p++ = ' ';
            *p++ = ':';
            *p++ = ' ';

            if (++i == r->upstream_states->nelts) {
                break;
            }

            continue;
        }
    }

    v->len = p - v->data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_upstream_response_length_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                     *p;
    size_t                      len;
    ngx_uint_t                  i;
    ngx_http_upstream_state_t  *state;

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    if (r->upstream_states == NULL || r->upstream_states->nelts == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    len = r->upstream_states->nelts * (NGX_OFF_T_LEN + 2);

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->data = p;

    i = 0;
    state = r->upstream_states->elts;

    for ( ;; ) {
        p = ngx_sprintf(p, "%O", state[i].response_length);

        if (++i == r->upstream_states->nelts) {
            break;
        }

        if (state[i].peer) {
            *p++ = ',';
            *p++ = ' ';

        } else {
            *p++ = ' ';
            *p++ = ':';
            *p++ = ' ';

            if (++i == r->upstream_states->nelts) {
                break;
            }

            continue;
        }
    }

    v->len = p - v->data;

    return NGX_OK;
}


ngx_int_t ngx_http_upstream_header_variable(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data)
{//»ñÈ¡·ÇÖøÃûµÄÍ·²¿×Ö¶Î¡£
    if (r->upstream == NULL) {
        v->not_found = 1;
        return NGX_OK;
    }
    return ngx_http_variable_unknown_header(v, (ngx_str_t *) data, &r->upstream->headers_in.headers.part, sizeof("upstream_http_") - 1);
}


#if (NGX_HTTP_CACHE)

ngx_int_t
ngx_http_upstream_cache_status(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_uint_t  n;

    if (r->upstream == NULL || r->upstream->cache_status == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    n = r->upstream->cache_status - 1;

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->len = ngx_http_cache_status[n].len;
    v->data = ngx_http_cache_status[n].data;

    return NGX_OK;
}

#endif


static char *
ngx_http_upstream(ngx_conf_t *cf, ngx_command_t *cmd, void *dummy)
{//µ±Åöµ½upstream{}Ö¸ÁîµÄÊ±ºòµ÷ÓÃÕâÀï¡£
    char                          *rv;
    void                          *mconf;
    ngx_str_t                     *value;
    ngx_url_t                      u;
    ngx_uint_t                     m;
    ngx_conf_t                     pcf;
    ngx_http_module_t             *module;
    ngx_http_conf_ctx_t           *ctx, *http_ctx;
    ngx_http_upstream_srv_conf_t  *uscf;

    ngx_memzero(&u, sizeof(ngx_url_t));
    value = cf->args->elts;
    u.host = value[1];
    u.no_resolve = 1;
	//ÏÂÃæ½«u´ú±íµÄÊı¾İÉèÖÃµ½umcf->upstreamsÀïÃæÈ¥¡£È»ºó·µ»Ø¶ÔÓ¦µÄupstream{}½á¹¹Êı¾İÖ¸Õë¡£
    uscf = ngx_http_upstream_add(cf, &u, NGX_HTTP_UPSTREAM_CREATE
                                         |NGX_HTTP_UPSTREAM_WEIGHT
                                         |NGX_HTTP_UPSTREAM_MAX_FAILS
                                         |NGX_HTTP_UPSTREAM_FAIL_TIMEOUT
                                         |NGX_HTTP_UPSTREAM_DOWN
                                         |NGX_HTTP_UPSTREAM_BACKUP);
    if (uscf == NULL) {
        return NGX_CONF_ERROR;
    }
	//ÉêÇëngx_http_conf_ctx_t½á¹¹£¬¾­µäµÄmain/srv/local_confÖ¸Õë½á¹¹
    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    http_ctx = cf->ctx;//¸úÉÏ²ãµÄHTTP¹«ÓÃmain_conf£¬ÕâÀï¸úserver{}Ö¸ÁîÒ»ÑùµÄ£¬¹²Ïímain_conf
    ctx->main_conf = http_ctx->main_conf;

    /* the upstream{}'s srv_conf */
    ctx->srv_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->srv_conf == NULL) {
        return NGX_CONF_ERROR;
    }
	//ÔÚngx_http_upstream_moduleÄ£¿éÀïÃæ¼ÇÂ¼ÎÒµÄsrv_conf¡£ÀïÃæ¼ÇÂ¼ÁËÎÒÀïÃæÓĞÄÄ¼¸¸öserverÖ¸Áî
    ctx->srv_conf[ngx_http_upstream_module.ctx_index] = uscf;//uscfÀïÃæ¼ÇÂ¼ÁËserverÁĞ±íĞÅÏ¢¡£
    uscf->srv_conf = ctx->srv_conf;//ÕâÒ»Ìõ£¬¼Ç×¡ÎÒÕâ¸öupstreamËùÊôµÄsrv_confÊı×é¡£Ò²¾ÍÊÇËùÊôµÄhttp{}¿éÀïÃæµÄsrv_conf

    /* the upstream{}'s loc_conf */
    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    for (m = 0; ngx_modules[m]; m++) {//ÀÏ¹æ¾Ø£¬³õÊ¼»¯ËùÓĞHTTPÄ£¿éµÄsrv,locÅäÖÃ¡£µ÷ÓÃÃ¿¸öÄ£¿éµÄcreate»Øµ÷
        if (ngx_modules[m]->type != NGX_HTTP_MODULE) {
            continue;
        }
        module = ngx_modules[m]->ctx;
        if (module->create_srv_conf) {
            mconf = module->create_srv_conf(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }
            ctx->srv_conf[ngx_modules[m]->ctx_index] = mconf;
        }
        if (module->create_loc_conf) {
            mconf = module->create_loc_conf(cf);
            if (mconf == NULL) {
                return NGX_CONF_ERROR;
            }
            ctx->loc_conf[ngx_modules[m]->ctx_index] = mconf;
        }
    }

    /* parse inside upstream{} */
    pcf = *cf;
    cf->ctx = ctx;//ÁÙÊ±ÇĞ»»ctx£¬½øÈëupstream{}¿éÖĞ½øĞĞ½âÎö¡£
    cf->cmd_type = NGX_HTTP_UPS_CONF;
    rv = ngx_conf_parse(cf, NULL);
    *cf = pcf;

    if (rv != NGX_CONF_OK) {
        return rv;
    }
    if (uscf->servers == NULL) { "no servers are inside upstream");
        return NGX_CONF_ERROR;
    }
    return rv;
}


static char *
ngx_http_upstream_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//½âÎöµ½"server"µÄÊ±ºòµ÷ÓÃÕâÀï.ÀïÃæÖ»ÊÇÔÚuscf->serversÀïÃæÔö¼ÓÁËÒ»¸öserver,²¢ÉèÖÃºÃngx_http_upstream_server_t½á¹¹µÄÊı¾İ¡£ÆäËûÃ»¸É¡£
    ngx_http_upstream_srv_conf_t  *uscf = conf;//´Óngx_conf_handlerÀïÃæ¿ÉÒÔ¿´³ö£¬Õâ¸öconf¾ÍÊÇupstreamµÄctx->srv_conf[upstreamÄ£¿é.index]µÄÖµ
	//upstreamÄ£¿éµÄconfÎªNGX_HTTP_SRV_CONF_OFFSET£¬ËùÒÔ¾ö¶¨ÁËngx_conf_handlerÀïÃæµÄconf²ÎÊı
    time_t                       fail_timeout;
    ngx_str_t                   *value, s;
    ngx_url_t                    u;
    ngx_int_t                    weight, max_fails;
    ngx_uint_t                   i;
    ngx_http_upstream_server_t  *us;

    if (uscf->servers == NULL) {//Èç¹û±¾upstreamµÄserversÊı×éÎª¿Õ£¬³õÊ¼»¯Ö®
        uscf->servers = ngx_array_create(cf->pool, 4, sizeof(ngx_http_upstream_server_t));
        if (uscf->servers == NULL) {
            return NGX_CONF_ERROR;
        }
    }
    us = ngx_array_push(uscf->servers);//Ôö¼ÓÒ»¸öserver.ÏÂÃæÈç¹ûÅäÖÃÊ§°Ü£¬»áÖ±½Ó·µ»ØÊ§°ÜµÄ£¬Ò²¾Í²»ĞèÒª°ÑÕâÏîÉ¾ÁË¡£
    if (us == NULL) {
        return NGX_CONF_ERROR;
    }
    ngx_memzero(us, sizeof(ngx_http_upstream_server_t));
    value = cf->args->elts;
    ngx_memzero(&u, sizeof(ngx_url_t));
    u.url = value[1];
    u.default_port = 80;

    if (ngx_parse_url(cf->pool, &u) != NGX_OK) {//½âÎöÒ»ÏÂURLµÄÊı¾İ½á¹¹¡£
        if (u.err) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%s in upstream \"%V\"", u.err, &u.url);
        }
        return NGX_CONF_ERROR;
    }
    weight = 1;
    max_fails = 1;
    fail_timeout = 10;
    for (i = 2; i < cf->args->nelts; i++) {//±éÀúºóÃæµÄÃ¿Ò»¸ö²ÎÊı£¬±ÈÈç: server 127.0.0.1:8080 max_fails=3 fail_timeout=30s;
        if (ngx_strncmp(value[i].data, "weight=", 7) == 0) {//µÃµ½ÕûÊıÀàĞÍµÄÈ¨ÖØ¡£
            if (!(uscf->flags & NGX_HTTP_UPSTREAM_WEIGHT)) {
                goto invalid;
            }//weight = NUMBER - set weight of the server, if not set weight is equal to one.
            weight = ngx_atoi(&value[i].data[7], value[i].len - 7);
            if (weight == NGX_ERROR || weight == 0) {
                goto invalid;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "max_fails=", 10) == 0) {//½âÎömax_fails²ÎÊı£¬±íÊ¾
            if (!(uscf->flags & NGX_HTTP_UPSTREAM_MAX_FAILS)) {
                goto invalid;
            }//NUMBER - number of unsuccessful attempts at communicating with the server within the time period
            max_fails = ngx_atoi(&value[i].data[10], value[i].len - 10);
            if (max_fails == NGX_ERROR) {
                goto invalid;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "fail_timeout=", 13) == 0) {//ÕâÃ´fail_timeout¶àµÄÊ±¼äÄÚ£¬³öÏÖmax_failsµÄÊ§°ÜµÄ·şÎñÆ÷½«±»±ê¼ÇÎª³öÎÊÌâµÄ¡£
            if (!(uscf->flags & NGX_HTTP_UPSTREAM_FAIL_TIMEOUT)) {
                goto invalid;
            }//fail_timeout = TIME - the time during which must occur *max_fails* number of unsuccessful attempts at communication with the server that would cause the server to be considered inoperative
            s.len = value[i].len - 13;
            s.data = &value[i].data[13];
            fail_timeout = ngx_parse_time(&s, 1);
            if (fail_timeout == NGX_ERROR) {
                goto invalid;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "backup", 6) == 0) {
            if (!(uscf->flags & NGX_HTTP_UPSTREAM_BACKUP)) {
                goto invalid;
            }//backup - (0.6.7 or later) only uses this server if the non-backup servers are all down or busy
            us->backup = 1;
            continue;
        }
        if (ngx_strncmp(value[i].data, "down", 4) == 0) {
            if (!(uscf->flags & NGX_HTTP_UPSTREAM_DOWN)) {
                goto invalid;
            }
            us->down = 1;
            continue;
        }

        goto invalid;
    }
	//ÏÂÃæ¿½±´ÉèÖÃÒ»ÏÂÕâ¸öserverµÄÏà¹ØĞÅÏ¢¡£
    us->addrs = u.addrs;
    us->naddrs = u.naddrs;
    us->weight = weight;
    us->max_fails = max_fails;
    us->fail_timeout = fail_timeout;
    return NGX_CONF_OK;

invalid:
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid parameter \"%V\"", &value[i]);
    return NGX_CONF_ERROR;
}


ngx_http_upstream_srv_conf_t *
ngx_http_upstream_add(ngx_conf_t *cf, ngx_url_t *u, ngx_uint_t flags)
{//Èç¹ûu´ú±íµÄserverÒÑ¾­´æÔÚ£¬Ôò·µ»Ø¾ä±ú£¬·ñÔòÔÚumcf->upstreamsÀïÃæĞÂ¼ÓÒ»¸ö£¬ÉèÖÃ³õÊ¼»¯¡£
//ÔÚngx_http_fastcgi_passµÈÅöµ½ºó¶ËµØÖ·µÄµØ·½£¬»áµ÷ÓÃÕâ¸öº¯Êı£¬Ôö¼ÓÒ»¸öupstreamµÄserver.
//ÕâÀïµÄµ¥Î»ÊÇupstream£¬²»ÊÇserverĞĞ£¬µ÷ÓÃµÄÉÏ²ãÒ»°ãÖ»ÓĞÒ»¸öµØÖ·£¬ÓÚÊÇ¾ÍÖ»ÓĞÒ»¸öserver.µ±×öµ¥¸öupstream´¦Àí
    ngx_uint_t                      i;
    ngx_http_upstream_server_t     *us;
    ngx_http_upstream_srv_conf_t   *uscf, **uscfp;
    ngx_http_upstream_main_conf_t  *umcf;

    if (!(flags & NGX_HTTP_UPSTREAM_CREATE)) {//Èç¹ûÃ»ÓĞÉèÖÃCREATE±êÖ¾£¬±íÊ¾²»ĞèÒª´´½¨¡£
        if (ngx_parse_url(cf->pool, u) != NGX_OK) {//¼òÎöÒ»ÏÂµØÖ·¸ñÊ½£¬unix:Óò£¬inet6,4µØÖ·£¬http://host:port/µÈ
            if (u->err) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "%s in upstream \"%V\"", u->err, &u->url);
            }
            return NULL;
        }
    }

    umcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_upstream_module);
    uscfp = umcf->upstreams.elts;
    for (i = 0; i < umcf->upstreams.nelts; i++) {
		//±éÀúµ±Ç°µÄupstream£¬Èç¹ûÓĞÖØ¸´µÄ£¬Ôò±È½ÏÆäÏà¹ØµÄ×Ö¶Î£¬²¢´òÓ¡ÈÕÖ¾¡£Èç¹ûÕÒµ½ÏàÍ¬µÄ£¬Ôò·µ»Ø¶ÔÓ¦Ö¸Õë¡£
        if (uscfp[i]->host.len != u->host.len || ngx_strncasecmp(uscfp[i]->host.data, u->host.data, u->host.len)  != 0) {
            continue;//²»ÏàÍ¬µÄ²»¹Ü¡£
        }

        if ((flags & NGX_HTTP_UPSTREAM_CREATE) && (uscfp[i]->flags & NGX_HTTP_UPSTREAM_CREATE)) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "duplicate upstream \"%V\"", &u->host);
            return NULL;
        }
        if ((uscfp[i]->flags & NGX_HTTP_UPSTREAM_CREATE) && u->port) {
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,  "upstream \"%V\" may not have port %d", &u->host, u->port);
            return NULL;
        }
        if ((flags & NGX_HTTP_UPSTREAM_CREATE) && uscfp[i]->port) {
            ngx_log_error(NGX_LOG_WARN, cf->log, 0, "upstream \"%V\" may not have port %d in %s:%ui",
                          &u->host, uscfp[i]->port, uscfp[i]->file_name, uscfp[i]->line);
            return NULL;
        }
        if (uscfp[i]->port != u->port) {
            continue;
        }
        if (uscfp[i]->default_port && u->default_port  && uscfp[i]->default_port != u->default_port)  {
            continue;
        }
        return uscfp[i];//ÕÒµ½ÏàÍ¬µÄÅäÖÃÊı¾İÁË£¬Ö±½Ó·µ»ØËüµÄÖ¸Õë¡£
    }
	//Ã»ÓĞÕÒµ½ÏàÍ¬µÄÅäÖÃupstream£¬ÏÂÃæ´´½¨Ò»¸ö¡£ÕâÀïµÄsrv_conf¸úserver{}²»ÊÇÒ»»ØÊÂ,ÊÇÖ¸upstream{}ÀïÃæµÄserver xxxx:xxx;ĞĞ
    uscf = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_srv_conf_t));
    if (uscf == NULL) {
        return NULL;
    }
    uscf->flags = flags;
    uscf->host = u->host;
    uscf->file_name = cf->conf_file->file.name.data;
    uscf->line = cf->conf_file->line;
    uscf->port = u->port;
    uscf->default_port = u->default_port;

    if (u->naddrs == 1) {//±ÈÈç: server xx.xx.xx.xx:xx weight=2 max_fails=3;  ¸Õ¿ªÊ¼£¬ngx_http_upstream»áµ÷ÓÃ±¾º¯Êı¡£µ«ÊÇÆänaddres=0.
        uscf->servers = ngx_array_create(cf->pool, 1,  sizeof(ngx_http_upstream_server_t));
        if (uscf->servers == NULL) {
            return NGX_CONF_ERROR;
        }
        us = ngx_array_push(uscf->servers);//¼ÇÂ¼±¾upstream{}¿éµÄËùÓĞserverÖ¸Áî¡£
        if (us == NULL) {
            return NGX_CONF_ERROR;
        }
        ngx_memzero(us, sizeof(ngx_http_upstream_server_t));
        us->addrs = u->addrs;//¿½±´µØÖ· ĞÅÏ¢¡£
        us->naddrs = u->naddrs;
    }
	//
    uscfp = ngx_array_push(&umcf->upstreams);//·Åµ½upstreamµÄmain_confÀïÃæÈ¥¡£
    if (uscfp == NULL) {
        return NULL;
    }
    *uscfp = uscf;//ÔÚµ±Ç°µÄumcf->upstreams updstreamÅäÖÃÀïÃæÔö¼ÓÒ»Ïî¡£

    return uscf;
}


char *
ngx_http_upstream_bind_set_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//Ç¿ÖÆ´Ó±¾µØµÄÄ³¸öipµØÖ·Á¬½Óµ½memcached·şÎñÆ÷¡£Õâ¸ö²ÎÊıÄÜ°üÀ¨±äÁ¿¡£
//Off²ÎÊıÈ¡ÏûÀ´×ÔÇ°Ò»¸öÅäÖÃ¼¶±ğµÄmemcached_bindÖ¸ÁîµÄÅäÖÃ£¬ÔÊĞíÏµÍ³×Ô¶¯·ÖÅä±¾µØµØÖ·½øĞĞÁ¬½Ó¡£
    char  *p = conf;

    ngx_int_t     rc;
    ngx_str_t    *value;
    ngx_addr_t  **paddr;
	//½«Õâ¸öÅäÖÃÉèÖÃµ½ngx_http_upstream_conf_t:localÉÏÃæ£¬µ½Ê±ºò°ó¶¨µÄÊ±ºò»á°ó¶¨±¾»úµØÖ·µÄ¡£
    paddr = (ngx_addr_t **) (p + cmd->offset);
    *paddr = ngx_palloc(cf->pool, sizeof(ngx_addr_t));
    if (*paddr == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;
    rc = ngx_parse_addr(cf->pool, *paddr, value[1].data, value[1].len);
    switch (rc) {
    case NGX_OK:
        (*paddr)->name = value[1];
        return NGX_CONF_OK;

    case NGX_DECLINED:
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "invalid address \"%V\"", &value[1]);
    default:
        return NGX_CONF_ERROR;
    }
}


ngx_int_t
ngx_http_upstream_hide_headers_hash(ngx_conf_t *cf,
    ngx_http_upstream_conf_t *conf, ngx_http_upstream_conf_t *prev,
    ngx_str_t *default_hide_headers, ngx_hash_init_t *hash)
{
    ngx_str_t       *h;
    ngx_uint_t       i, j;
    ngx_array_t      hide_headers;
    ngx_hash_key_t  *hk;

    if (conf->hide_headers == NGX_CONF_UNSET_PTR
        && conf->pass_headers == NGX_CONF_UNSET_PTR)
    {
        conf->hide_headers_hash = prev->hide_headers_hash;

        if (conf->hide_headers_hash.buckets
#if (NGX_HTTP_CACHE)
            && ((conf->cache == NULL) == (prev->cache == NULL))
#endif
           )
        {
            return NGX_OK;
        }

        conf->hide_headers = prev->hide_headers;
        conf->pass_headers = prev->pass_headers;

    } else {
        if (conf->hide_headers == NGX_CONF_UNSET_PTR) {
            conf->hide_headers = prev->hide_headers;
        }

        if (conf->pass_headers == NGX_CONF_UNSET_PTR) {
            conf->pass_headers = prev->pass_headers;
        }
    }

    if (ngx_array_init(&hide_headers, cf->temp_pool, 4, sizeof(ngx_hash_key_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    for (h = default_hide_headers; h->len; h++) {
        hk = ngx_array_push(&hide_headers);
        if (hk == NULL) {
            return NGX_ERROR;
        }

        hk->key = *h;
        hk->key_hash = ngx_hash_key_lc(h->data, h->len);
        hk->value = (void *) 1;
    }

    if (conf->hide_headers != NGX_CONF_UNSET_PTR) {

        h = conf->hide_headers->elts;

        for (i = 0; i < conf->hide_headers->nelts; i++) {

            hk = hide_headers.elts;

            for (j = 0; j < hide_headers.nelts; j++) {
                if (ngx_strcasecmp(h[i].data, hk[j].key.data) == 0) {
                    goto exist;
                }
            }

            hk = ngx_array_push(&hide_headers);
            if (hk == NULL) {
                return NGX_ERROR;
            }

            hk->key = h[i];
            hk->key_hash = ngx_hash_key_lc(h[i].data, h[i].len);
            hk->value = (void *) 1;

        exist:

            continue;
        }
    }

    if (conf->pass_headers != NGX_CONF_UNSET_PTR) {

        h = conf->pass_headers->elts;
        hk = hide_headers.elts;

        for (i = 0; i < conf->pass_headers->nelts; i++) {
            for (j = 0; j < hide_headers.nelts; j++) {

                if (hk[j].key.data == NULL) {
                    continue;
                }

                if (ngx_strcasecmp(h[i].data, hk[j].key.data) == 0) {
                    hk[j].key.data = NULL;
                    break;
                }
            }
        }
    }

    hash->hash = &conf->hide_headers_hash;
    hash->key = ngx_hash_key_lc;
    hash->pool = cf->pool;
    hash->temp_pool = NULL;

    return ngx_hash_init(hash, hide_headers.elts, hide_headers.nelts);
}


static void *
ngx_http_upstream_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_upstream_main_conf_t  *umcf;
    umcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_upstream_main_conf_t));
    if (umcf == NULL) {
        return NULL;
    }
    if (ngx_array_init(&umcf->upstreams, cf->pool, 4,  sizeof(ngx_http_upstream_srv_conf_t *)) != NGX_OK)  {
        return NULL;
    }
    return umcf;
}


static char *
ngx_http_upstream_init_main_conf(ngx_conf_t *cf, void *conf)
{//³õÊ¼»¯upstreamÄ£¿éµÄmain_confÊı¾İ,ÔÚhttp{}Ö¸Áî½âÎöÍê³ÉÖ®Ç°»áÏÈcreate,Íê³ÉÖ®ºóinitÖ®
//ÏÂÃæÖ»ÊÇ½«ngx_http_upstream_headers_in·ÅÈëumcf->headers_in_hashÀïÃæ¡£
    ngx_http_upstream_main_conf_t  *umcf = conf;

    ngx_uint_t                      i;
    ngx_array_t                     headers_in;
    ngx_hash_key_t                 *hk;
    ngx_hash_init_t                 hash;
    ngx_http_upstream_init_pt       init;
    ngx_http_upstream_header_t     *header;
    ngx_http_upstream_srv_conf_t  **uscfp;

    uscfp = umcf->upstreams.elts;
    for (i = 0; i < umcf->upstreams.nelts; i++) {//Èç¹ûÅäÖÃÎÄ¼şÀïÃæÃ»ÓĞÖ¸¶¨Ä¬ÈÏ²ßÂÔ£¬ÔòÊ¹ÓÃÂÖÑ¯²ßÂÔ¡£
        init = uscfp[i]->peer.init_upstream ? uscfp[i]->peer.init_upstream : ngx_http_upstream_init_round_robin;
        if (init(cf, uscfp[i]) != NGX_OK) {//µ÷ÓÃÖ®½øĞĞ³õÊ¼»¯¡£Õâ¸öÊÇÖ¸²»Í¬ÀàĞÍµÄÂÖÑ¯²ßÂÔ£¬¹şÏ£²ßÂÔ¶ÔÓ¦µÄ³õÊ¼»¯·½·¨¡£
            return NGX_CONF_ERROR;
        }
    }

    /* upstream_headers_in_hash */
    if (ngx_array_init(&headers_in, cf->temp_pool, 32, sizeof(ngx_hash_key_t)) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
	//ÏÂÃæ½«ngx_http_upstream_headers_inÀïÃæÖ¸¶¨µÄHTTPÍ··ÅÈëheaders_inÀïÃæ¡£
    for (header = ngx_http_upstream_headers_in; header->name.len; header++) {
        hk = ngx_array_push(&headers_in);
        if (hk == NULL) {
            return NGX_CONF_ERROR;
        }
        hk->key = header->name;
        hk->key_hash = ngx_hash_key_lc(header->name.data, header->name.len);
        hk->value = header;
    }
    hash.hash = &umcf->headers_in_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = 512;
    hash.bucket_size = ngx_align(64, ngx_cacheline_size);
    hash.name = "upstream_headers_in_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;
	//³õÊ¼»¯¹şÏ£±íÊı¾İ½á¹¹¡£
    if (ngx_hash_init(&hash, headers_in.elts, headers_in.nelts) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
