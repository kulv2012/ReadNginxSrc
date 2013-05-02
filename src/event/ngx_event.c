
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#define DEFAULT_CONNECTIONS  512


extern ngx_module_t ngx_kqueue_module;
extern ngx_module_t ngx_eventport_module;
extern ngx_module_t ngx_devpoll_module;
extern ngx_module_t ngx_epoll_module;
extern ngx_module_t ngx_rtsig_module;
extern ngx_module_t ngx_select_module;


static ngx_int_t ngx_event_module_init(ngx_cycle_t *cycle);
static ngx_int_t ngx_event_process_init(ngx_cycle_t *cycle);
static char *ngx_events_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *ngx_event_connections(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_event_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_event_debug_connection(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static void *ngx_event_create_conf(ngx_cycle_t *cycle);
static char *ngx_event_init_conf(ngx_cycle_t *cycle, void *conf);


static ngx_uint_t     ngx_timer_resolution;//ÊÇ·ñÒª¿ªÆô¾«È·Ê±¼äÑ¡Ïî£¬ÖµÊÇ¾«È·¶È
sig_atomic_t          ngx_event_timer_alarm;//Õâ¸öÊÇÉ¶ÒâË¼

static ngx_uint_t     ngx_event_max_module;

ngx_uint_t            ngx_event_flags;
ngx_event_actions_t   ngx_event_actions;//ngx_epoll_initÖĞÉèÖÃ£¬´ú±íÊ¹ÓÃµÄevent½á¹¹¡£ngx_epoll_module_ctx.actions;


static ngx_atomic_t   connection_counter = 1;
ngx_atomic_t         *ngx_connection_counter = &connection_counter;


ngx_atomic_t         *ngx_accept_mutex_ptr;
ngx_shmtx_t           ngx_accept_mutex; //¼àÌıµÄËø
ngx_uint_t            ngx_use_accept_mutex;//ÊÇ·ñÊ¹ÓÃÉÏÃæµÄËø
//ngx_use_accept_mutex±íÊ¾ÊÇ·ñĞèÒªÍ¨¹ı¶Ôaccept¼ÓËøÀ´½â¾ö¾ªÈºÎÊÌâ¡£µ±nginx worker½ø³ÌÊı>1Ê±ÇÒÅäÖÃÎÄ¼şÖĞ´ò¿ªaccept_mutexÊ±£¬Õâ¸ö±êÖ¾ÖÃÎª1  
ngx_uint_t            ngx_accept_events;
ngx_uint_t            ngx_accept_mutex_held;//ÎÒÒÑ¾­ÄÃµ½ÁËacceptËø£¬
ngx_msec_t            ngx_accept_mutex_delay;

ngx_int_t             ngx_accept_disabled;
//ngx_accept_disabled±íÊ¾´ËÊ±Âú¸ººÉ£¬Ã»±ØÒªÔÙ´¦ÀíĞÂÁ¬½ÓÁË£¬ÎÒÃÇÔÚnginx.confÔø¾­ÅäÖÃÁËÃ¿Ò»¸ö
//ginx worker½ø³ÌÄÜ¹»´¦ÀíµÄ×î´óÁ¬½ÓÊı£¬µ±´ïµ½×î´óÊıµÄ7/8Ê±£¬ngx_accept_disabledÎªÕı£¬ËµÃ÷±¾nginx worker½ø³Ì·Ç³£·±Ã¦£¬
//½«²»ÔÙÈ¥´¦ÀíĞÂÁ¬½Ó£¬ÕâÒ²ÊÇ¸ö¼òµ¥µÄ¸ºÔØ¾ùºâ  

ngx_file_t            ngx_accept_mutex_lock_file;


#if (NGX_STAT_STUB)
ngx_atomic_t   ngx_stat_accepted0;
ngx_atomic_t  *ngx_stat_accepted = &ngx_stat_accepted0;
ngx_atomic_t   ngx_stat_handled0;
ngx_atomic_t  *ngx_stat_handled = &ngx_stat_handled0;
ngx_atomic_t   ngx_stat_requests0;
ngx_atomic_t  *ngx_stat_requests = &ngx_stat_requests0;
ngx_atomic_t   ngx_stat_active0;
ngx_atomic_t  *ngx_stat_active = &ngx_stat_active0;
ngx_atomic_t   ngx_stat_reading0;
ngx_atomic_t  *ngx_stat_reading = &ngx_stat_reading0;
ngx_atomic_t   ngx_stat_writing0;
ngx_atomic_t  *ngx_stat_writing = &ngx_stat_writing0;
#endif



static ngx_command_t  ngx_events_commands[] = {//¸Ãngx_events_moduleÄ£¿éµÄËùÓĞÅäÖÃÖ¸ÁîÈçÏÂ¡£
    { ngx_string("events"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_events_block,//Ö¸ÁîµÄsetº¯Êı,»áµ÷ÓÃngx_conf_parse¼ÌĞø½âÎö±¾¿éµÄÄÚÈİ
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_events_module_ctx = {
    ngx_string("events"),//events {}ÕâÑùµÄ¿é½âÎö£¬Ã»ÓĞÉ¶¶«Î÷¡£
    NULL,//create_conf
    NULL//init_conf»Øµ÷
};

ngx_module_t  ngx_events_module = {//ngx_modulesÀïÃæÉèÖÃµÄ£¬Õâ¸öÊÇÕû¸öÊÂ¼şµÄ¹ÜÀíÄ£¿é£¬ÀïÃæ»á³õÊ¼»¯¸÷ÖÖÊÂ¼şÄ£¿é±ÈÈçngx_event_core_module¡£
    NGX_MODULE_V1,
    &ngx_events_module_ctx,                /* module context */
    ngx_events_commands,                   /* module directives *///±¾Ä£¿éµÄÖ¸ÁîÁĞ±í
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master *///Îª¿Õ
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};
////////////////////////////////////////////////////////


static ngx_str_t  event_core_name = ngx_string("event_core");
static ngx_command_t  ngx_event_core_commands[] = {

    { ngx_string("worker_connections"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,//Ö»ÓĞÒ»¸ö²ÎÊı
      ngx_event_connections,
      0,
      0,
      NULL },

    { ngx_string("connections"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_event_connections,
      0,
      0,
      NULL },

    { ngx_string("use"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_event_use,
      0,
      0,
      NULL },

    { ngx_string("multi_accept"),
      NGX_EVENT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_event_conf_t, multi_accept),
      NULL },

    { ngx_string("accept_mutex"),
      NGX_EVENT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_event_conf_t, accept_mutex),
      NULL },

    { ngx_string("accept_mutex_delay"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      0,
      offsetof(ngx_event_conf_t, accept_mutex_delay),
      NULL },

    { ngx_string("debug_connection"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_event_debug_connection,
      0,
      0,
      NULL },

      ngx_null_command
};


ngx_event_module_t  ngx_event_core_module_ctx = {
    &event_core_name,
    ngx_event_create_conf,                 /* create configuration */
    ngx_event_init_conf,                   /* init configuration */

    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};
//ngx_events_moduleºÍngx_event_core_module¶¼ÓĞ
ngx_module_t  ngx_event_core_module = {
    NGX_MODULE_V1,
    &ngx_event_core_module_ctx,            /* module context */
    ngx_event_core_commands,               /* module directives */
    NGX_EVENT_MODULE,                      /* module type */
    NULL,                                  /* init master */
    ngx_event_module_init,                 /* init module */
    ngx_event_process_init,                /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


void
ngx_process_events_and_timers(ngx_cycle_t *cycle)
{//´¦ÀíÒ»ÂÖÊÂ¼şºÍ¶¨Ê±Æ÷¡£
    ngx_uint_t  flags;
    ngx_msec_t  timer, delta;
//Ò²¾ÍÊÇËµ£¬ÅäÖÃÎÄ¼şÖĞÊ¹ÓÃÁËtimer_resolutionÖ¸Áîºó£¬epoll_wait½«Ê¹ÓÃĞÅºÅÖĞ¶ÏµÄ»úÖÆÀ´Çı¶¯¶¨Ê±Æ÷£¬
//·ñÔò½«Ê¹ÓÃ¶¨Ê±Æ÷ºìºÚÊ÷µÄ×îĞ¡Ê±¼ä×÷Îªepoll_wait³¬Ê±Ê±¼äÀ´Çı¶¯¶¨Ê±Æ÷¡£¶¨Ê±Æ÷ÔÚngx_event_process_initÉèÖÃ
    if (ngx_timer_resolution) {//Èç¹ûÅäÖÃÀïÃæÉèÖÃÁËÊ±¼ä¾«È·¶È.
        timer = NGX_TIMER_INFINITE;//ÒòÎªÕâÀïÎÒÃÇÒòÎªngx_timer_resolution¡£ÄÇÕâÀïµÄÒâË¼ÊÇ£¬¾ÍËãÓĞ¶¨Ê±Æ÷¿ìÒª³¬Ê±ÁË£¬µ«»¹ÊÇÒªµÈµ½¶¨Ê±Æ÷´¥·¢²ÅĞĞ¡£
//ÎªºÎ²»È¡¸ö×îĞ¡Öµ£¬ÒòÎªÃ»·¨È¡×îĞ¡Öµ£¬ÕâÊÇÕÛÖĞ
        flags = 0;
    } else {//ÒòÎªÃ»ÓĞ¶¨Ê±Æ÷£¬ËùÒÔµÃÓÃºìºÚÊ÷×îĞ¡Ê±¼ä
        timer = ngx_event_find_timer();//·µ»Ø×îĞ¡µÄ»¹Òª¶à¾Ã³¬Ê±£¬ÎÒÕâ»Øepoll_wait×î³¤µÈÕâÃ´¾ÃÁË£¬²»È»ÍíÁË
        flags = NGX_UPDATE_TIME;//´ı»áepoll_waitµÈ´ıÖ®ºó£¬ĞèÒª¸üĞÂÒ»ÏÂÊ±¼ä¡£
#if (NGX_THREADS)
        if (timer == NGX_TIMER_INFINITE || timer > 500) {
            timer = 500;
        }
#endif
    }
//Èç¹ûÅäÖÃĞèÒªÓĞacceptËø±ÜÃâ½øÈºÎÊÌâ£¬ÔòÏÈ»ñµÃËù£¬ÔÚ»ñµÃËøµÄÄÚ²¿£¬»á½«¼àÌı¾ä±ú·ÅÈëepollµÄ¡£
//¸ÄÌì²âÊÔÒ»ÏÂ
    if (ngx_use_accept_mutex) {//listupdate·şÎñÊÇÃ»ÓĞ¿ªÆôµÄ£¬Ò²¾ÍÎŞ·¨±ÜÃâ¾ªÈºÎÊÌâ
        if (ngx_accept_disabled > 0) {//¿ØÖÆÆµÂÊµÄ£¬´óÓÚ7/8¾Í¿ªÆô
            ngx_accept_disabled--;
        } else {//»ñÈ¡Ëø,²¢ÇÒ½«¼àÌıSOCK·ÅÈëepoll£¬Ò»±éÕâ´Î½øĞĞ¼à¿Ø£¬·ñÔò²»¼à¿ØĞÂÁ¬½Ó
            if (ngx_trylock_accept_mutex(cycle) == NGX_ERROR) {
                return;
            }

            if (ngx_accept_mutex_held) {//ÄÃµ½ËøÁË£¬´ı»á¾ÍµÃÓÆ×Åµã
                flags |= NGX_POST_EVENTS;
//ÄÃµ½ËøµÄ»°£¬ÖÃflagÎªNGX_POST_EVENTS£¬ÕâÒâÎ¶×Ångx_process_eventsº¯ÊıÖĞ£¬
//ÈÎºÎÊÂ¼ş¶¼½«ÑÓºó´¦Àí£¬»á°ÑacceptÊÂ¼ş¶¼·Åµ½ngx_posted_accept_eventsÁ´±íÖĞ£¬
//epollin|epolloutÊÂ¼ş¶¼·Åµ½ngx_posted_eventsÁ´±íÖĞ  ¡£ÆäÊµ¾ÍÊÇÏë¾¡ÔçÊÍ·ÅÕâ¸öËø£¬ÒÔ±ã¸ø±ğµÄ½ø³ÌÓÃ
            } else {//Èç¹ûÃ»ÓĞÄÃµ½Ëø
                if (timer == NGX_TIMER_INFINITE || timer > ngx_accept_mutex_delay) {
                    timer = ngx_accept_mutex_delay;
                }
            }
        }
    }//Èç¹ûÃ»ÓĞÅäÖÃaccept_mutex on £¬ ÄÇÃ´¾Í»áÓĞ¾ªÈºÎÊÌâ³öÏÖ

    delta = ngx_current_msec;//µ±Ç°Ê±¼ä
//½øĞĞepoll_wait£¬Èç¹ûĞèÒªacceptËøÇÒÄÃµ½ÁË£¬¾ÍÍ¬Ê±¼à¿Ølistening fd£¬·ñÔò¼à¿Ø¿É¶Á¿ÉĞ´ÊÂ¼ş£¬¸ù¾İĞèÒª·ÅÈëngx_posted_accept_eventsÁ´±í
    (void) ngx_process_events(cycle, timer, flags);//µ÷ÓÃngx_epoll_process_events

    delta = ngx_current_msec - delta;//´¦ÀíÊÂ¼şÊ±¼ä²î
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "timer delta: %M", delta);

    if (ngx_posted_accept_events) {//Èç¹ûacceptÑÓºó´¦ÀíÁ´±íÖĞÓĞÊı¾İ£¬ÄÇÃ´¾ÍÏÈ¸Ï½ôacceptÖ®£¬È»ºóÂíÉÏÊÍ·ÅËø£¬ÈÃ±ğµÄ½ø³ÌÄÜ·ÃÎÊ
        ngx_event_process_posted(cycle, &ngx_posted_accept_events);//ngx_event_accept
    }
    if (ngx_accept_mutex_held) {//¸ÕÄÃµ½ÁËËø£¬Èç¹ûÓĞĞÂµÄÁ¬½Ó£¬ÎÒÒÑ¾­acceptÁË£¬½âËø
        ngx_shmtx_unlock(&ngx_accept_mutex);
    }

    if (delta) {//Ãî!Èç¹û¸Õ²ÅµÄngx_process_eventsÃ»ÓĞ»¨·ÑÌ«¾Ã£¬1Ãë¶¼Ã»ÓĞ£¬ÄÇÑ¾µÄ¶¼²»ÓÃÈ¥´¦Àí¶¨Ê±Æ÷£¬ÒòÎªÑ¹¸ùÃ»ÓĞ³¬Ê±µÄ¿Ï¶¨¡£Å£±Æ
        ngx_event_expire_timers();//°Ñ³¬Ê±µÄ»Øµ÷ÁË
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "posted events %p", ngx_posted_events);
    if (ngx_posted_events) {//ËøÒ²ÊÍ·ÅÁË£¬ÏÖÔÚĞèÒª´¦ÀíÒ»ÏÂÊı¾İ¶ÁĞ´ÊÂ¼şÁË
        if (ngx_threaded) {
            ngx_wakeup_worker_thread(cycle);
        } else {
            ngx_event_process_posted(cycle, &ngx_posted_events);//´¦Àí¹Òµ½¶ÓÁĞµÄ¶ÁĞ´ÊÂ¼ş¡£
        }
    }
}


ngx_int_t
ngx_handle_read_event(ngx_event_t *rev, ngx_uint_t flags)
{//½«Ò»¸öÁ¬½Ó¼ÓÈë¿É¶ÁÊÂ¼ş¼àÌıÖĞ¡£
    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {
        /* kqueue, epoll */
        if (!rev->active && !rev->ready) {//Èç¹û²»»îÔ¾£¬»¹Ã»ÓĞÉèÖÃ½øÈ¥£¬²»ready£¬Ã»ÓĞÊı¾İ¿ÉÒÔ¶Á£¬¾Í¼ÓÈëµ½epoll
            if (ngx_add_event(rev, NGX_READ_EVENT, NGX_CLEAR_EVENT)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }
        return NGX_OK;
    } else if (ngx_event_flags & NGX_USE_LEVEL_EVENT) {
        /* select, poll, /dev/poll */
        if (!rev->active && !rev->ready) {
            if (ngx_add_event(rev, NGX_READ_EVENT, NGX_LEVEL_EVENT)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
            return NGX_OK;
        }
        if (rev->active && (rev->ready || (flags & NGX_CLOSE_EVENT))) {
            if (ngx_del_event(rev, NGX_READ_EVENT, NGX_LEVEL_EVENT | flags)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
            return NGX_OK;
        }
    } else if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT) {
        /* event ports */
        if (!rev->active && !rev->ready) {
            if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }
            return NGX_OK;
        }
        if (rev->oneshot && !rev->ready) {
            if (ngx_del_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }
            return NGX_OK;
        }
    }
    /* aio, iocp, rtsig */
    return NGX_OK;
}


ngx_int_t
ngx_handle_write_event(ngx_event_t *wev, size_t lowat)
{//Ö»ÊÇ×¢²áÁËÒ»ÏÂ¶ÁĞ´ÊÂ¼ş¡£
    ngx_connection_t  *c;

    if (lowat) {
        c = wev->data;

        if (ngx_send_lowat(c, lowat) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {

        /* kqueue, epoll */

        if (!wev->active && !wev->ready) {
            if (ngx_add_event(wev, NGX_WRITE_EVENT,
                              NGX_CLEAR_EVENT | (lowat ? NGX_LOWAT_EVENT : 0))
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }

        return NGX_OK;

    } else if (ngx_event_flags & NGX_USE_LEVEL_EVENT) {

        /* select, poll, /dev/poll */

        if (!wev->active && !wev->ready) {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (wev->active && wev->ready) {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

    } else if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT) {

        /* event ports */

        if (!wev->active && !wev->ready) {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (wev->oneshot && wev->ready) {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }

            return NGX_OK;
        }
    }

    /* aio, iocp, rtsig */

    return NGX_OK;
}


static ngx_int_t ngx_event_module_init(ngx_cycle_t *cycle)
{//ngx_init_cycleº¯ÊıÀïÃæ»á±éÀúµ÷ÓÃËùÓĞÄ£¿éµÄÄ£¿é³õÊ¼»¯º¯Êı¡£ÕâÊÇÔÚÖ÷º¯ÊıÀïÃæµ÷ÓÃµÄ¡£
//·ÖÅäËùĞèµÄ¹²ÏíÄÚ´æ£¬±ÈÈçngx_accept_mutex_ptrµÈ¡£
    void              ***cf;
    u_char              *shared;
    size_t               size, cl;
    ngx_shm_t            shm;
    ngx_time_t          *tp;
    ngx_core_conf_t     *ccf;
    ngx_event_conf_t    *ecf;

    cf = ngx_get_conf(cycle->conf_ctx, ngx_events_module);//µÃµ½ÔÚcreate_confÀïÃæ·ÖÅäµÄÅäÖÃÊı¾İ
    if (cf == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "no \"events\" section in configuration");
        return NGX_ERROR;
    }

    ecf = (*cf)[ngx_event_core_module.ctx_index];//µÃµ½Ö÷Ä£¿éµÄÅäÖÃ£¬ÔÚÆäcreate_conf Ò²¾ÍÊÇngx_event_create_conf·µ»ØµÄÅäÖÃÊı¾İ
    if (!ngx_test_config && ngx_process <= NGX_PROCESS_MASTER) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "using the \"%s\" event method", ecf->name);
    }
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);//µÃµ½×æÊ¦Ò¯µÄÅäÖÃ

    ngx_timer_resolution = ccf->timer_resolution;//ÊÇ·ñÒª¿ªÆô¾«È·Ê±¼äÑ¡Ïî£¬ÖµÊÇ¾«È·¶È

#if !(NGX_WIN32)
    {
    ngx_int_t      limit;
    struct rlimit  rlmt;
    if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,  "getrlimit(RLIMIT_NOFILE) failed, ignored");

    } else {
        if (ecf->connections > (ngx_uint_t) rlmt.rlim_cur && (ccf->rlimit_nofile == NGX_CONF_UNSET
			|| ecf->connections > (ngx_uint_t) ccf->rlimit_nofile)) {
            limit = (ccf->rlimit_nofile == NGX_CONF_UNSET) ? (ngx_int_t) rlmt.rlim_cur : ccf->rlimit_nofile;
            ngx_log_error(NGX_LOG_WARN, cycle->log, 0, "%ui worker_connections are more than open file resource limit: %i", ecf->connections, limit);
        }
    }
	}
#endif /* !(NGX_WIN32) */
    if (ccf->master == 0) {
        return NGX_OK;
    }
    if (ngx_accept_mutex_ptr) {
        return NGX_OK;
    }
    /* cl should be equal or bigger than cache line size */
    cl = 128;
    size = cl            /* ngx_accept_mutex */
           + cl          /* ngx_connection_counter */
           + cl;         /* ngx_temp_number */

#if (NGX_STAT_STUB)
    size += cl           /* ngx_stat_accepted */
           + cl          /* ngx_stat_handled */
           + cl          /* ngx_stat_requests */
           + cl          /* ngx_stat_active */
           + cl          /* ngx_stat_reading */
           + cl;         /* ngx_stat_writing */
#endif
	//ÎªÉÏÊöÊı¾İ½á¹¹·ÖÅä¹²ÏíÄÚ´æ¡£
    shm.size = size;
    shm.name.len = sizeof("nginx_shared_zone");
    shm.name.data = (u_char *) "nginx_shared_zone";
    shm.log = cycle->log;

    if (ngx_shm_alloc(&shm) != NGX_OK) {
        return NGX_ERROR;
    }
    shared = shm.addr;
    ngx_accept_mutex_ptr = (ngx_atomic_t *) shared;
    if (ngx_shmtx_create(&ngx_accept_mutex, shared, cycle->lock_file.data)
        != NGX_OK)
    {
        return NGX_ERROR;
    }
    ngx_connection_counter = (ngx_atomic_t *) (shared + 1 * cl);
    (void) ngx_atomic_cmp_set(ngx_connection_counter, 0, 1);
    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "counter: %p, %d", ngx_connection_counter, *ngx_connection_counter);
    ngx_temp_number = (ngx_atomic_t *) (shared + 2 * cl);
    tp = ngx_timeofday();
    ngx_random_number = (tp->msec << 16) + ngx_pid;
#if (NGX_STAT_STUB)

    ngx_stat_accepted = (ngx_atomic_t *) (shared + 3 * cl);
    ngx_stat_handled = (ngx_atomic_t *) (shared + 4 * cl);
    ngx_stat_requests = (ngx_atomic_t *) (shared + 5 * cl);
    ngx_stat_active = (ngx_atomic_t *) (shared + 6 * cl);
    ngx_stat_reading = (ngx_atomic_t *) (shared + 7 * cl);
    ngx_stat_writing = (ngx_atomic_t *) (shared + 8 * cl);

#endif

    return NGX_OK;
}


#if !(NGX_WIN32)

void
ngx_timer_signal_handler(int signo)
{//¶¨Ê±Æ÷ĞÅºÅ´¦Àíº¯Êı£¬ÔÙ´ÎÉèÖÃºó£¬epoll_waitµÈ´ıºóÈç¹ûÎª1£¬±íÊ¾¶¨Ê±Æ÷µ½ÁË£¬ĞèÒª¸üĞÂÊ±¼ä
    ngx_event_timer_alarm = 1;

#if 1
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ngx_cycle->log, 0, "timer signal");
#endif
}

#endif


static ngx_int_t
ngx_event_process_init(ngx_cycle_t *cycle)
{//½ø³Ì³õÊ¼»¯ºó£¬»áµ÷ÓÃÃ¿¸öÄ£¿éµÄ½ø³Ì³õÊ¼»¯º¯Êı¡£
	//ÉèÖÃ¼àÌıSOCKµÄhandler»Øµ÷º¯Êı£¬Õâ¸öº¯Êı¸ºÔğaccept£¬È»ºó¼ÓÈë¶ÁĞ´ÊÂ¼şµÈ

    ngx_uint_t           m, i;
    ngx_event_t         *rev, *wev;
    ngx_listening_t     *ls;
    ngx_connection_t    *c, *next, *old;
    ngx_core_conf_t     *ccf;
    ngx_event_conf_t    *ecf;
    ngx_event_module_t  *module;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);//µÃµ½×æÊ¦Ò¯µÄÅäÖÃ
    ecf = ngx_event_get_conf(cycle->conf_ctx, ngx_event_core_module);//ÏÈµÃµ½ngx_events_moduleÈİÆ÷µÄÅäÖÃ£¬È»ºóµÃµ½coreÊÂ¼ş½á¹¹µÄÅäÖÃ

    if (ccf->master && ccf->worker_processes > 1 && ecf->accept_mutex) {//Èç¹û½ø³ÌÊı´óÓÚ1ÇÒÅäÖÃÖĞaccept_mutex·Ç0
        ngx_use_accept_mutex = 1;//ÒªÊ¹ÓÃacceptËø
        ngx_accept_mutex_held = 0;
        ngx_accept_mutex_delay = ecf->accept_mutex_delay;

    } else {
        ngx_use_accept_mutex = 0;
    }

#if (NGX_THREADS)
    ngx_posted_events_mutex = ngx_mutex_init(cycle->log, 0);
    if (ngx_posted_events_mutex == NULL) {
        return NGX_ERROR;
    }
#endif
//³õÊ¼»¯ºìºÚÊ÷½á¹¹
    if (ngx_event_timer_init(cycle->log) == NGX_ERROR) {
        return NGX_ERROR;
    }

    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_EVENT_MODULE) {
            continue;
        }
        if (ngx_modules[m]->ctx_index != ecf->use) {//ÕÒµ½ËùÊ¹ÓÃµÄÄÇ¸öÊÂ¼şÄ£ĞÍ¡£
            continue;//ÔÚuseÀïÃæÉèÖÃµÄ
        }
        module = ngx_modules[m]->ctx;//Èç¹ûÊ¹ÓÃÁËngx_epoll_module¡£ÄÇÃ´ÆäctxÎªngx_epoll_module_ctx¡£ÀïÃæ°üº¬ºÜ¶à»Øµ÷¡£
        if (module->actions.init(cycle, ngx_timer_resolution) != NGX_OK) {//µ÷ÓÃngx_epoll_init
            /* fatal */
            exit(2);
        }

        break;
    }

#if !(NGX_WIN32)
//ÅäÖÃÁËngx_timer_resolution²Å»áÉèÖÃ¶¨Ê±Æ÷£¬Ö¸¶¨Ê±¼ä³é·¢£¬ÕâÑùepoll²»ÓÃÉèÖÃ³¬Ê±Ê±¼äÁË£¬ÒòÎª¶¨Ê±Æ÷»á´¥·¢Ëü·µ»ØµÄ
    if (ngx_timer_resolution && !(ngx_event_flags & NGX_USE_TIMER_EVENT)) {
        struct sigaction  sa;
        struct itimerval  itv;

        ngx_memzero(&sa, sizeof(struct sigaction));
        sa.sa_handler = ngx_timer_signal_handler;
        sigemptyset(&sa.sa_mask);
//×¢²á¶¨Ê±Æ÷»Øµ÷º¯ÊıÎªngx_timer_signal_handler
        if (sigaction(SIGALRM, &sa, NULL) == -1) {//×¢ÒâÓÃµÄ²»ÊÇsignal£¬ÕâÑù²»ÓÃÃ¿´Î¶¼ÉèÖÃ£¬²»»á±»ÄÚºËÃ¿´ÎÖØÖÃ
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "sigaction(SIGALRM) failed");
            return NGX_ERROR;
        }

        itv.it_interval.tv_sec = ngx_timer_resolution / 1000;
        itv.it_interval.tv_usec = (ngx_timer_resolution % 1000) * 1000;
        itv.it_value.tv_sec = ngx_timer_resolution / 1000;
        itv.it_value.tv_usec = (ngx_timer_resolution % 1000 ) * 1000;
//ÉèÖÃ¶¨Ê±Æ÷
        if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setitimer() failed");
        }
    }

    if (ngx_event_flags & NGX_USE_FD_EVENT) {
        struct rlimit  rlmt;
        if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno, "getrlimit(RLIMIT_NOFILE) failed");
            return NGX_ERROR;
        }
        cycle->files_n = (ngx_uint_t) rlmt.rlim_cur;
        cycle->files = ngx_calloc(sizeof(ngx_connection_t *) * cycle->files_n,   cycle->log);
        if (cycle->files == NULL) {
            return NGX_ERROR;
        }
    }
