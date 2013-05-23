
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_http_upstream_conf_t   upstream;//upstream配置结构，用来存储配置信息的。u->conf = &flcf->upstream;
    ngx_int_t                  index;//代表缓存的key: memcached_key在&cmcf->variables中的下标。
} ngx_http_memcached_loc_conf_t;


typedef struct {
    size_t                     rest;//等于NGX_HTTP_MEMCACHED_END。因为mecache发送时，最后总是会加这个标志的: "END\r\n",正好前面还有一行末尾的\r\n
    ngx_http_request_t        *request;
    ngx_str_t                  key;
} ngx_http_memcached_ctx_t;


static ngx_int_t ngx_http_memcached_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_process_header(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_filter_init(void *data);
static ngx_int_t ngx_http_memcached_filter(void *data, ssize_t bytes);
static void ngx_http_memcached_abort_request(ngx_http_request_t *r);
static void ngx_http_memcached_finalize_request(ngx_http_request_t *r,
    ngx_int_t rc);

static void *ngx_http_memcached_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_memcached_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);

static char *ngx_http_memcached_pass(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_conf_bitmask_t  ngx_http_memcached_next_upstream_masks[] = {
    { ngx_string("error"), NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"), NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_response"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("not_found"), NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { ngx_string("off"), NGX_HTTP_UPSTREAM_FT_OFF },
    { ngx_null_string, 0 }
};


static ngx_command_t  ngx_http_memcached_commands[] = {

    { ngx_string("memcached_pass"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_memcached_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("memcached_bind"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_bind_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.local),
      NULL },

    { ngx_string("memcached_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.connect_timeout),
      NULL },

    { ngx_string("memcached_send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.send_timeout),
      NULL },

    { ngx_string("memcached_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.buffer_size),
      NULL },

    { ngx_string("memcached_read_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.read_timeout),
      NULL },

    { ngx_string("memcached_next_upstream"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.next_upstream),
      &ngx_http_memcached_next_upstream_masks },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_memcached_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_memcached_create_loc_conf,    /* create location configration */
    ngx_http_memcached_merge_loc_conf      /* merge location configration */
};


ngx_module_t  ngx_http_memcached_module = {
    NGX_MODULE_V1,
    &ngx_http_memcached_module_ctx,        /* module context */
    ngx_http_memcached_commands,           /* module directives */
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


static ngx_str_t  ngx_http_memcached_key = ngx_string("memcached_key");


#define NGX_HTTP_MEMCACHED_END   (sizeof(ngx_http_memcached_end) - 1)
static u_char  ngx_http_memcached_end[] = CRLF "END" CRLF;


static ngx_int_t
ngx_http_memcached_handler(ngx_http_request_t *r)
{
    ngx_int_t                       rc;
    ngx_http_upstream_t            *u;
    ngx_http_memcached_ctx_t       *ctx;
    ngx_http_memcached_loc_conf_t  *mlcf;

    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {//memcached只能处理get/head请求。
        return NGX_HTTP_NOT_ALLOWED;
    }
	//由于这是GET和简单HEAD请求，body没用，就丢了。
	//删除客户端连接读事件，如果可以，读取客户端BODY，然后丢掉。如果读完整个BODY了，lingering_close=0.
    rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }
	//自动根据后缀名，如果ngx_http_core_default_types初始化了后缀，
    if ( ngx_http_set_content_type(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
//用来申请upstream大结构体，设置到r->upstream = u;
    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u = r->upstream;

    ngx_str_set(&u->schema, "memcached://");
    u->output.tag = (ngx_buf_tag_t) &ngx_http_memcached_module;

    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_memcached_module);

    u->conf = &mlcf->upstream;//这是upstream的配置数据。
//设置各个回调函数。
    u->create_request = ngx_http_memcached_create_request;
    u->reinit_request = ngx_http_memcached_reinit_request;
    u->process_header = ngx_http_memcached_process_header;
    u->abort_request = ngx_http_memcached_abort_request;
    u->finalize_request = ngx_http_memcached_finalize_request;

    ctx = ngx_palloc(r->pool, sizeof(ngx_http_memcached_ctx_t));
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->rest = NGX_HTTP_MEMCACHED_END;
    ctx->request = r;

    ngx_http_set_ctx(r, ctx, ngx_http_memcached_module);

//设置用来读取memcache的数据的回调函数。
    u->input_filter_init = ngx_http_memcached_filter_init;
    u->input_filter = ngx_http_memcached_filter;
    u->input_filter_ctx = ctx;

    r->main->count++;

//下面跟ngx_http_fastcgi_handler不一样，不需要调用ngx_http_read_client_request_body(r, ngx_http_upstream_init);
//因为上面已经ngx_http_discard_request_body了，设置了ngx_http_discarded_request_body_handler为读数据回调了，该回调直接丢弃BODY。
//所以，我们现在可以直接进入init阶段了。
    ngx_http_upstream_init(r);

    return NGX_DONE;
}

//命令 get <key>*\r\n 。
//nginx只支持单个键值一次获取，不能支持一次获取多个，这个如果有需求其实可以考虑优化。
static ngx_int_t ngx_http_memcached_create_request(ngx_http_request_t *r)
{//根据缓存的key,组成memcache的get 行，设置到r->upstream->request_bufs链表上面去，就一个块。然后没事了。
//上层是ngx_http_upstream_init_request调用这里，调用完成后，会connect后端mecached的。然后就是发送数据。
    size_t                          len;
    uintptr_t                       escape;
    ngx_buf_t                      *b;
    ngx_chain_t                    *cl;
    ngx_http_memcached_ctx_t       *ctx;
    ngx_http_variable_value_t      *vv;
    ngx_http_memcached_loc_conf_t  *mlcf;

    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_memcached_module);
//根据代表缓存的key: memcached_key在&cmcf->variables中的下标。从而得到主键的值。
//这个key 从哪里来呢，这里: set $memcached_key $host$uri;
    vv = ngx_http_get_indexed_variable(r, mlcf->index);
    if (vv == NULL || vv->not_found || vv->len == 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "the \"$memcached_key\" variable is not set");
        return NGX_ERROR;
    }
//第一个参数为NULL，就不会拷贝，只是返回能不能需要转义，为啥是2倍呢，因为返回的是需要转义的字符数目。转义翻倍
    escape = 2 * ngx_escape_uri(NULL, vv->data, vv->len, NGX_ESCAPE_MEMCACHED);
    len = sizeof("get ") - 1 + vv->len + escape + sizeof(CRLF) - 1;

    b = ngx_create_temp_buf(r->pool, len);
    if (b == NULL) {
        return NGX_ERROR;
    }
	//申请一个链接节点，用来存储这个简单的get指令
    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }
    cl->buf = b;//指向这坨buffer
    cl->next = NULL;

    r->upstream->request_bufs = cl;//代表要发送给后端的数据链表结构，发送的时候会发request_bufs这里的内容
    *b->last++ = 'g'; *b->last++ = 'e'; *b->last++ = 't'; *b->last++ = ' ';

    ctx = ngx_http_get_module_ctx(r, ngx_http_memcached_module);
    ctx->key.data = b->last;

    if (escape == 0) {//如果不需要做转义，就直接拷贝。
        b->last = ngx_copy(b->last, vv->data, vv->len);

    } else {//否则在"get "后面追加转义后的key.这样就组成了: get mykey
        b->last = (u_char *) ngx_escape_uri(b->last, vv->data, vv->len, NGX_ESCAPE_MEMCACHED);
    }
	//得到key 的长度，设置一下。
    ctx->key.len = b->last - ctx->key.data;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http memcached request: \"%V\"", &ctx->key);
    *b->last++ = CR; *b->last++ = LF;//\r\n
    return NGX_OK;//返回，上层会connect mecached的。
}


static ngx_int_t
ngx_http_memcached_reinit_request(ngx_http_request_t *r)
{
    return NGX_OK;
}


/*
取回命令
一行取回命令如下：
get <key>*\r\n
- <key>* 表示一个或多个键值，由空格隔开的字串
这行命令以后，客户端的等待0个或多个项目，每项都会收到一行文本，然后跟着数据区块。所有项目传送完毕后，服务器发送以下字串：
"END\r\n"
来指示回应完毕。
服务器用以下形式发送每项内容：
VALUE <key> <flags> <bytes>\r\n
<data block>\r\n
- <key> 是所发送的键名
- <flags> 是存储命令所设置的记号
- <bytes> 是随后数据块的长度，*不包括* 它的界定符“\r\n”
- <data block> 是发送的数据

如果在取回请求中发送了一些键名，而服务器没有送回项目列表，这意味着服务器没这些键名（可能因为它们从未被存储，
或者为给其他内容腾出空间而被删除，或者到期，或者被已客户端删除）。
*/
static ngx_int_t ngx_http_memcached_process_header(ngx_http_request_t *r)
{//这个函数的调用时机是: ngx_http_upstream_process_header函数调用ngx_unix_recv读取mecached的数据，
//然后会调用u->process_header，也就是这个函数，来解析mecached的格式的返回数据。
//函数处理了mecached返回的第一行: VALUE <key> <flags> <bytes>\r\n,设置content_length_n，status。不过HTML还没有解析或者读取。
    u_char                    *p, *len;
    ngx_str_t                  line;
    ngx_http_upstream_t       *u;
    ngx_http_memcached_ctx_t  *ctx;

    u = r->upstream;
	//下面是不是有点偷懒，如果没有得到LF换行，那么每次都会循环这个buffer，多不好呀。
    for (p = u->buffer.pos; p < u->buffer.last; p++) {
        if (*p == LF) {//碰到了\n
            goto found;
        }
    }
    return NGX_AGAIN;
found:
//得到
    *p = '\0';//从这个回车截断。
    line.len = p - u->buffer.pos - 1;
    line.data = u->buffer.pos;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "memcached: \"%V\"", &line);

    p = u->buffer.pos;
    ctx = ngx_http_get_module_ctx(r, ngx_http_memcached_module);
    if (ngx_strncmp(p, "VALUE ", sizeof("VALUE ") - 1) == 0) {
        p += sizeof("VALUE ") - 1;
        if (ngx_strncmp(p, ctx->key.data, ctx->key.len) != 0) {//key 跟我这个请求发送的不一样，悲剧了。
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "memcached sent invalid key in response \"%V\" for key \"%V\"", &line, &ctx->key);
            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
        }
		//返回行是这样的: VALUE <key> <flags> <bytes>\r\n
        p += ctx->key.len;//跳过key.去看flag
        if (*p++ != ' ') {
            goto no_valid;
        }
        /* skip flags */
        while (*p) {//nginx不处理flags。没用
            if (*p++ == ' ') {
                goto length;
            }
        }
        goto no_valid;
    length:
        len = p;
        while (*p && *p++ != CR) { /* void */ }//一直扫描到\r的地方。如果mecache不发\r,就傻眼了，因为这里没有处理是不是有 \r
        //从n到p的地方就是后面的结果长度了: <bytes>
        r->headers_out.content_length_n = ngx_atoof(len, p - len - 1);
        if (r->headers_out.content_length_n == -1) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "memcached sent invalid length in response \"%V\" for key \"%V\"", &line, &ctx->key);
            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
        }

        u->headers_in.status_n = 200;
        u->state->status = 200;
        u->buffer.pos = p + 1;//从这后面的就是buf啦。不过buf还没有读取到。这个buf就是读取的后端mecached的数据。
        return NGX_OK;
    }
    if (ngx_strcmp(p, "END\x0d") == 0) {//结束了，那说明这一行没东西，因为nginx一次只发送一块数据。所以这里很简单。
    //这里虽然是404但是，其实我们可以做的有意思的，比如针对404做redirect到实际的后端机器。这样就是缓存失效，自动回源了。
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0, "key: \"%V\" was not found by memcached", &ctx->key);
        u->headers_in.status_n = 404;
        u->state->status = 404;
        return NGX_OK;
    }
