
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_HTTP_CORE_H_INCLUDED_
#define _NGX_HTTP_CORE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#define NGX_HTTP_GZIP_PROXIED_OFF       0x0002
#define NGX_HTTP_GZIP_PROXIED_EXPIRED   0x0004
#define NGX_HTTP_GZIP_PROXIED_NO_CACHE  0x0008
#define NGX_HTTP_GZIP_PROXIED_NO_STORE  0x0010
#define NGX_HTTP_GZIP_PROXIED_PRIVATE   0x0020
#define NGX_HTTP_GZIP_PROXIED_NO_LM     0x0040
#define NGX_HTTP_GZIP_PROXIED_NO_ETAG   0x0080
#define NGX_HTTP_GZIP_PROXIED_AUTH      0x0100
#define NGX_HTTP_GZIP_PROXIED_ANY       0x0200


#define NGX_HTTP_AIO_OFF                0
#define NGX_HTTP_AIO_ON                 1
#define NGX_HTTP_AIO_SENDFILE           2


#define NGX_HTTP_SATISFY_ALL            0
#define NGX_HTTP_SATISFY_ANY            1


#define NGX_HTTP_IMS_OFF                0
#define NGX_HTTP_IMS_EXACT              1
#define NGX_HTTP_IMS_BEFORE             2


typedef struct ngx_http_location_tree_node_s  ngx_http_location_tree_node_t;
typedef struct ngx_http_core_loc_conf_s  ngx_http_core_loc_conf_t;


typedef struct {//一个listen ***;的唯一配置信息.
    union {
        struct sockaddr        sockaddr;
        struct sockaddr_in     sockaddr_in;
#if (NGX_HAVE_INET6)
        struct sockaddr_in6    sockaddr_in6;
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
        struct sockaddr_un     sockaddr_un;
#endif
        u_char                 sockaddr_data[NGX_SOCKADDRLEN];
    } u;

    socklen_t                  socklen;

    unsigned                   set:1;
    unsigned                   default_server:1;//保存十分该监听端口是默认的server
    unsigned                   bind:1;
    unsigned                   wildcard:1;
#if (NGX_HTTP_SSL)
    unsigned                   ssl:1;
#endif
#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
    unsigned                   ipv6only:2;
#endif

    int                        backlog;
    int                        rcvbuf;
    int                        sndbuf;
#if (NGX_HAVE_SETFIB)
    int                        setfib;
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
    char                      *accept_filter;
#endif
#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
    ngx_uint_t                 deferred_accept;
#endif

    u_char                     addr[NGX_SOCKADDR_STRLEN + 1];
} ngx_http_listen_opt_t;


typedef enum {
    NGX_HTTP_POST_READ_PHASE = 0,//读取请求数据

    NGX_HTTP_SERVER_REWRITE_PHASE,//进行全局的重定向，卸载server指令里面的。
    NGX_HTTP_FIND_CONFIG_PHASE,//根据URI查找配置，然后将uri和location的数据关联起来  
    NGX_HTTP_REWRITE_PHASE,//这个主要处理location的rewrite。  
    NGX_HTTP_POST_REWRITE_PHASE,

    NGX_HTTP_PREACCESS_PHASE,//访问权限的初步判断
    NGX_HTTP_ACCESS_PHASE,//访权限的控制，存取控制，权限验证。进行细致的控制
    NGX_HTTP_POST_ACCESS_PHASE,//访问控制后处理

    NGX_HTTP_TRY_FILES_PHASE,//也就是对应配置文件中的try_files指令。
    NGX_HTTP_CONTENT_PHASE,//内容处理模块，我们一般的handle都是处于这个模块

    NGX_HTTP_LOG_PHASE//日志。
} ngx_http_phases;
//上面各个过程对应的处理函数在这里ngx_http_core_run_phases

typedef struct ngx_http_phase_handler_s  ngx_http_phase_handler_t;

typedef ngx_int_t (*ngx_http_phase_handler_pt)(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);

struct ngx_http_phase_handler_s {
    ngx_http_phase_handler_pt  checker;//处理过程循环的入口函数，比如ngx_http_core_content_phase。
    ngx_http_handler_pt        handler;//回调句柄，在postconfiguration设置的回调。
    //下一个处理过程类型的数组下标，注意不是+1,而是指比如下一个过程大类型是NGX_HTTP_CONTENT_PHASE的下标。跳过同类型
    ngx_uint_t                 next;
};


