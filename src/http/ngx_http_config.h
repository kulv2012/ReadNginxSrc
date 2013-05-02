
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_HTTP_CONFIG_H_INCLUDED_
#define _NGX_HTTP_CONFIG_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {//下面都是数组，表示所有的NGX_HTTP_MODULE模块的对应的配置都在此，一一对应于他们的ctx_index
    void        **main_conf;//
    void        **srv_conf;
    void        **loc_conf;
} ngx_http_conf_ctx_t;


typedef struct {
    ngx_int_t   (*preconfiguration)(ngx_conf_t *cf);//准备ngx_conf_parse这些模块之前，会先调用这个。在读入配置前调用
    ngx_int_t   (*postconfiguration)(ngx_conf_t *cf);//在读入配置后调用

    void       *(*create_main_conf)(ngx_conf_t *cf);
	//在创建main配置时调用（比如，用来分配空间和设置默认值）。创建碰到http指令就会调用个
    char       *(*init_main_conf)(ngx_conf_t *cf, void *conf);
	//在初始化main配置时调用（比如，把原来的默认值用nginx.conf读到的值来覆盖）
	
    void       *(*create_srv_conf)(ngx_conf_t *cf);//在创建server配置时调用
    char       *(*merge_srv_conf)(ngx_conf_t *cf, void *prev, void *conf);//合并server和main配置时调用

    void       *(*create_loc_conf)(ngx_conf_t *cf);//创建location配置时调用
    char       *(*merge_loc_conf)(ngx_conf_t *cf, void *prev, void *conf);//合并location和server配置时调用
} ngx_http_module_t;


#define NGX_HTTP_MODULE           0x50545448   /* "HTTP" */

#define NGX_HTTP_MAIN_CONF        0x02000000
#define NGX_HTTP_SRV_CONF         0x04000000
#define NGX_HTTP_LOC_CONF         0x08000000
#define NGX_HTTP_UPS_CONF         0x10000000
#define NGX_HTTP_SIF_CONF         0x20000000
#define NGX_HTTP_LIF_CONF         0x40000000
#define NGX_HTTP_LMT_CONF         0x80000000


#define NGX_HTTP_MAIN_CONF_OFFSET  offsetof(ngx_http_conf_ctx_t, main_conf)
#define NGX_HTTP_SRV_CONF_OFFSET   offsetof(ngx_http_conf_ctx_t, srv_conf)
#define NGX_HTTP_LOC_CONF_OFFSET   offsetof(ngx_http_conf_ctx_t, loc_conf)


#define ngx_http_get_module_main_conf(r, module) (r)->main_conf[module.ctx_index]
#define ngx_http_get_module_srv_conf(r, module)  (r)->srv_conf[module.ctx_index]
#define ngx_http_get_module_loc_conf(r, module)  (r)->loc_conf[module.ctx_index]


#define ngx_http_conf_get_module_main_conf(cf, module)                        \
    ((ngx_http_conf_ctx_t *) cf->ctx)->main_conf[module.ctx_index]
#define ngx_http_conf_get_module_srv_conf(cf, module)                         \
    ((ngx_http_conf_ctx_t *) cf->ctx)->srv_conf[module.ctx_index]
#define ngx_http_conf_get_module_loc_conf(cf, module)                         \
    ((ngx_http_conf_ctx_t *) cf->ctx)->loc_conf[module.ctx_index]

#define ngx_http_cycle_get_module_main_conf(cycle, module)                    \
    (cycle->conf_ctx[ngx_http_module.index] ?                                 \
        ((ngx_http_conf_ctx_t *) cycle->conf_ctx[ngx_http_module.index])      \
            ->main_conf[module.ctx_index]:                                    \
        NULL)


#endif /* _NGX_HTTP_CONFIG_H_INCLUDED_ */