#endif
//cycle->connection_nÎªconnectionsÅäÖÃÏîµÄ´óĞ¡
    cycle->connections = ngx_alloc(sizeof(ngx_connection_t) * cycle->connection_n, cycle->log);
    if (cycle->connections == NULL) {
        return NGX_ERROR;
    }

    c = cycle->connections;
//·ÖÅä¶ÁÊÂ¼şÄÚ´æ
    cycle->read_events = ngx_alloc(sizeof(ngx_event_t) * cycle->connection_n, cycle->log);
    if (cycle->read_events == NULL) {
        return NGX_ERROR;
    }

    rev = cycle->read_events;
    for (i = 0; i < cycle->connection_n; i++) {
        rev[i].closed = 1;
        rev[i].instance = 1;
#if (NGX_THREADS)
        rev[i].lock = &c[i].lock;
        rev[i].own_lock = &c[i].lock;
#endif
    }
//·ÖÅäĞ´ÊÂ¼şÄÚ´æ
    cycle->write_events = ngx_alloc(sizeof(ngx_event_t) * cycle->connection_n,cycle->log);
    if (cycle->write_events == NULL) {
        return NGX_ERROR;
    }
    wev = cycle->write_events;
    for (i = 0; i < cycle->connection_n; i++) {
        wev[i].closed = 1;
#if (NGX_THREADS)
        wev[i].lock = &c[i].lock;
        wev[i].own_lock = &c[i].lock;
#endif
    }
    i = cycle->connection_n;
    next = NULL;
    do {
        i--;
        c[i].data = next;//´®ÆğÀ´£¬ĞÎ³ÉÊı×é,´ÓÇ°ÍùºóÖ¸
        c[i].read = &cycle->read_events[i];
        c[i].write = &cycle->write_events[i];
        c[i].fd = (ngx_socket_t) -1;
        next = &c[i];

#if (NGX_THREADS)
        c[i].lock = 0;
#endif
    } while (i);

    cycle->free_connections = next;
    cycle->free_connection_n = cycle->connection_n;

    /* for each listening socket */
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        c = ngx_get_connection(ls[i].fd, cycle->log);
        if (c == NULL) {
            return NGX_ERROR;
        }
        c->log = &ls[i].log;
        c->listening = &ls[i];//Ö¸ÏòÕâ¸ö¼àÌıµÄÁ´½ÓËùÊôµÄlisting½á¹¹
        ls[i].connection = c;//Ö¸ÏòÆäËùÖ¸µÄÁ¬½Ó¡£×¢ÒâÒ»¸ö¼àÌıSOCK¿ÉÄÜÓĞºÜ¶àÁ¬½ÓÖ¸Ïò×Ô¼º£¬¶øËüµÄconnectionÖ»Ö¸ÏòÆäËùÖ¸µÄÄÇ¸öÁ¬½Ó£¬¼´·ÅÈëepollµÄ

        rev = c->read;//Êµ¼ÊÉÏ£¬c[i].read = &cycle->read_events[i];Á¬½Ó½á¹¹ÀïÃæµÄreadÊÂ¼şÖ¸Ïòcycle->read_events¶ÔÓ¦Ïî
