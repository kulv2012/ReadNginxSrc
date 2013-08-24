
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static void ngx_http_read_client_request_body_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_do_read_client_request_body(ngx_http_request_t *r);
static ngx_int_t ngx_http_write_request_body(ngx_http_request_t *r,
    ngx_chain_t *body);
static ngx_int_t ngx_http_read_discarded_request_body(ngx_http_request_t *r);
static ngx_int_t ngx_http_test_expect(ngx_http_request_t *r);


/*
 * on completion ngx_http_read_client_request_body() adds to
 * r->request_body->bufs one or two bufs:
 *    *) one memory buf that was preread in r->header_in;Èç¹ûÔÚ¶ÁÈ¡ÇëÇóÍ·µÄÊ±ºòÒÑ¾­¶ÁÈëÁËÒ»²¿·ÖÊý¾Ý£¬Ôò·ÅÈëÕâÀï
 *    *) one memory or file buf that contains the rest of the body Ã»ÓÐÔ¤¶ÁµÄÊý¾Ý²¿·Ö·ÅÈëÕâÀï
 */
ngx_int_t ngx_http_read_client_request_body(ngx_http_request_t *r, ngx_http_client_body_handler_pt post_handler)
{//post_handler = ngx_http_upstream_init¡£NGINX»áµÈµ½ÇëÇóµÄBODYÈ«²¿¶ÁÈ¡Íê±Ïºó²Å½øÐÐupstreamµÄ³õÊ¼»¯£¬GOOD
    size_t                     preread;
    ssize_t                    size;
    ngx_buf_t                 *b;
    ngx_chain_t               *cl, **next;
    ngx_temp_file_t           *tf;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    r->main->count++;
    if (r->request_body || r->discard_body) {//discard_bodyÊÇ·ñÐèÒª¶ªÆúÇëÇóÄÚÈÝ²¿·Ö¡£»òÕßÒÑ¾­ÓÐÇëÇóÌåÁË¡£ÔòÖ±½Ó»Øµ÷
        post_handler(r);//²»ÐèÒªÇëÇóÌå£¬Ö±½Óµ÷ÓÃngx_http_upstream_init
        return NGX_OK;
    }

    if (ngx_http_test_expect(r) != NGX_OK) {//¼ì²éÊÇ·ñÐèÒª·¢ËÍHTTP/1.1 100 Continue
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    rb = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));
    if (rb == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    r->request_body = rb;//·ÖÅäÇëÇóÌå½á¹¹£¬ÏÂÃæ½øÐÐ°´ÐèÌî³ä¡£
    if (r->headers_in.content_length_n < 0) {
        post_handler(r);//Èç¹û²»ÐèÒª¶ÁÈ¡body²¿·Ö£¬³¤¶ÈÐ¡ÓÚ0
        return NGX_OK;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    if (r->headers_in.content_length_n == 0) {//Èç¹ûÃ»ÓÐÉèÖÃcontent_length_n
        if (r->request_body_in_file_only) {//client_body_in_file_onlyÕâ¸öÖ¸ÁîÊ¼ÖÕ´æ´¢Ò»¸öÁ¬½ÓÇëÇóÊµÌåµ½Ò»¸öÎÄ¼þ¼´Ê¹ËüÖ»ÓÐ0×Ö½Ú¡£
            tf = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
            if (tf == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            tf->file.fd = NGX_INVALID_FILE;
            tf->file.log = r->connection->log;
            tf->path = clcf->client_body_temp_path;
            tf->pool = r->pool;
            tf->warn = "a client request body is buffered to a temporary file";
            tf->log_level = r->request_body_file_log_level;
            tf->persistent = r->request_body_in_persistent_file;
            tf->clean = r->request_body_in_clean_file;
            if (r->request_body_file_group_access) {
                tf->access = 0660;
            }
            rb->temp_file = tf;//´´½¨Ò»¸öÁÙÊ±ÎÄ¼þÓÃÀ´´æ´¢POST¹ýÀ´µÄbody¡£ËäÈ»Õâ¸öÖ»ÓÐ0×Ö½Ú£¬É¶¶«Î÷¶¼Ã»ÓÐ¡£
            if (ngx_create_temp_file(&tf->file, tf->path, tf->pool, tf->persistent, tf->clean, tf->access) != NGX_OK) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
        }
		//ÓÉÓÚÊµ¼ÊµÄcontent_length_n³¤¶ÈÎª0£¬Ò²¾Í²»ÐèÒª½øÐÐ¶ÁÈ¡ÁË¡£Ö±½Óµ½init
        post_handler(r);//Ò»°ãGETÇëÇóÖ±½Óµ½ÕâÀïÁË
        return NGX_OK;
    }
	//ºÃ°É£¬Õâ»Øcontent_length_n´óÓÚ0 ÁË£¬Ò²¾ÍÊÇ¸öPOSTÇëÇó¡£ÕâÀïÏÈ¼ÇÂ¼Ò»ÏÂ£¬´ý»áPOSTÊý¾Ý¶ÁÈ¡Íê±Ïºó£¬ÐèÒªµ÷ÓÃµ½Õâ¸öngx_http_upstream_init
    rb->post_handler = post_handler;
    /*
     * set by ngx_pcalloc():
     *     rb->bufs = NULL;
     *     rb->buf = NULL;
     *     rb->rest = 0;
     */
    preread = r->header_in->last - r->header_in->pos;//Ê¹ÓÃÖ®Ç°¶ÁÈëµÄÊ£ÓàÊý¾Ý£¬Èç¹ûÖ®Ç°Ô¤¶ÁÁËÊý¾ÝµÄ»°¡£
    if (preread) {//Èç¹ûÖ®Ç°Ô¤¶ÁÁË¶àÓàµÄÇëÇóÌå
        /* there is the pre-read part of the request body */
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http client request body preread %uz", preread);

        b = ngx_calloc_buf(r->pool);//·ÖÅängx_buf_t½á¹¹£¬ÓÃÓÚ´æ´¢Ô¤¶ÁµÄÊý¾Ý
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        b->temporary = 1;
        b->start = r->header_in->pos;//Ö±½ÓÖ¸ÏòÒÑ¾­Ô¤¶ÁµÄÊý¾ÝµÄ¿ªÍ·¡£Õâ¸öPOSÒÑ¾­ÔÚÍâÃæ¾ÍÉèÖÃºÃÁËµÄ¡£¶ÁÈ¡ÇëÇóÍ·£¬HEADERSºó¾ÍÒÆÎ»ÁË¡£
        b->pos = r->header_in->pos;
        b->last = r->header_in->last;
        b->end = r->header_in->end;

        rb->bufs = ngx_alloc_chain_link(r->pool);//ÉêÇëÒ»¸öbufÁ´½Ó±í¡£ÓÃÀ´´æ´¢2¸öBODY²¿·Ö
        if (rb->bufs == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        rb->bufs->buf = b;//Ô¤¶ÁµÄBODY²¿·Ö·ÅÔÚÕâÀï
        rb->bufs->next = NULL;//ÆäÓà²¿·Ö´ý»á¶ÁÈ¡µÄÊ±ºò·ÅÔÚÕâÀï
        rb->buf = b;//ngx_http_request_body_t µÄbufÖ¸ÏòÕâ¿éÐÂµÄbuf

        if ((off_t) preread >= r->headers_in.content_length_n) {//OK£¬ÎÒÒÑ¾­¶ÁÁË×ã¹»µÄBODYÁË£¬¿ÉÒÔÏëµ½£¬ÏÂÃæ¿ÉÒÔÖ±½ÓÈ¥ngx_http_upstream_initÁË
            /* the whole request body was pre-read */
            r->header_in->pos += (size_t) r->headers_in.content_length_n;
            r->request_length += r->headers_in.content_length_n;//Í³¼ÆÇëÇóµÄ×Ü³¤¶È

            if (r->request_body_in_file_only) {//Èç¹ûÐèÒª¼ÇÂ¼µ½ÎÄ¼þ£¬ÔòÐ´ÈëÎÄ¼þ
                if (ngx_http_write_request_body(r, rb->bufs) != NGX_OK) {
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
            }
            post_handler(r);//½øÐÐ´¦Àí
            return NGX_OK;
        }
		//Èç¹ûÔ¤¶ÁµÄÊý¾Ý»¹²»¹»£¬»¹ÓÐ²¿·ÖÊý¾ÝÃ»ÓÐ¶ÁÈë½øÀ´¡£
        /*
         * to not consider the body as pipelined request in
         * ngx_http_set_keepalive()
         */
        r->header_in->pos = r->header_in->last;//ºóÒÆ£¬´ý»á´ÓÕâÀïÐ´Èë£¬Ò²¾ÍÊÇ×·¼Ó°É
        r->request_length += preread;//Í³¼Æ×Ü³¤¶È

        rb->rest = r->headers_in.content_length_n - preread;//¼ÆËã»¹ÓÐ¶àÉÙÊý¾ÝÐèÒª¶ÁÈ¡£¬¼õÈ¥¸Õ²ÅÔ¤¶ÁµÄ²¿·Ö¡£
        if (rb->rest <= (off_t) (b->end - b->last)) {//Èç¹û»¹Òª¶ÁÈ¡µÄÊý¾Ý´óÐ¡×ã¹»ÈÝÄÉµ½ÏÖÔÚµÄÔ¤¶ÁBUFFERÀïÃæ£¬ÄÇ¾Í¸É´à·ÅÈëÆäÖÐ°É¡£
            /* the whole request body may be placed in r->header_in */
            rb->to_write = rb->bufs;//¿ÉÒÔÐ´ÈëµÚÒ»¸öÎ»ÖÃrb->bufs->buf = b;
            r->read_event_handler = ngx_http_read_client_request_body_handler;//ÉèÖÃÎª¶ÁÈ¡¿Í»§¶ËµÄÇëÇóÌå
            return ngx_http_do_read_client_request_body(r);//¹û¶ÏµÄÈ¥¿ªÊ¼¶ÁÈ¡Ê£ÓàÊý¾ÝÁË
        }
		//Èç¹ûÔ¤¶ÁµÄBUFFERÈÝ²»ÏÂËùÓÐµÄ¡£ÄÇ¾ÍÐèÒª·ÖÅäÒ»¸öÐÂµÄÁË¡£
        next = &rb->bufs->next;//ÉèÖÃÒª¶ÁÈ¡µÄÊý¾ÝÎªµÚ¶þ¸öbuf

    } else {
        b = NULL;//Ã»ÓÐÔ¤¶ÁÊý¾Ý
        rb->rest = r->headers_in.content_length_n;//ÉèÖÃËùÐè¶ÁÈ¡µÄÊý¾ÝÎªËùÓÐµÄ¡£
        next = &rb->bufs;//È»ºóÉèÖÃÒª¶ÁÈ¡µÄÊý¾ÝËù´æ·ÅµÄÎ»ÖÃÎªbufsµÄ¿ªÍ·
    }

    size = clcf->client_body_buffer_size;//ÅäÖÃµÄ×î´ó»º³åÇø´óÐ¡¡£
    size += size >> 2;//ÉèÖÃ´óÐ¡Îªsize + 1/4*size,Ê£ÓàµÄÄÚÈÝ²»³¬¹ý»º³åÇø´óÐ¡µÄ1.25±¶£¬Ò»´Î¶ÁÍê£¨1.25¿ÉÄÜÊÇ¾­ÑéÖµ°É£©£¬·ñÔò£¬°´»º³åÇø´óÐ¡¶ÁÈ¡¡£

    if (rb->rest < size) {//Èç¹ûÊ£ÏÂµÄ±È1.25±¶×î´ó»º³åÇø´óÐ¡ÒªÐ¡µÄ»°
        size = (ssize_t) rb->rest;//¼ÇÂ¼ËùÐèÊ£Óà¶ÁÈë×Ö½ÚÊý
        if (r->request_body_in_single_buf) {//Èç¹ûÖ¸¶¨Ö»ÓÃÒ»¸öbufferÔòÒª¼ÓÉÏÔ¤¶ÁµÄ¡£
            size += preread;
        }

    } else {//Èç¹û1.25±¶×î´ó»º³åÇø´óÐ¡²»×ãÒÔÈÝÄÉPOSTÊý¾Ý£¬ÄÇÎÒÃÇÒ²Ö»¶ÁÈ¡×î´óPOSTÊý¾ÝÁË¡£
        size = clcf->client_body_buffer_size;
        /* disable copying buffer for r->request_body_in_single_buf */
        b = NULL;
    }

    rb->buf = ngx_create_temp_buf(r->pool, size);//·ÖÅäÕâÃ´¶àÁÙÊ±ÄÚ´æ
    if (rb->buf == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    cl = ngx_alloc_chain_link(r->pool);//·ÖÅäÒ»¸öÁ´½Ó±í
    if (cl == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    cl->buf = rb->buf;//¼ÇÂ¼Ò»ÏÂ¸Õ²ÅÉêÇëµÄÄÚ´æ£¬´ý»áÊý¾Ý¾Í´æ·ÅÔÚÕâÁË¡£
    cl->next = NULL;//Ã»ÓÐÏÂÒ»¸öÁË¡£
    if (b && r->request_body_in_single_buf) {//Èç¹ûÖ¸¶¨Ö»ÓÃÒ»¸öbufferÔòÒª¼ÓÉÏÔ¤¶ÁµÄ,ÄÇ¾ÍÐèÒª°ÑÖ®Ç°µÄÊý¾Ý¿½±´¹ýÀ´
        size = b->last - b->pos;
        ngx_memcpy(rb->buf->pos, b->pos, size);
        rb->buf->last += size;
        next = &rb->bufs;//´ý»áÁ´½ÓÔÚÍ·²¿¡£
    }

    *next = cl;//GOOD£¬Á´½ÓÆðÀ´¡£Èç¹ûÓÐÔ¤¶ÁÊý¾Ý£¬ÇÒ¿ÉÒÔ·Å¶à¸öbuffer,¾ÍÁ´½ÓÔÚµÚ¶þ¸öÎ»ÖÃ£¬·ñÔòÁ´½ÓÔÚµÚÒ»¸öÎ»ÖÃ¡£
    if (r->request_body_in_file_only || r->request_body_in_single_buf) {
        rb->to_write = rb->bufs;//ÉèÖÃÒ»ÏÂ´ý»áÐèÒªÐ´ÈëµÄÎ»ÖÃ¡£Èç¹ûÒ»¸öbuffer£¬¾ÍÍ·²¿

    } else {//·ñÔòÈç¹ûÒÑ¾­ÉèÖÃÁËµÚ¶þ¸öÎ»ÖÃ£¬Ò²¾ÍÊÇÓÐÔ¤¶ÁÊý¾ÝÇÒÓÐ2·ÝBUFFER£¬ÄÇ¾Í´æÔÚµÚ¶þ¸öÀïÃæ£¬·ñÔòÍ·²¿¡£
        rb->to_write = rb->bufs->next ? rb->bufs->next : rb->bufs;
    }
    r->read_event_handler = ngx_http_read_client_request_body_handler;//ÉèÖÃÎª¶ÁÈ¡¿Í»§¶ËµÄÇëÇóÌå¶ÁÈ¡º¯Êý£¬ÆäÊµ¾ÍµÈÓÚÏÂÃæµÄ£¬Ö»ÊÇ½øÐÐÁË³¬Ê±ÅÐ¶Ï

    return ngx_http_do_read_client_request_body(r);//¹û¶ÏµÄÈ¥¿ªÊ¼¶ÁÈ¡Ê£ÓàÊý¾ÝÁË
}


static void
ngx_http_read_client_request_body_handler(ngx_http_request_t *r)
{
    ngx_int_t  rc;
    if (r->connection->read->timedout) {//ÀÏ¹æ¾Ø£¬³¬Ê±ÅÐ¶Ï¡£
        r->connection->timedout = 1;
        ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }
    rc = ngx_http_do_read_client_request_body(r);//ÕæÕýÈ¥¶ÁÈ¡Êý¾ÝÁË
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        ngx_http_finalize_request(r, rc);
    }
}


static ngx_int_t
ngx_http_do_read_client_request_body(ngx_http_request_t *r)
{//¿ªÊ¼¶ÁÈ¡Ê£ÓàµÄPOSTÊý¾Ý£¬´æ·ÅÔÚr->request_bodyÀïÃæ£¬Èç¹û¶ÁÍêÁË£¬»Øµ÷post_handler£¬ÆäÊµ¾ÍÊÇngx_http_upstream_init¡£
    size_t                     size;
    ssize_t                    n;
    ngx_buf_t                 *b;
    ngx_connection_t          *c;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;//ÄÃµ½Õâ¸öÁ¬½ÓµÄngx_connection_t½á¹¹
    rb = r->request_body;//ÄÃµ½Êý¾Ý´æ·ÅÎ»ÖÃ
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,"http read client request body");
    for ( ;; ) {
        for ( ;; ) {
            if (rb->buf->last == rb->buf->end) {//Èç¹ûÊý¾Ý»º³åÇø²»¹»ÁË¡£
                if (ngx_http_write_request_body(r, rb->to_write) != NGX_OK) {//²»ÐÐÁË£¬µØ·½²»¹»£¬¹û¶ÏÐ´µ½ÎÄ¼þÀïÃæÈ¥°É¡£
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }
                rb->to_write = rb->bufs->next ? rb->bufs->next : rb->bufs;//Èç¹û»¹ÓÐµÚ¶þ¸ö»º³åÇø£¬ÔòÐ´ÈëµÚ¶þ¸ö£¬·ñÔòÐ´µÚÒ»¸ö¡£
                rb->buf->last = rb->buf->start;
            }
            size = rb->buf->end - rb->buf->last;//¼ÆËãÕâ¸ö»º³åÇøµÄÊ£Óà´óÐ¡
            if ((off_t) size > rb->rest) {//²Á£¬¹»ÁË£¬size¾ÍµÈÓÚÎÒÒª¶ÁÈ¡µÄ´óÐ¡¡£·ñÔòµÄ»°¾Í¶ÁÊ£ÓàµÄÈÝÁ¿´óÐ¡¡£
                size = (size_t) rb->rest;
            }
            n = c->recv(c, rb->buf->last, size);//Ê¹¾¢¶ÁÊý¾Ý¡£µÈÓÚngx_unix_recv£¬¾Í¹â¶ÁÊý¾Ý£¬²»¸Ä±äepollÊôÐÔ¡£
            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http client request body recv %z", n);

            if (n == NGX_AGAIN) {//Ã»ÓÐÁË
                break;
            }
            if (n == 0) {//·µ»Ø0±íÊ¾¿Í»§¶Ë¹Ø±ÕÁ¬½ÓÁË
                ngx_log_error(NGX_LOG_INFO, c->log, 0,"client closed prematurely connection");
            }
            if (n == 0 || n == NGX_ERROR) {
                c->error = 1;//±ê¼ÇÎª´íÎó¡£ÕâÑùÍâ²¿»á¹Ø±ÕÁ¬½ÓµÄ¡£
                return NGX_HTTP_BAD_REQUEST;
            }
            rb->buf->last += n;//¶ÁÁËn×Ö½Ú¡£
            rb->rest -= n;//»¹ÓÐÕâÃ´¶à×Ö½ÚÒª¶ÁÈ¡
            r->request_length += n;//Í³¼Æ×Ü´«Êä×Ö½ÚÊý
            if (rb->rest == 0) {
                break;
            }

            if (rb->buf->last < rb->buf->end) {
                break;//Õâ¸ö¿Ï¶¨ÁË°É¡£
            }
        }

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0, "http client request body rest %O", rb->rest);
        if (rb->rest == 0) {
            break;//²»Ê£ÏÂÁË£¬È«¶ÁÍêÁË¡£
        }

        if (!c->read->ready) {//Èç¹ûÕâ¸öÁ¬½ÓÔÚngx_unix_recvÀïÃæ±ê¼ÇÎªÃ»ÓÐ×ã¹»Êý¾Ý¿ÉÒÔ¶ÁÈ¡ÁË£¬ÄÇÎÒÃÇ¾ÍÐèÒª¼ÓÈëepool¿É¶Á¼àÌýÊÂ¼þÀïÃæÈ¥¡£
            clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
            ngx_add_timer(c->read, clcf->client_body_timeout);//Éè¸ö³¬Ê±°É¡£
//½«Á¬½ÓÉèÖÃµ½¿É¶ÁÊÂ¼þ¼à¿ØÖÐ£¬ÓÐ¿É¶ÁÊÂ¼þ¾Í»áµ÷ÓÃngx_http_request_handler->r->read_event_handler = ngx_http_read_client_request_body_handler; 
            if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }
            return NGX_AGAIN;//·µ»Ø
        }
    }
//Èç¹ûÈ«²¿¶ÁÍêÁË£¬ÄÇ¾Í»áµ½ÕâÀïÀ´¡£
    if (c->read->timer_set) {
        ngx_del_timer(c->read);
    }
    if (rb->temp_file || r->request_body_in_file_only) {//¸ÄÐ´ÎÄ¼þµÄÐ´ÎÄ¼þ
        /* save the last part */
        if (ngx_http_write_request_body(r, rb->to_write) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        b = ngx_calloc_buf(r->pool);
        if (b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
        b->in_file = 1;
        b->file_pos = 0;
        b->file_last = rb->temp_file->file.offset;
        b->file = &rb->temp_file->file;
        if (rb->bufs->next) {//ÓÐµÚ¶þ¸ö¾Í·ÅµÚ¶þ¸ö
            rb->bufs->next->buf = b;

        } else {
            rb->bufs->buf = b;//·ñÔò·ÅµÚÒ»¸ö»º³åÇø
        }
    }

    if (r->request_body_in_file_only && rb->bufs->next) {//Èç¹ûPOSTÊý¾Ý±ØÐë´æ·ÅÔÚÎÄ¼þÖÐ£¬²¢ÇÒÓÐ2¸ö»º³åÇø£¬ÏÂÃæÊÇÉ¶ÒâË¼?
        rb->bufs = rb->bufs->next;//Ö¸ÏòµÚ¶þ¸ö»º³åÇø£¬ÆäÊµ¾ÍÊÇÉÏÃæµÄÎÄ¼þ¡£Ò²¾ÍÊÇbufsÖ¸ÏòÎÄ¼þ
    }

    rb->post_handler(r);

    return NGX_OK;
}


static ngx_int_t
ngx_http_write_request_body(ngx_http_request_t *r, ngx_chain_t *body)
{
    ssize_t                    n;
    ngx_temp_file_t           *tf;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    rb = r->request_body;

    if (rb->temp_file == NULL) {
        tf = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
        if (tf == NULL) {
            return NGX_ERROR;
        }

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        tf->file.fd = NGX_INVALID_FILE;
        tf->file.log = r->connection->log;
        tf->path = clcf->client_body_temp_path;
        tf->pool = r->pool;
        tf->warn = "a client request body is buffered to a temporary file";
        tf->log_level = r->request_body_file_log_level;
        tf->persistent = r->request_body_in_persistent_file;
        tf->clean = r->request_body_in_clean_file;

        if (r->request_body_file_group_access) {
            tf->access = 0660;
        }

        rb->temp_file = tf;
    }

    n = ngx_write_chain_to_temp_file(rb->temp_file, body);
    /* TODO: n == 0 or not complete and level event */
    if (n == NGX_ERROR) {
        return NGX_ERROR;
    }

    rb->temp_file->offset += n;

    return NGX_OK;
}


ngx_int_t
ngx_http_discard_request_body(ngx_http_request_t *r)
{//É¾³ý¿Í»§¶ËÁ¬½Ó¶ÁÊÂ¼þ£¬Èç¹û¿ÉÒÔ£¬¶ÁÈ¡¿Í»§¶ËBODY£¬È»ºó¶ªµô¡£Èç¹û¶ÁÍêÕû¸öBODYÁË£¬lingering_close=0.
    ssize_t       size;
    ngx_event_t  *rev;

    if (r != r->main || r->discard_body) {
        return NGX_OK;//Èç¹û²»ÊÇÖ÷ÇëÇó£¬»òÕßÒÑ¾­¶ª¹ýBODYÁË£¬Ö±½Ó·µ»Ø
    }
	//Èç¹ûÐèÒª½øÐÐHTTP 1.1µÄ 100-continueµÄ·´À¡£¬Ôòµ÷ÓÃngx_unix_send·¢ËÍ·´À¡»ØÈ¥
    if (ngx_http_test_expect(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rev = r->connection->read;//µÃµ½Á¬½ÓµÄ¶ÁÊÂ¼þ½á¹¹
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0, "http set discard body");
    if (rev->timer_set) {//ÓÉÓÚ²»ÐèÒª¿Í»§¶Ë·¢ËÍµÄbodyÁË£¬Òò´ËÉ¾³ý¶Á³¬Ê±¶¨Ê±Æ÷£¬²»care¶ÁÈ¡ÊÂ¼þÁË¡£
        ngx_del_timer(rev);
    }

    if (r->headers_in.content_length_n <= 0 || r->request_body) {
        return NGX_OK;//Èç¹ûÇëÇóÌå³¤¶ÈÎª0£¬¶ªÆú£¬ºóÃæµÄrequest_bodyÊÇËµÈç¹ûÇëÇóÊý¾ÝÒÑ¾­¶ÁÈ¡ÁË£¬ÄÇÒ²ËãÁË¡£²»¶ªÆúÁËÂð å
    }

    size = r->header_in->last - r->header_in->pos;//ÕâºóÃæµÄÊý¾Ý¿Ï¶¨ÊÇbodyÁË¡£
    if (size) {//ÒÑ¾­²»Ð¡ÐÄÔ¤¶ÁÁËÒ»Ð©Êý¾Ý£¬ÄÇÃ´Èç¹ûÔ¤¶ÁµÄÊý¾Ý»¹²»ÊÇiÈ«²¿µÄbody£¬ÄÇ¾ÍÒÆ¶¯Ò»ÏÂposÒÔ¼°¼õÉÙÒ»ÏÂcontent_length_n£¬
    //¾ÍÆ­ËûËµÎÒµÄcontent_length_nÃ»ÄÇÃ´¶à£¬Êµ¼ÊÉÏÊÇÒÑ¾­¶ÁÈ¡ÁË¡£
        if (r->headers_in.content_length_n > size) {
            r->header_in->pos += size;
            r->headers_in.content_length_n -= size;
        } else {//·ñÔòbodyÒÑ¾­¶ÁÍêÁË£¬Ô¤¶ÁµÄÊý¾Ý±Ècontent_length_n¶¼´óÁË£¬ÄÇ¿Ï¶¨¶ÁÍêÁË£¬
        //ÄÇ¾ÍÒÆ¶¯pos,È»ºó½«content_length_nÉèÖÃÎª0£¬Æ­ËûËµÃ»Êý¾Ý¶ÁÁË£¬¿Í»§¶ËÃ»·¢ËÍÊý¾Ý
            r->header_in->pos += (size_t) r->headers_in.content_length_n;
            r->headers_in.content_length_n = 0;
            return NGX_OK;
        }
    }
	//ÉèÖÃÕâ¸ö¿Í»§¶ËÁ¬½ÓµÄ¶ÁÈ¡ÊÂ¼þ¾ä±úÉèÖÃÎªÈçÏÂ¡£
    r->read_event_handler = ngx_http_discarded_request_body_handler;
	//ÏÂÃæ²ÎÊýÎª0£¬É¶¶¼Ã»¸É
    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
	//Õâ¸öº¯Êý²»¶Ï¶ÁÈ¡¿Í»§¶ËÁ¬½ÓµÄÊý¾Ý£¬È»ºó¶ªµô¡£Èç¹û·µ»ØNGX_OK£¬
	//±íÊ¾Õâ¸öÁ¬½Ó·¢ËÍµÄÊý¾Ýµ½Í·ÁË£¬±ÈÈç·¢ËÍÁËFIN°ü£¬»òÕßÃ»ÓÐBODYÁË¡£ÄÇ¾ÍÉèÖÃÒ»ÏÂ
    if (ngx_http_read_discarded_request_body(r) == NGX_OK) {
//±ÜÃâÒ»¸öÎÊÌâ: Èç¹û¿Í»§¶ËÕýÔÚ·¢ËÍÊý¾Ý£¬»òÊý¾Ý»¹Ã»ÓÐµ½´ï·þÎñ¶Ë£¬·þÎñ¶Ë¾Í½«Á¬½Ó¹ØµôÁË¡£ÄÇÃ´£¬¿Í»§¶Ë·¢ËÍµÄÊý¾Ý»áÊÕµ½RST°ü,²»ÓÑºÃ¡£
//ÓÉÓÚÃ÷ÏÔÖªµÀ¶Ô·½²»»á·¢ËÍÊý¾ÝÁË£¬ÄÇÃ´Çå³þÕâ¸ö±êÖ¾°É¡£Ö±½Ó¹Ø±Õ¿Í»§¶ËµÄÁ¬½Ó¡£ÕâÑùÔÚngx_http_finalize_connectionÀïÃæ¾Í²»ÓÃÑÓ³Ù¹Ø±ÕÁË¡£
        r->lingering_close = 0;

    } else {
        r->count++;
        r->discard_body = 1;//±¾´Î»¹Ã»ÓÐ¶ÁÍêÕû¸öbody£¬ÉèÖÃÕâ¸ö±êÖ¾ºó£¬±¾º¯ÊýÏÂ´Î½øÀ´¾Í»áÔÚ¿ªÍ·Ö±½Ó·µ»Ø¡£ÒòÎªÒÑ¾­ÉèÖÃ¹ýÏà¹ØµÄÊý¾ÝÁË¡£
    }

    return NGX_OK;
}


void
ngx_http_discarded_request_body_handler(ngx_http_request_t *r)
{//¸ºÔð¶ªÆú¿Í»§¶ËµÄÁ³¼ÕÊý¾Ý
    ngx_int_t                  rc;
    ngx_msec_t                 timer;
    ngx_event_t               *rev;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    c = r->connection;
    rev = c->read;

    if (rev->timedout) {
        c->timedout = 1;
        c->error = 1;
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (r->lingering_time) {
        timer = (ngx_msec_t) (r->lingering_time - ngx_time());

        if (timer <= 0) {
            r->discard_body = 0;
            r->lingering_close = 0;
            ngx_http_finalize_request(r, NGX_ERROR);
            return;
        }

    } else {
        timer = 0;
    }

    rc = ngx_http_read_discarded_request_body(r);

    if (rc == NGX_OK) {
        r->discard_body = 0;
        r->lingering_close = 0;
        ngx_http_finalize_request(r, NGX_DONE);
        return;
    }

    /* rc == NGX_AGAIN */

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        c->error = 1;
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    if (timer) {

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        timer *= 1000;

        if (timer > clcf->lingering_timeout) {
            timer = clcf->lingering_timeout;
        }

        ngx_add_timer(rev, timer);
    }
}


static ngx_int_t
ngx_http_read_discarded_request_body(ngx_http_request_t *r)
{//Õâ¸öº¯Êý²»¶Ï¶ÁÈ¡¿Í»§¶ËÁ¬½ÓµÄÊý¾Ý£¬È»ºó¶ªµô£¬ÄÇÎÊÌâÀ´ÁË: ÎªÉ¶²»¸É´à²»¶ÁÈ¡ÄØ£¬±ðÀíËü¾ÍÐÐÁË¡£
//²»ÐÐ£¬Èç¹û²»¶ÁÈ¡£¬ÄÇ¾ÍholdÔÚTCPÐ­ÒéÕ»ÀïÃæ£¬ÄÇ¶ÔÏµÍ³ÄÚ´æÓÐÑ¹Á¦£¬
//×îÖØÒªµÄÊÇ: ¿Í»§¶Ë½«ÎÞ·¨·¢ËÍÊý¾Ý£¬ÒòÎªÎÒÃÇµÄÓµÈû´°¿ÚwindowÎª0ÁË
    size_t   size;
    ssize_t  n;
    u_char   buffer[NGX_HTTP_DISCARD_BUFFER_SIZE];

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,  "http read discarded body");
    for ( ;; ) {
        if (r->headers_in.content_length_n == 0) {
			//ÏÂÃæµÄ¾ä±úÉ¾³ýÁ¬½ÓµÄ¶ÁÊÂ¼þ×¢²á£¬²»¹Ø×¢¶ÁÊÂ¼þÁË¡£
            r->read_event_handler = ngx_http_block_reading;
            return NGX_OK;
        }

        if (!r->connection->read->ready) {
            return NGX_AGAIN;
        }
//È¡¸ö×îÐ¡£¬×î´ó4096´óÐ¡µÄ¿éÒ»´Î´ÎµÄ¶ÁÈ¡£¬È»ºóÎÞÇéµÄ¶ªµô
        size = (r->headers_in.content_length_n > NGX_HTTP_DISCARD_BUFFER_SIZE) ? NGX_HTTP_DISCARD_BUFFER_SIZE:(size_t) r->headers_in.content_length_n;
        n = r->connection->recv(r->connection, buffer, size);
        if (n == NGX_ERROR) {
            r->connection->error = 1;
            return NGX_OK;
        }
        if (n == NGX_AGAIN) {
            return NGX_AGAIN;
        }
        if (n == 0) {
            return NGX_OK;
        }
		//¼õÉÙcontent_length_n´óÐ¡£¬ÕâÑù¾ÍÒÔÎªÃ»ÄÇÃ´¶àbody£¬Êµ¼ÊÉÏÊÇ¶ÁÈ¡µ½ÁË¡£
        r->headers_in.content_length_n -= n;
    }
}


static ngx_int_t
ngx_http_test_expect(ngx_http_request_t *r)
{//Èç¹ûÐèÒª½øÐÐ100-continueµÄ·´À¡£¬Ôòµ÷ÓÃngx_unix_send·¢ËÍ·´À¡»ØÈ¥
    ngx_int_t   n;
    ngx_str_t  *expect;

    if (r->expect_tested
        || r->headers_in.expect == NULL
        || r->http_version < NGX_HTTP_VERSION_11)
    {
        return NGX_OK;
    }
    r->expect_tested = 1;
    expect = &r->headers_in.expect->value;
    if (expect->len != sizeof("100-continue") - 1 || ngx_strncasecmp(expect->data, (u_char *) "100-continue", sizeof("100-continue") - 1) != 0)
    {
        return NGX_OK;
    }
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "send 100 Continue");
    n = r->connection->send(r->connection, (u_char *) "HTTP/1.1 100 Continue" CRLF CRLF, sizeof("HTTP/1.1 100 Continue" CRLF CRLF) - 1);

    if (n == sizeof("HTTP/1.1 100 Continue" CRLF CRLF) - 1) {
        return NGX_OK;
    }
    /* we assume that such small packet should be send successfully */
    return NGX_ERROR;
}