no_valid:
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "memcached sent invalid response: \"%V\"", &line);
    return NGX_HTTP_UPSTREAM_INVALID_HEADER;
}


static ngx_int_t
ngx_http_memcached_filter_init(void *data)
{
    ngx_http_memcached_ctx_t  *ctx = data;

    ngx_http_upstream_t  *u;
    u = ctx->request->upstream;
	//因为mecache发送时，最后总是会加这个标志的: "END\r\n",正好前面还有一行末尾的\r\n。
    u->length += NGX_HTTP_MEMCACHED_END;
    return NGX_OK;
}


static ngx_int_t
ngx_http_memcached_filter(void *data, ssize_t bytes)
{//这个函数用来接收mecache的body数据，conf->upstream.buffering = 0，所以memcached模块不支持buffering.
//这个函数的调用时机: ngx_http_upstream_process_non_buffered_upstream等调用ngx_unix_recv接收到upstream返回的数据后
//就调用这里进行协议转换，不过目前转换不多。
//注意这个函数调用的时候，u->buffer->last和pos并没有更新的，
//也就是什么呢，刚刚读取的bytes个字节的数据，位于u->buffer->last之后。pos目前不准。

    ngx_http_memcached_ctx_t  *ctx = data;

    u_char               *last;
    ngx_buf_t            *b;
    ngx_chain_t          *cl, **ll;
    ngx_http_upstream_t  *u;

    u = ctx->request->upstream;
    b = &u->buffer;//得到接收的数据。

    if (u->length == ctx->rest) {//rest初始化为NGX_HTTP_MEMCACHED_END。rest表示还需要读取多少"END\r\n"类型的数据。
        if (ngx_strncmp(b->last, ngx_http_memcached_end + NGX_HTTP_MEMCACHED_END - ctx->rest, bytes) != 0) {
            ngx_log_error(NGX_LOG_ERR, ctx->request->connection->log, 0, "memcached sent invalid trailer");
            u->length = 0;
            ctx->rest = 0;
            return NGX_OK;
        }
		//搞到末尾了。所以下面其实应该不用减了，没了的。
        u->length -= bytes;
        ctx->rest -= bytes;
        return NGX_OK;
    }

    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {
        ll = &cl->next;//找到要输出去的数据链表的最后部分。
    }
//申请一个链接节点。
    cl = ngx_chain_get_free_buf(ctx->request->pool, &u->free_bufs);
    if (cl == NULL) {
        return NGX_ERROR;
    }
    cl->buf->flush = 1;
    cl->buf->memory = 1;
    *ll = cl;//这个新的节点的数据挂载到out_bufs的最后面。
    last = b->last;//得到这块buf之前的尾部，可能有残留数据。
    cl->buf->pos = last;//等于尾部，因为这个之前的b->last是上一块数据的值。
    b->last += bytes;//调整尾部。
    cl->buf->last = b->last;//初始化要发送给客户端的这块的尾部，可能需要调，比如满了数据了。
    cl->buf->tag = u->output.tag;

    ngx_log_debug4(NGX_LOG_DEBUG_HTTP, ctx->request->connection->log, 0, "memcached filter bytes:%z size:%z length:%z rest:%z", bytes, b->last - b->pos, u->length, ctx->rest);
    if (bytes <= (ssize_t) (u->length - NGX_HTTP_MEMCACHED_END)) {//读取的字节数还不是数据区的末尾。将这块数据纳入链表末尾，然后减少还剩下的数据量变量。
        u->length -= bytes;
        return NGX_OK;
    }
//否则，头部都在这里了。
    last += u->length - NGX_HTTP_MEMCACHED_END;//直接移动到后面去。
    if (ngx_strncmp(last, ngx_http_memcached_end, b->last - last) != 0) {
        ngx_log_error(NGX_LOG_ERR, ctx->request->connection->log, 0,
                      "memcached sent invalid trailer");
    }

    ctx->rest -= b->last - last;//后面还有这么多的"END\r\n"数据，所以直接rest记录还需要读取多少这样的无用数据。
    b->last = last;//标记这块buf的结尾。
    cl->buf->last = last;//标记这块buf的结尾。
    u->length = ctx->rest;//后面只需要处理尾部的结尾了。
    return NGX_OK;
}


