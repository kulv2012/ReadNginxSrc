
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_HTTP_REQUEST_H_INCLUDED_
#define _NGX_HTTP_REQUEST_H_INCLUDED_


#define NGX_HTTP_MAX_URI_CHANGES           10
#define NGX_HTTP_MAX_SUBREQUESTS           50

/* must be 2^n */
#define NGX_HTTP_LC_HEADER_LEN             32


#define NGX_HTTP_DISCARD_BUFFER_SIZE       4096
#define NGX_HTTP_LINGERING_BUFFER_SIZE     4096


#define NGX_HTTP_VERSION_9                 9
#define NGX_HTTP_VERSION_10                1000
#define NGX_HTTP_VERSION_11                1001

#define NGX_HTTP_UNKNOWN                   0x0001
#define NGX_HTTP_GET                       0x0002
#define NGX_HTTP_HEAD                      0x0004
#define NGX_HTTP_POST                      0x0008
#define NGX_HTTP_PUT                       0x0010
#define NGX_HTTP_DELETE                    0x0020
#define NGX_HTTP_MKCOL                     0x0040
#define NGX_HTTP_COPY                      0x0080
#define NGX_HTTP_MOVE                      0x0100
#define NGX_HTTP_OPTIONS                   0x0200
#define NGX_HTTP_PROPFIND                  0x0400
#define NGX_HTTP_PROPPATCH                 0x0800
#define NGX_HTTP_LOCK                      0x1000
#define NGX_HTTP_UNLOCK                    0x2000
#define NGX_HTTP_PATCH                     0x4000
#define NGX_HTTP_TRACE                     0x8000

#define NGX_HTTP_CONNECTION_CLOSE          1
#define NGX_HTTP_CONNECTION_KEEP_ALIVE     2


#define NGX_NONE                           1


#define NGX_HTTP_PARSE_HEADER_DONE         1

#define NGX_HTTP_CLIENT_ERROR              10
#define NGX_HTTP_PARSE_INVALID_METHOD      10
#define NGX_HTTP_PARSE_INVALID_REQUEST     11
#define NGX_HTTP_PARSE_INVALID_09_METHOD   12

#define NGX_HTTP_PARSE_INVALID_HEADER      13


/* unused                                  1 */
#define NGX_HTTP_SUBREQUEST_IN_MEMORY      2
#define NGX_HTTP_SUBREQUEST_WAITED         4
#define NGX_HTTP_LOG_UNSAFE                8


#define NGX_HTTP_OK                        200
#define NGX_HTTP_CREATED                   201
#define NGX_HTTP_ACCEPTED                  202
#define NGX_HTTP_NO_CONTENT                204
#define NGX_HTTP_PARTIAL_CONTENT           206

#define NGX_HTTP_SPECIAL_RESPONSE          300
#define NGX_HTTP_MOVED_PERMANENTLY         301
#define NGX_HTTP_MOVED_TEMPORARILY         302
#define NGX_HTTP_SEE_OTHER                 303
#define NGX_HTTP_NOT_MODIFIED              304

#define NGX_HTTP_BAD_REQUEST               400
#define NGX_HTTP_UNAUTHORIZED              401
#define NGX_HTTP_FORBIDDEN                 403
#define NGX_HTTP_NOT_FOUND                 404
#define NGX_HTTP_NOT_ALLOWED               405
#define NGX_HTTP_REQUEST_TIME_OUT          408
#define NGX_HTTP_CONFLICT                  409
#define NGX_HTTP_LENGTH_REQUIRED           411
#define NGX_HTTP_PRECONDITION_FAILED       412
#define NGX_HTTP_REQUEST_ENTITY_TOO_LARGE  413
#define NGX_HTTP_REQUEST_URI_TOO_LARGE     414
#define NGX_HTTP_UNSUPPORTED_MEDIA_TYPE    415
#define NGX_HTTP_RANGE_NOT_SATISFIABLE     416


/* Our own HTTP codes */

/* The special code to close connection without any response */
#define NGX_HTTP_CLOSE                     444

#define NGX_HTTP_OWN_CODES                 495

#define NGX_HTTPS_CERT_ERROR               495
#define NGX_HTTPS_NO_CERT                  496