//Ò²¾ÍÊÇËµ£¬cycle->read_eventsÊÇÊÂ¼ş³Ø£¬Ò»¸ö³Ø×Ó¡£¸úÁ¬½Ó¶ÔÓ¦µÄ¡£
        rev->log = c->log;
        rev->accept = 1;//Õâ¸öµ±È»ÊÇ¼àÌıfdÁË

#if (NGX_HAVE_DEFERRED_ACCEPT)
        rev->deferred_accept = ls[i].deferred_accept;
#endif
        if (!(ngx_event_flags & NGX_USE_IOCP_EVENT)) {
            if (ls[i].previous) {
                /*
                 * delete the old accept events that were bound to
                 * the old cycle read events array
                 */
                old = ls[i].previous->connection;
                if (ngx_del_event(old->read, NGX_READ_EVENT, NGX_CLOSE_EVENT)  == NGX_ERROR) {
                    return NGX_ERROR;
                }
                old->fd = (ngx_socket_t) -1;
            }
        }
//ÉèÖÃ¼àÌıSOCKµÄÊÂ¼ş»Øµ÷¾ä±ú¡£´Ëº¯Êı¸ºÔğaccept
        rev->handler = ngx_event_accept;//ÉèÖÃ¼àÌıSOCKµÄhandler»Øµ÷º¯Êı£¬Õâ¸öº¯Êı¸ºÔğaccept£¬È»ºó¼ÓÈë¶ÁĞ´ÊÂ¼şµÈ

        if (ngx_use_accept_mutex) {
            continue;//Èç¹ûÓĞngx_use_accept_mutex£¬ÄÇÃ´ÕâÀïÏÈ²»ÓÃ¼Óµ½epoll£¬ÒòÎªÃ¿´ÎÑ­»·£¬»ñµÃËøºó»á¼ÓµÄ£¬»òÕß»áÈ¥µôµÄ¡£
        }

        if (ngx_event_flags & NGX_USE_RTSIG_EVENT) {
            if (ngx_add_conn(c) == NGX_ERROR) {
                return NGX_ERROR;
            }

        } else {//Ä¬ÈÏÏÈ½«Õâ¸öÁ¬½Ó¼Ó½øÈ¥ÔÙËµ£¬´ı»áÔÚÅĞ¶Ï¡£ÒòÎªÃ»ÓĞngx_use_accept_mutex
            if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

#endif

    }

    return NGX_OK;
}


