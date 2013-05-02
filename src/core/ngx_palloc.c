
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


static void *ngx_palloc_block(ngx_pool_t *pool, size_t size);
static void *ngx_palloc_large(ngx_pool_t *pool, size_t size);


ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{//´´½¨ÄÚ´æ³ØÖ¸Õë£¬²ÉÓÃ´óÄÚ´æÖ±½Ómalloc·ÖÅä£¬Ğ¡ÄÚ´æµÄ·½Ê½Ô¤·ÖÅä.²»ÓÃµÄÏÈ»º´æ¡£Ò»´ÎÊÍ·Å¡£
    ngx_pool_t  *p;
/*NGX_POOL_ALIGNMENTÎª16*/
    p = ngx_memalign(NGX_POOL_ALIGNMENT, size, log);//k ÉêÇëÄÚ´æ
    if (p == NULL) {
        return NULL;
    }
    p->d.last = (u_char *) p + sizeof(ngx_pool_t);//Ç°Ãæ±£ÁôÒ»¸öÍ·²¿
    p->d.end = (u_char *) p + size;//k Ç°ÃæÊµ¼ÊÃ»ÓĞÄÇÃ´¶àÊı¾İ²¿·ÖµÄ£¬ÓĞ¸öÍ·²¿sizeof(ngx_pool_t)
    p->d.next = NULL;
    p->d.failed = 0;

    size = size - sizeof(ngx_pool_t);
    p->max = (size < NGX_MAX_ALLOC_FROM_POOL) ? size : NGX_MAX_ALLOC_FROM_POOL;

    p->current = p;
    p->chain = NULL;
    p->large = NULL;
    p->cleanup = NULL;
    p->log = log;

    return p;
}

/*
ÏÈµ÷ÓÃÒªcleanupÄÚ´æµÄhandler£¬
È»ºóÊÍ·Å´ó¿éÄÚ´æ¡£
È»ºóÊÍ·ÅĞ¡¿éÄÚ´æÁ´±í
*/
void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_t          *p, *n;
    ngx_pool_large_t    *l;
    ngx_pool_cleanup_t  *c;

//¶ÔcleanupÁĞ±í±éÀúµ÷ÓÃhandlerº¯Êı
    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "run cleanup: %p", c);
            c->handler(c->data);
        }
    }

    for (l = pool->large; l; l = l->next) {
//¶ÔÓÚ´ó¿éÄÚ´æ£¬Ö±½Ófree
        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p", l->alloc);
        if (l->alloc) {
            ngx_free(l->alloc);//ÊÍ·ÅÁËÒ²²»ÖÃNULL£¬Ê²Ã´Çé¿ö?
        }
    }
#if (NGX_DEBUG)
    /*
     * we could allocate the pool->log from this pool
     * so we can not use this log while the free()ing the pool
     */
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, pool->log, 0, "free: %p, unused: %uz", p, p->d.end - p->d.last);
        if (n == NULL) {
            break;
        }
    }
#endif
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        ngx_free(p);
        if (n == NULL) {
            break;
        }
    }
}


//¹¦ÄÜÊÇÊÍ·Å´ó¿éÄÚ´æ£¬Ğ¡ÄÚ´æÖ»ÖÃ¿Õ
void
ngx_reset_pool(ngx_pool_t *pool)
{
    ngx_pool_t        *p;
    ngx_pool_large_t  *l;
//ÏÈ°Ñ´óµÄÈ«²¿ÊÍ·Å¡£large±¾ÉíÕâ¸öÁ´±íÔõÃ´²»ÊÍ·Å?
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            ngx_free(l->alloc);
        }
    }
//Ö±½ÓÖÃ¿Õ?Á´±í±¾Éí²»ÓÃÊÍ·ÅÂğ?o ,god like £¬nginxĞ¡¿éÄÚ´æ¶¼²»ÓÃÊÍ·Å
    pool->large = NULL;
//Ğ¡¿éÄÚ´æ²»ÊÍ·Å£¬Ö»ÊÇÖØĞÂÖ¸Ïò¿ªÍ·£¬µ«ÓÖ²»ÖÃ0
    for (p = pool; p; p = p->d.next) {
        p->d.last = (u_char *) p + sizeof(ngx_pool_t);
    }
}


void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    ngx_pool_t  *p;

    if (size <= pool->max) {
		//´Óµ±Ç°poll¿ªÊ¼£¬Ç°ÃæµÄÄØ?²»¹Ü
        p = pool->current;
        do {
            m = ngx_align_ptr(p->d.last, NGX_ALIGNMENT);
            if ((size_t) (p->d.end - m) >= size) {
                p->d.last = m + size;//Èç¹û»¹Ê£ÏÂÄÇÃ´¶à£¬ÄÇ¾ÍÒÆ¶¯last£¬È»ºó·µ»ØÖ¸Õë¾ÍĞĞ¡£
                return m;
            }
//²»ĞĞÔÙ¿´ÏÂÒ»¸öpoll°Ñ£¬µ«Í¨¹ıdata.nextÖ¸ÏòÏÂÒ»¸ö£¬¹ÖÒì
            p = p->d.next;
        } while (p);
//µ±Ç°³ØÒÔºóµÄËùÒÔ³Ø¶¼²»ÄÜÂú×ãÕâ´ÎÇëÇó£¬ÄÇÎÒµÃÔÙÉêÇëÒ»Õû¿éÁË¡£
        return ngx_palloc_block(pool, size);
    }