/*
 * We use the special code for the plain HTTP requests that are sent to
 * HTTPS port to distinguish it from 4XX in an error page redirection
 */
#define NGX_HTTP_TO_HTTPS                  497

/* 498 is the canceled code for the requests with invalid host name */

/*
 * HTTP does not define the code for the case when a client closed
 * the connection while we are processing its request so we introduce
 * own code to log such situation when a client has closed the connection
 * before we even try to send the HTTP header to it
 */
#define NGX_HTTP_CLIENT_CLOSED_REQUEST     499


#define NGX_HTTP_INTERNAL_SERVER_ERROR     500
#define NGX_HTTP_NOT_IMPLEMENTED           501
#define NGX_HTTP_BAD_GATEWAY               502
#define NGX_HTTP_SERVICE_UNAVAILABLE       503
#define NGX_HTTP_GATEWAY_TIME_OUT          504
#define NGX_HTTP_INSUFFICIENT_STORAGE      507


#define NGX_HTTP_LOWLEVEL_BUFFERED         0xf0
#define NGX_HTTP_WRITE_BUFFERED            0x10
#define NGX_HTTP_GZIP_BUFFERED             0x20
#define NGX_HTTP_SSI_BUFFERED              0x01
#define NGX_HTTP_SUB_BUFFERED              0x02
#define NGX_HTTP_COPY_BUFFERED             0x04


typedef enum {
    NGX_HTTP_INITING_REQUEST_STATE = 0,
    NGX_HTTP_READING_REQUEST_STATE,
    NGX_HTTP_PROCESS_REQUEST_STATE,

    NGX_HTTP_CONNECT_UPSTREAM_STATE,
    NGX_HTTP_WRITING_UPSTREAM_STATE,
    NGX_HTTP_READING_UPSTREAM_STATE,

    NGX_HTTP_WRITING_REQUEST_STATE,
    NGX_HTTP_LINGERING_CLOSE_STATE,
    NGX_HTTP_KEEPALIVE_STATE
} ngx_http_state_e;


typedef struct {
    ngx_str_t                         name;
    ngx_uint_t                        offset;
    ngx_http_header_handler_pt        handler;
} ngx_http_header_t;


typedef struct {
    ngx_str_t                         name;
    ngx_uint_t                        offset;
} ngx_http_header_out_t;


typedef struct {
    ngx_list_t                        headers;//请求的HTTP HEADER K-V队列

    ngx_table_elt_t                  *host;
    ngx_table_elt_t                  *connection;
    ngx_table_elt_t                  *if_modified_since;
    ngx_table_elt_t                  *user_agent;
    ngx_table_elt_t                  *referer;
    ngx_table_elt_t                  *content_length;
    ngx_table_elt_t                  *content_type;

    ngx_table_elt_t                  *range;
    ngx_table_elt_t                  *if_range;

    ngx_table_elt_t                  *transfer_encoding;
    ngx_table_elt_t                  *expect;

#if (NGX_HTTP_GZIP)
    ngx_table_elt_t                  *accept_encoding;
    ngx_table_elt_t                  *via;
#endif

    ngx_table_elt_t                  *authorization;

    ngx_table_elt_t                  *keep_alive;

#if (NGX_HTTP_PROXY || NGX_HTTP_REALIP || NGX_HTTP_GEO)
    ngx_table_elt_t                  *x_forwarded_for;
#endif

#if (NGX_HTTP_REALIP)
    ngx_table_elt_t                  *x_real_ip;
#endif

#if (NGX_HTTP_HEADERS)
    ngx_table_elt_t                  *accept;
    ngx_table_elt_t                  *accept_language;
#endif

#if (NGX_HTTP_DAV)
    ngx_table_elt_t                  *depth;
    ngx_table_elt_t                  *destination;
    ngx_table_elt_t                  *overwrite;
    ngx_table_elt_t                  *date;
#endif

    ngx_str_t                         user;
    ngx_str_t                         passwd;

    ngx_array_t                       cookies;

    ngx_str_t                         server;//HOST 虚拟主机的名字，或者主机域的名字
    off_t                             content_length_n;
    time_t                            keep_alive_n;

    unsigned                          connection_type:2;//连接是keep-alive还是要close
    unsigned                          msie:1;
    unsigned                          msie4:1;
    unsigned                          msie6:1;
    unsigned                          opera:1;
    unsigned                          gecko:1;
    unsigned                          chrome:1;
    unsigned                          safari:1;
    unsigned                          konqueror:1;
} ngx_http_headers_in_t;


