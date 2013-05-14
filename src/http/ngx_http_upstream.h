
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_HTTP_UPSTREAM_H_INCLUDED_
#define _NGX_HTTP_UPSTREAM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>
#include <ngx_event_pipe.h>
#include <ngx_http.h>


#define NGX_HTTP_UPSTREAM_FT_ERROR           0x00000002
#define NGX_HTTP_UPSTREAM_FT_TIMEOUT         0x00000004
#define NGX_HTTP_UPSTREAM_FT_INVALID_HEADER  0x00000008
#define NGX_HTTP_UPSTREAM_FT_HTTP_500        0x00000010
#define NGX_HTTP_UPSTREAM_FT_HTTP_502        0x00000020
#define NGX_HTTP_UPSTREAM_FT_HTTP_503        0x00000040
#define NGX_HTTP_UPSTREAM_FT_HTTP_504        0x00000080
#define NGX_HTTP_UPSTREAM_FT_HTTP_404        0x00000100
#define NGX_HTTP_UPSTREAM_FT_UPDATING        0x00000200
#define NGX_HTTP_UPSTREAM_FT_BUSY_LOCK       0x00000400
#define NGX_HTTP_UPSTREAM_FT_MAX_WAITING     0x00000800
#define NGX_HTTP_UPSTREAM_FT_NOLIVE          0x40000000
#define NGX_HTTP_UPSTREAM_FT_OFF             0x80000000

#define NGX_HTTP_UPSTREAM_FT_STATUS          (NGX_HTTP_UPSTREAM_FT_HTTP_500  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_502  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_503  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_504  \
                                             |NGX_HTTP_UPSTREAM_FT_HTTP_404)

#define NGX_HTTP_UPSTREAM_INVALID_HEADER     40


#define NGX_HTTP_UPSTREAM_IGN_XA_REDIRECT    0x00000002
#define NGX_HTTP_UPSTREAM_IGN_XA_EXPIRES     0x00000004
#define NGX_HTTP_UPSTREAM_IGN_EXPIRES        0x00000008
#define NGX_HTTP_UPSTREAM_IGN_CACHE_CONTROL  0x00000010
#define NGX_HTTP_UPSTREAM_IGN_SET_COOKIE     0x00000020


typedef struct {
    ngx_msec_t                       bl_time;
    ngx_uint_t                       bl_state;

    ngx_uint_t                       status;
    time_t                           response_sec;
    ngx_uint_t                       response_msec;
    off_t                           response_length;

    ngx_str_t                       *peer;
} ngx_http_upstream_state_t;


typedef struct {//Õâ¸öÊý×éÊÇÃ¿http{}¿é¶¼ÓÐÒ»·ÝµÄ¡£»òÕßÀïÃæÓÐµÄ»°Ò²¿ÉÒÔ¡£Context: 	http£¬²»ÄÜ´æÔÚserverÀïÃæµÄ
    ngx_hash_t                       headers_in_hash;//ngx_http_upstream_headers_inÀïÃæµÄÊý¾Ý.
    ngx_array_t                      upstreams;//Êý×é£¬´ú±íÓÐ¶àÉÙ¸öupstream{}¿é¡£server xx.xx.xx.xx:xx weight=2 max_fails=3;  ÐÅÏ¢µÄÊý×é¡£
    //ÓÉngx_http_upstream_create_main_confº¯Êý·µ»Ø¡£´æ·ÅÔÚÉÏ²ãµÄctxÖÐ¡£
                                             /* ngx_http_upstream_srv_conf_t */
} ngx_http_upstream_main_conf_t;

typedef struct ngx_http_upstream_srv_conf_s  ngx_http_upstream_srv_conf_t;

typedef ngx_int_t (*ngx_http_upstream_init_pt)(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us);
typedef ngx_int_t (*ngx_http_upstream_init_peer_pt)(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);


