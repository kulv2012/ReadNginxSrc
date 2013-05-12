
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>


static ngx_int_t ngx_http_variable_request(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static void ngx_http_variable_request_set(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_request_get_size(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static void ngx_http_variable_request_set_size(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_header(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_headers(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);

static ngx_int_t ngx_http_variable_unknown_header_in(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_unknown_header_out(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_request_line(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_cookie(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_argument(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);

static ngx_int_t ngx_http_variable_host(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_binary_remote_addr(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_remote_addr(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_remote_port(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_server_addr(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_server_port(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_scheme(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_is_args(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_document_root(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_realpath_root(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_request_filename(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_server_name(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_request_method(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_remote_user(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_body_bytes_sent(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_request_completion(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_request_body(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_request_body_file(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);

static ngx_int_t ngx_http_variable_sent_content_type(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_sent_content_length(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_sent_location(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_sent_last_modified(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_sent_connection(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_sent_keep_alive(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_sent_transfer_encoding(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);

static ngx_int_t ngx_http_variable_nginx_version(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_hostname(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_variable_pid(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);

/*
 * TODO:
 *     Apache CGI: AUTH_TYPE, PATH_INFO (null), PATH_TRANSLATED
 *                 REMOTE_HOST (null), REMOTE_IDENT (null),
 *                 SERVER_SOFTWARE
 *
 *     Apache SSI: DOCUMENT_NAME, LAST_MODIFIED, USER_NAME (file owner)
 */

/*
 * the $http_host, $http_user_agent, $http_referer, $http_via,
 * and $http_x_forwarded_for variables may be handled by generic
 * ngx_http_variable_unknown_header_in(), but for perfomance reasons
 * they are handled using dedicated entries
 */

static ngx_http_variable_t  ngx_http_core_variables[] = {

    { ngx_string("http_host"), NULL, ngx_http_variable_header,
      offsetof(ngx_http_request_t, headers_in.host), 0, 0 },

    { ngx_string("http_user_agent"), NULL, ngx_http_variable_header,
      offsetof(ngx_http_request_t, headers_in.user_agent), 0, 0 },

    { ngx_string("http_referer"), NULL, ngx_http_variable_header,
      offsetof(ngx_http_request_t, headers_in.referer), 0, 0 },

#if (NGX_HTTP_GZIP)
    { ngx_string("http_via"), NULL, ngx_http_variable_header,
      offsetof(ngx_http_request_t, headers_in.via), 0, 0 },
#endif

#if (NGX_HTTP_PROXY || NGX_HTTP_REALIP)
    { ngx_string("http_x_forwarded_for"), NULL, ngx_http_variable_header,
      offsetof(ngx_http_request_t, headers_in.x_forwarded_for), 0, 0 },
#endif

    { ngx_string("http_cookie"), NULL, ngx_http_variable_headers,
      offsetof(ngx_http_request_t, headers_in.cookies), 0, 0 },

    { ngx_string("content_length"), NULL, ngx_http_variable_header,
      offsetof(ngx_http_request_t, headers_in.content_length), 0, 0 },

    { ngx_string("content_type"), NULL, ngx_http_variable_header,
      offsetof(ngx_http_request_t, headers_in.content_type), 0, 0 },

    { ngx_string("host"), NULL, ngx_http_variable_host, 0, 0, 0 },

    { ngx_string("binary_remote_addr"), NULL,
      ngx_http_variable_binary_remote_addr, 0, 0, 0 },

    { ngx_string("remote_addr"), NULL, ngx_http_variable_remote_addr, 0, 0, 0 },

    { ngx_string("remote_port"), NULL, ngx_http_variable_remote_port, 0, 0, 0 },

    { ngx_string("server_addr"), NULL, ngx_http_variable_server_addr, 0, 0, 0 },

    { ngx_string("server_port"), NULL, ngx_http_variable_server_port, 0, 0, 0 },

    { ngx_string("server_protocol"), NULL, ngx_http_variable_request,
      offsetof(ngx_http_request_t, http_protocol), 0, 0 },

    { ngx_string("scheme"), NULL, ngx_http_variable_scheme, 0, 0, 0 },

    { ngx_string("request_uri"), NULL, ngx_http_variable_request,
      offsetof(ngx_http_request_t, unparsed_uri), 0, 0 },

    { ngx_string("uri"), NULL, ngx_http_variable_request,
      offsetof(ngx_http_request_t, uri),
      NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("document_uri"), NULL, ngx_http_variable_request,
      offsetof(ngx_http_request_t, uri),
      NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("request"), NULL, ngx_http_variable_request_line, 0, 0, 0 },

    { ngx_string("document_root"), NULL,
      ngx_http_variable_document_root, 0, NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("realpath_root"), NULL,
      ngx_http_variable_realpath_root, 0, NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("query_string"), NULL, ngx_http_variable_request,
      offsetof(ngx_http_request_t, args),
      NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("args"),
      ngx_http_variable_request_set,
      ngx_http_variable_request,
      offsetof(ngx_http_request_t, args),
      NGX_HTTP_VAR_CHANGEABLE|NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("is_args"), NULL, ngx_http_variable_is_args,
      0, NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("request_filename"), NULL,
      ngx_http_variable_request_filename, 0,
      NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("server_name"), NULL, ngx_http_variable_server_name, 0, 0, 0 },

    { ngx_string("request_method"), NULL,
      ngx_http_variable_request_method, 0,
      NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("remote_user"), NULL, ngx_http_variable_remote_user, 0, 0, 0 },

    { ngx_string("body_bytes_sent"), NULL, ngx_http_variable_body_bytes_sent,
      0, 0, 0 },

    { ngx_string("request_completion"), NULL,
      ngx_http_variable_request_completion,
      0, 0, 0 },

    { ngx_string("request_body"), NULL,
      ngx_http_variable_request_body,
      0, 0, 0 },

    { ngx_string("request_body_file"), NULL,
      ngx_http_variable_request_body_file,
      0, 0, 0 },

    { ngx_string("sent_http_content_type"), NULL,
      ngx_http_variable_sent_content_type, 0, 0, 0 },

    { ngx_string("sent_http_content_length"), NULL,
      ngx_http_variable_sent_content_length, 0, 0, 0 },

    { ngx_string("sent_http_location"), NULL,
      ngx_http_variable_sent_location, 0, 0, 0 },

    { ngx_string("sent_http_last_modified"), NULL,
      ngx_http_variable_sent_last_modified, 0, 0, 0 },

    { ngx_string("sent_http_connection"), NULL,
      ngx_http_variable_sent_connection, 0, 0, 0 },

    { ngx_string("sent_http_keep_alive"), NULL,
      ngx_http_variable_sent_keep_alive, 0, 0, 0 },

    { ngx_string("sent_http_transfer_encoding"), NULL,
      ngx_http_variable_sent_transfer_encoding, 0, 0, 0 },

    { ngx_string("sent_http_cache_control"), NULL, ngx_http_variable_headers,
      offsetof(ngx_http_request_t, headers_out.cache_control), 0, 0 },

    { ngx_string("limit_rate"), ngx_http_variable_request_set_size,
      ngx_http_variable_request_get_size,
      offsetof(ngx_http_request_t, limit_rate),
      NGX_HTTP_VAR_CHANGEABLE|NGX_HTTP_VAR_NOCACHEABLE, 0 },

    { ngx_string("nginx_version"), NULL, ngx_http_variable_nginx_version,
      0, 0, 0 },

    { ngx_string("hostname"), NULL, ngx_http_variable_hostname,
      0, 0, 0 },

    { ngx_string("pid"), NULL, ngx_http_variable_pid,
      0, 0, 0 },

    { ngx_null_string, NULL, NULL, 0, 0, 0 }
};


ngx_http_variable_value_t  ngx_http_variable_null_value =
    ngx_http_variable("");
ngx_http_variable_value_t  ngx_http_variable_true_value =
    ngx_http_variable("1");


ngx_http_variable_t *
ngx_http_add_variable(ngx_conf_t *cf, ngx_str_t *name, ngx_uint_t flags)
{//在cmcf->variables_keys里面查找有没有key相同的，如果有就返回，否则申请一个新的槽位。
//变量的get/set回调还没有设置，需要上层设置的。
    ngx_int_t                   rc;
    ngx_uint_t                  i;
    ngx_hash_key_t             *key;
    ngx_http_variable_t        *v;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    key = cmcf->variables_keys->keys.elts;
    for (i = 0; i < cmcf->variables_keys->keys.nelts; i++) {
        if (name->len != key[i].key.len || ngx_strncasecmp(name->data, key[i].key.data, name->len) != 0) {
            continue;
        }
        v = key[i].value;
        if (!(v->flags & NGX_HTTP_VAR_CHANGEABLE)) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "the duplicate \"%V\" variable", name);
            return NULL;
        }
        return v;
    }
	//没有找到已有的，下面申请一个
    v = ngx_palloc(cf->pool, sizeof(ngx_http_variable_t));
    if (v == NULL) {
        return NULL;
    }
    v->name.len = name->len;
    v->name.data = ngx_pnalloc(cf->pool, name->len);
    if (v->name.data == NULL) {
        return NULL;
    }
    ngx_strlow(v->name.data, name->data, name->len);
    v->set_handler = NULL;//回调需要上层设置。
    v->get_handler = NULL;
    v->data = 0;
    v->flags = flags;
    v->index = 0;//
    rc = ngx_hash_add_key(cmcf->variables_keys, &v->name, v, 0);
    if (rc == NGX_ERROR) {
        return NULL;
    }
    if (rc == NGX_BUSY) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "conflicting variable name \"%V\"", name);
        return NULL;
    }
    return v;
}


ngx_int_t
ngx_http_get_variable_index(ngx_conf_t *cf, ngx_str_t *name)
{//根据变量名字，获取其在&cmcf->variables里面的下标。如果没有，就新建它。
    ngx_uint_t                  i;
    ngx_http_variable_t        *v;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    v = cmcf->variables.elts;
    if (v == NULL) {//默认申请四个变量空间，为啥是4个呢
        if (ngx_array_init(&cmcf->variables, cf->pool, 4, sizeof(ngx_http_variable_t)) != NGX_OK) {
            return NGX_ERROR;
        }
    } else {
        for (i = 0; i < cmcf->variables.nelts; i++) {//如果cmcf->variables.elts已存在，则根据名字查找，如果找到就返回下标，否则添加一个。
            if (name->len != v[i].name.len || ngx_strncasecmp(name->data, v[i].name.data, name->len) != 0)  {
                continue;
            }
            return i;
        }
    }
    v = ngx_array_push(&cmcf->variables);
    if (v == NULL) {
        return NGX_ERROR;
    }
    v->name.len = name->len;
    v->name.data = ngx_pnalloc(cf->pool, name->len);
    if (v->name.data == NULL) {
        return NGX_ERROR;
    }
    ngx_strlow(v->name.data, name->data, name->len);
    v->set_handler = NULL;//回调上层设置。不过这个变量貌似鸭羹就没有设置过。
    v->get_handler = NULL;
    v->data = 0;
    v->flags = 0;
    v->index = cmcf->variables.nelts - 1;

    return cmcf->variables.nelts - 1;
}


ngx_http_variable_value_t *
ngx_http_get_indexed_variable(ngx_http_request_t *r, ngx_uint_t index)
{//根据下表获取变量地址。
    ngx_http_variable_t        *v;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    if (cmcf->variables.nelts <= index) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0, "unknown variable index: %d", index);
        return NULL;
    }
    if (r->variables[index].not_found || r->variables[index].valid) {//已经获取过一次了。
        return &r->variables[index];
    }
    v = cmcf->variables.elts;
	//调用这个变量的get_handler句柄，从而得到其值。handler为ngx_http_variable_argument或者ngx_http_variable_cookie等。
	//这些handler里面会将valid设置为1的，也就是调用一次就OK了。
    if (v[index].get_handler(r, &r->variables[index], v[index].data) == NGX_OK) {
        if (v[index].flags & NGX_HTTP_VAR_NOCACHEABLE) {
            r->variables[index].no_cacheable = 1;
        }
        return &r->variables[index];
    }

    r->variables[index].valid = 0;
    r->variables[index].not_found = 1;

    return NULL;
}


ngx_http_variable_value_t *
ngx_http_get_flushed_variable(ngx_http_request_t *r, ngx_uint_t index)
{
    ngx_http_variable_value_t  *v;
    v = &r->variables[index];
    if (v->valid) {
        if (!v->no_cacheable) {
            return v;
        }
        v->valid = 0;
        v->not_found = 0;
    }
    return ngx_http_get_indexed_variable(r, index);
}


ngx_http_variable_value_t *
ngx_http_get_variable(ngx_http_request_t *r, ngx_str_t *name, ngx_uint_t key)
{
    ngx_http_variable_t        *v;
    ngx_http_variable_value_t  *vv;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
	//先根据哈希表查找变量是否存在，如果存在，则根据其保存在哈希表里面的index获取变量的内容。或者调用get_handler
    v = ngx_hash_find(&cmcf->variables_hash, key, name->data, name->len);
    if (v) {
        if (v->flags & NGX_HTTP_VAR_INDEXED) {
            return ngx_http_get_flushed_variable(r, v->index);
        } else {
            vv = ngx_palloc(r->pool, sizeof(ngx_http_variable_value_t));
            if (vv && v->get_handler(r, vv, v->data) == NGX_OK) {
                return vv;
            }
            return NULL;
        }
    }
	//如果不存在variables里面，则看一下是否是预定义的变量。
    vv = ngx_palloc(r->pool, sizeof(ngx_http_variable_value_t));
    if (vv == NULL) {
        return NULL;
    }
    if (ngx_strncmp(name->data, "http_", 5) == 0) {
        if (ngx_http_variable_unknown_header_in(r, vv, (uintptr_t) name) == NGX_OK){
            return vv;
        }
        return NULL;
    }
    if (ngx_strncmp(name->data, "sent_http_", 10) == 0) {
        if (ngx_http_variable_unknown_header_out(r, vv, (uintptr_t) name)  == NGX_OK)  {
            return vv;
        }
        return NULL;
    }
    if (ngx_strncmp(name->data, "upstream_http_", 14) == 0) {
        if (ngx_http_upstream_header_variable(r, vv, (uintptr_t) name) == NGX_OK) {
            return vv;
        }
        return NULL;
    }
    if (ngx_strncmp(name->data, "cookie_", 7) == 0) {
        if (ngx_http_variable_cookie(r, vv, (uintptr_t) name) == NGX_OK) {
            return vv;
        }
        return NULL;
    }
    if (ngx_strncmp(name->data, "arg_", 4) == 0) {
        if (ngx_http_variable_argument(r, vv, (uintptr_t) name) == NGX_OK) {
            return vv;
        }
        return NULL;
    }
    vv->not_found = 1;
    return vv;
}


static ngx_int_t
ngx_http_variable_request(ngx_http_request_t *r, ngx_http_variable_value_t *v,  uintptr_t data)
{
    ngx_str_t  *s;

    s = (ngx_str_t *) ((char *) r + data);
    if (s->data) {
        v->len = s->len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = s->data;
    } else {
        v->not_found = 1;
    }
    return NGX_OK;
}


static void ngx_http_variable_request_set(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_str_t  *s;
    s = (ngx_str_t *) ((char *) r + data);
    s->len = v->len;
    s->data = v->data;
}


static ngx_int_t
ngx_http_variable_request_get_size(ngx_http_request_t *r,ngx_http_variable_value_t *v, uintptr_t data)
{
    size_t  *sp;
    sp = (size_t *) ((char *) r + data);
    v->data = ngx_pnalloc(r->pool, NGX_SIZE_T_LEN);
    if (v->data == NULL) {
        return NGX_ERROR;
    }

    v->len = ngx_sprintf(v->data, "%uz", *sp) - v->data;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;
}


static void
ngx_http_variable_request_set_size(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ssize_t    s, *sp;
    ngx_str_t  val;

    val.len = v->len;
    val.data = v->data;

    s = ngx_parse_size(&val);
    if (s == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "invalid size \"%V\"", &val);
        return;
    }
    sp = (ssize_t *) ((char *) r + data);
    *sp = s;
    return;
}


static ngx_int_t
ngx_http_variable_header(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_table_elt_t  *h;

    h = *(ngx_table_elt_t **) ((char *) r + data);
    if (h) {
        v->len = h->value.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = h->value.data;

    } else {
        v->not_found = 1;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_headers(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
    ssize_t            len;
    u_char            *p;
    ngx_uint_t         i, n;
    ngx_array_t       *a;
    ngx_table_elt_t  **h;

    a = (ngx_array_t *) ((char *) r + data);

    n = a->nelts;

    if (n == 0) {
        v->not_found = 1;
        return NGX_OK;
    }

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    h = a->elts;

    if (n == 1) {
        v->len = (*h)->value.len;
        v->data = (*h)->value.data;

        return NGX_OK;
    }

    len = - (ssize_t) (sizeof("; ") - 1);

    for (i = 0; i < n; i++) {
        len += h[i]->value.len + sizeof("; ") - 1;
    }

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->len = len;
    v->data = p;

    for (i = 0; /* void */ ; i++) {
        p = ngx_copy(p, h[i]->value.data, h[i]->value.len);

        if (i == n - 1) {
            break;
        }

        *p++ = ';'; *p++ = ' ';
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_unknown_header_in(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    return ngx_http_variable_unknown_header(v, (ngx_str_t *) data,
                                            &r->headers_in.headers.part,
                                            sizeof("http_") - 1);
}


static ngx_int_t
ngx_http_variable_unknown_header_out(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    return ngx_http_variable_unknown_header(v, (ngx_str_t *) data,
                                            &r->headers_out.headers.part,
                                            sizeof("sent_http_") - 1);
}


ngx_int_t
ngx_http_variable_unknown_header(ngx_http_variable_value_t *v, ngx_str_t *var,
    ngx_list_part_t *part, size_t prefix)
{
    u_char            ch;
    ngx_uint_t        i, n;
    ngx_table_elt_t  *header;

    header = part->elts;

    for (i = 0; /* void */ ; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            header = part->elts;
            i = 0;
        }

        for (n = 0; n + prefix < var->len && n < header[i].key.len; n++) {
            ch = header[i].key.data[n];

            if (ch >= 'A' && ch <= 'Z') {
                ch |= 0x20;

            } else if (ch == '-') {
                ch = '_';
            }

            if (var->data[n + prefix] != ch) {
                break;
            }
        }

        if (n + prefix == var->len && n == header[i].key.len) {
            v->len = header[i].value.len;
            v->valid = 1;
            v->no_cacheable = 0;
            v->not_found = 0;
            v->data = header[i].value.data;

            return NGX_OK;
        }
    }

    v->not_found = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_request_line(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char  *p, *s;

    s = r->request_line.data;
    if (s == NULL) {//如果请求行没有，肯定是还没有设置，这里将其设置了。
        s = r->request_start;
        if (s == NULL) {
            v->not_found = 1;
            return NGX_OK;
        }
        for (p = s; p < r->header_in->last; p++) {
            if (*p == CR || *p == LF) {
                break;
            }
        }
        r->request_line.len = p - s;
        r->request_line.data = s;
    }
	//记录请求航的数据到输出参数中
    v->len = r->request_line.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = s;
    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_cookie(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_str_t *name = (ngx_str_t *) data;//参数data指向要获取的cookie名字。
    ngx_str_t  cookie, s;

    s.len = name->len - (sizeof("cookie_") - 1);
    s.data = name->data + sizeof("cookie_") - 1;//移动到后面指向cookie名字
    if (ngx_http_parse_multi_header_lines(&r->headers_in.cookies, &s, &cookie)  == NGX_DECLINED)  {
        v->not_found = 1;//解析cookie格式，查找指定cookie
        return NGX_OK;
    }
    v->len = cookie.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = cookie.data;
    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_argument(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_str_t *name = (ngx_str_t *) data;

    u_char     *arg;
    size_t      len;
    ngx_str_t   value;

    len = name->len - (sizeof("arg_") - 1);
    arg = name->data + sizeof("arg_") - 1;
    if (ngx_http_arg(r, arg, len, &value) != NGX_OK) {
        v->not_found = 1;
        return NGX_OK;
    }
    v->data = value.data;
    v->len = value.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_host(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
    ngx_http_core_srv_conf_t  *cscf;

    if (r->headers_in.server.len) {
        v->len = r->headers_in.server.len;
        v->data = r->headers_in.server.data;
    } else {
        cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);
        v->len = cscf->server_name.len;
        v->data = cscf->server_name.data;
    }
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_binary_remote_addr(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    struct sockaddr_in   *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6  *sin6;
#endif

    switch (r->connection->sockaddr->sa_family) {

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = (struct sockaddr_in6 *) r->connection->sockaddr;

        v->len = sizeof(struct in6_addr);
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = sin6->sin6_addr.s6_addr;

        break;
#endif

    default: /* AF_INET */
        sin = (struct sockaddr_in *) r->connection->sockaddr;

        v->len = sizeof(in_addr_t);
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = (u_char *) &sin->sin_addr;

        break;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_remote_addr(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    v->len = r->connection->addr_text.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = r->connection->addr_text.data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_remote_port(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_uint_t            port;
    struct sockaddr_in   *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6  *sin6;
#endif

    v->len = 0;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    v->data = ngx_pnalloc(r->pool, sizeof("65535") - 1);
    if (v->data == NULL) {
        return NGX_ERROR;
    }

    switch (r->connection->sockaddr->sa_family) {

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = (struct sockaddr_in6 *) r->connection->sockaddr;
        port = ntohs(sin6->sin6_port);
        break;
#endif

    default: /* AF_INET */
        sin = (struct sockaddr_in *) r->connection->sockaddr;
        port = ntohs(sin->sin_port);
        break;
    }

    if (port > 0 && port < 65536) {
        v->len = ngx_sprintf(v->data, "%ui", port) - v->data;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_server_addr(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_str_t  s;
    u_char     addr[NGX_SOCKADDR_STRLEN];

    s.len = NGX_SOCKADDR_STRLEN;
    s.data = addr;

    if (ngx_connection_local_sockaddr(r->connection, &s, 0) != NGX_OK) {
        return NGX_ERROR;
    }

    s.data = ngx_pnalloc(r->pool, s.len);
    if (s.data == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(s.data, addr, s.len);

    v->len = s.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = s.data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_server_port(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_uint_t            port;
    struct sockaddr_in   *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6  *sin6;
#endif

    v->len = 0;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    if (ngx_connection_local_sockaddr(r->connection, NULL, 0) != NGX_OK) {
        return NGX_ERROR;
    }

    v->data = ngx_pnalloc(r->pool, sizeof("65535") - 1);
    if (v->data == NULL) {
        return NGX_ERROR;
    }

    switch (r->connection->local_sockaddr->sa_family) {

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin6 = (struct sockaddr_in6 *) r->connection->local_sockaddr;
        port = ntohs(sin6->sin6_port);
        break;
#endif

    default: /* AF_INET */
        sin = (struct sockaddr_in *) r->connection->local_sockaddr;
        port = ntohs(sin->sin_port);
        break;
    }

    if (port > 0 && port < 65536) {
        v->len = ngx_sprintf(v->data, "%ui", port) - v->data;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_scheme(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
#if (NGX_HTTP_SSL)

    if (r->connection->ssl) {
        v->len = sizeof("https") - 1;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = (u_char *) "https";

        return NGX_OK;
    }

#endif

    v->len = sizeof("http") - 1;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = (u_char *) "http";

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_is_args(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    if (r->args.len == 0) {
        v->len = 0;
        v->data = NULL;
        return NGX_OK;
    }

    v->len = 1;
    v->data = (u_char *) "?";

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_document_root(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_str_t                  path;
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (clcf->root_lengths == NULL) {
        v->len = clcf->root.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = clcf->root.data;

    } else {
        if (ngx_http_script_run(r, &path, clcf->root_lengths->elts, 0,
                                clcf->root_values->elts)
            == NULL)
        {
            return NGX_ERROR;
        }

        if (ngx_conf_full_name((ngx_cycle_t *) ngx_cycle, &path, 0) != NGX_OK) {
            return NGX_ERROR;
        }

        v->len = path.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = path.data;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_realpath_root(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    size_t                     len;
    ngx_str_t                  path;
    ngx_http_core_loc_conf_t  *clcf;
    u_char                     real[NGX_MAX_PATH];

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (clcf->root_lengths == NULL) {
        path = clcf->root;
    } else {
        if (ngx_http_script_run(r, &path, clcf->root_lengths->elts, 1, clcf->root_values->elts) == NULL) {
            return NGX_ERROR;
        }
        path.data[path.len - 1] = '\0';
        if (ngx_conf_full_name((ngx_cycle_t *) ngx_cycle, &path, 0) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (ngx_realpath(path.data, real) == NULL) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, ngx_errno,
                      ngx_realpath_n " \"%s\" failed", path.data);
        return NGX_ERROR;
    }

    len = ngx_strlen(real);

    v->data = ngx_pnalloc(r->pool, len);
    if (v->data == NULL) {
        return NGX_ERROR;
    }

    v->len = len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    ngx_memcpy(v->data, real, len);

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_request_filename(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    size_t     root;
    ngx_str_t  path;

    if (ngx_http_map_uri_to_path(r, &path, &root, 0) == NULL) {
        return NGX_ERROR;
    }

    /* ngx_http_map_uri_to_path() allocates memory for terminating '\0' */

    v->len = path.len - 1;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = path.data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_server_name(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_http_core_srv_conf_t  *cscf;

    cscf = ngx_http_get_module_srv_conf(r, ngx_http_core_module);

    v->len = cscf->server_name.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = cscf->server_name.data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_request_method(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    if (r->main->method_name.data) {
        v->len = r->main->method_name.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = r->main->method_name.data;

    } else {
        v->not_found = 1;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_remote_user(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_int_t  rc;

    rc = ngx_http_auth_basic_user(r);

    if (rc == NGX_DECLINED) {
        v->not_found = 1;
        return NGX_OK;
    }

    if (rc == NGX_ERROR) {
        return NGX_ERROR;
    }

    v->len = r->headers_in.user.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = r->headers_in.user.data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_body_bytes_sent(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    off_t    sent;
    u_char  *p;

    sent = r->connection->sent - r->header_size;

    if (sent < 0) {
        sent = 0;
    }

    p = ngx_pnalloc(r->pool, NGX_OFF_T_LEN);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->len = ngx_sprintf(p, "%O", sent) - p;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = p;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_sent_content_type(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    if (r->headers_out.content_type.len) {
        v->len = r->headers_out.content_type.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = r->headers_out.content_type.data;

    } else {
        v->not_found = 1;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_sent_content_length(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char  *p;

    if (r->headers_out.content_length) {
        v->len = r->headers_out.content_length->value.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = r->headers_out.content_length->value.data;

        return NGX_OK;
    }

    if (r->headers_out.content_length_n >= 0) {
        p = ngx_pnalloc(r->pool, NGX_OFF_T_LEN);
        if (p == NULL) {
            return NGX_ERROR;
        }

        v->len = ngx_sprintf(p, "%O", r->headers_out.content_length_n) - p;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = p;

        return NGX_OK;
    }

    v->not_found = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_sent_location(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_str_t  name;

    if (r->headers_out.location) {
        v->len = r->headers_out.location->value.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = r->headers_out.location->value.data;

        return NGX_OK;
    }

    ngx_str_set(&name, "sent_http_location");

    return ngx_http_variable_unknown_header(v, &name,
                                            &r->headers_out.headers.part,
                                            sizeof("sent_http_") - 1);
}


static ngx_int_t
ngx_http_variable_sent_last_modified(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char  *p;

    if (r->headers_out.last_modified) {
        v->len = r->headers_out.last_modified->value.len;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = r->headers_out.last_modified->value.data;

        return NGX_OK;
    }

    if (r->headers_out.last_modified_time >= 0) {
        p = ngx_pnalloc(r->pool,
                   sizeof("Last-Modified: Mon, 28 Sep 1970 06:00:00 GMT") - 1);
        if (p == NULL) {
            return NGX_ERROR;
        }

        v->len = ngx_http_time(p, r->headers_out.last_modified_time) - p;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = p;

        return NGX_OK;
    }

    v->not_found = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_sent_connection(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    size_t   len;
    char    *p;

    if (r->keepalive) {
        len = sizeof("keep-alive") - 1;
        p = "keep-alive";

    } else {
        len = sizeof("close") - 1;
        p = "close";
    }

    v->len = len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = (u_char *) p;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_sent_keep_alive(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char                    *p;
    ngx_http_core_loc_conf_t  *clcf;

    if (r->keepalive) {
        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        if (clcf->keepalive_header) {

            p = ngx_pnalloc(r->pool, sizeof("timeout=") - 1 + NGX_TIME_T_LEN);
            if (p == NULL) {
                return NGX_ERROR;
            }

            v->len = ngx_sprintf(p, "timeout=%T", clcf->keepalive_header) - p;
            v->valid = 1;
            v->no_cacheable = 0;
            v->not_found = 0;
            v->data = p;

            return NGX_OK;
        }
    }

    v->not_found = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_sent_transfer_encoding(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    if (r->chunked) {
        v->len = sizeof("chunked") - 1;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = (u_char *) "chunked";

    } else {
        v->not_found = 1;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_request_completion(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    if (r->request_complete) {
        v->len = 2;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = (u_char *) "OK";

        return NGX_OK;
    }

    v->len = 0;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = (u_char *) "";

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_request_body(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char       *p;
    size_t        len;
    ngx_buf_t    *buf, *next;
    ngx_chain_t  *cl;

    if (r->request_body == NULL
        || r->request_body->bufs == NULL
        || r->request_body->temp_file)
    {
        v->not_found = 1;

        return NGX_OK;
    }

    cl = r->request_body->bufs;
    buf = cl->buf;

    if (cl->next == NULL) {
        v->len = buf->last - buf->pos;
        v->valid = 1;
        v->no_cacheable = 0;
        v->not_found = 0;
        v->data = buf->pos;

        return NGX_OK;
    }

    next = cl->next->buf;
    len = (buf->last - buf->pos) + (next->last - next->pos);

    p = ngx_pnalloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->data = p;

    p = ngx_cpymem(p, buf->pos, buf->last - buf->pos);
    ngx_memcpy(p, next->pos, next->last - next->pos);

    v->len = len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_request_body_file(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    if (r->request_body == NULL || r->request_body->temp_file == NULL) {
        v->not_found = 1;

        return NGX_OK;
    }

    v->len = r->request_body->temp_file->file.name.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = r->request_body->temp_file->file.name.data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_nginx_version(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    v->len = sizeof(NGINX_VERSION) - 1;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = (u_char *) NGINX_VERSION;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_hostname(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    v->len = ngx_cycle->hostname.len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = ngx_cycle->hostname.data;

    return NGX_OK;
}


static ngx_int_t
ngx_http_variable_pid(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    u_char  *p;

    p = ngx_pnalloc(r->pool, NGX_INT64_LEN);
    if (p == NULL) {
        return NGX_ERROR;
    }

    v->len = ngx_sprintf(p, "%P", ngx_pid) - p;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = p;

    return NGX_OK;
}


#if (NGX_PCRE)

static ngx_int_t
ngx_http_variable_not_found(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
    v->not_found = 1;
    return NGX_OK;
}

/*
1. 调用ngx_regex_compile编译正则表达式，并且得到相关的数据，比如子模式数目，命名子模式数目，列表等。
2.	将正则表达式信息存储到ngx_http_regex_t里面，包括正则句柄，子模式数目
3. 	将命名子模式 加入到cmcf->variables_keys和cmcf->variables中。以备后续通过名字查找变量值。*/
ngx_http_regex_t * ngx_http_regex_compile(ngx_conf_t *cf, ngx_regex_compile_t *rc) {
    u_char                     *p;
    size_t                      size;
    ngx_str_t                   name;
    ngx_uint_t                  i, n;
    ngx_http_variable_t        *v;
    ngx_http_regex_t           *re;
    ngx_http_regex_variable_t  *rv;
    ngx_http_core_main_conf_t  *cmcf;

    rc->pool = cf->pool;
    if (ngx_regex_compile(rc) != NGX_OK) {//调用pcre_compile编译正则表达。返回结果存在rc->regex = re;
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V", &rc->err);
        return NULL;
    }
    re = ngx_pcalloc(cf->pool, sizeof(ngx_http_regex_t));
    if (re == NULL) {
        return NULL;
    }

    re->regex = rc->regex;
    re->ncaptures = rc->captures;//得到$2,$3有多少个。pcre_fullinfo(re, NULL, PCRE_INFO_CAPTURECOUNT, &rc->captures);
    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    cmcf->ncaptures = ngx_max(cmcf->ncaptures, re->ncaptures);
    n = (ngx_uint_t) rc->named_captures;//命名的子模式: pcre_fullinfo(re, NULL, PCRE_INFO_NAMECOUNT, &rc->named_captures);
    if (n == 0) {
        return re;
    }
    rv = ngx_palloc(rc->pool, n * sizeof(ngx_http_regex_variable_t));
    if (rv == NULL) {//为每一个命名变量申请空间单独存储。
        return NULL;
    }
    re->variables = rv;//记录这种命名的子模式
    re->nvariables = n;
    re->name = rc->pattern;//记录正则表达式字符串

    size = rc->name_size;
    p = rc->names;//正则的命名子模式的二维表，里面记录了子模式的名称，以及在所有模式中的下标.
    //names是这么得到的: pcre_fullinfo(re, NULL, PCRE_INFO_NAMETABLE, &rc->names);
    //一个命名子模式的二维表存储在p的位置。每一行是一个命名模式，每行的字节数为name_size
    for (i = 0; i < n; i++) {//遍历每一个named_captures，
        rv[i].capture = 2 * ((p[0] << 8) + p[1]);//第一个自己左移8位+第二个字节等于这个命名子模式在所有模式中的下标。
        name.data = &p[2];//第三个字节开始就是命名的名字。
        name.len = ngx_strlen(name.data);//名字长度
		///将这个命名子模式加入到cmcf->variables_keys中
        v = ngx_http_add_variable(cf, &name, NGX_HTTP_VAR_CHANGEABLE);
        if (v == NULL) {
            return NULL;
        }
		//然后从cmcf->variables中查找这个变量对应的下标是多少，如果没有，当然就增加到里面去。
        rv[i].index = ngx_http_get_variable_index(cf, &name);
        if (rv[i].index == NGX_ERROR) {
            return NULL;
        }
        v->get_handler = ngx_http_variable_not_found;//初始化为空函数
        p += size;
    }
    return re;
}


ngx_int_t
ngx_http_regex_exec(ngx_http_request_t *r, ngx_http_regex_t *re, ngx_str_t *s)
{//用已经编译的regex 跟e->line,也就是s去匹配，看看是否匹配成功。如果匹配成功，整理存放在r->variables里面、
    ngx_int_t                   rc, index;
    ngx_uint_t                  i, n, len;
    ngx_http_variable_value_t  *vv;
    ngx_http_core_main_conf_t  *cmcf;
    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    if (re->ncaptures) {//根据ncaptures大小，申请内存用于存放正则匹配的结果。
        len = cmcf->ncaptures;
        if (r->captures == NULL) {
            r->captures = ngx_palloc(r->pool, len * sizeof(int));
            if (r->captures == NULL) {
                return NGX_ERROR;
            }
        }
    } else {
        len = 0;
    }
	//进行正则匹配，结果存放在captures里面，2个槽位为单位，[beg,end], [beg,end]
    rc = ngx_regex_exec(re->regex, s, r->captures, len);
    if (rc == NGX_REGEX_NO_MATCHED) {
        return NGX_DECLINED;
    }
    if (rc < 0) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,  ngx_regex_exec_n " failed: %i on \"%V\" using \"%V\"", rc, s, &re->name);
        return NGX_ERROR;
    }
    for (i = 0; i < re->nvariables; i++) {
        n = re->variables[i].capture;
        index = re->variables[i].index;//找到这个正则表达式的这一项对应r->variables[index]的下标。
        vv = &r->variables[index];//然后用匹配结果填充r->variables[index]
        vv->len = r->captures[n + 1] - r->captures[n];
        vv->valid = 1;
        vv->no_cacheable = 0;
        vv->not_found = 0;
        vv->data = &s->data[r->captures[n]];
#if (NGX_DEBUG)
        {
        ngx_http_variable_t  *v;
        v = cmcf->variables.elts;
        ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http regex set $%V to \"%*s\"",  &v[index].name, vv->len, vv->data);
        }
#endif
    }
    r->ncaptures = rc * 2;
    r->captures_data = s->data;
    return NGX_OK;
}
#endif


ngx_int_t
ngx_http_variables_add_core_vars(ngx_conf_t *cf)
{//ngx_http_core_preconfiguration函数在程序解析配置之前就调用这里
//将ngx_http_core_variables 的变量全部导入到variables_keys里面，其里面会有各个handler来帮助存取变量值。
    ngx_int_t                   rc;
    ngx_http_variable_t        *v;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    cmcf->variables_keys = ngx_pcalloc(cf->temp_pool, sizeof(ngx_hash_keys_arrays_t));
    if (cmcf->variables_keys == NULL) {
        return NGX_ERROR;
    }

    cmcf->variables_keys->pool = cf->pool;
    cmcf->variables_keys->temp_pool = cf->pool;
    if (ngx_hash_keys_array_init(cmcf->variables_keys, NGX_HASH_SMALL) != NGX_OK) {
        return NGX_ERROR;
    }
	//循环将这些变量加入到哈希表中。用户自定义的是不会存在这里面的，因为太多了。
    for (v = ngx_http_core_variables; v->name.len; v++) {
		//将著名的头部字段加入variables_keys，注意没有加入variables。为了效率。
        rc = ngx_hash_add_key(cmcf->variables_keys, &v->name, v, NGX_HASH_READONLY_KEY);
        if (rc == NGX_OK) {
            continue;
        }
        if (rc == NGX_BUSY) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "conflicting variable name \"%V\"", &v->name);
        }
        return NGX_ERROR;
    }
    return NGX_OK;
}


ngx_int_t
ngx_http_variables_init_vars(ngx_conf_t *cf)
{//ngx_http_block调用这里，初始化variables和variables_keys。
//不过，远在解析配置文件之前，ngx_http_core_preconfiguration函数就调用了ngx_http_variables_add_core_vars
//后者将ngx_http_core_variables核心变量全部初始化到了variables_keys中。因此variables_keys比variables中的变量多。
//且variables_keys中的核心变量其get_handler都是预定过的。
    ngx_uint_t                  i, n;
    ngx_hash_key_t             *key;
    ngx_hash_init_t             hash;
    ngx_http_variable_t        *v, *av;
    ngx_http_core_main_conf_t  *cmcf;

    /* set the handlers for the indexed http variables */

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    v = cmcf->variables.elts;
    key = cmcf->variables_keys->keys.elts;
    for (i = 0; i < cmcf->variables.nelts; i++) {
        for (n = 0; n < cmcf->variables_keys->keys.nelts; n++) {

            av = key[n].value;
            if (av->get_handler
                && v[i].name.len == key[n].key.len
                && ngx_strncmp(v[i].name.data, key[n].key.data, v[i].name.len)
                   == 0)
            {
                v[i].get_handler = av->get_handler;
                v[i].data = av->data;

                av->flags |= NGX_HTTP_VAR_INDEXED;
                v[i].flags = av->flags;
                av->index = i;
                goto next;
            }
        }

        if (ngx_strncmp(v[i].name.data, "http_", 5) == 0) {
            v[i].get_handler = ngx_http_variable_unknown_header_in;
            v[i].data = (uintptr_t) &v[i].name;
            continue;
        }
        if (ngx_strncmp(v[i].name.data, "sent_http_", 10) == 0) {
            v[i].get_handler = ngx_http_variable_unknown_header_out;
            v[i].data = (uintptr_t) &v[i].name;
            continue;
        }
        if (ngx_strncmp(v[i].name.data, "upstream_http_", 14) == 0) {
            v[i].get_handler = ngx_http_upstream_header_variable;
            v[i].data = (uintptr_t) &v[i].name;
            v[i].flags = NGX_HTTP_VAR_NOCACHEABLE;
            continue;
        }
        if (ngx_strncmp(v[i].name.data, "cookie_", 7) == 0) {
            v[i].get_handler = ngx_http_variable_cookie;
            v[i].data = (uintptr_t) &v[i].name;
            continue;
        }
        if (ngx_strncmp(v[i].name.data, "arg_", 4) == 0) {
            v[i].get_handler = ngx_http_variable_argument;
            v[i].data = (uintptr_t) &v[i].name;
            v[i].flags = NGX_HTTP_VAR_NOCACHEABLE;
            continue;
        }
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0, "unknown \"%V\" variable", &v[i].name);
        return NGX_ERROR;
    next:
        continue;
    }
    for (n = 0; n < cmcf->variables_keys->keys.nelts; n++) {
        av = key[n].value;
        if (av->flags & NGX_HTTP_VAR_NOHASH) {
            key[n].key.data = NULL;
        }
    }

    hash.hash = &cmcf->variables_hash;
    hash.key = ngx_hash_key;
    hash.max_size = cmcf->variables_hash_max_size;
    hash.bucket_size = cmcf->variables_hash_bucket_size;
    hash.name = "variables_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    if (ngx_hash_init(&hash, cmcf->variables_keys->keys.elts, cmcf->variables_keys->keys.nelts)  != NGX_OK)  {
        return NGX_ERROR;
    }
    cmcf->variables_keys = NULL;
    return NGX_OK;
}
