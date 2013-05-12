
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_REGEX_H_INCLUDED_
#define _NGX_REGEX_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#include <pcre.h>


#define NGX_REGEX_NO_MATCHED  PCRE_ERROR_NOMATCH   /* -1 */

#define NGX_REGEX_CASELESS    PCRE_CASELESS

typedef pcre  ngx_regex_t;


typedef struct {
    ngx_str_t     pattern;
    ngx_pool_t   *pool;
    ngx_int_t     options;

    ngx_regex_t  *regex;//pcre_compile返回的结果
    int           captures;
    int           named_captures;//命名的正则表达式子模式，只有这种才会出现在cmcf->variables里面
    int           name_size;//一个命名子模式的二维表存储在p的位置。每一行是一个命名模式，每行的字节数为name_size
    u_char       *names;//
    ngx_str_t     err;
} ngx_regex_compile_t;


typedef struct {
    ngx_regex_t  *regex;
    u_char       *name;
} ngx_regex_elt_t;


void ngx_regex_init(void);
ngx_int_t ngx_regex_compile(ngx_regex_compile_t *rc);

#define ngx_regex_exec(re, s, captures, size)                                \
    pcre_exec(re, NULL, (const char *) (s)->data, (s)->len, 0, 0,            \
              captures, size)
#define ngx_regex_exec_n      "pcre_exec()"

ngx_int_t ngx_regex_exec_array(ngx_array_t *a, ngx_str_t *s, ngx_log_t *log);


#endif /* _NGX_REGEX_H_INCLUDED_ */