//Èç¹û´óÓÚmax£¬ÔòÖ±½ÓÉêÇë´ó¿éÄÚ´æ
    return ngx_palloc_large(pool, size);
}


//¸úngx_pallocµÄÇø±ğÊÇngx_pnalloc²»¶ÔÆë
void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    ngx_pool_t  *p;

    if (size <= pool->max) {//Èç¹ûÊÇ´ó¿éÄÚ´æ·ÖÅä£¬ÔòÖ±½Ó·ÖÅä

        p = pool->current;//µÃµ½µ±Ç°µÄÒ»¿é

        do {
            m = p->d.last;

            if ((size_t) (p->d.end - m) >= size) {//Èç¹ûlastµ±Ç°×îºóµÄÖ¸Õëµ½½áÎ²end,¹»ÁË£¬¾ÍÒÆ¶¯Ò»ÏÂ£¬·µ»Ø¡£
                p->d.last = m + size;//ÕâÑùµÄÄÚ´æÉêÇë²»ÓÃÊÍ·Å
                return m;
            }

            p = p->d.next;

        } while (p);

        return ngx_palloc_block(pool, size);//ÖØĞÂÀ´Ò»¿é
    }

    return ngx_palloc_large(pool, size);
}


static void *
ngx_palloc_block(ngx_pool_t *pool, size_t size)
{
    u_char      *m;
    size_t       psize;
    ngx_pool_t  *p, *new, *current;

    psize = (size_t) (pool->d.end - (u_char *) pool);//Ò»Õû¿éÓĞ¶àÉÙÈ¥ÁË¿ ¼ÆËãÒ»ÏÂÖ®Ç°µÄ¾ÍÖªµÀÁË
//×¢Òâ£¬ÉÏÃæÃ»ÓĞÉêÇëÕû¸öngx_pool_t½á¹¹£¬¶øÖ»ÊÇÕâ¸ö½á¹¹µÄdata²¿·Ö£¬²»»á°üÀ¨²»ÓÃµÄcleanup,large
    m = ngx_memalign(NGX_POOL_ALIGNMENT, psize, pool->log);//ÉêÇëÒ»¿é¸úpoolÒ»Ñù´óĞ¡µÄ
    if (m == NULL) {
        return NULL;
    }

    new = (ngx_pool_t *) m;
//good ÎÒÉêÇëÁËÒ»¿éĞÂµÄ
    new->d.end = m + psize;
    new->d.next = NULL;
    new->d.failed = 0;
//ÂíÉÏ·ÖÅäsize³öÈ¥°É
    m += sizeof(ngx_pool_data_t);
    m = ngx_align_ptr(m, NGX_ALIGNMENT);
    new->d.last = m + size;//ÒÆ¶¯Ö¸Õë

    current = pool->current;//ºÃµÄ£¬ÕâÊÇ¾ÉµÄµ±Ç°Ö¸Õë
//ÕâÊÇÉ¶ÒâË¼¡£Èç¹ûÓĞÏÂÒ»¸ö£¬ÄÇ¾Í¼ÌĞø×ßÏòÏÂÒ»¸ö£¬Ö±µ½Î²²¿
//Ã¿´Î´Óµ±Ç°³Ø×ßµ½µ¹ÊıµÚ¶ş¸ö³Ø£¬È«¶¼Ôö¼Ófailed,±íÊ¾¸Õ²ÅÎÒÔÚÄãÄÇÎÊÁËÒ»ÏÂ£¬ÄãÈ´Âú×ã²»ÁËÎÒ!
//Èç¹ûÄãÂú×ã²»ÁËÎÒ³¬¹ı4´Î£¬µÃÁË£¬ÄãºÍÇ°ÃæµÄ¶¼Ã»»ú»áÁË£¬ÎÒ²»ÔÚĞèÒªÄãÁË.
//ºÃ¾ø£¬Èç¹ûÁ¬ĞøÉêÇë¼¸¿é±¯¾ç´óĞ¡µÄ£¬ÄÇ²»±¯¾çÁË
//¶Ô£¬µ«ÕâÑùÄÜÓĞĞ§±ÜÃâÈç¹ûÕâ¸öµØ·½ÓĞºÜ¶àºÜĞ¡µÄ£¬¼¸ºõ²»ÄÜÂú×ãĞèÇó£¬¾ÍÄÜÇÉÃîµÄ¹ıÂËËü£¬±ÜÃâ×ÜÊÇÊ§°ÜµÄ³¢ÊÔ¼¸´Î
    for (p = current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {//Èç¹ûÕâ¿éÊ§°Ü¹ı4´Î,current¾ÍÖ¸ÏòËü£¬²¢Ôö¼ÓËùÓĞµÄ´íÎó´ÎÊı
            current = p->d.next;
        }
    }
//½Óµ½×îºó
    p->d.next = new;

//³ı·ÇµÚÒ»´Î£¬·ñÔòcurrent²»»áÎª¿Õ¡£ÉÏÊöfor¿É¿´³ö
    pool->current = current ? current : new;

    return m;
}


