
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


//ÒÔÏÂÊı¾İ½á¹¹²Î¿¼http://cjhust.blog.163.com/blog/static/17582715720111017114121678/
typedef struct {//ÓÃÀ´´æ·ÅÃ¿¸ö¿Í»§¶Ë½ÚµãµÄÏà¹ØĞÅÏ¢¡£
    u_char                       color;
    u_char                       dummy;
    u_short                      len;//data×Ö·û´®µÄ³¤¶È¡£
    ngx_queue_t                  queue;
    ngx_msec_t                   last;//Õâ¸ö½ÚµãÊ²Ã´Ê±ºò¼ÓÈëµÄ¡£
    /* integer value, 1 corresponds to 0.001 r/s */
    ngx_uint_t                   excess;//³¬¹ı£¬³¬¶îÁ¿£¬¶àÓàÁ¿¡£ 1µÈ¼ÛÓÚ0.001 r/s
    u_char                       data[1];
} ngx_http_limit_req_node_t;


typedef struct {//¹ÜÀí¿Í»§¶Ë½ÚµãµÄĞÅÏ¢¡£Õâ¸öÊÇÓÉ¹²ÏíÄÚ´æ¿éµÄdataÖ¸ÏòµÄ£¬¿´ÆğÃû×Ö¾ÍÖªµÀÁË¡£ngx_shm_zone_t->data
    ngx_rbtree_t                  rbtree;//ºìºÚÊ÷µÄ¸ù¡£
    ngx_rbtree_node_t             sentinel;
    ngx_queue_t                   queue;//ÓÃÀ´¼ÇÂ¼²»Í¬Ê±¼ä¶Î½øÀ´µÄÇëÇó£¬¶ÓÁĞ²Ù×÷£¬Ç°Ãæ½ø£¬ºóÃæ³ö¡£
} ngx_http_limit_req_shctx_t;


typedef struct {//¸Ã½á¹¹ÓÃÓÚ´æ·Ålimit_req_zoneÖ¸ÁîµÄÏà¹ØĞÅÏ¢
    ngx_http_limit_req_shctx_t  *sh;//Ö¸ÏòÉÏÃæµÄºìºÚÊ÷£¬¶ÓÁĞ½á¹¹
    ngx_slab_pool_t             *shpool;//Ö¸Ïòslab³Ø¡£Êµ¼ÊÎª(ngx_slab_pool_t *) shm_zone->shm.addr;
    /* integer value, 1 corresponds to 0.001 r/s */
    ngx_uint_t                   rate;//Ã¿ÃëµÄÇëÇóÁ¿*1000¡£
    ngx_int_t                    index;//±äÁ¿ÔÚ cmcf->variables.neltsÖĞµÄÏÂ±ê¡£
    ngx_str_t                    var;//value[i];//¼Ç×¡±äÁ¿µÄÃû×Ö¡£ ´æ·Å¾ßÌåµÄ$binary_remote_addr
} ngx_http_limit_req_ctx_t;


typedef struct {//¸Ã½á¹¹ÓÃÓÚ´æ·Ålimit_reqÖ¸ÁîµÄÏà¹ØĞÅÏ¢
    ngx_shm_zone_t              *shm_zone;//ngx_shared_memory_add·µ»ØµÄÖ¸Õë
    /* integer value, 1 corresponds to 0.001 r/s */
    ngx_uint_t                   burst;//Ã¿Ãë÷Ş·¢ÊıÄ¿*1000.
    ngx_uint_t                   limit_log_level;
    ngx_uint_t                   delay_log_level;

    ngx_uint_t                   nodelay; /* unsigned  nodelay:1 */
} ngx_http_limit_req_conf_t;


static void ngx_http_limit_req_delay(ngx_http_request_t *r);
static ngx_int_t ngx_http_limit_req_lookup(ngx_http_limit_req_conf_t *lrcf,
    ngx_uint_t hash, u_char *data, size_t len, ngx_uint_t *ep);
static void ngx_http_limit_req_expire(ngx_http_limit_req_ctx_t *ctx,
    ngx_uint_t n);

static void *ngx_http_limit_req_create_conf(ngx_conf_t *cf);
static char *ngx_http_limit_req_merge_conf(ngx_conf_t *cf, void *parent,
    void *child);