typedef struct {
    ngx_http_phase_handler_t  *handlers;//处理过程的句柄数组。数目等于phases[i].handlers里面的句柄数加上rewrite,try_files等。也就是每个函数一个
    ngx_uint_t                 server_rewrite_index;//标记一下，NGX_HTTP_SERVER_REWRITE_PHASE这个阶段的下标，或者说起始位置
    ngx_uint_t                 location_rewrite_index;
} ngx_http_phase_engine_t;


typedef struct {
    ngx_array_t                handlers;
} ngx_http_phase_t;


typedef struct {
    ngx_array_t                servers;/* ngx_http_core_srv_conf_t *///每遇到一个server{},都会在这里记录一下。数量不一定等于server_name。
    ngx_http_phase_engine_t    phase_engine;//NGINX处理过程结构，里面有句柄数组等。ngx_http_init_phase_handlers设置这个。
    ngx_hash_t                 headers_in_hash;//预定义的著名HTTP头部的hash存储。在这里面:ngx_http_headers_in
    ngx_hash_t                 variables_hash;
    ngx_array_t                variables;/* ngx_http_variable_t *///配置中出现的变量(在处理请求时会用到的)，会放到cmcf->variables中。
    ngx_uint_t                 ncaptures;//这个main_conf里面，所有正则表达式中，含有$2,$3变量做多的是多少。
    ngx_uint_t                 server_names_hash_max_size;
    ngx_uint_t                 server_names_hash_bucket_size;
    ngx_uint_t                 variables_hash_max_size;
    ngx_uint_t                 variables_hash_bucket_size;

    ngx_hash_keys_arrays_t    *variables_keys;//保存了系统中所有预定义，自定义等所有变量，出了"http_"前缀的，参考ngx_http_variables_init_vars
    ngx_array_t               *ports;//转为网络序后的端口列表。ngx_http_conf_port_t结构
    ngx_uint_t                 try_files;       /* unsigned  try_files:1 *///是否配置了try_files指令
    ngx_http_phase_t           phases[NGX_HTTP_LOG_PHASE + 1];
	//这是各个模块通过postconfiguration回调注册的处理过程。比如我关心XX过程的事件，我的回调在此。
} ngx_http_core_main_conf_t;


typedef struct {
    /* array of the ngx_http_server_name_t, "server_name" directive */
    ngx_array_t                 server_names;//对于某个server{}，每一个server_name *.com;都会有一个成员在里面

    /* server ctx */
    ngx_http_conf_ctx_t        *ctx;//也就是server的ctx。指向所属的ngx_http_conf_ctx_t结构。三个指针，分别指向一个数组。
    ngx_str_t                   server_name;//用来记录当前解析到的server_name指令的。

    size_t                      connection_pool_size;
    size_t                      request_pool_size;
    size_t                      client_header_buffer_size;

    ngx_bufs_t                  large_client_header_buffers;

    ngx_msec_t                  client_header_timeout;

    ngx_flag_t                  ignore_invalid_headers;
    ngx_flag_t                  merge_slashes;
    ngx_flag_t                  underscores_in_headers;

    unsigned                    listen:1;//表示这个server{}有listen指令，监听端口。
#if (NGX_PCRE)
    unsigned                    captures:1;
#endif

    ngx_http_core_loc_conf_t  **named_locations;//@开头的命名location放在这里。为什么要放在这里呢，因为命名节点是server里面全局的
} ngx_http_core_srv_conf_t;


/* list of structures to find core_srv_conf quickly at run time */


typedef struct {
    /* the default server configuration for this address:port */
    ngx_http_core_srv_conf_t  *default_server;//该地址所对应的默认虚拟主机

    ngx_http_virtual_names_t  *virtual_names;

#if (NGX_HTTP_SSL)
    ngx_uint_t                 ssl;   /* unsigned  ssl:1; */
#endif
} ngx_http_addr_conf_t;


typedef struct {
    in_addr_t                  addr;
    ngx_http_addr_conf_t       conf;
} ngx_http_in_addr_t;


#if (NGX_HAVE_INET6)

typedef struct {
    struct in6_addr            addr6;
    ngx_http_addr_conf_t       conf;
} ngx_http_in6_addr_t;

#endif