typedef struct {
    ngx_list_t                        headers;

    ngx_uint_t                        status;
    ngx_str_t                         status_line;

    ngx_table_elt_t                  *server;
    ngx_table_elt_t                  *date;
    ngx_table_elt_t                  *content_length;
    ngx_table_elt_t                  *content_encoding;
    ngx_table_elt_t                  *location;
    ngx_table_elt_t                  *refresh;
    ngx_table_elt_t                  *last_modified;
    ngx_table_elt_t                  *content_range;
    ngx_table_elt_t                  *accept_ranges;
    ngx_table_elt_t                  *www_authenticate;
    ngx_table_elt_t                  *expires;
    ngx_table_elt_t                  *etag;

    ngx_str_t                        *override_charset;

    size_t                            content_type_len;
    ngx_str_t                         content_type;
    ngx_str_t                         charset;
    u_char                           *content_type_lowcase;
    ngx_uint_t                        content_type_hash;

    ngx_array_t                       cache_control;

    off_t                             content_length_n;
    time_t                            date_time;
    time_t                            last_modified_time;
} ngx_http_headers_out_t;


typedef void (*ngx_http_client_body_handler_pt)(ngx_http_request_t *r);

typedef struct {
    ngx_temp_file_t                  *temp_file;//如果请求的POST数据需要写入文件，则此处记录文件
    ngx_chain_t                      *bufs;//请求BODY的BUFFER链接表，如果在解析HEADER的时候预读了数据，则会分2块存放在这里
    ngx_buf_t                        *buf;//指向所POST的，可以写入的缓冲区
    off_t                             rest;//r->headers_in.content_length_n - preread;计算还有多少数据需要读取，减去刚才预读的部分。
    ngx_chain_t                      *to_write;//数据将要写入所指向的这个链接表的位置。
    ngx_http_client_body_handler_pt   post_handler;//POST数据读取完毕后需要调用的HANDLER函数，也就是ngx_http_upstream_init
} ngx_http_request_body_t;


typedef struct {
    ngx_http_request_t               *request;//指向这个HTTP连接的巨大的请求结构体

    ngx_buf_t                       **busy;
    ngx_int_t                         nbusy;

    ngx_buf_t                       **free;
    ngx_int_t                         nfree;

    ngx_uint_t                        pipeline;    /* unsigned  pipeline:1; */
} ngx_http_connection_t;//HTTP专用连接，放入ngx_connection_t的数据部分


typedef struct ngx_http_server_name_s  ngx_http_server_name_t;


typedef struct {
     ngx_hash_combined_t              names;

     ngx_uint_t                       nregex;
     ngx_http_server_name_t          *regex;
} ngx_http_virtual_names_t;


typedef void (*ngx_http_cleanup_pt)(void *data);

typedef struct ngx_http_cleanup_s  ngx_http_cleanup_t;

struct ngx_http_cleanup_s {
    ngx_http_cleanup_pt               handler;//为ngx_http_upstream_cleanup
    void                             *data;
    ngx_http_cleanup_t               *next;
};


typedef ngx_int_t (*ngx_http_post_subrequest_pt)(ngx_http_request_t *r,
    void *data, ngx_int_t rc);

typedef struct {
    ngx_http_post_subrequest_pt       handler;
    void                             *data;
} ngx_http_post_subrequest_t;


typedef struct ngx_http_postponed_request_s  ngx_http_postponed_request_t;

struct ngx_http_postponed_request_s {
    ngx_http_request_t               *request;
    ngx_chain_t                      *out;
    ngx_http_postponed_request_t     *next;
};


typedef struct ngx_http_posted_request_s  ngx_http_posted_request_t;

struct ngx_http_posted_request_s {
    ngx_http_request_t               *request;
    ngx_http_posted_request_t        *next;
};


typedef ngx_int_t (*ngx_http_handler_pt)(ngx_http_request_t *r);
typedef void (*ngx_http_event_handler_pt)(ngx_http_request_t *r);