static char *ngx_http_limit_req_zone(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_limit_req(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static ngx_int_t ngx_http_limit_req_init(ngx_conf_t *cf);


static ngx_conf_enum_t  ngx_http_limit_req_log_levels[] = {
    { ngx_string("info"), NGX_LOG_INFO },
    { ngx_string("notice"), NGX_LOG_NOTICE },
    { ngx_string("warn"), NGX_LOG_WARN },
    { ngx_string("error"), NGX_LOG_ERR },
    { ngx_null_string, 0 }
};

//limit_req_zone $variable zone=name:size rate=rate;
//limit_req zone=name [burst=number] [nodelay];
static ngx_command_t  ngx_http_limit_req_commands[] = {

    { ngx_string("limit_req_zone"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE3,
      ngx_http_limit_req_zone,
      0,
      0,
      NULL },

    { ngx_string("limit_req"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE123,
      ngx_http_limit_req,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("limit_req_log_level"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_limit_req_conf_t, limit_log_level),
      &ngx_http_limit_req_log_levels 
    },
      ngx_null_command
};


static ngx_http_module_t  ngx_http_limit_req_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_limit_req_init,               /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_limit_req_create_conf,        /* create location configration */
    ngx_http_limit_req_merge_conf          /* merge location configration */
};


ngx_module_t  ngx_http_limit_req_module = {
    NGX_MODULE_V1,
    &ngx_http_limit_req_module_ctx,        /* module context */
    ngx_http_limit_req_commands,           /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t ngx_http_limit_req_handler(ngx_http_request_t *r)
{//ÇëÇóÆµÂÊÏŞÖÆ´¦Àí¹ı³Ìº¯Êı¡£ÏÂÃæ¸ù¾İ¿Í»§¶ËµÄIP»ñÈ¡Æä·ÃÎÊÆµÂÊĞÅÏ¢¡£È·¶¨ÊÇ·ñÔÊĞíÁ¬½Ó»¹ÊÇÑÓ³Ù¡£
    size_t                      len, n;
    uint32_t                    hash;
    ngx_int_t                   rc;
    ngx_uint_t                  excess;//1ÃëµÄÓàÁ¿
    ngx_time_t                 *tp;
    ngx_rbtree_node_t          *node;
    ngx_http_variable_value_t  *vv;
    ngx_http_limit_req_ctx_t   *ctx;
    ngx_http_limit_req_node_t  *lr;
    ngx_http_limit_req_conf_t  *lrcf;

    if (r->main->limit_req_set) {
        return NGX_DECLINED;
    }
    lrcf = ngx_http_get_module_loc_conf(r, ngx_http_limit_req_module);
    if (lrcf->shm_zone == NULL) {
        return NGX_DECLINED;
    }
    ctx = lrcf->shm_zone->data;//dataÎªngx_http_limit_req_zoneÉèÖÃµÄngx_http_limit_req_ctx_t½Ó¿Ú£¬ÀïÃæ¼ÇÂ¼ÁËÕâ¸ösessionµÄÃû×Ö£¬´óĞ¡µÈ¡£
    vv = ngx_http_get_indexed_variable(r, ctx->index);//¸ù¾İÕâ¸özonÔÚ×ÜµÄvariablesÊı×éÖĞµÄÏÂ±ê£¬ÕÒµ½ÆäÖµ£¬ÆäÖĞ¿ÉÄÜĞèÒª¼ÆËãÒ»ÏÂ¡£get_handler
    //vv Îª&r->variables[index];µÄÒ»Ïî¡£
    if (vv == NULL || vv->not_found) {
        return NGX_DECLINED;
    }
    len = vv->len;
    if (len == 0) {
        return NGX_DECLINED;
    }
    if (len > 65535) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "the value of the \"%V\" variable is more than 65535 bytes: \"%v\"",&ctx->var, vv);
        return NGX_DECLINED;
    }

    r->main->limit_req_set = 1;
    hash = ngx_crc32_short(vv->data, len);
	
    ngx_shmtx_lock(&ctx->shpool->mutex);
    ngx_http_limit_req_expire(ctx, 1);//É¾³ı60ÃëÖ®ÍâµÄ¶ÓÁĞÏî¡£
    rc = ngx_http_limit_req_lookup(lrcf, hash, vv->data, len, &excess);

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "limit_req: %i %ui.%03ui", rc, excess / 1000, excess % 1000);
    if (rc == NGX_DECLINED) {//ÔÚºìºÚÊ÷ÉÏÃ»ÓĞÕÒµ½£¬ËùÒÔĞèÒªÔö¼ÓÒ»¸ö½øÈ¥ÁË¡£
        n = offsetof(ngx_rbtree_node_t, color) + offsetof(ngx_http_limit_req_node_t, data) + len;

        node = ngx_slab_alloc_locked(ctx->shpool, n);//ÔÚ¹²ÏíÄÚ´æÖĞÉêÇëÒ»¸ö½Úµã
        if (node == NULL) {//Èç¹ûÊ§°Ü£¬¾Í³¢ÊÔÊÍ·ÅÒ»Ğ©¾ÉµÄ½Úµã¡£
            ngx_http_limit_req_expire(ctx, 0);//È¥µôÈı¸ö60·ÖÖÓÖ®ÍâµÄ
            node = ngx_slab_alloc_locked(ctx->shpool, n);
            if (node == NULL) {
                ngx_shmtx_unlock(&ctx->shpool->mutex);
                return NGX_HTTP_SERVICE_UNAVAILABLE;
            }
        }
		//×¼±¸Ò»¸öĞÂµÄÇëÇó½á¹¹£¬¼ÓÈëµ½ºìºÚÊ÷ÀïÃæ¡£
        lr = (ngx_http_limit_req_node_t *) &node->color;//µÃµ½Ê×µØÖ·¡£
        node->key = hash;
        lr->len = (u_char) len;
        tp = ngx_timeofday();
        lr->last = (ngx_msec_t) (tp->sec * 1000 + tp->msec);//µ±Ç°ÃëÊı
        lr->excess = 0;
        ngx_memcpy(lr->data, vv->data, len);

        ngx_rbtree_insert(&ctx->sh->rbtree, node);//½«ĞÂ½Úµã¼ÓÈëµ½ºìºÚÊ÷
        ngx_queue_insert_head(&ctx->sh->queue, &lr->queue);//¼ÓÈëµ½¶ÓÁĞµÄÍ·²¿£¬±íÊ¾ÊÇ×îĞÂµÄÊı¾İ¡£
        ngx_shmtx_unlock(&ctx->shpool->mutex);
        return NGX_DECLINED;
    }

    ngx_shmtx_unlock(&ctx->shpool->mutex);

    if (rc == NGX_OK) {//OK£¬Ã»ÓĞ³¬¹ıÉè¶¨Öµ£¬¿ÉÒÔ·ÃÎÊ
        return NGX_DECLINED;
    }
    if (rc == NGX_BUSY) {//·ÃÎÊÆ½ÂÊ³¬¹ıÁËÏŞÖÆ¡£
        ngx_log_error(lrcf->limit_log_level, r->connection->log, 0,
                      "limiting requests, excess: %ui.%03ui by zone \"%V\"", excess / 1000, excess % 1000, &lrcf->shm_zone->shm.name);
        return NGX_HTTP_SERVICE_UNAVAILABLE;
    }
    /* rc == NGX_AGAIN */
    if (lrcf->nodelay) {//Èç¹ûÉèÖÃÁËnodelay£¬²»ÒªÍÏÇëÇó£¬¾ÍÖ±½Ó·µ»Ø£¬Í£Ö¹Õâ¸öÁ¬½Ó¡£
        return NGX_DECLINED;
    }

    ngx_log_error(lrcf->delay_log_level, r->connection->log, 0,
                  "delaying request, excess: %ui.%03ui, by zone \"%V\"", excess / 1000, excess % 1000, &lrcf->shm_zone->shm.name);

    if (ngx_handle_read_event(r->connection->read, 0) != NGX_OK) {//É¾³ı¿É¶ÁÊÂ¼ş£¬ÕâÑù¾Í²»»ácareÕâ¸öÁ¬½ÓµÄÊÂ¼şÁË
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

	//ÉèÖÃÒ»¸ö¿ÉĞ´³¬Ê±£¬È»ºóµ½ÄÇÊ±ºò¾ÍÖØĞÂÅÜÕâ¸öÁ¬½Ó¡£´Óngx_http_limit_req_delay¿ªÊ¼¡£
    r->read_event_handler = ngx_http_test_reading;
    r->write_event_handler = ngx_http_limit_req_delay;
    ngx_add_timer(r->connection->write, (ngx_msec_t) excess * 1000 / ctx->rate);//ÒÀ¿¿Ê£ÓàµÄÀ´¾ö¶¨³¬Ê±Ê±¼ä¡£

    return NGX_AGAIN;
}


static void
ngx_http_limit_req_delay(ngx_http_request_t *r)
{//ngx_http_limit_req_handler´¦Àí¹ı³ÌÉèÖÃµÄÑÓ³Ù»Øµ÷£¬¶¨Ê±Æ÷µ½À´ºó¾Íµ÷ÓÃÕâÀï£¬¿Í»§¶ËÇëÇó±»ÑÓ³ÙÁËÕâÃ´¾Ã¡£
    ngx_event_t  *wev;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "limit_req delay");

    wev = r->connection->write;
    if (!wev->timedout) {//³¬Ê±ÁË£¬½«¿ÉĞ´ÊÂ¼şÉ¾³ı£¬ÕâÊÇÎªÊ²Ã´ ¿
        if (ngx_handle_write_event(wev, 0) != NGX_OK) {
            ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        }
        return;
    }

    wev->timedout = 0;
    if (ngx_handle_read_event(r->connection->read, 0) != NGX_OK) {//½«¿É¶ÁÊÂ¼şÉ¾³ı£¬²»¹Ø×¢ÁË£¬ÕâÒ²ÊÇÊ²Ã´Ô­Òò?
        ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
        return;
    }

    r->read_event_handler = ngx_http_block_reading;
    r->write_event_handler = ngx_http_core_run_phases;//ÖØĞÂÉèÖÃ»Øngx_http_core_run_phases£¬ÕâÑùÓÖ¿ÉÒÔ½øÈë´¦Àí¹ı³ÌÑ­»·ÖĞÁË¡£
    ngx_http_core_run_phases(r);//ÊÖ¶¯½øÈë´¦Àí¹ı³Ì¡£
}


static void
ngx_http_limit_req_rbtree_insert_value(ngx_rbtree_node_t *temp,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel)
{
    ngx_rbtree_node_t          **p;
    ngx_http_limit_req_node_t   *lrn, *lrnt;

    for ( ;; ) {
        if (node->key < temp->key) {
            p = &temp->left;
        } else if (node->key > temp->key) {
            p = &temp->right;
        } else { /* node->key == temp->key */
            lrn = (ngx_http_limit_req_node_t *) &node->color;
            lrnt = (ngx_http_limit_req_node_t *) &temp->color;
            p = (ngx_memn2cmp(lrn->data, lrnt->data, lrn->len, lrnt->len) < 0)
                ? &temp->left : &temp->right;
        }
        if (*p == sentinel) {
            break;
        }
        temp = *p;
    }
    *p = node;
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    ngx_rbt_red(node);
}


/*£¨1£©NGX_ DECLINED£º½Úµã²»´æÔÚ£»
£¨2£©NGX_OK£º¸Ã¿Í»§¶Ë·ÃÎÊÆµÂÊÎ´³¬¹ıÉè¶¨Öµ£»
£¨3£©NGX_AGAIN£º¸Ã¿Í»§¶Ë·ÃÎÊÆµÂÊ³¬¹ıÁËÉè¶¨Öµ£¬µ«ÊÇ²¢Î´³¬¹ıãĞÖµ£¨ÓëburstÓĞ¹Ø£©£»
£¨4£©NGX_BUSY£º¸Ã¿Í»§¶Ë·ÃÎÊÆµÂÊ³¬¹ıÁËãĞÖµ£»*/
static ngx_int_t
ngx_http_limit_req_lookup(ngx_http_limit_req_conf_t *lrcf, ngx_uint_t hash, u_char *data, size_t len, ngx_uint_t *ep)
{//ÔÚºìºÚÊ÷ÖĞ²éÕÒÖ¸¶¨hash£¬Ãû×ÖµÄ½Úµã£¬·µ»ØÊÇ·ñ³¬¹ıÁËãĞÖµ¡£
//¸öÈË¸Ğ¾õÕâ¸öÁîÅÆÍ°Ëã·¨ÓĞµÄç©ÁÓ£¬¿ØÖÆ²»×¼£¬´ó¸ÅµÄË¼ÏëÊÇ: Ã¿ÃëÎªÃ¿¸öIP·ÖÅäx¸öÇëÇó£¬Í¬Ê±ÒªÇóÄ³Ò»Ãë×î¶à²»ÄÜ³¬¹ıy¸öÇëÇó£»
//È»ºóµÚÒ»ÃëÓÃÁËx+1¸öÇëÇó£¬µÚ¶şÃë¾ÍÖ»ÄÜÓÃx-1¸öÁË¡£Ä³Ò»Ãë×î¶àÓÃµÄÇëÇóÊıÎªy(burst)
    ngx_int_t                   rc, excess;
    ngx_time_t                 *tp;
    ngx_msec_t                  now;
    ngx_msec_int_t              ms;
    ngx_rbtree_node_t          *node, *sentinel;
    ngx_http_limit_req_ctx_t   *ctx;
    ngx_http_limit_req_node_t  *lr;//ÓÃÀ´´æ·ÅÃ¿¸ö¿Í»§¶Ë½ÚµãµÄÏà¹ØĞÅÏ¢¡£

    ctx = lrcf->shm_zone->data;
    node = ctx->sh->rbtree.root;//ºìºÚÊ÷µÄ¸ù¡£
    sentinel = ctx->sh->rbtree.sentinel;
    while (node != sentinel) {
        if (hash < node->key) {//Ä¿±êĞ¡ÓÚµ±Ç°£¬×ó±ß£»
            node = node->left;
            continue;
        }
        if (hash > node->key) {//ÓÒ±ß
            node = node->right;
            continue;
        }
        /* hash == node->key */
        do {
            lr = (ngx_http_limit_req_node_t *) &node->color;//ÄÃµ½Ê×µØÖ·
            rc = ngx_memn2cmp(data, lr->data, len, (size_t) lr->len);
            if (rc == 0) {//Ãû×ÖÈ·ÊµÏàÍ¬¡£OK£¬¾ÍÊÇÁË
                ngx_queue_remove(&lr->queue);//°ÑÕâ¸ö½ÚµãÏÈÉ¾³ı£¬È»ºó²åÈëµ½¶ÓÁĞÍ·²¿¡£±íÊ¾Ëû¸üĞÂÁË¡£
                ngx_queue_insert_head(&ctx->sh->queue, &lr->queue);
                tp = ngx_timeofday();
                now = (ngx_msec_t) (tp->sec * 1000 + tp->msec);
                ms = (ngx_msec_int_t) (now - lr->last);//µÃµ½Õâ¸ö½ÚµãÒÑ¾­¹ıÁË¶à¾Ã¡£
                /*×¢ÒâÏÂÃæµÈÊ½£¬ÓĞ2¸öµØ·½Òª×¢Òâ:
                1. +1000µÄÒâË¼ÊÇ£¬Õâ´ÎÓĞ¸öĞÂµÄÁ¬½ÓÀ´ÁË£¬¼ÆÊıÓ¦¸ÃÔö¼Ó1000.ÒÔ±¸ºóÃæÅĞ¶ÏÊÇ·ñÓĞ³¬¹ıãĞÖµ
                2. ctx->rate * ngx_abs(ms)µÄÔ­ÒòÊÇ: ¼ÙÈçÕâ¸öIP5ÃëÃ»ÓĞ·ÃÎÊ¹ıÁË£¬ÄÇÃ´¾ÍËãËüÃ»ÓĞ·ÃÎÊ£¬µ«ÊÇÅä¶îÎÒÃÇÊÇ¸øËû×¼±¸£¬²¢±»ÀË·ÑÁË
                	ÎÒÃÇÒ²Ó¦¸Ã¼õÈ¥Õâ5·ÖÖÓÒÔÀ´ÎªËû×¼±¸µÄÅä¶î£¬²»¹ÜËûµ½µ×ÓÃÁË¶àÉÙ(lr->excess)¡£¼õÉÙÎª0ÊÇÃ»ÊÂµÄ¡£
                	Èç¹ûmsÎª0£¬Ò²¾ÍÊÇÕâÒ»ÃëÍ¬Ò»¸öIPÀ´ÁË2´Î£¬ÄÇÃ´lr->excessÏàµ±ÓÚÔö¼ÓÁË2000£¬Èç¹û2000> burst£¬Ôò²¢·¢Ì«¸ß¾Ü¾ø£»
                	lr->excess´ú±íµÄÊÇÀÛ»ıµÄ£¬Õâ¸öIPÒ»¹²·ÅÁË¶àÉÙ¸öÇëÇó³öÈ¥¡£
                */
                excess = lr->excess - ctx->rate * ngx_abs(ms) / 1000 + 1000;//Ã¿Ò»¸öÇëÇó£¬¶¼Ôö¼Ó1000£¬È»ºó¼õÈ¥¾ÉµÄ¡£
                if (excess < 0) {
                    excess = 0;//Èç¹ûĞ¡ÓÚ0£¬¸ÄÎª0£¬¶ªµôÃ»ÓÃµÄÅä¶îÁË¡£
                }
                *ep = excess;//·µ»Ø²ÎÊı£¬ÉèÖÃÎªµ±Ç°»¹´æÔÚ¶àÉÙ

                if ((ngx_uint_t) excess > lrcf->burst) {//Èç¹ûµ±Ç°´æÔÚ¶ÓÁĞ×ÜµÄÊıÄ¿£¬·µ»Ø±íÃ÷ÒÑ¾­ºÜÃ¦ÁË£¬²»ÄÜ·ÅÁË¡£
                    return NGX_BUSY;
                }

                lr->excess = excess;//¼Ç×¡µ½Ä¿Ç°ÎªÖ¹£¬Ê¹ÓÃµÄÊıÄ¿¡£
                lr->last = now;//ÎÒÕâ¸ö½ÚµãÊÇµ±Ç°Õâ¸öÊ±ºò·ÅÈëµÄ¡£
                if (excess) {//»¹ºÃÃ»ÓĞ³¬¹ıburstÊıÄ¿£¬µ«ÊÇĞèÒªµÈ´ı»òÕßnodelayµÄ»°µÃ503ÁË¡£
                    return NGX_AGAIN;
                }
                return NGX_OK;
            }
            node = (rc < 0) ? node->left : node->right;//¾ö¶¨×ó×ß»¹ÊÇÓÒ×ß¡£
        } while (node != sentinel && hash == node->key);

        break;
    }
    *ep = 0;
    return NGX_DECLINED;
}


static void
ngx_http_limit_req_expire(ngx_http_limit_req_ctx_t *ctx, ngx_uint_t n)
{//É¾³ı¶ÓÁĞºÍºìºÚÊ÷ÀïÃæÊ±¼ä³¬¹ı1·ÖÖÓµÄ½Úµã¡£´Ó¶ÓÁĞµÄÎ²²¿È¡¾ÍĞĞ£¬²»¶ÏµÄÈ¡£¬Ö±µ½È¡µ½µÄ½ÚµãÊ±¼äÔÚ60ÃëÖ®ÄÚÎªÖ¹£¬È»ºóÍË³ö¡£
    ngx_int_t                   excess;
    ngx_time_t                 *tp;
    ngx_msec_t                  now;
    ngx_queue_t                *q;
    ngx_msec_int_t              ms;
    ngx_rbtree_node_t          *node;
    ngx_http_limit_req_node_t  *lr;

    tp = ngx_timeofday();//´ø»º´æ¶ÁÈ¡Ê±¼äÖµ¡£
    now = (ngx_msec_t) (tp->sec * 1000 + tp->msec);//µ±Ç°ÃëÊı
    /*
     * n == 1 deletes one or two zero rate entries
     * n == 0 deletes oldest entry by force
     *        and one or two zero rate entries
     */

    while (n < 3) {
        if (ngx_queue_empty(&ctx->sh->queue)) {//Èç¹û¶ÓÁĞÀïÃæÃ»¶«Î÷£¬¾Í²»ÓÃÉ¾³ı¡£
            return;
        }
        q = ngx_queue_last(&ctx->sh->queue);//·ñÔòÈ¡¶ÓÁĞµÄ×îºóÒ»¸öÏî£¬ÄÃµ½ÆäÉèÖÃµÄÊ±¼ä
        lr = ngx_queue_data(q, ngx_http_limit_req_node_t, queue);
        if (n++ != 0) {
            ms = (ngx_msec_int_t) (now - lr->last);//¼ÆËãÊ±¼ä²î£¬¸úµ±Ç°µÄÊ±¼ä²î¡£
            ms = ngx_abs(ms);//Ïà²î60Ãë
            if (ms < 60000) {
                return;
            }
            excess = lr->excess - ctx->rate * ms / 1000;//ÕâÊÇÉ¶ÒâË¼
            if (excess > 0) {
                return;
            }
        }
		//½«Õâ¸ö¹ıÊ±µÄ½Úµã´Ó¶ÓÁĞ£¬ºìºÚÊ÷ÖĞÉ¾³ı¡£È»ºó½âËø
        ngx_queue_remove(q);
        node = (ngx_rbtree_node_t *) ((u_char *) lr - offsetof(ngx_rbtree_node_t, color));
        ngx_rbtree_delete(&ctx->sh->rbtree, node);
	
        ngx_slab_free_locked(ctx->shpool, node);
    }
}


static ngx_int_t
ngx_http_limit_req_init_zone(ngx_shm_zone_t *shm_zone, void *data)
{//³õÊ¼»¯ÏŞÁ÷ÓÃµÄ¹²ÏíÄÚ´æ£¬ºìºÚÊ÷£¬¶ÓÁĞ¡£ctx->shºÍctx->shpool
    ngx_http_limit_req_ctx_t  *octx = data;
    size_t                     len;
    ngx_http_limit_req_ctx_t  *ctx;

    ctx = shm_zone->data;
    if (octx) {//reloadµÄÊ±ºòÒ²»áµ÷ÓÃÕâÀï£¬Õâ¸öÊ±ºòdata²»Îª¿Õ¡£
        if (ngx_strcmp(ctx->var.data, octx->var.data) != 0) {
            ngx_log_error(NGX_LOG_EMERG, shm_zone->shm.log, 0,
                          "limit_req \"%V\" uses the \"%V\" variable while previously it used the \"%V\" variable",
                          &shm_zone->shm.name, &ctx->var, &octx->var);
            return NGX_ERROR;
        }
		//ÓÉÓÚoctxÊÇ±¾µØÄÚ´æÖĞ·ÖÅäµÄ£¬Ò²ÊÇÔÚold_cycleÖĞ·ÖÅäµÄ£¬ËùÒÔĞèÒªÔÚĞÂµÄctxÖĞÖØĞÂ³õÊ¼»¯Ò»ÏÂ  
        // ËùÒÔÕâÀïÖ»ÊÇ¹ØÓÚ±¾µØÄÚ´æµÄÖØĞÂ³õÊ¼»¯£¬¶ø¹ØÓÚ¹²ÏíÄÚ´æÖĞµÄ³õÊ¼»¯¹¤×÷¾Í²»ĞèÒªÔÙ×öÁË  
        ctx->sh = octx->sh;
        ctx->shpool = octx->shpool;
        return NGX_OK;
    }

    ctx->shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;
    if (shm_zone->shm.exists) {
        ctx->sh = ctx->shpool->data;
        return NGX_OK;
    }
    ctx->sh = ngx_slab_alloc(ctx->shpool, sizeof(ngx_http_limit_req_shctx_t));
    if (ctx->sh == NULL) {
        return NGX_ERROR;
    }
    ctx->shpool->data = ctx->sh;

    ngx_rbtree_init(&ctx->sh->rbtree, &ctx->sh->sentinel, ngx_http_limit_req_rbtree_insert_value);
    ngx_queue_init(&ctx->sh->queue);
    len = sizeof(" in limit_req zone \"\"") + shm_zone->shm.name.len;

    ctx->shpool->log_ctx = ngx_slab_alloc(ctx->shpool, len);
    if (ctx->shpool->log_ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_sprintf(ctx->shpool->log_ctx, " in limit_req zone \"%V\"%Z", &shm_zone->shm.name);
    return NGX_OK;
}


static void *
ngx_http_limit_req_create_conf(ngx_conf_t *cf)
{
    ngx_http_limit_req_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_limit_req_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    /*
     * set by ngx_pcalloc():
     *     conf->shm_zone = NULL;
     *     conf->burst = 0;
     *     conf->nodelay = 0;
     */
    conf->limit_log_level = NGX_CONF_UNSET_UINT;
    return conf;
}


static char *
ngx_http_limit_req_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_limit_req_conf_t *prev = parent;
    ngx_http_limit_req_conf_t *conf = child;

    if (conf->shm_zone == NULL) {
        *conf = *prev;
    }
    ngx_conf_merge_uint_value(conf->limit_log_level, prev->limit_log_level, NGX_LOG_ERR);
    conf->delay_log_level = (conf->limit_log_level == NGX_LOG_INFO) ? NGX_LOG_INFO : conf->limit_log_level + 1;
    return NGX_CONF_OK;
}


//limit_req_zone $variable zone=name:size rate=rate;
//http://nginx.org/en/docs/http/ngx_http_limit_req_module.html
static char * ngx_http_limit_req_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//Åöµ½"limit_req_zone"Ö¸ÁîµÄÊ±ºòµ÷ÓÃÕâÀï¡£
    u_char                    *p;
    size_t                     size, len;
    ngx_str_t                 *value, name, s;
    ngx_int_t                  rate, scale;//scaleÎªÊ±¼äµ¥Î»ÊÇÃ¿Ãë»¹ÊÇÃ¿·ÖÖÓ
    ngx_uint_t                 i;
    ngx_shm_zone_t            *shm_zone;
    ngx_http_limit_req_ctx_t  *ctx;

    value = cf->args->elts;

    ctx = NULL;
    size = 0;
    rate = 1;
    scale = 1;
    name.len = 0;

    for (i = 1; i < cf->args->nelts; i++) {

        if (ngx_strncmp(value[i].data, "zone=", 5) == 0) {//zone=name:size
            name.data = value[i].data + 5;
            p = (u_char *) ngx_strchr(name.data, ':');//ÕÒµ½name:size
            if (p) {
                *p = '\0';
                name.len = p - name.data;
                p++;
                s.len = value[i].data + value[i].len - p;
                s.data = p;//´óĞ¡×Ö¶Î

                size = ngx_parse_size(&s);//½âÎöÒ»ÏÂ´óĞ¡£¬·µ»Ø×Ö½ÚÊı¡£
                if (size > 8191) {
                    continue;
                }
            }
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid zone size \"%V\"", &value[i]);
            return NGX_CONF_ERROR;
        }

        if (ngx_strncmp(value[i].data, "rate=", 5) == 0) {//rate=rate;
            len = value[i].len;
            p = value[i].data + len - 3;
            if (ngx_strncmp(p, "r/s", 3) == 0) {
                scale = 1;
                len -= 3;
            } else if (ngx_strncmp(p, "r/m", 3) == 0) {
                scale = 60;
                len -= 3;
            }
            rate = ngx_atoi(value[i].data + 5, len - 5);
            if (rate <= NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid rate \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
            continue;
        }

        if (value[i].data[0] == '$') {//$variable 
            value[i].len--;//ĞèÒª½âÎö±äÁ¿
            value[i].data++;
            ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_limit_req_ctx_t));
            if (ctx == NULL) {
                return NGX_CONF_ERROR;
            }
			//¸ù¾İ±äÁ¿Ãû×Ö£¬²éÕÒ»òÕßÌí¼ÓÒ»¸öÏî£¬ÔÚcmcf->variables.nelts ÀïÃæ¡£²»¹ıÆäset/get_handler»¹Ã»ÓĞÉèÖÃºÃ¡£Îª¿Õ
            ctx->index = ngx_http_get_variable_index(cf, &value[i]);//´«Èë±äÁ¿µÄÃû×Ö£¬»ñÈ¡ÆäÔÚÅäÖÃÀïÃæµÄÏÂ±ê¡£
            if (ctx->index == NGX_ERROR) {
                return NGX_CONF_ERROR;
            }
            ctx->var = value[i];//¼Ç×¡±äÁ¿µÄÃû×Ö¡£
            continue;
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid parameter \"%V\"", &value[i]);
        return NGX_CONF_ERROR;
    }

    if (name.len == 0 || size == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"%V\" must have \"zone\" parameter", &cmd->name);
        return NGX_CONF_ERROR;
    }
    if (ctx == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "no variable is defined for limit_req_zone \"%V\"", &cmd->name);
        return NGX_CONF_ERROR;
    }
    ctx->rate = rate * 1000 / scale;//Ëã³öÃ¿·ÖÃëµÄÁ¿ÇëÇóÁ¿¡£
	//ÏÂÃæ¸ù¾İÃû×Ö£¬ÉêÇëÒ»¿é¹²ÏíÄÚ´æ£¬´óĞ¡Îªsize¡£²¢¼ÇÂ¼init³õÊ¼»¯º¯Êı£¬¼ÇÂ¼ngx_http_limit_req_ctx_tµÄÉÏÏÂÎÄ½á¹¹¡£
    shm_zone = ngx_shared_memory_add(cf, &name, size, &ngx_http_limit_req_module);
    if (shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }
    if (shm_zone->data) {
        ctx = shm_zone->data;
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"limit_req_zone \"%V\" is already bound to variable \"%V\"", &value[1], &ctx->var);
        return NGX_CONF_ERROR;
    }

    shm_zone->init = ngx_http_limit_req_init_zone;
    shm_zone->data = ctx;//¼ÇÂ¼¸ÕÉêÇë×¼±¸µÄctx£¬ÀïÃæ¼ÇÂ¼ÁËrate,index,varµÈĞÅÏ¢¡£
    return NGX_CONF_OK;
}