typedef struct {//HTTP 监听端口信息。
    /* ngx_http_in_addr_t or ngx_http_in6_addr_t */
    void                      *addrs;
    ngx_uint_t                 naddrs;
} ngx_http_port_t;


typedef struct {//同一个协议族+端口号的监听端口信息，存在这里面。
    ngx_int_t                  family;
    in_port_t                  port;//网络字节序的端口
    ngx_array_t                addrs; /* array of ngx_http_conf_addr_t *///这个端口对应的地址列表，到底是什么?端口怎么会有多个地址列表?
    /*ddrs记录的是同一个port绑定的不同ip地址结构的列表。介绍一下listen指令的语法就知道了:
	listen 127.0.0.1:80; 80; 127.0.0.1; *:80 ; unix:path
    */
} ngx_http_conf_port_t;


typedef struct {
    ngx_http_listen_opt_t      opt;//这个地址结构的端口信息。

    ngx_hash_t                 hash;
    ngx_hash_wildcard_t       *wc_head;
    ngx_hash_wildcard_t       *wc_tail;

#if (NGX_PCRE)
    ngx_uint_t                 nregex;
    ngx_http_server_name_t    *regex;
#endif

    /* the default server configuration for this address:port */
    ngx_http_core_srv_conf_t  *default_server;//指向这个端口的默认server的srv_conf配置。从而可以找到ctx
    ngx_array_t                servers;  /* array of ngx_http_core_srv_conf_t *///数组，表示这个端口有哪些server_name虚拟主机对应着
} ngx_http_conf_addr_t;


struct ngx_http_server_name_s {
#if (NGX_PCRE)
    ngx_http_regex_t          *regex;
#endif
    ngx_http_core_srv_conf_t  *server;   /* virtual name server conf */
    ngx_str_t                  name;
};


typedef struct {
    ngx_int_t                  status;
    ngx_int_t                  overwrite;
    ngx_http_complex_value_t   value;
    ngx_str_t                  args;
} ngx_http_err_page_t;


typedef struct {
    ngx_array_t               *lengths;
    ngx_array_t               *values;
    ngx_str_t                  name;

    unsigned                   code:10;
    unsigned                   test_dir:1;
} ngx_http_try_file_t;

/*〖=〗 表示精确匹配，如果找到，立即停止搜索并立即处理此请求。
〖~ 〗 表示区分大小写匹配
〖~*〗 表示不区分大小写匹配
〖^~ 〗 表示只匹配字符串,不查询正则表达式。
〖@〗 指定一个命名的location，一般只用于内部重定向请求。
*/
struct ngx_http_core_loc_conf_s {
    ngx_str_t     name;          /* location name */

#if (NGX_PCRE)
    ngx_http_regex_t  *regex;//正则编译后的正则表达式结构
#endif

    unsigned      noname:1;   /* "if () {}" block or limit_except */
    unsigned      lmt_excpt:1;
    unsigned      named:1;//指定了一个命名的location。用@符号开头

    unsigned      exact_match:1;//是否是精确匹配。比如:location =xxx
    unsigned      noregex:1;//^~ 开头表示uri以某个常规字符串开头，理解为匹配 url路径即可。

    unsigned      auto_redirect:1;//如果配置的locaton 后面最后一个字符为/路径结束符，则需要自动重定向。
#if (NGX_HTTP_GZIP)
    unsigned      gzip_disable_msie6:2;
#if (NGX_HTTP_DEGRADATION)
    unsigned      gzip_disable_degradation:2;
#endif
#endif

    ngx_http_location_tree_node_t   *static_locations;//一颗由inclusive类型的location组成的三叉树
#if (NGX_PCRE)
    ngx_http_core_loc_conf_t       **regex_locations;//正则表达式的location再此
#endif

    /* pointer to the modules' loc_conf */
    void        **loc_conf;//loc_conf成员会保存所有其他module的location配置用到的时候引用方便。详见ngx_http_core_location

    uint32_t      limit_except;
    void        **limit_except_loc_conf;

    ngx_http_handler_pt  handler;//对于fcgi，为ngx_http_fastcgi_handler

    /* location name length for inclusive location with inherited alias */
    size_t        alias;
    ngx_str_t     root;                    /* root, alias */
    ngx_str_t     post_action;

    ngx_array_t  *root_lengths;
    ngx_array_t  *root_values;

    ngx_array_t  *types;
    ngx_hash_t    types_hash;
    ngx_str_t     default_type;

