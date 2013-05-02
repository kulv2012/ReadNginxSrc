
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


typedef struct {//һ��listen ***;��Ψһ������Ϣ.
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
    unsigned                   default_server:1;//����ʮ�ָü����˿���Ĭ�ϵ�server
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
    NGX_HTTP_POST_READ_PHASE = 0,//��ȡ��������

    NGX_HTTP_SERVER_REWRITE_PHASE,//����ȫ�ֵ��ض���ж��serverָ������ġ�
    NGX_HTTP_FIND_CONFIG_PHASE,//����URI�������ã�Ȼ��uri��location�����ݹ�������  
    NGX_HTTP_REWRITE_PHASE,//�����Ҫ����location��rewrite��  
    NGX_HTTP_POST_REWRITE_PHASE,

    NGX_HTTP_PREACCESS_PHASE,//����Ȩ�޵ĳ����ж�
    NGX_HTTP_ACCESS_PHASE,//��Ȩ�޵Ŀ��ƣ���ȡ���ƣ�Ȩ����֤������ϸ�µĿ���
    NGX_HTTP_POST_ACCESS_PHASE,//���ʿ��ƺ���

    NGX_HTTP_TRY_FILES_PHASE,//Ҳ���Ƕ�Ӧ�����ļ��е�try_filesָ�
    NGX_HTTP_CONTENT_PHASE,//���ݴ���ģ�飬����һ���handle���Ǵ������ģ��

    NGX_HTTP_LOG_PHASE//��־��
} ngx_http_phases;
/* ����ĸ����׶�Ĭ������µľ���ֱ�Ϊ:ngx_http_init_phase_handlers�������õ�
ph[0].checker = ngx_http_core_generic_phase;  
ph[0].handler = ngx_http_realip_init;  
ph[1].checker = ngx_http_core_rewrite_phase;  
ph[1].handler = ngx_http_rewrite_handler;  
ph[2].checker = ngx_http_core_find_config_phase;
ph[2].handler = NULL;  
ph[3].checker = ngx_http_core_rewrite_phase;
ph[3].handler = ngx_http_rewrite_handler;  
ph[4].checker = ngx_http_core_post_rewrite_phase;
ph[4].handler = NULL;  
ph[5].checker = ngx_http_core_rewrite_phase;  
ph[5].handler = ngx_http_realip_handler,ngx_http_limit_zone_handler��ngx_http_limit_req_handler,ngx_http_degradation_handler
ph[6].checker = ngx_http_core_access_phase
ph[6].handler = ngx_http_access_handler,ngx_http_auth_basic_handler
ph[7].checker = ngx_http_core_post_access_phase
ph[7].handler = NULL
ph[8].checker = ngx_http_core_try_files_phase
ph[8].handler = NULL
ph[9].checker = ngx_http_core_content_phase
ph[9].handler = ngx_http_static_handler,ngx_http_fastcgi_handler,ngx_http_index_handler,,,
ph[10].checker = ngx_http_core_generic_phase
ph[10].handler = ngx_http_log_handler
*/

typedef struct ngx_http_phase_handler_s  ngx_http_phase_handler_t;

typedef ngx_int_t (*ngx_http_phase_handler_pt)(ngx_http_request_t *r,
    ngx_http_phase_handler_t *ph);

struct ngx_http_phase_handler_s {
    ngx_http_phase_handler_pt  checker;
    ngx_http_handler_pt        handler;
    ngx_uint_t                 next;
};


typedef struct {
    ngx_http_phase_handler_t  *handlers;//�������̵ľ�����顣
    ngx_uint_t                 server_rewrite_index;//���һ�£�NGX_HTTP_SERVER_REWRITE_PHASE����׶ε��±꣬����˵��ʼλ��
    ngx_uint_t                 location_rewrite_index;
} ngx_http_phase_engine_t;


typedef struct {
    ngx_array_t                handlers;
} ngx_http_phase_t;


