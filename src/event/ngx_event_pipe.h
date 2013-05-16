
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_EVENT_PIPE_H_INCLUDED_
#define _NGX_EVENT_PIPE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


typedef struct ngx_event_pipe_s  ngx_event_pipe_t;

typedef ngx_int_t (*ngx_event_pipe_input_filter_pt)(ngx_event_pipe_t *p,
                                                    ngx_buf_t *buf);
typedef ngx_int_t (*ngx_event_pipe_output_filter_pt)(void *data,
                                                     ngx_chain_t *chain);


struct ngx_event_pipe_s {
    ngx_connection_t  *upstream;//表示nginx和client，以及和后端的两条连接
    ngx_connection_t  *downstream;//这个表示客户端的连接

    ngx_chain_t       *free_raw_bufs;//保存了从upstream读取的数据(没有经过任何处理的)，以及缓存的buf.
    ngx_chain_t       *in;
    ngx_chain_t      **last_in;

    ngx_chain_t       *out;//buf到tempfile的数据会放到out中
    ngx_chain_t      **last_out;

    ngx_chain_t       *free;
    ngx_chain_t       *busy;

    /*
     * the input filter i.e. that moves HTTP/1.1 chunks
     * from the raw bufs to an incoming chain
     *///FCGI为ngx_http_fastcgi_input_filter，其他为ngx_event_pipe_copy_input_filter 。用来解析特定格式数据
    ngx_event_pipe_input_filter_pt    input_filter;
    void                             *input_ctx;

    ngx_event_pipe_output_filter_pt   output_filter;//ngx_http_output_filter输出filter
    void                             *output_ctx;

    unsigned           read:1;//标记是否读了数据。
    unsigned           cacheable:1;
    unsigned           single_buf:1;//如果使用了NGX_USE_AIO_EVENT异步IO标志，则设置为1
    unsigned           free_bufs:1;
    unsigned           upstream_done:1;
    unsigned           upstream_error:1;
    unsigned           upstream_eof:1;
    unsigned           upstream_blocked:1;
    unsigned           downstream_done:1;
    unsigned           downstream_error:1;
    unsigned           cyclic_temp_file:1;

    ngx_int_t          allocated;//表示已经分配了的bufs的个数，每次会++
    ngx_bufs_t         bufs;//fastcgi_buffers等指令设置的nginx用来缓存body的内存块数目以及大小。ngx_conf_set_bufs_slot函数会解析这样的配置。
    				//对应xxx_buffers,也就是读取后端的数据时的bufer大小以及个数
    ngx_buf_tag_t      tag;

    ssize_t            busy_size;

    off_t              read_length;

    off_t              max_temp_file_size;
    ssize_t            temp_file_write_size;

    ngx_msec_t         read_timeout;
    ngx_msec_t         send_timeout;
    ssize_t            send_lowat;

    ngx_pool_t        *pool;
    ngx_log_t         *log;

    ngx_chain_t       *preread_bufs;//指读取upstream的时候多读的，或者说预读的body部分数据。p->preread_bufs->buf = &u->buffer;
    size_t             preread_size;
    ngx_buf_t         *buf_to_file;

    ngx_temp_file_t   *temp_file;

    /* STUB */ int     num;
};


ngx_int_t ngx_event_pipe(ngx_event_pipe_t *p, ngx_int_t do_write);
ngx_int_t ngx_event_pipe_copy_input_filter(ngx_event_pipe_t *p, ngx_buf_t *buf);
ngx_int_t ngx_event_pipe_add_free_buf(ngx_event_pipe_t *p, ngx_buf_t *b);


#endif /* _NGX_EVENT_PIPE_H_INCLUDED_ */