ngx_int_t
ngx_send_lowat(ngx_connection_t *c, size_t lowat)
{
    int  sndlowat;

#if (NGX_HAVE_LOWAT_EVENT)

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
        c->write->available = lowat;
        return NGX_OK;
    }

#endif

    if (lowat == 0 || c->sndlowat) {
        return NGX_OK;
    }

    sndlowat = (int) lowat;

    if (setsockopt(c->fd, SOL_SOCKET, SO_SNDLOWAT,
                   (const void *) &sndlowat, sizeof(int))
        == -1)
    {
        ngx_connection_error(c, ngx_socket_errno,
                             "setsockopt(SO_SNDLOWAT) failed");
        return NGX_ERROR;
    }

    c->sndlowat = 1;

    return NGX_OK;
}


static char *
ngx_events_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//µ±½âÎöµ½events {}¿éµÄÊ±ºò£¬µ÷ÓÃ±¾Ö¸ÁîµÄsetº¯Êı¡£
    char                 *rv;
    void               ***ctx;
    ngx_uint_t            i;
    ngx_conf_t            pcf;
    ngx_event_module_t   *m;

    /* count the number of the event modules and set up their indices *///£¨indexµÄ¸´Êı£©
    ngx_event_max_module = 0;
    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {//±ÈÈçngx_event_core_moduleÕâ¸öÄ£¿é
            continue;
        }//¶ÔNGX_EVENT_MODULEÀàĞÍµÄÄ£¿é½øĞĞ±àºÅ£¬ÉèÖÃĞòºÅ£¬Õâ¸ö±êºÅÎªEVENT_MODULEÀàĞÍµÄÄ£¿éµÄ±àºÅ£¬²»ÊÇÈ«¾ÖµÄ
        ngx_modules[i]->ctx_index = ngx_event_max_module++;
    }

    ctx = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    *ctx = ngx_pcalloc(cf->pool, ngx_event_max_module * sizeof(void *));//·ÖÅäÒ»¸öÉÏÏÂÎÄÖ¸Õë
    if (*ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    *(void **) conf = ctx;//¸øÉÏ²ãÉèÖÃÉÏÏÂÎÄ½á¹¹Ìå

    for (i = 0; ngx_modules[i]; i++) {//±éÀúÃ¿Ò»¸öNGX_EVENT_MODULEÀàĞÍµÄÄ£¿é£¬µ÷ÓÃÆäcreate_conf»Øµ÷
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
            continue;
        }
        m = ngx_modules[i]->ctx;//
        if (m->create_conf) {//ngx_modules[i]->ctx_indexÊÇ¸ÃÄ£¿éÔÚ¸ÃÀàĞÍµÄĞòºÅ¡£
            (*ctx)[ngx_modules[i]->ctx_index] = m->create_conf(cf->cycle);//·µ»ØÖµ¼¸Î»Õâ¸öÅäÖÃ
            if ((*ctx)[ngx_modules[i]->ctx_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }
    pcf = *cf;//ÉèÖÃÒ»ÏÂÉÏÏÂÎÄ£¬½øĞĞµİ¹é½âÎö£¬²¢±£³ÖÕû¸ö½á¹¹ÌåµÄÄÚÈİ¡£
    cf->ctx = ctx;
    cf->module_type = NGX_EVENT_MODULE;
    cf->cmd_type = NGX_EVENT_CONF;
    rv = ngx_conf_parse(cf, NULL);
    *cf = pcf;//»¹Ô­ÉÏÏÂÎÄ
    if (rv != NGX_CONF_OK)
        return rv;

    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
            continue;
        }
        m = ngx_modules[i]->ctx;
        if (m->init_conf) {//½øĞĞÄ£¿éµÄ³õÊ¼»¯¡£´ËÊ±ÒÑ¾­¼ÓÔØÍêÅäÖÃÁË
            rv = m->init_conf(cf->cycle, (*ctx)[ngx_modules[i]->ctx_index]);
            if (rv != NGX_CONF_OK) {
                return rv;
            }
        }
    }

    return NGX_CONF_OK;
}