struct ngx_http_request_s {
    uint32_t                          signature;         /* "HTTP" */

    ngx_connection_t                 *connection;//索引所对应的SOCK连接  ngx_connection_t

    void                            **ctx;//这是什么?r->ctx[module.ctx_index] = c;那么，每个模块都有一个。
    void                            **main_conf;
    void                            **srv_conf;
    void                            **loc_conf;

    ngx_http_event_handler_pt         read_event_handler;//由ngx_http_request_handler调用的读句柄ngx_http_block_reading，ngx_http_read_client_request_body_handler
    ngx_http_event_handler_pt         write_event_handler;//ngx_http_core_run_phases

#if (NGX_HTTP_CACHE)
    ngx_http_cache_t                 *cache;
#endif

    ngx_http_upstream_t              *upstream;
    ngx_array_t                      *upstream_states;//数组，保存当前的upstream的状态
                                         /* of ngx_http_upstream_state_t */

    ngx_pool_t                       *pool;//一个HTTP请求结构体一个POOL，大小为request_pool_size。其对应的连接也有个POOL
    ngx_buf_t                        *header_in;//header信息的缓存buffer，通过这个缓存对header进行分析
    ngx_http_headers_in_t             headers_in;//请求的header结构体，客户端发送过来的头部数据
    ngx_http_headers_out_t            headers_out;//输出给客户端的头部数据

    ngx_http_request_body_t          *request_body;//客户端发送过来的POST数据存放在此,ngx_http_read_client_request_body放的

    time_t                            lingering_time;
    time_t                            start_sec;//请求开始时间
    ngx_msec_t                        start_msec;

    ngx_uint_t                        method;
    ngx_uint_t                        http_version;

    ngx_str_t                         request_line;
    ngx_str_t                         uri;//解析后的URI字符串。原始的在unparsed_uri
    ngx_str_t                         args;
    ngx_str_t                         exten;
    ngx_str_t                         unparsed_uri;//解析前的原始字符串URI。

    ngx_str_t                         method_name;
    ngx_str_t                         http_protocol;

    ngx_chain_t                      *out;//要输出给客户端的数据的链接结构
    ngx_http_request_t               *main;//用来区分是否是subrequest
    ngx_http_request_t               *parent;
    ngx_http_postponed_request_t     *postponed;
    ngx_http_post_subrequest_t       *post_subrequest;
    ngx_http_posted_request_t        *posted_requests;

    ngx_http_virtual_names_t         *virtual_names;//什么东西。r->virtual_names = addr_conf->virtual_names;

    ngx_int_t                         phase_handler;//进行各个phrases的解析时，代表当前处理到了第几个cmcf->phase_engine.handlers数组下标了。
    ngx_http_handler_pt               content_handler;
    ngx_uint_t                        access_code;

    ngx_http_variable_value_t        *variables;//啥东西

#if (NGX_PCRE)
    ngx_uint_t                        ncaptures;
    int                              *captures;
    u_char                           *captures_data;
#endif

    size_t                            limit_rate;

    /* used to learn the Apache compatible response length without a header */
    size_t                            header_size;//记录返回客户端的头部字段的总长度。

    off_t                             request_length;//这次请求的总长度，包括总发送的长度

    ngx_uint_t                        err_status;

    ngx_http_connection_t            *http_connection;//指向所属的ngx_http_connection_t

    ngx_http_log_handler_pt           log_handler;//ngx_http_log_error_handler

    ngx_http_cleanup_t               *cleanup;

    unsigned                          subrequests:8;
    unsigned                          count:8;
    unsigned                          blocked:8;

    unsigned                          aio:1;

    unsigned                          http_state:4;

    /* URI with "/." and on Win32 with "//" */
    unsigned                          complex_uri:1;

    /* URI with "%" */
    unsigned                          quoted_uri:1;

    /* URI with "+" */
    unsigned                          plus_in_uri:1;

    /* URI with " " */
    unsigned                          space_in_uri:1;

    unsigned                          invalid_header:1;

    unsigned                          add_uri_to_alias:1;
    unsigned                          valid_location:1;
    unsigned                          valid_unparsed_uri:1;
    unsigned                          uri_changed:1;
    unsigned                          uri_changes:4;//进行重定向的最多次数为11次，每次不断减少1.