typedef struct {
    ngx_http_upstream_init_pt        init_upstream;//ngx_http_upstream_init_ip_hashº¯ÊýµÈ¡£Ä¬ÈÏÎªngx_http_upstream_init_round_robin
    ngx_http_upstream_init_peer_pt   init;
    void                            *data;
} ngx_http_upstream_peer_t;


typedef struct {//Ò»¸öserver xx.xx.xx.xx:xx weight=2 max_fails=3;  µÄÅäÖÃÊý¾Ý
    ngx_addr_t                      *addrs;
    ngx_uint_t                       naddrs;
    ngx_uint_t                       weight;
    ngx_uint_t                       max_fails;
    time_t                           fail_timeout;

    unsigned                         down:1;
    unsigned                         backup:1;
} ngx_http_upstream_server_t;


#define NGX_HTTP_UPSTREAM_CREATE        0x0001
#define NGX_HTTP_UPSTREAM_WEIGHT        0x0002
#define NGX_HTTP_UPSTREAM_MAX_FAILS     0x0004
#define NGX_HTTP_UPSTREAM_FAIL_TIMEOUT  0x0008
#define NGX_HTTP_UPSTREAM_DOWN          0x0010
#define NGX_HTTP_UPSTREAM_BACKUP        0x0020


struct ngx_http_upstream_srv_conf_s {//Ò»¸öupstream{}ÅäÖÃ½á¹¹µÄÊý¾Ý,Õâ¸öÊÇumcf->upstreamsÀïÃæµÄÊý×éÏî¡£umcfÊÇupstreamÄ£¿éµÄ¶¥²ãÅäÖÃÁË¡£
    ngx_http_upstream_peer_t         peer;
    void                           **srv_conf;//ÎÒËùÊôµÄupstreamµÄctxÀïÃæµÄsrv_conf¡£»ØÖ¸Ò»ÏÂÎÒËùÊôµÄÅäÖÃÊý×éctx->srv_conf¡£

    ngx_array_t                     *servers;  /* ngx_http_upstream_server_t *///¼ÇÂ¼±¾upstream{}¿éµÄËùÓÐserverÖ¸Áî¡£²»ÊÇserver{}¿é

    ngx_uint_t                       flags;
    ngx_str_t                        host;
    u_char                          *file_name;//ÅäÖÃÎÄ¼þÃû³Æ
    ngx_uint_t                       line;//ÅäÖÃÎÄ¼þÖÐµÄÐÐºÅ
    in_port_t                        port;
    in_port_t                        default_port;
};


typedef struct {
    ngx_http_upstream_srv_conf_t    *upstream;

    ngx_msec_t                       connect_timeout;
    ngx_msec_t                       send_timeout;
    ngx_msec_t                       read_timeout;
    ngx_msec_t                       timeout;

    size_t                           send_lowat;
    size_t                           buffer_size;

    size_t                           busy_buffers_size;
    size_t                           max_temp_file_size;
    size_t                           temp_file_write_size;

    size_t                           busy_buffers_size_conf;
    size_t                           max_temp_file_size_conf;
    size_t                           temp_file_write_size_conf;

    ngx_bufs_t                       bufs;

    ngx_uint_t                       ignore_headers;
    ngx_uint_t                       next_upstream;
    ngx_uint_t                       store_access;
    ngx_flag_t                       buffering;
    ngx_flag_t                       pass_request_headers;//ÊÇ·ñÒª½«HTTPÇëÇóÍ·²¿µÄHEADER·¢ËÍ¸øºó¶Ë£¬ÒÑHTTP_ÎªÇ°×º
    ngx_flag_t                       pass_request_body;//ÊÇ·ñÒª·¢ËÍÇëÇóµÄBODY£¬Ò»°ã¿Ï¶¨Òª·¢ËÍµÄ

    ngx_flag_t                       ignore_client_abort;
    ngx_flag_t                       intercept_errors;
    ngx_flag_t                       cyclic_temp_file;

    ngx_path_t                      *temp_path;

    ngx_hash_t                       hide_headers_hash;
    ngx_array_t                     *hide_headers;
    ngx_array_t                     *pass_headers;

    ngx_addr_t                      *local;

#if (NGX_HTTP_CACHE)
    ngx_shm_zone_t                  *cache;

    ngx_uint_t                       cache_min_uses;
    ngx_uint_t                       cache_use_stale;
    ngx_uint_t                       cache_methods;

    ngx_array_t                     *cache_valid;
    ngx_array_t                     *cache_bypass;
    ngx_array_t                     *no_cache;
#endif

    ngx_array_t                     *store_lengths;
    ngx_array_t                     *store_values;

    signed                           store:2;
    unsigned                         intercept_404:1;
    unsigned                         change_buffering:1;

#if (NGX_HTTP_SSL)
    ngx_ssl_t                       *ssl;
    ngx_flag_t                       ssl_session_reuse;
#endif

} ngx_http_upstream_conf_t;