static char *
ngx_event_connections(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//×î´óÁ¬½ÓÊı
    ngx_event_conf_t  *ecf = conf;

    ngx_str_t  *value;

    if (ecf->connections != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }
    if (ngx_strcmp(cmd->name.data, "connections") == 0) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "the \"connections\" directive is deprecated, use the \"worker_connections\" directive instead");
    }

    value = cf->args->elts;//²ÎÊıÊı×é
    ecf->connections = ngx_atoi(value[1].data, value[1].len);//µÚÒ»¸ö²ÎÊıÓ¦¸ÃÊÇÕûÊı£¬ÉèÖÃµ½eventÄ£¿éµÄ±äÁ¿ÖĞ¡£
    if (ecf->connections == (ngx_uint_t) NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "invalid number \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }
    cf->cycle->connection_n = ecf->connections;//Í¬Ê±ÉèÖÃ¸øcycleµÄconnection_n
    return NGX_CONF_OK;
}


static char *
ngx_event_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//É¨Ò»±éËùÓÃµÄÄ£¿é£¬¸ù¾İÃû×ÖÕÒµ½Ä£¿é¡£È»ºóÉèÖÃÉÏÈ¥¡£
    ngx_event_conf_t  *ecf = conf;

    ngx_int_t             m;
    ngx_str_t            *value;
    ngx_event_conf_t     *old_ecf;
    ngx_event_module_t   *module;

    if (ecf->use != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    value = cf->args->elts;
    if (cf->cycle->old_cycle->conf_ctx) {
        old_ecf = ngx_event_get_conf(cf->cycle->old_cycle->conf_ctx, ngx_event_core_module);
    } else {
        old_ecf = NULL;
    }

    for (m = 0; ngx_modules[m]; m++) {
        if (ngx_modules[m]->type != NGX_EVENT_MODULE) {
            continue;
        }
        module = ngx_modules[m]->ctx;//ÕÒµ½ËùÒªÉèÖÃµÄÄ£¿é¡£
        if (module->name->len == value[1].len) {
            if (ngx_strcmp(module->name->data, value[1].data) == 0) {
                ecf->use = ngx_modules[m]->ctx_index;//ÉèÖÃÎª¸ÃÄ£¿éÔÚËùÊôÀàĞÍµÄÏÂ±ê
                ecf->name = module->name->data;//Ãû×Ö

                if (ngx_process == NGX_PROCESS_SINGLE && old_ecf  && old_ecf->use != ecf->use)  {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "when the server runs without a master process "
                               "the \"%V\" event type must be the same as "
                               "in previous configuration - \"%s\" "
                               "and it can not be changed on the fly, "
                               "to change it you need to stop server "
                               "and start it again",
                               &value[1], old_ecf->name);
                    return NGX_CONF_ERROR;
                }
                return NGX_CONF_OK;
            }
        }
    }
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid event type \"%V\"", &value[1]);
    return NGX_CONF_ERROR;
}