typedef struct {
    ngx_array_t                servers;/* ngx_http_core_srv_conf_t *///ÿ����һ��server{},�����������¼һ�¡�������һ������server_name��
    ngx_http_phase_engine_t    phase_engine;//NGINX�������̽ṹ�������о������ȡ�
    ngx_hash_t                 headers_in_hash;//Ԥ���������HTTPͷ����hash�洢����������:ngx_http_headers_in
    ngx_hash_t                 variables_hash;
    ngx_array_t                variables;       /* ngx_http_variable_t */
    ngx_uint_t                 ncaptures;
    ngx_uint_t                 server_names_hash_max_size;
    ngx_uint_t                 server_names_hash_bucket_size;
    ngx_uint_t                 variables_hash_max_size;
    ngx_uint_t                 variables_hash_bucket_size;

    ngx_hash_keys_arrays_t    *variables_keys;
    ngx_array_t               *ports;//תΪ�������Ķ˿��б���ngx_http_conf_port_t�ṹ
    ngx_uint_t                 try_files;       /* unsigned  try_files:1 */
    ngx_http_phase_t           phases[NGX_HTTP_LOG_PHASE + 1];
} ngx_http_core_main_conf_t;


typedef struct {
    /* array of the ngx_http_server_name_t, "server_name" directive */
    ngx_array_t                 server_names;//����ĳ��server{}��ÿһ��server_name *.com;������һ����Ա������

    /* server ctx */
    ngx_http_conf_ctx_t        *ctx;//Ҳ����server��ctx��ָ��������ngx_http_conf_ctx_t�ṹ������ָ�룬�ֱ�ָ��һ�����顣
    ngx_str_t                   server_name;//������¼��ǰ��������server_nameָ��ġ�

    size_t                      connection_pool_size;
    size_t                      request_pool_size;
    size_t                      client_header_buffer_size;

    ngx_bufs_t                  large_client_header_buffers;

    ngx_msec_t                  client_header_timeout;

    ngx_flag_t                  ignore_invalid_headers;
    ngx_flag_t                  merge_slashes;
    ngx_flag_t                  underscores_in_headers;

    unsigned                    listen:1;//��ʾ���server{}��listenָ������˿ڡ�
#if (NGX_PCRE)
    unsigned                    captures:1;
#endif

    ngx_http_core_loc_conf_t  **named_locations;//@��ͷ������location�������ΪʲôҪ���������أ���Ϊ�����ڵ���server����ȫ�ֵ�
} ngx_http_core_srv_conf_t;


/* list of structures to find core_srv_conf quickly at run time */