typedef struct {
    ngx_str_t                        name;
    ngx_http_header_handler_pt       handler;//µÃµ½Ò»¸öFCGI·µ»ØÊý¾Ý/»òÕßÒ»¸ö´úÀíÄ£¿é·µ»ØÐÐºó£¬¾Í»áµ÷ÓÃÕâ¸ö
    ngx_uint_t                       offset;
    ngx_http_header_handler_pt       copy_handler;
    ngx_uint_t                       conf;
    ngx_uint_t                       redirect;  /* unsigned   redirect:1; */
} ngx_http_upstream_header_t;//Õâ¸öÍ·²¿×Ö¶Î´¦Àíº¯Êý¼¯ÖÐÔÚngx_http_upstream_headers_inÀïÃæÉèÖÃ


typedef struct {
    ngx_list_t                       headers;//±£´æÁËËùÓÐµÄ½«Òª´«µÝ¸øclientµÄÍ·
    ngx_uint_t                       status_n;//ÕâÀïÓÃÀ´ÉèÖÃ·¢ËÍ¸øclientµÄ ×´Ì¬Âë
    ngx_str_t                        status_line;
	//ÏÂÃæÕâÐ©Í·ÊÇÎªÁË¸ü·½±ãµÄ´æÈ¡Öµ
    ngx_table_elt_t                 *status;
    ngx_table_elt_t                 *date;
    ngx_table_elt_t                 *server;
    ngx_table_elt_t                 *connection;

    ngx_table_elt_t                 *expires;
    ngx_table_elt_t                 *etag;
    ngx_table_elt_t                 *x_accel_expires;
    ngx_table_elt_t                 *x_accel_redirect;
    ngx_table_elt_t                 *x_accel_limit_rate;

    ngx_table_elt_t                 *content_type;
    ngx_table_elt_t                 *content_length;

    ngx_table_elt_t                 *last_modified;
    ngx_table_elt_t                 *location;
    ngx_table_elt_t                 *accept_ranges;
    ngx_table_elt_t                 *www_authenticate;

#if (NGX_HTTP_GZIP)
    ngx_table_elt_t                 *content_encoding;
#endif

    off_t                            content_length_n;

    ngx_array_t                      cache_control;
} ngx_http_upstream_headers_in_t;


typedef struct {
    ngx_str_t                        host;
    in_port_t                        port;
    ngx_uint_t                       no_port; /* unsigned no_port:1 */

    ngx_uint_t                       naddrs;
    in_addr_t                       *addrs;

    struct sockaddr                 *sockaddr;
    socklen_t                        socklen;

    ngx_resolver_ctx_t              *ctx;//ngx_resolver_ctx_t½á¹¹£¬Ö¸Ïò½âÎöµÄ¸÷ÏîÊý¾Ý
} ngx_http_upstream_resolved_t;


typedef void (*ngx_http_upstream_handler_pt)(ngx_http_request_t *r,
    ngx_http_upstream_t *u);


struct ngx_http_upstream_s {//±¾½á¹¹ÌåÓÃÀ´±£´æÒ»¸öÁ¬½ÓµÄupstreamÐÅÏ¢£¬°üÀ¨¸÷ÖÖÐèÒªupstream»Øµ÷µÄº¯ÊýµÈ¡£
    ngx_http_upstream_handler_pt     read_event_handler; //ngx_http_upstream_process_header
    ngx_http_upstream_handler_pt     write_event_handler;// ngx_http_upstream_send_request_handler