static char *
ngx_event_debug_connection(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_DEBUG)
    ngx_event_conf_t  *ecf = conf;

    ngx_int_t           rc;
    ngx_str_t          *value;
    ngx_event_debug_t  *dc;
    struct hostent     *h;
    ngx_cidr_t          cidr;

    value = cf->args->elts;

    dc = ngx_array_push(&ecf->debug_connection);
    if (dc == NULL) {
        return NGX_CONF_ERROR;
    }

    rc = ngx_ptocidr(&value[1], &cidr);

    if (rc == NGX_DONE) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "low address bits of %V are meaningless", &value[1]);
        rc = NGX_OK;
    }

    if (rc == NGX_OK) {

        /* AF_INET only */

        if (cidr.family != AF_INET) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "\"debug_connection\" supports IPv4 only");
            return NGX_CONF_ERROR;
        }

        dc->mask = cidr.u.in.mask;
        dc->addr = cidr.u.in.addr;

        return NGX_CONF_OK;
    }

    h = gethostbyname((char *) value[1].data);

    if (h == NULL || h->h_addr_list[0] == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "host \"%s\" not found", value[1].data);
        return NGX_CONF_ERROR;
    }

    dc->mask = 0xffffffff;
    dc->addr = *(in_addr_t *)(h->h_addr_list[0]);