static void *
ngx_palloc_large(ngx_pool_t *pool, size_t size)
{
    void              *p;
    ngx_uint_t         n;
    ngx_pool_large_t  *large;

    p = ngx_alloc(size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    n = 0;

    for (large = pool->large; large; large = large->next) {
        if (large->alloc == NULL) {//ÕâÀïÔÚÊ²Ã´Ê±ºòÓĞÓÃÄØ?
            large->alloc = p;
            return p;
        }
//ÔÚµ±Ç°largeÁ´±íÖĞÕÒÁË3´Î¶¼Ã»ÓĞÕÒµ½¿ÕµÄÎ»ÖÃ£¬¸É´àÉêÇëÒ»¸ö½ÚµãËãÁË
        if (n++ > 3) {
            break;
        }
    }

    large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }
//·Åµ½Ç°Ãæ
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


//¶ÔÆëÉêÇëÒ»¿é´óµÄÄÚ´æ
void *
ngx_pmemalign(ngx_pool_t *pool, size_t size, size_t alignment)
{
    void              *p;
    ngx_pool_large_t  *large;

    p = ngx_memalign(alignment, size, pool->log);
    if (p == NULL) {
        return NULL;
    }

    large = ngx_palloc(pool, sizeof(ngx_pool_large_t));
    if (large == NULL) {
        ngx_free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


//ÊÍ·Å´óÄÚ´æ
ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t  *l;
//²ÎÊıpÖ¸µÄÊÇÊµ¼ÊÄÚ´æµÄµØÖ·£¬Ã»°ì·¨£¬É¨Ò»±é¡£ÒòÎªÎÒÃÇĞèÒªalloc=NULL
    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, pool->log, 0,
                           "free: %p", l->alloc);
            ngx_free(l->alloc);
            l->alloc = NULL;

            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}


void *
ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    void *p;

    p = ngx_palloc(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}


//¿¿cleanup¿¿¿¿¿size¿¿¿p¿cleanup¿¿¿¿
ngx_pool_cleanup_t *
ngx_pool_cleanup_add(ngx_pool_t *p, size_t size)
{
    ngx_pool_cleanup_t  *c;
//¿¿¿¿¿¿¿
    c = ngx_palloc(p, sizeof(ngx_pool_cleanup_t));
    if (c == NULL) {
        return NULL;
    }

    if (size) {//¿¿¿¿¿¿¿¿
        c->data = ngx_palloc(p, size);
        if (c->data == NULL) {
            return NULL;
        }

    } else {
        c->data = NULL;
    }

	//¿¿¿cleanup¿¿
    c->handler = NULL;
    c->next = p->cleanup;
    p->cleanup = c;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, p->log, 0, "add cleanup: %p", c);

    return c;
}


//¿¿p¿cleanup¿¿¿¿¿¿¿¿handler¿ngx_pool_cleanup_file,¿¿¿¿¿¿¿¿
void
ngx_pool_run_cleanup_file(ngx_pool_t *p, ngx_fd_t fd)
{
    ngx_pool_cleanup_t       *c;
    ngx_pool_cleanup_file_t  *cf;

    for (c = p->cleanup; c; c = c->next) {
        if (c->handler == ngx_pool_cleanup_file) {

            cf = c->data;

            if (cf->fd == fd) {
                c->handler(cf);
                c->handler = NULL;
                return;
            }
        }
    }
}


void
ngx_pool_cleanup_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d",
                   c->fd);

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


void
ngx_pool_delete_file(void *data)
{
    ngx_pool_cleanup_file_t  *c = data;

    ngx_err_t  err;

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, c->log, 0, "file cleanup: fd:%d %s",
                   c->fd, c->name);

    if (ngx_delete_file(c->name) == NGX_FILE_ERROR) {
        err = ngx_errno;

        if (err != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_CRIT, c->log, err,
                          ngx_delete_file_n " \"%s\" failed", c->name);
        }
    }

    if (ngx_close_file(c->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", c->name);
    }
}


#if 0

static void *
ngx_get_cached_block(size_t size)
{
    void                     *p;
    ngx_cached_block_slot_t  *slot;

    if (ngx_cycle->cache == NULL) {
        return NULL;
    }

    slot = &ngx_cycle->cache[(size + ngx_pagesize - 1) / ngx_pagesize];

    slot->tries++;

    if (slot->number) {
        p = slot->block;
        slot->block = slot->block->next;
        slot->number--;
        return p;
    }

    return NULL;
}

#endif