    ngx_peer_connection_t            peer;

    ngx_event_pipe_t                *pipe;

    ngx_chain_t                     *request_bufs;//¿Í»§¶Ë·¢ËÍ¹ýÀ´µÄÊý¾Ýbody²¿·Ö£¬ÔÚngx_http_upstream_init_requestÉèÖÃÎª¿Í»§¶Ë·¢ËÍµÄHTTP BODY
    //Ò²¿ÉÄÜÊÇ´ú±íÒª·¢ËÍ¸øºó¶ËµÄÊý¾ÝÁ´±í½á¹¹£¬±ÈÈçngx_http_proxy_create_request»áÕâÃ´·ÅµÄ¡£±ÈÈçÊÇFCGI½á¹¹Êý¾Ý£¬»òÕßProxy½á¹¹µÈ¡£

    ngx_output_chain_ctx_t           output;//Êä³öÊý¾ÝµÄ½á¹¹£¬ÀïÃæ´æÓÐÒª·¢ËÍµÄÊý¾Ý£¬ÒÔ¼°·¢ËÍµÄoutput_filterÖ¸Õë
    ngx_chain_writer_ctx_t           writer;//²Î¿¼ngx_chain_writer£¬ÀïÃæ»á½«Êä³öbufÒ»¸ö¸öÁ¬½Óµ½ÕâÀï¡£

    ngx_http_upstream_conf_t        *conf;//Îªu->conf = &flcf->upstream;

    ngx_http_upstream_headers_in_t   headers_in;//´æ·Å´ÓÉÏÓÎ·µ»ØµÄÍ·²¿ÐÅÏ¢£¬

    ngx_http_upstream_resolved_t    *resolved;//½âÎö³öÀ´µÄfastcgi_pass   127.0.0.1:9000;ºóÃæµÄ×Ö·û´®ÄÚÈÝ£¬¿ÉÄÜÓÐ±äÁ¿Âï¡£

    ngx_buf_t                        buffer;///¶ÁÈ¡ÉÏÓÎ·µ»ØµÄÊý¾ÝµÄ»º³åÇø
    size_t                           length;//Òª·¢ËÍ¸ø¿Í»§¶ËµÄÊý¾Ý´óÐ¡

    ngx_chain_t                     *out_bufs;//Õâ¸öÊÇÒª·¢ËÍ¸ø¿Í»§¶ËµÄÊý¾ÝÁ´½Ó±íå?
    ngx_chain_t                     *busy_bufs;//µ÷ÓÃÁËngx_http_output_filter£¬²¢½«out_bufsµÄÁ´±íÊý¾ÝÒÆ¶¯µ½ÕâÀï£¬´ý·¢ËÍÍê±Ïºó£¬»áÒÆ¶¯µ½free_bufs
    ngx_chain_t                     *free_bufs;//¿ÕÏÐµÄ»º³åÇø¡£¿ÉÒÔ·ÖÅä