#else

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"debug_connection\" is ignored, you need to rebuild "
                       "nginx using --with-debug option to enable it");

#endif

    return NGX_CONF_OK;
}


static void *
ngx_event_create_conf(ngx_cycle_t *cycle)
{//Åöµ½events{}µÄÊ±ºò£¬»áÔ¤ÏÈµ÷ÓÃÕâÀï¡£
    ngx_event_conf_t  *ecf;
    ecf = ngx_palloc(cycle->pool, sizeof(ngx_event_conf_t));
    if (ecf == NULL) {
        return NULL;
    }
	//·ÖÅäºÃÁËÅäÖÃµÄÄÚ´æ£¬½øĞĞ³õÊ¼»¯
    ecf->connections = NGX_CONF_UNSET_UINT;
    ecf->use = NGX_CONF_UNSET_UINT;
    ecf->multi_accept = NGX_CONF_UNSET;
    ecf->accept_mutex = NGX_CONF_UNSET;
    ecf->accept_mutex_delay = NGX_CONF_UNSET_MSEC;
    ecf->name = (void *) NGX_CONF_UNSET;
#if (NGX_DEBUG)
    if (ngx_array_init(&ecf->debug_connection, cycle->pool, 4, sizeof(ngx_event_debug_t)) == NGX_ERROR)  {
        return NULL;
    }
#endif
    return ecf;//·µ»ØÕâ¶ÎÄÚ´æ¸øÉÏ²ã¡£ÉÏ²ã»áÉèÖÃµ½ÆäctxÊı×éµÄ
}