typedef struct {
    /* the default server configuration for this address:port */
    ngx_http_core_srv_conf_t  *default_server;//�õ�ַ����Ӧ��Ĭ����������

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


typedef struct {//HTTP �����˿���Ϣ��
    /* ngx_http_in_addr_t or ngx_http_in6_addr_t */
    void                      *addrs;
    ngx_uint_t                 naddrs;
} ngx_http_port_t;


typedef struct {//ͬһ��Э����+�˿ںŵļ����˿���Ϣ�����������档
    ngx_int_t                  family;
    in_port_t                  port;//�����ֽ���Ķ˿�
    ngx_array_t                addrs; /* array of ngx_http_conf_addr_t *///����˿ڶ�Ӧ�ĵ�ַ�б���������ʲô?�˿���ô���ж����ַ�б�?
    /*ddrs��¼����ͬһ��port�󶨵Ĳ�ͬip��ַ�ṹ���б�������һ��listenָ����﷨��֪����:
	listen 127.0.0.1:80; 80; 127.0.0.1; *:80 ; unix:path
    */
} ngx_http_conf_port_t;


typedef struct {
    ngx_http_listen_opt_t      opt;//�����ַ�ṹ�Ķ˿���Ϣ��

    ngx_hash_t                 hash;
    ngx_hash_wildcard_t       *wc_head;
    ngx_hash_wildcard_t       *wc_tail;

#if (NGX_PCRE)
    ngx_uint_t                 nregex;
    ngx_http_server_name_t    *regex;
#endif

    /* the default server configuration for this address:port */
    ngx_http_core_srv_conf_t  *default_server;//ָ������˿ڵ�Ĭ��server��srv_conf���á��Ӷ������ҵ�ctx
    ngx_array_t                servers;  /* array of ngx_http_core_srv_conf_t *///���飬��ʾ����˿�����Щserver_name����������Ӧ��
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

/*��=�� ��ʾ��ȷƥ�䣬����ҵ�������ֹͣ��������������������
��~ �� ��ʾ���ִ�Сдƥ��
��~*�� ��ʾ�����ִ�Сдƥ��
��^~ �� ��ʾֻƥ���ַ���,����ѯ�������ʽ��
��@�� ָ��һ��������location��һ��ֻ�����ڲ��ض�������
*/
struct ngx_http_core_loc_conf_s {
    ngx_str_t     name;          /* location name */

#if (NGX_PCRE)
    ngx_http_regex_t  *regex;//����������������ʽ�ṹ
#endif

    unsigned      noname:1;   /* "if () {}" block or limit_except */
    unsigned      lmt_excpt:1;
    unsigned      named:1;//ָ����һ��������location����@���ſ�ͷ

    unsigned      exact_match:1;//�Ƿ��Ǿ�ȷƥ�䡣����:location =xxx
    unsigned      noregex:1;//^~ ��ͷ��ʾuri��ĳ�������ַ�����ͷ������Ϊƥ�� url·�����ɡ�

    unsigned      auto_redirect:1;
#if (NGX_HTTP_GZIP)
    unsigned      gzip_disable_msie6:2;
#if (NGX_HTTP_DEGRADATION)
    unsigned      gzip_disable_degradation:2;
#endif
#endif

    ngx_http_location_tree_node_t   *static_locations;//һ����inclusive���͵�location��ɵ�������
#if (NGX_PCRE)
    ngx_http_core_loc_conf_t       **regex_locations;//�������ʽ��location�ٴ�
#endif

    /* pointer to the modules' loc_conf */
    void        **loc_conf;//loc_conf��Ա�ᱣ����������module��location�����õ���ʱ�����÷��㡣���ngx_http_core_location

    uint32_t      limit_except;
    void        **limit_except_loc_conf;

    ngx_http_handler_pt  handler;

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
    size_t        postpone_output;         /* postpone_output */
    size_t        limit_rate;              /* limit_rate */
    size_t        limit_rate_after;        /* limit_rate_after */
    size_t        sendfile_max_chunk;      /* sendfile_max_chunk */
    size_t        read_ahead;              /* read_ahead */

    ngx_msec_t    client_body_timeout;     /* client_body_timeout *///��ʾ�ͻ��˷�һ���ֽں��ٹ����֮�ڱ����ٷ����ݡ������Է�һ���ֽڣ��Ȼᣬ�ٷ�һ���Լ�
    										//client_body_timeout���Ǳ�ʾ�ܵ�BODY����ʱ�䣬���Ǽ��ʱ�䡣
    ngx_msec_t    send_timeout;            /* send_timeout */
    ngx_msec_t    keepalive_timeout;       /* keepalive_timeout */
    ngx_msec_t    lingering_time;          /* lingering_time */
    ngx_msec_t    lingering_timeout;       /* lingering_timeout */
    ngx_msec_t    resolver_timeout;        /* resolver_timeout */

    ngx_resolver_t  *resolver;             /* resolver */

    time_t        keepalive_header;        /* keepalive_timeout */

    ngx_uint_t    keepalive_requests;      /* keepalive_requests */
    ngx_uint_t    satisfy;                 /* satisfy */
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

    ngx_queue_t  *locations;//location�ڵ��б���
    //����˳��Ϊ: <���ַ���ƥ�䣬�ݹ�ڵ��ں���> <����ƥ��,�ڲ�������> <@������,�ڲ��ַ�������>  <if�����ģ��ڲ�����>
#if 0
    ngx_http_core_loc_conf_t  *prev_location;
#endif
};


typedef struct {
    ngx_queue_t                      queue;
    ngx_http_core_loc_conf_t        *exact;//�����Ϊ�գ����ʾ��ȷƥ�䣬����ƥ��ȡ�ָ���Ӧ������
    ngx_http_core_loc_conf_t        *inclusive;//��Ҫ����ƥ�䣬����exact����ƥ��ɹ�������location /abc {}
    ngx_str_t                       *name;
    u_char                          *file_name;
    ngx_uint_t                       line;
    ngx_queue_t                      list;
} ngx_http_location_queue_t;//location���е�һ�


struct ngx_http_location_tree_node_s {
    ngx_http_location_tree_node_t   *left;//�ַ����Ƚϵ�ǰ�沿�֣�������Ŀ��ȡ�
    ngx_http_location_tree_node_t   *right;
    ngx_http_location_tree_node_t   *tree;//����ͬǰ׺��������

    ngx_http_core_loc_conf_t        *exact;
    ngx_http_core_loc_conf_t        *inclusive;//Ҫ���а���ƥ��ġ�ָ�����ýṹ�塣

    u_char                           auto_redirect;
    u_char                           len;//���ڵ�����ݳ��ȡ�������ǰ׺��
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