//limit_req zone=name [burst=number] [nodelay];
static char * ngx_http_limit_req(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_limit_req_conf_t  *lrcf = conf;

    ngx_int_t    burst;
    ngx_str_t   *value, s;
    ngx_uint_t   i;

    if (lrcf->shm_zone) {
        return "is duplicate";
    }
    value = cf->args->elts;
    burst = 0;
    for (i = 1; i < cf->args->nelts; i++) {
        if (ngx_strncmp(value[i].data, "zone=", 5) == 0) {//¼ÇÂ¼Ãû×Ö¡£
            s.len = value[i].len - 5;
            s.data = value[i].data + 5;
			//²éÕÒÒ»¸öÒÑ¾­´æÔÚÃû×ÖµÄ£¬»òÕßĞÂÉêÇëÒ»¸ö¹²ÏíÄÚ´æ½á¹¹£¬´æÈë&cf->cycle->shared_memory.partÀïÃæ¡£
            lrcf->shm_zone = ngx_shared_memory_add(cf, &s, 0, &ngx_http_limit_req_module);
            if (lrcf->shm_zone == NULL) {
                return NGX_CONF_ERROR;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "burst=", 6) == 0) {//Í»·¢ÊıÄ¿¡£
            burst = ngx_atoi(value[i].data + 6, value[i].len - 6);
            if (burst <= 0) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid burst rate \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }
            continue;
        }
        if (ngx_strncmp(value[i].data, "nodelay", 7) == 0) {
            lrcf->nodelay = 1;//ÉèÖÃÁËnodelay±êÖ¾£¬ÕâÑù¹ıÔØµÄÇëÇó»áÖ±½Ó·µ»Ø503¶ø²»ÓÌÔ¥¡£
            continue;
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,"invalid parameter \"%V\"", &value[i]);
        return NGX_CONF_ERROR;
    }

    if (lrcf->shm_zone == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"%V\" must have \"zone\" parameter", &cmd->name);
        return NGX_CONF_ERROR;
    }

    if (lrcf->shm_zone->data == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unknown limit_req_zone \"%V\"", &lrcf->shm_zone->shm.name);
        return NGX_CONF_ERROR;
    }
    lrcf->burst = burst * 1000;
    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_limit_req_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers);//ÔÚPREACCESS¹ı³ÌÖ®Ç°ÉêÇëÒ»¸ö²ÛÎ»£¬Ôö¼ÓÕâ¸öhandler
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_limit_req_handler;//×¢²áÕâ¸öº¯ÊıÎªphrase¹ıÂËº¯Êı£¬ÇëÇóÔÚcontent phraseÖ®Ç°»áµ÷ÓÃÕâÀï½øĞĞ·ÃÎÊÆµ¶ÈÏŞÖÆ¡£
    return NGX_OK;
}