static void
ngx_http_memcached_abort_request(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "abort http memcached request");
    return;
}


static void
ngx_http_memcached_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "finalize http memcached request");
    return;
}


static void *
ngx_http_memcached_create_loc_conf(ngx_conf_t *cf)
{//初始化配置。
    ngx_http_memcached_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_memcached_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->upstream.bufs.num = 0;
     *     conf->upstream.next_upstream = 0;
     *     conf->upstream.temp_path = NULL;
     *     conf->upstream.uri = { 0, NULL };
     *     conf->upstream.location = NULL;
     */

    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;

    /* the hardcoded values */
    conf->upstream.cyclic_temp_file = 0;
    conf->upstream.buffering = 0;//我是个简单的模块，不支持buffering那么好的东西。接收一点发送一点。
    conf->upstream.ignore_client_abort = 0;
    conf->upstream.send_lowat = 0;
    conf->upstream.bufs.num = 0;
    conf->upstream.busy_buffers_size = 0;
    conf->upstream.max_temp_file_size = 0;
    conf->upstream.temp_file_write_size = 0;
    conf->upstream.intercept_errors = 1;
    conf->upstream.intercept_404 = 1;
    conf->upstream.pass_request_headers = 0;
    conf->upstream.pass_request_body = 0;

    conf->index = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_memcached_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_memcached_loc_conf_t *prev = parent;
    ngx_http_memcached_loc_conf_t *conf = child;

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);

    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                              prev->upstream.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
    }

    if (conf->index == NGX_CONF_UNSET) {
        conf->index = prev->index;
    }

    return NGX_CONF_OK;
}