    off_t         client_max_body_size;    /* client_max_body_size */
    off_t         directio;                /* directio */
    off_t         directio_alignment;      /* directio_alignment */

    size_t        client_body_buffer_size; /* client_body_buffer_size */
    size_t        send_lowat;              /* send_lowat */
	//输出缓存: If possible, the output of client data will be postponed until 
	//ginx has at least size bytes of data to send. Value of zero disables postponing
    size_t        postpone_output;         /* postpone_output */
    size_t        limit_rate;              /* limit_rate */
    size_t        limit_rate_after;        /* limit_rate_after */
    size_t        sendfile_max_chunk;      /* sendfile_max_chunk */
    size_t        read_ahead;              /* read_ahead */

    ngx_msec_t    client_body_timeout;     /* client_body_timeout *///表示客户端发一个字节后，再过多久之内必须再发数据。但可以发一个字节，等会，再发一个自己
    										//client_body_timeout不是表示总的BODY发送时间，而是间隔时间。
    ngx_msec_t    send_timeout;            /* send_timeout */
    ngx_msec_t    keepalive_timeout;       /* keepalive_timeout */
    ngx_msec_t    lingering_time;          /* lingering_time */
    ngx_msec_t    lingering_timeout;       /* lingering_timeout */
    ngx_msec_t    resolver_timeout;        /* resolver_timeout */

    ngx_resolver_t  *resolver;             /* resolver */

    time_t        keepalive_header;        /* keepalive_timeout */

    ngx_uint_t    keepalive_requests;      /* keepalive_requests */
    ngx_uint_t    satisfy;                 /* satisfy *///对应于satisfy指令，用来做安全限制。
    ngx_uint_t    if_modified_since;       /* if_modified_since */
    ngx_uint_t    client_body_in_file_only; /* client_body_in_file_only */

    ngx_flag_t    client_body_in_single_buffer;
                                           /* client_body_in_singe_buffer */
    ngx_flag_t    internal;                /* internal */
    ngx_flag_t    sendfile;                /* sendfile */
#if (NGX_HAVE_FILE_AIO)
    ngx_flag_t    aio;                     /* aio */
#endif
    ngx_flag_t    tcp_nopush;              /* tcp_nopush */
    ngx_flag_t    tcp_nodelay;             /* tcp_nodelay */
    ngx_flag_t    reset_timedout_connection; /* reset_timedout_connection */
    ngx_flag_t    server_name_in_redirect; /* server_name_in_redirect */
    ngx_flag_t    port_in_redirect;        /* port_in_redirect */
    ngx_flag_t    msie_padding;            /* msie_padding */
    ngx_flag_t    msie_refresh;            /* msie_refresh */
    ngx_flag_t    log_not_found;           /* log_not_found */
    ngx_flag_t    log_subrequest;          /* log_subrequest */
    ngx_flag_t    recursive_error_pages;   /* recursive_error_pages */
    ngx_flag_t    server_tokens;           /* server_tokens */
    ngx_flag_t    chunked_transfer_encoding; /* chunked_transfer_encoding */

#if (NGX_HTTP_GZIP)
    ngx_flag_t    gzip_vary;               /* gzip_vary */

    ngx_uint_t    gzip_http_version;       /* gzip_http_version */
    ngx_uint_t    gzip_proxied;            /* gzip_proxied */

#if (NGX_PCRE)
    ngx_array_t  *gzip_disable;            /* gzip_disable */
#endif
#endif

    ngx_array_t  *error_pages;             /* error_page */
    ngx_http_try_file_t    *try_files;     /* try_files */

    ngx_path_t   *client_body_temp_path;   /* client_body_temp_path */

    ngx_open_file_cache_t  *open_file_cache;
    time_t        open_file_cache_valid;
    ngx_uint_t    open_file_cache_min_uses;
    ngx_flag_t    open_file_cache_errors;
    ngx_flag_t    open_file_cache_events;

    ngx_log_t    *error_log;

    ngx_uint_t    types_hash_max_size;
    ngx_uint_t    types_hash_bucket_size;

    ngx_queue_t  *locations;//location节点列表。
    //最后的顺序为: <简单字符串匹配，递归节点在后面> <正则匹配,内部不排序> <@命名的,内部字符串排序>  <if无名的，内部无序>
#if 0
    ngx_http_core_loc_conf_t  *prev_location;
#endif
};