    unsigned                          request_body_in_single_buf:1;
    unsigned                          request_body_in_file_only:1;/*这个指令始终存储一个连接请求实体到一个文件即使它只有0字节。
																	注意：如果这个指令打开，那么一个连接请求完成后，所存储的文件并不会删除。
																	这个指令可以用于debug调试*/
    unsigned                          request_body_in_persistent_file:1;
    unsigned                          request_body_in_clean_file:1;
    unsigned                          request_body_file_group_access:1;
    unsigned                          request_body_file_log_level:3;

    unsigned                          subrequest_in_memory:1;
    unsigned                          waited:1;

#if (NGX_HTTP_CACHE)
    unsigned                          cached:1;
#endif

#if (NGX_HTTP_GZIP)
    unsigned                          gzip_tested:1;
    unsigned                          gzip_ok:1;
    unsigned                          gzip_vary:1;
#endif

    unsigned                          proxy:1;
    unsigned                          bypass_cache:1;
    unsigned                          no_cache:1;

    /*
     * instead of using the request context data in
     * ngx_http_limit_zone_module and ngx_http_limit_req_module
     * we use the single bits in the request structure
     */
    unsigned                          limit_zone_set:1;
    unsigned                          limit_req_set:1;

#if 0
    unsigned                          cacheable:1;
#endif

    unsigned                          pipeline:1;//r->pipeline = hc->pipeline;从ngx_http_connection_t来
    unsigned                          plain_http:1;//是否通过SSL发送一般的HTTP请求，非加密的
    unsigned                          chunked:1;
    unsigned                          header_only:1;
    unsigned                          keepalive:1;//跟r->headers_in.connection_type对应，连接是否关闭等
    unsigned                          lingering_close:1;//详见:http://tengine.taobao.org/book/chapter_2.html
    unsigned                          discard_body:1;//是否需要丢弃请求内容部分。
    unsigned                          internal:1;//是否在内部重定向过程中，如果是，则不用从头进行phase处理
    unsigned                          error_page:1;
    unsigned                          ignore_content_encoding:1;
    unsigned                          filter_finalize:1;
    unsigned                          post_action:1;
    unsigned                          request_complete:1;
    unsigned                          request_output:1;
    unsigned                          header_sent:1;
    unsigned                          expect_tested:1;
    unsigned                          root_tested:1;
    unsigned                          done:1;
    unsigned                          logged:1;

    unsigned                          buffered:4;

    unsigned                          main_filter_need_in_memory:1;
    unsigned                          filter_need_in_memory:1;
    unsigned                          filter_need_temporary:1;
    unsigned                          allow_ranges:1;

#if (NGX_STAT_STUB)
    unsigned                          stat_reading:1;
    unsigned                          stat_writing:1;
#endif

    /* used to parse HTTP headers */
    ngx_uint_t                        state;
    ngx_uint_t                        header_hash;
    ngx_uint_t                        lowcase_index;
    u_char                            lowcase_header[NGX_HTTP_LC_HEADER_LEN];
    u_char                           *header_name_start;
    u_char                           *header_name_end;
    u_char                           *header_start;
    u_char                           *header_end;
    /*
     * a memory that can be reused after parsing a request line
     * via ngx_http_ephemeral_t
     */

    u_char                           *uri_start;
    u_char                           *uri_end;
    u_char                           *uri_ext;
    u_char                           *args_start;
    u_char                           *request_start;
    u_char                           *request_end;
    u_char                           *method_end;
    u_char                           *schema_start;
    u_char                           *schema_end;
    u_char                           *host_start;
    u_char                           *host_end;
    u_char                           *port_start;
    u_char                           *port_end;

    unsigned                          http_minor:16;
    unsigned                          http_major:16;
};


typedef struct {
    ngx_http_posted_request_t         terminal_posted_request;
#if (NGX_HAVE_AIO_SENDFILE)
    u_char                            aio_preload;
#endif
} ngx_http_ephemeral_t;


extern ngx_http_header_t       ngx_http_headers_in[];
extern ngx_http_header_out_t   ngx_http_headers_out[];


#endif /* _NGX_HTTP_REQUEST_H_INCLUDED_ */