//解析到syntax:	memcached_pass address;指令的 时候被调用,注册ngx_http_memcached_handler为内容处理模块
//并ngx_http_memcached_key的下标设置到mlcf->index上，这是i缓存的主键在变量表中的下标。
static char * ngx_http_memcached_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_memcached_loc_conf_t *mlcf = conf;

    ngx_str_t                 *value;
    ngx_url_t                  u;
    ngx_http_core_loc_conf_t  *clcf;

    if (mlcf->upstream.upstream) {
        return "is duplicate";
    }

    value = cf->args->elts;
    ngx_memzero(&u, sizeof(ngx_url_t));
    u.url = value[1];
    u.no_resolve = 1;

	//如果u代表的server已经存在，则返回句柄，否则在umcf->upstreams里面新加一个，设置初始化。
	//把这条指令当做一个新的upstream存在，放入cmcf->upstreams数组里面，记录有多少upstreams
    mlcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);
    if (mlcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);//设置到核心模块的loc_conf里面

	//老办法，设置这个到handler,这样在find_config_phrase上
	//设置句柄，会在ngx_http_update_location_config里面设置为content_handle的，从而在content phase中被调用
    clcf->handler = ngx_http_memcached_handler;

	//如果配置的locaton 后面最后一个字符为/路径结束符，则需要自动重定向。
    if (clcf->name.data[clcf->name.len - 1] == '/') {//指令的最后一个字符是/，那就需要重定向。
        clcf->auto_redirect = 1;
    }
	//定义了一个固定的名字，代表缓存的key: memcached_key。记住其在&cmcf->variables中的下标，这样下次可以获取key ,发送get memcached请求。
    mlcf->index = ngx_http_get_variable_index(cf, &ngx_http_memcached_key);
    if (mlcf->index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