typedef struct {
    ngx_queue_t                      queue;
    ngx_http_core_loc_conf_t        *exact;//如果不为空，则表示精确匹配，正则匹配等。指向对应的配置
    ngx_http_core_loc_conf_t        *inclusive;//需要包含匹配，不能exact绝对匹配成功。比如location /abc {}
    ngx_str_t                       *name;
    u_char                          *file_name;
    ngx_uint_t                       line;
    ngx_queue_t                      list;
} ngx_http_location_queue_t;//location队列的一项。


struct ngx_http_location_tree_node_s {
    ngx_http_location_tree_node_t   *left;//字符串比较的前面部分，左右数目相等。
    ngx_http_location_tree_node_t   *right;
    ngx_http_location_tree_node_t   *tree;//有相同前缀的字树。代表该location的包含location结构

    ngx_http_core_loc_conf_t        *exact;
    ngx_http_core_loc_conf_t        *inclusive;//要进行包含匹配的。指向配置结构体。

    u_char                           auto_redirect;
    u_char                           len;//本节点的数据长度。不包括前缀。
    u_char                           name[1];
};


void ngx_http_core_run_phases(ngx_http_request_t *r);
ngx_int_t ngx_http_core_generic_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_rewrite_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_find_config_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_post_rewrite_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_access_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_post_access_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_try_files_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);
ngx_int_t ngx_http_core_content_phase(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);


void *ngx_http_test_content_type(ngx_http_request_t *r, ngx_hash_t *types_hash);
ngx_int_t ngx_http_set_content_type(ngx_http_request_t *r);
void ngx_http_set_exten(ngx_http_request_t *r);
ngx_int_t ngx_http_send_response(ngx_http_request_t *r, ngx_uint_t status,
    ngx_str_t *ct, ngx_http_complex_value_t *cv);
u_char *ngx_http_map_uri_to_path(ngx_http_request_t *r, ngx_str_t *name,
    size_t *root_length, size_t reserved);
ngx_int_t ngx_http_auth_basic_user(ngx_http_request_t *r);
#if (NGX_HTTP_GZIP)
ngx_int_t ngx_http_gzip_ok(ngx_http_request_t *r);
#endif


ngx_int_t ngx_http_subrequest(ngx_http_request_t *r,
    ngx_str_t *uri, ngx_str_t *args, ngx_http_request_t **sr,
    ngx_http_post_subrequest_t *psr, ngx_uint_t flags);
ngx_int_t ngx_http_internal_redirect(ngx_http_request_t *r,
    ngx_str_t *uri, ngx_str_t *args);
ngx_int_t ngx_http_named_location(ngx_http_request_t *r, ngx_str_t *name);


ngx_http_cleanup_t *ngx_http_cleanup_add(ngx_http_request_t *r, size_t size);


typedef ngx_int_t (*ngx_http_output_header_filter_pt)(ngx_http_request_t *r);
typedef ngx_int_t (*ngx_http_output_body_filter_pt)
    (ngx_http_request_t *r, ngx_chain_t *chain);


ngx_int_t ngx_http_output_filter(ngx_http_request_t *r, ngx_chain_t *chain);
ngx_int_t ngx_http_write_filter(ngx_http_request_t *r, ngx_chain_t *chain);


extern ngx_module_t  ngx_http_core_module;

extern ngx_uint_t ngx_http_max_module;

extern ngx_str_t  ngx_http_core_get_method;


#define ngx_http_clear_content_length(r)                                      \
                                                                              \
    r->headers_out.content_length_n = -1;                                     \
    if (r->headers_out.content_length) {                                      \
        r->headers_out.content_length->hash = 0;                              \
        r->headers_out.content_length = NULL;                                 \
    }
                                                                              
#define ngx_http_clear_accept_ranges(r)                                       \
                                                                              \
    r->allow_ranges = 0;                                                      \
    if (r->headers_out.accept_ranges) {                                       \
        r->headers_out.accept_ranges->hash = 0;                               \
        r->headers_out.accept_ranges = NULL;                                  \
    }

#define ngx_http_clear_last_modified(r)                                       \
                                                                              \
    r->headers_out.last_modified_time = -1;                                   \
    if (r->headers_out.last_modified) {                                       \
        r->headers_out.last_modified->hash = 0;                               \
        r->headers_out.last_modified = NULL;                                  \
    }


#endif /* _NGX_HTTP_CORE_H_INCLUDED_ */
