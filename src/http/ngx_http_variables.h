
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_HTTP_VARIABLES_H_INCLUDED_
#define _NGX_HTTP_VARIABLES_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef ngx_variable_value_t  ngx_http_variable_value_t;

#define ngx_http_variable(v)     { sizeof(v) - 1, 1, 0, 0, 0, (u_char *) v }

typedef struct ngx_http_variable_s  ngx_http_variable_t;

typedef void (*ngx_http_set_variable_pt) (ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
typedef ngx_int_t (*ngx_http_get_variable_pt) (ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);


#define NGX_HTTP_VAR_CHANGEABLE   1
#define NGX_HTTP_VAR_NOCACHEABLE  2
#define NGX_HTTP_VAR_INDEXED      4
#define NGX_HTTP_VAR_NOHASH       8


struct ngx_http_variable_s {//cmcf->variables是一个数组，它的元素类型为ngx_http_variable_t。
    ngx_str_t                     name;   /* must be first to build the hash */
    ngx_http_set_variable_pt      set_handler;
    ngx_http_get_variable_pt      get_handler;//handler为ngx_http_variable_argument或者ngx_http_variable_cookie等。
    								//get_handler会在ngx_http_variables_init_vars里面进行初始化，或者ngx_http_core_preconfiguration里面
    uintptr_t                     data;
    ngx_uint_t                    flags;
    ngx_uint_t                    index;//变量在cmcf->variables.nelts 中的下标。
};


ngx_http_variable_t *ngx_http_add_variable(ngx_conf_t *cf, ngx_str_t *name,
    ngx_uint_t flags);
ngx_int_t ngx_http_get_variable_index(ngx_conf_t *cf, ngx_str_t *name);
ngx_http_variable_value_t *ngx_http_get_indexed_variable(ngx_http_request_t *r,
    ngx_uint_t index);
ngx_http_variable_value_t *ngx_http_get_flushed_variable(ngx_http_request_t *r,
    ngx_uint_t index);

ngx_http_variable_value_t *ngx_http_get_variable(ngx_http_request_t *r,
    ngx_str_t *name, ngx_uint_t key);

ngx_int_t ngx_http_variable_unknown_header(ngx_http_variable_value_t *v,
    ngx_str_t *var, ngx_list_part_t *part, size_t prefix);


#define ngx_http_clear_variable(r, index) r->variables0[index].text.data = NULL;


#if (NGX_PCRE)
//关于子模式，有2种: 命名子模式(named subpattern)和非命名子模式(numbering subpattern)
typedef struct {
    ngx_uint_t                    capture;//第几个$2,$1
    ngx_int_t                     index;//在cmcf->variables[]中的下标，这样可以方便的找到这个变量应该存储的地方。
} ngx_http_regex_variable_t;


typedef struct {
    ngx_regex_t                  *regex;//pcre_compile返回的正则句柄
    ngx_uint_t                    ncaptures;//正则表达式里面有几个$1,$2,大小为2倍，开始，结束
    ngx_http_regex_variable_t    *variables;//为每一个命名变量申请空间单独存储。数组长度等于named_captures，也就是nvariables成员
    ngx_uint_t                    nvariables;//等于named_captures，记录variables命名变量的长度
    ngx_str_t                     name;//就是正则表达式的模式啦，那串字符串rc->pattern;
} ngx_http_regex_t;


ngx_http_regex_t *ngx_http_regex_compile(ngx_conf_t *cf,
    ngx_regex_compile_t *rc);
ngx_int_t ngx_http_regex_exec(ngx_http_request_t *r, ngx_http_regex_t *re,
    ngx_str_t *s);

#endif


ngx_int_t ngx_http_variables_add_core_vars(ngx_conf_t *cf);
ngx_int_t ngx_http_variables_init_vars(ngx_conf_t *cf);


extern ngx_http_variable_value_t  ngx_http_variable_null_value;
extern ngx_http_variable_value_t  ngx_http_variable_true_value;


#endif /* _NGX_HTTP_VARIABLES_H_INCLUDED_ */