    ngx_int_t                      (*input_filter_init)(void *data);//½øÐÐ³õÊ¼»¯£¬Ã»Ê²Ã´ÓÃ£¬memcacheÉèÖÃÎªngx_http_memcached_filter_init
    ngx_int_t                      (*input_filter)(void *data, ssize_t bytes);//ngx_http_upstream_non_buffered_filter£¬ngx_http_memcached_filterµÈ¡£
    void                            *input_filter_ctx;//Ö¸ÏòËùÊôµÄÇëÇóµÈÉÏÏÂÎÄ

#if (NGX_HTTP_CACHE)
    ngx_int_t                      (*create_key)(ngx_http_request_t *r);
#endif
	//ÏÂÃæµÄupstream»Øµ÷Ö¸ÕëÊÇ¸÷¸öÄ£¿éÉèÖÃµÄ£¬±ÈÈçngx_http_fastcgi_handlerÀïÃæÉèÖÃÁËfcgiµÄÏà¹Ø»Øµ÷º¯Êý¡£
    ngx_int_t                      (*create_request)(ngx_http_request_t *r);//Éú³É·¢ËÍµ½ÉÏÓÎ·þÎñÆ÷µÄÇëÇó»º³å£¨»òÕßÒ»Ìõ»º³åÁ´£©
    ngx_int_t                      (*reinit_request)(ngx_http_request_t *r);//ÔÚºó¶Ë·þÎñÆ÷±»ÖØÖÃµÄÇé¿öÏÂ£¨ÔÚcreate_request±»µÚ¶þ´Îµ÷ÓÃÖ®Ç°£©±»µ÷ÓÃ
    ngx_int_t                      (*process_header)(ngx_http_request_t *r);//´¦ÀíÉÏÓÎ·þÎñÆ÷»Ø¸´µÄµÚÒ»¸öbit£¬Ê±³£ÊÇ±£´æÒ»¸öÖ¸ÏòÉÏÓÎ»Ø¸´¸ºÔØµÄÖ¸Õë
    void                           (*abort_request)(ngx_http_request_t *r);//ÔÚ¿Í»§¶Ë·ÅÆúÇëÇóµÄÊ±ºò±»µ÷ÓÃ
    void                           (*finalize_request)(ngx_http_request_t *r,//ÔÚNginxÍê³É´ÓÉÏÓÎ·þÎñÆ÷¶ÁÈë»Ø¸´ÒÔºó±»µ÷ÓÃ
                                         ngx_int_t rc);
    ngx_int_t                      (*rewrite_redirect)(ngx_http_request_t *r,
                                         ngx_table_elt_t *h, size_t prefix);

    ngx_msec_t                       timeout;

    ngx_http_upstream_state_t       *state;//µ±Ç°µÄ×´Ì¬

    ngx_str_t                        method;
    ngx_str_t                        schema;
    ngx_str_t                        uri;

    ngx_http_cleanup_pt             *cleanup;//ngx_http_upstream_cleanup

    unsigned                         store:1;
    unsigned                         cacheable:1;
    unsigned                         accel:1;
    unsigned                         ssl:1;
#if (NGX_HTTP_CACHE)
    unsigned                         cache_status:3;
#endif

    unsigned                         buffering:1;

    unsigned                         request_sent:1;//ÊÇ·ñÒÑ¾­½«request_bufsµÄÊý¾Ý·ÅÈëÊä³öÁ´±íÀïÃæ
    unsigned                         header_sent:1;//±ê¼ÇÒÑ¾­·¢ËÍÁËÍ·²¿×Ö¶Î¡£
};


typedef struct {
    ngx_uint_t                      status;
    ngx_uint_t                      mask;
} ngx_http_upstream_next_t;


ngx_int_t ngx_http_upstream_header_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);

ngx_int_t ngx_http_upstream_create(ngx_http_request_t *r);
void ngx_http_upstream_init(ngx_http_request_t *r);
ngx_http_upstream_srv_conf_t *ngx_http_upstream_add(ngx_conf_t *cf,
    ngx_url_t *u, ngx_uint_t flags);
char *ngx_http_upstream_bind_set_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
ngx_int_t ngx_http_upstream_hide_headers_hash(ngx_conf_t *cf,
    ngx_http_upstream_conf_t *conf, ngx_http_upstream_conf_t *prev,
    ngx_str_t *default_hide_headers, ngx_hash_init_t *hash);


#define ngx_http_conf_upstream_srv_conf(uscf, module)                         \
    uscf->srv_conf[module.ctx_index]


extern ngx_module_t        ngx_http_upstream_module;
extern ngx_conf_bitmask_t  ngx_http_upstream_cache_method_mask[];
extern ngx_conf_bitmask_t  ngx_http_upstream_ignore_headers_masks[];


#endif /* _NGX_HTTP_UPSTREAM_H_INCLUDED_ */