static char *
ngx_event_init_conf(ngx_cycle_t *cycle, void *conf)
{//³õÊ¼»¯Ä£¿é£¬´ËÊ±ÒÑ¾­¼ÓÔØÁËÏà¹ØµÄÅäÖÃÁË¡£ÕâÀïÖ»ÊÇÉèÖÃÁËÒ»Ğ©³õÊ¼Öµ
    ngx_event_conf_t  *ecf = conf;//µÃµ½ÎÒÔÚngx_event_create_confÀïÃæÉèÖÃµÄÅäÖÃ½á¹¹
#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)
    int                  fd;
#endif
#if (NGX_HAVE_RTSIG)
    ngx_uint_t           rtsig;
    ngx_core_conf_t     *ccf;
#endif
    ngx_int_t            i;
    ngx_module_t        *module;
    ngx_event_module_t  *event_module;

//ÏÂÃæÅĞ¶ÏÓ¦¸ÃÊ¹ÓÃÄÄÒ»¸öÍøÂçÄ£¿é¡£ÉèÖÃÔÚmodule±äÁ¿ÉÏ¡£
    module = NULL;
#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)
    fd = epoll_create(100);
    if (fd != -1) {//¹ûÈ»ÓĞepoll£¬ÓÃÖ®
        close(fd);
        module = &ngx_epoll_module;//ÅäÖÃÁËÒªÓÃepoll£¬¾ÍÓÃepoll
    } else if (ngx_errno != NGX_ENOSYS) {
        module = &ngx_epoll_module;
    }
#endif
#if (NGX_HAVE_RTSIG)
    if (module == NULL) {
        module = &ngx_rtsig_module;
        rtsig = 1;
    } else {
        rtsig = 0;
    }
#endif
#if (NGX_HAVE_DEVPOLL)
    module = &ngx_devpoll_module;
#endif
#if (NGX_HAVE_KQUEUE)
    module = &ngx_kqueue_module;
#endif
#if (NGX_HAVE_SELECT)
    if (module == NULL) {
        module = &ngx_select_module;
    }
#endif
//µÃµ½ÁËËùÓ¦¸ÃÊ¹ÓÃµÄÄ£¿é
    if (module == NULL) {//Èç¹ûÃ»ÓĞµÃµ½¡£¾ÍÕÒµÚÒ»¸öÃû×Ö²»µÈÓÚevent_core_nameµÄÄ£¿é
        for (i = 0; ngx_modules[i]; i++) {
            if (ngx_modules[i]->type != NGX_EVENT_MODULE) {
                continue;
            }
            event_module = ngx_modules[i]->ctx;
            if (ngx_strcmp(event_module->name->data, event_core_name.data) == 0) {
                continue;//ÕâÀïÊÇÈç¹ûµÈÓÚevent_core£¬¾Í¼ÌĞø£¬ÄÇÃ´²»µÈÓÚ¾ÍOKÁË£¬¾ÍÊÇËµÕÒÒ»¸ö¾ÍĞĞ¿
            }
            module = ngx_modules[i];
            break;
        }
    }
    if (module == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "no events module found");
        return NGX_CONF_ERROR;
    }
	
    ngx_conf_init_uint_value(ecf->connections, DEFAULT_CONNECTIONS);//Èç¹ûÃ»ÓĞÉèÖÃÖµ£¬¾ÍÉèÖÃÎªºóÃæµÄÄ¬ÈÏÖµ
    cycle->connection_n = ecf->connections;
    ngx_conf_init_uint_value(ecf->use, module->ctx_index);
    event_module = module->ctx;//µÃµ½¸ÃÄ£¿éµÄÉÏÏÂÎÄ£¬±ÈÈçngx_event_module_t ngx_event_core_module_ctx 
    ngx_conf_init_ptr_value(ecf->name, event_module->name->data);

    ngx_conf_init_value(ecf->multi_accept, 0);
    ngx_conf_init_value(ecf->accept_mutex, 1);
    ngx_conf_init_msec_value(ecf->accept_mutex_delay, 500);


#if (NGX_HAVE_RTSIG)
    if (!rtsig) {
        return NGX_CONF_OK;
    }
    if (ecf->accept_mutex) {
        return NGX_CONF_OK;
    }
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    if (ccf->worker_processes == 0) {
        return NGX_CONF_OK;
    }
    ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,   "the \"rtsig\" method requires \"accept_mutex\" to be on");
    return NGX_CONF_ERROR;
#else
    return NGX_CONF_OK;

#endif
}
