
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_pipe.h>


static ngx_int_t ngx_event_pipe_read_upstream(ngx_event_pipe_t *p);
static ngx_int_t ngx_event_pipe_write_to_downstream(ngx_event_pipe_t *p);

static ngx_int_t ngx_event_pipe_write_chain_to_temp_file(ngx_event_pipe_t *p);
static ngx_inline void ngx_event_pipe_remove_shadow_links(ngx_buf_t *buf);
static ngx_inline void ngx_event_pipe_free_shadow_raw_buf(ngx_chain_t **free,
                                                          ngx_buf_t *buf);
static ngx_int_t ngx_event_pipe_drain_chains(ngx_event_pipe_t *p);


ngx_int_t ngx_event_pipe(ngx_event_pipe_t *p, ngx_int_t do_write)
{//ÔÚÓÐbufferingµÄÊ±ºò£¬Ê¹ÓÃevent_pipe½øÐÐÊý¾ÝµÄ×ª·¢£¬µ÷ÓÃngx_event_pipe_write_to*º¯Êý¶ÁÈ¡Êý¾Ý£¬»òÕß·¢ËÍÊý¾Ý¸ø¿Í»§¶Ë¡£
//ngx_event_pipe½«upstreamÏìÓ¦·¢ËÍ»Ø¿Í»§¶Ë¡£do_write´ú±íÊÇ·ñÒªÍù¿Í»§¶Ë·¢ËÍ£¬Ð´Êý¾Ý¡£
//Èç¹ûÉèÖÃÁË£¬ÄÇÃ´»áÏÈ·¢¸ø¿Í»§¶Ë£¬ÔÙ¶ÁupstreamÊý¾Ý£¬µ±È»£¬Èç¹û¶ÁÈ¡ÁËÊý¾Ý£¬Ò²»áµ÷ÓÃÕâÀïµÄ¡£
    u_int         flags;
    ngx_int_t     rc;
    ngx_event_t  *rev, *wev;

//Õâ¸öforÑ­»·ÊÇ²»¶ÏµÄÓÃngx_event_pipe_read_upstream¶ÁÈ¡¿Í»§¶ËÊý¾Ý£¬È»ºóµ÷ÓÃngx_event_pipe_write_to_downstream
    for ( ;; ) {
        if (do_write) {
            p->log->action = "sending to client";
            rc = ngx_event_pipe_write_to_downstream(p);
            if (rc == NGX_ABORT) {
                return NGX_ABORT;
            }
            if (rc == NGX_BUSY) {
                return NGX_OK;
            }
        }
        p->read = 0;
        p->upstream_blocked = 0;
        p->log->action = "reading upstream";
		//´Óupstream¶ÁÈ¡Êý¾Ýµ½chainµÄÁ´±íÀïÃæ£¬È»ºóÕû¿éÕû¿éµÄµ÷ÓÃinput_filter½øÐÐÐ­ÒéµÄ½âÎö£¬²¢½«HTTP½á¹û´æ·ÅÔÚp->in£¬p->last_inµÄÁ´±íÀïÃæ¡£
        if (ngx_event_pipe_read_upstream(p) == NGX_ABORT) {
            return NGX_ABORT;
        }
		//upstream_blockedÊÇÔÚngx_event_pipe_read_upstreamÀïÃæÉèÖÃµÄ±äÁ¿,´ú±íÊÇ·ñÓÐÊý¾ÝÒÑ¾­´Óupstream¶ÁÈ¡ÁË¡£
        if (!p->read && !p->upstream_blocked) {
            break;
        }
        do_write = 1;//»¹ÒªÐ´¡£ÒòÎªÎÒÕâ´Î¶Áµ½ÁËÒ»Ð©Êý¾Ý
    }
	
//ÏÂÃæÊÇ´¦ÀíÊÇ·ñÐèÒªÉèÖÃ¶¨Ê±Æ÷£¬»òÕßÉ¾³ý¶ÁÐ´ÊÂ¼þµÄepoll¡£
    if (p->upstream->fd != -1) {//Èç¹ûºó¶ËphpµÈµÄÁ¬½ÓfdÊÇÓÐÐ§µÄ£¬Ôò×¢²á¶ÁÐ´ÊÂ¼þ¡£
        rev = p->upstream->read;//µÃµ½Õâ¸öÁ¬½ÓµÄ¶ÁÐ´ÊÂ¼þ½á¹¹£¬Èç¹ûÆä·¢ÉúÁË´íÎó£¬ÄÇÃ´½«Æä¶ÁÐ´ÊÂ¼þ×¢²áÉ¾³ýµô£¬·ñÔò±£´æÔ­Ñù¡£
        flags = (rev->eof || rev->error) ? NGX_CLOSE_EVENT : 0;
        if (ngx_handle_read_event(rev, flags) != NGX_OK) {
            return NGX_ABORT;//¿´¿´ÊÇ·ñÐèÒª½«Õâ¸öÁ¬½ÓÉ¾³ý¶ÁÐ´ÊÂ¼þ×¢²á¡£
        }
        if (rev->active && !rev->ready) {//Ã»ÓÐ¶ÁÐ´Êý¾ÝÁË£¬ÄÇ¾ÍÉèÖÃÒ»¸ö¶Á³¬Ê±¶¨Ê±Æ÷
            ngx_add_timer(rev, p->read_timeout);
        } else if (rev->timer_set) {
            ngx_del_timer(rev);
        }
    }
    if (p->downstream->fd != -1 && p->downstream->data == p->output_ctx) {
        wev = p->downstream->write;//¶Ô¿Í»§¶ËµÄÁ¬½Ó£¬×¢²á¿ÉÐ´ÊÂ¼þ¡£¹ØÐÄ¿ÉÐ´
        if (ngx_handle_write_event(wev, p->send_lowat) != NGX_OK) {
            return NGX_ABORT;
        }
        if (!wev->delayed) {
            if (wev->active && !wev->ready) {//Í¬Ñù£¬×¢²áÒ»ÏÂ³¬Ê±¡£
                ngx_add_timer(wev, p->send_timeout);
            } else if (wev->timer_set) {
                ngx_del_timer(wev);
            }
        }
    }
    return NGX_OK;
}
/*
1.´Ópreread_bufs£¬free_raw_bufs»òÕßngx_create_temp_bufÑ°ÕÒÒ»¿é¿ÕÏÐµÄ»ò²¿·Ö¿ÕÏÐµÄÄÚ´æ£»
2.µ÷ÓÃp->upstream->recv_chain==ngx_readv_chain£¬ÓÃwritevµÄ·½Ê½¶ÁÈ¡FCGIµÄÊý¾Ý,Ìî³ächain¡£
3.¶ÔÓÚÕû¿ébuf¶¼ÂúÁËµÄchain½Úµãµ÷ÓÃinput_filter(ngx_http_fastcgi_input_filter)½øÐÐupstreamÐ­Òé½âÎö£¬±ÈÈçFCGIÐ­Òé£¬½âÎöºóµÄ½á¹û·ÅÈëp->inÀïÃæ£»
4.¶ÔÓÚÃ»ÓÐÌî³äÂúµÄbuffer½Úµã£¬·ÅÈëfree_raw_bufsÒÔ´ýÏÂ´Î½øÈëÊ±´ÓºóÃæ½øÐÐ×·¼Ó¡£
5.µ±È»ÁË£¬Èç¹û¶Ô¶Ë·¢ËÍÍêÊý¾ÝFINÁË£¬ÄÇ¾ÍÖ±½Óµ÷ÓÃinput_filter´¦Àífree_raw_bufsÕâ¿éÊý¾Ý¡£
*/
static ngx_int_t ngx_event_pipe_read_upstream(ngx_event_pipe_t *p)
{//ngx_event_pipeµ÷ÓÃÕâÀï¶ÁÈ¡ºó¶ËµÄÊý¾Ý¡£
    ssize_t       n, size;
    ngx_int_t     rc;
    ngx_buf_t    *b;
    ngx_chain_t  *chain, *cl, *ln;

    if (p->upstream_eof || p->upstream_error || p->upstream_done) {
        return NGX_OK;
    }
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe read upstream: %d", p->upstream->read->ready);

    for ( ;; ) {
        if (p->upstream_eof || p->upstream_error || p->upstream_done) {
            break;//×´Ì¬ÅÐ¶Ï¡£
        }
		//Èç¹ûÃ»ÓÐÔ¤¶ÁÊý¾Ý£¬²¢ÇÒ¸úupstreamµÄÁ¬½Ó»¹Ã»ÓÐread£¬ÄÇ¾Í¿ÉÒÔÍË³öÁË£¬ÒòÎªÃ»Êý¾Ý¿É¶Á¡£
        if (p->preread_bufs == NULL && !p->upstream->read->ready) {
            break;
        }
		//ÏÂÃæÕâ¸ö´óµÄif-else¾Í¸ÉÒ»¼þÊÂÇé: Ñ°ÕÒÒ»¿é¿ÕÏÐµÄÄÚ´æ»º³åÇø£¬ÓÃÀ´´ý»á´æ·Å¶ÁÈ¡½øÀ´µÄupstreamµÄÊý¾Ý¡£
		//Èç¹ûpreread_bufs²»Îª¿Õ£¬¾ÍÏÈÓÃÖ®£¬·ñÔò¿´¿´free_raw_bufsÓÐÃ»ÓÐ£¬»òÕßÉêÇëÒ»¿é
        if (p->preread_bufs) {//Èç¹ûÔ¤¶ÁÊý¾ÝÓÐµÄ»°£¬±ÈÈçµÚÒ»´Î½øÀ´£¬Á¬½ÓÉÐÎ´¿É¶Á£¬µ«ÊÇÖ®Ç°¶Áµ½ÁËÒ»²¿·Öbody¡£ÄÇ¾ÍÏÈ´¦ÀíÍêÕâ¸öbodyÔÙ½øÐÐ¶ÁÈ¡¡£
            /* use the pre-read bufs if they exist */
            chain = p->preread_bufs;//ÄÇ¾Í½«Õâ¸ö¿éµÄÊý¾ÝÁ´½ÓÆðÀ´,´ý»áÓÃÀ´´æ·Å¶ÁÈëµÄÊý¾Ý¡£²¢Çå¿Õpreread_bufs,
            p->preread_bufs = NULL;
            n = p->preread_size;
            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0,  "pipe preread: %z", n);
            if (n) {
                p->read = 1;//¶ÁÁËÊý¾Ý¡£
            }
        } else {//·ñÔò£¬preread_bufsÎª¿Õ£¬Ã»ÓÐÁË¡£
#if (NGX_HAVE_KQUEUE)//ÖÐ¼äÉ¾³ýÁË¡£²»¿¼ÂÇ¡£
#endif
            if (p->free_raw_bufs) {
                /* use the free bufs if they exist */
                chain = p->free_raw_bufs;
                if (p->single_buf) {//Èç¹ûÉèÖÃÁËNGX_USE_AIO_EVENT±êÖ¾£¬ the posted aio operation may currupt a shadow buffer
                    p->free_raw_bufs = p->free_raw_bufs->next;
                    chain->next = NULL;
                } else {//Èç¹û²»ÊÇAIO£¬ÄÇÃ´¿ÉÒÔÓÃ¶à¿éÄÚ´æÒ»´ÎÓÃreadv¶ÁÈ¡µÄ¡£
                    p->free_raw_bufs = NULL;
                }
            } else if (p->allocated < p->bufs.num) {
            //Èç¹ûÃ»ÓÐ³¬¹ýfastcgi_buffersµÈÖ¸ÁîµÄÏÞÖÆ£¬ÄÇÃ´ÉêÇëÒ»¿éÄÚ´æ°É¡£ÒòÎªÏÖÔÚÃ»ÓÐ¿ÕÏÐÄÚ´æÁË¡£
                /* allocate a new buf if it's still allowed */
			//ÉêÇëÒ»¸öngx_buf_tÒÔ¼°size´óÐ¡µÄÊý¾Ý¡£ÓÃÀ´´æ´¢´ÓFCGI¶ÁÈ¡µÄÊý¾Ý¡£
                b = ngx_create_temp_buf(p->pool, p->bufs.size);
                if (b == NULL) {
                    return NGX_ABORT;
                }
                p->allocated++;
                chain = ngx_alloc_chain_link(p->pool);//ÉêÇëÒ»¸öÁ´±í½á¹¹£¬Ö¸Ïò¸ÕÉêÇëµÄÄÇÛçbuf,Õâ¸öbuf ±È½Ï´óµÄ¡£¼¸Ê®KÒÔÉÏ¡£
                if (chain == NULL) {
                    return NGX_ABORT;
                }
                chain->buf = b;
                chain->next = NULL;
            } else if (!p->cacheable && p->downstream->data == p->output_ctx && p->downstream->write->ready && !p->downstream->write->delayed) {
			//µ½ÕâÀï£¬ÄÇËµÃ÷Ã»·¨ÉêÇëÄÚ´æÁË£¬µ«ÊÇÅäÖÃÀïÃæÃ»ÒªÇó±ØÐëÏÈ±£ÁôÔÚcacheÀï£¬ÄÇÎÒÃÇ¿ÉÒÔ°Éµ±Ç°µÄÊý¾Ý·¢ËÍ¸ø¿Í»§¶ËÁË¡£Ìø³öÑ­»·¡£
                /*
                 * if the bufs are not needed to be saved in a cache and
                 * a downstream is ready then write the bufs to a downstream
                 */
                p->upstream_blocked = 1;//±ê¼ÇÒÑ¾­¶ÁÈ¡ÁËÊý¾Ý£¬¿ÉÒÔwriteÁË¡£
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe downstream ready");
                break;
            } else if (p->cacheable || p->temp_file->offset < p->max_temp_file_size)
            {//±ØÐë»º´æ£¬¶øÇÒµ±Ç°µÄ»º´æÎÄ¼þµÄÎ»ÒÆ£¬¾ÍÊÇ´óÐ¡Ð¡ÓÚ¿ÉÔÊÐíµÄ´óÐ¡£¬ÄÇgood£¬¿ÉÒÔÐ´ÈëÎÄ¼þÁË¡£
                /*
                 * if it is allowed, then save some bufs from r->in
                 * to a temporary file, and add them to a r->out chain
                 */
//ÏÂÃæ½«r->inµÄÊý¾ÝÐ´µ½ÁÙÊ±ÎÄ¼þ
                rc = ngx_event_pipe_write_chain_to_temp_file(p);
                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0,  "pipe temp offset: %O", p->temp_file->offset);
                if (rc == NGX_BUSY) {
                    break;
                }
                if (rc == NGX_AGAIN) {
                    if (ngx_event_flags & NGX_USE_LEVEL_EVENT && p->upstream->read->active && p->upstream->read->ready){
                        if (ngx_del_event(p->upstream->read, NGX_READ_EVENT, 0) == NGX_ERROR) {
                            return NGX_ABORT;
                        }
                    }
                }
                if (rc != NGX_OK) {
                    return rc;
                }
                chain = p->free_raw_bufs;
                if (p->single_buf) {
                    p->free_raw_bufs = p->free_raw_bufs->next;
                    chain->next = NULL;
                } else {
                    p->free_raw_bufs = NULL;
                }
            } else {//Ã»°ì·¨ÁË¡£
                /* there are no bufs to read in */
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0,  "no pipe bufs to read in");
                break;
            }
			//µ½ÕâÀï£¬¿Ï¶¨ÊÇÕÒµ½¿ÕÏÐµÄbufÁË£¬chainÖ¸ÏòÖ®ÁË¡£ÏÈË¯¾õ£¬µçÄÔÃ»µçÁË¡£
			//ngx_readv_chain .µ÷ÓÃreadv²»¶ÏµÄ¶ÁÈ¡Á¬½ÓµÄÊý¾Ý¡£·ÅÈëchainµÄÁ´±íÀïÃæ
			//ÕâÀïµÄchainÊÇ²»ÊÇÖ»ÓÐÒ»¿é? Æänext³ÉÔ±Îª¿ÕÄØ£¬²»Ò»¶¨£¬Èç¹ûfree_raw_bufs²»Îª¿Õ£¬
			//ÉÏÃæµÄ»ñÈ¡¿ÕÏÐbufÖ»ÒªÃ»ÓÐÊ¹ÓÃAIOµÄ»°£¬¾Í¿ÉÄÜÓÐ¶à¸öbufferÁ´±íµÄ¡£
            n = p->upstream->recv_chain(p->upstream, chain);

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe recv chain: %z", n);
            if (p->free_raw_bufs) {//free_raw_bufs²»Îª¿Õ£¬ÄÇ¾Í½«chainÖ¸ÏòµÄÕâ¿é·Åµ½free_raw_bufsÍ·²¿¡£
                chain->next = p->free_raw_bufs;
            }
            p->free_raw_bufs = chain;//·ÅÈëÍ·²¿
            if (n == NGX_ERROR) {
                p->upstream_error = 1;
                return NGX_ERROR;
            }
            if (n == NGX_AGAIN) {
                if (p->single_buf) {
                    ngx_event_pipe_remove_shadow_links(chain->buf);
                }
                break;
            }
            p->read = 1;
            if (n == 0) {
                p->upstream_eof = 1;//Ã»ÓÐ¶Áµ½Êý¾Ý£¬¿Ï¶¨upstream·¢ËÍÁËFIN°ü£¬ÄÇ¾Í¶ÁÈ¡Íê³ÉÁË¡£
                break;
            }
        }//´ÓÉÏÃæforÑ­»·¸Õ¿ªÊ¼µÄif (p->preread_bufs) {µ½ÕâÀï£¬¶¼ÔÚÑ°ÕÒÒ»¸ö¿ÕÏÐµÄ»º³åÇø£¬È»ºó¶ÁÈ¡Êý¾ÝÌî³ächain¡£¹»³¤µÄ¡£
//¶ÁÈ¡ÁËÊý¾Ý£¬ÏÂÃæÒª½øÐÐFCGIÐ­Òé½âÎö£¬±£´æÁË¡£
        p->read_length += n;
        cl = chain;//chainÒÑ¾­ÊÇÁ´±íµÄÍ·²¿ÁË£¬µÈÓÚfree_raw_bufsËùÒÔÏÂÃæ¿ÉÒÔÖÃ¿ÕÏÈ¡£
        p->free_raw_bufs = NULL;

        while (cl && n > 0) {//Èç¹û»¹ÓÐÁ´±íÊý¾Ý²¢ÇÒ³¤¶È²»Îª0£¬Ò²¾ÍÊÇÕâ´ÎµÄ»¹Ã»ÓÐ´¦ÀíÍê¡£ÄÇÈç¹ûÖ®Ç°±£ÁôÓÐÒ»²¿·ÖÊý¾ÝÄØ?
        //²»»áµÄ£¬Èç¹ûÖ®Ç°Ô¤¶ÁÁËÊý¾Ý£¬ÄÇÃ´ÉÏÃæµÄ´óifÓï¾äelseÀïÃæ½ø²»È¥£¬¾ÍÊÇ´ËÊ±µÄn¿Ï¶¨µÈÓÚpreread_bufsµÄ³¤¶Èpreread_size¡£
        //Èç¹ûÖ®Ç°Ã»ÓÐÔ¤¶ÁÊý¾Ý£¬µ«free_raw_bufs²»Îª¿Õ£¬ÄÇÒ²Ã»¹ØÏµ£¬free_raw_bufsÀïÃæµÄÊý¾Ý¿Ï¶¨ÒÑ¾­ÔÚÏÂÃæ¼¸ÐÐ´¦Àí¹ýÁË¡£

		//ÏÂÃæµÄº¯Êý½«c->bufÖÐÓÃshadowÖ¸ÕëÁ¬½ÓÆðÀ´µÄÁ´±íÖÐËùÓÐ½ÚµãµÄrecycled,temporary,shadow³ÉÔ±ÖÃ¿Õ¡£
            ngx_event_pipe_remove_shadow_links(cl->buf);

            size = cl->buf->end - cl->buf->last;
            if (n >= size) {
                cl->buf->last = cl->buf->end;//°ÑÕâÛçÈ«²¿ÓÃÁË,readvÌî³äÁËÊý¾Ý¡£
                /* STUB */ cl->buf->num = p->num++;//µÚ¼¸¿é
				//FCGIÎªngx_http_fastcgi_input_filter£¬ÆäËûÎªngx_event_pipe_copy_input_filter ¡£ÓÃÀ´½âÎöÌØ¶¨¸ñÊ½Êý¾Ý
                if (p->input_filter(p, cl->buf) == NGX_ERROR) {//Õû¿ébufferµÄµ÷ÓÃÐ­Òé½âÎö¾ä±ú
                //ÕâÀïÃæ£¬Èç¹ûcl->bufÕâ¿éÊý¾Ý½âÎö³öÀ´ÁËDATAÊý¾Ý£¬ÄÇÃ´cl->buf->shadow³ÉÔ±Ö¸ÏòÒ»¸öÁ´±í£¬
                //Í¨¹ýshadow³ÉÔ±Á´½ÓÆðÀ´µÄÁ´±í£¬Ã¿¸ö³ÉÔ±¾ÍÊÇÁãÉ¢µÄfcgi dataÊý¾Ý²¿·Ö¡£
                    return NGX_ABORT;
                }
                n -= size;
                ln = cl;
                cl = cl->next;//¼ÌÐø´¦ÀíÏÂÒ»¿é£¬²¢ÊÍ·ÅÕâ¸ö½Úµã¡£
                ngx_free_chain(p->pool, ln);

            } else {//Èç¹ûÕâ¸ö½ÚµãµÄ¿ÕÏÐÄÚ´æÊýÄ¿´óÓÚÊ£ÏÂÒª´¦ÀíµÄ£¬¾Í½«Ê£ÏÂµÄ´æ·ÅÔÚÕâÀï¡£
                cl->buf->last += n;//É¶ÒâË¼£¬²»ÓÃµ÷ÓÃinput_filterÁËÂð£¬²»ÊÇ¡£ÊÇÕâÑùµÄ£¬Èç¹ûÊ£ÏÂµÄÕâ¿éÊý¾Ý»¹²»¹»ÈûÂúµ±Ç°Õâ¸öclµÄ»º´æ´óÐ¡£¬
                n = 0;//ÄÇ¾ÍÏÈ´æÆðÀ´£¬ÔõÃ´´æÄØ: ±ðÊÍ·ÅclÁË£¬Ö»ÊÇÒÆ¶¯Æä´óÐ¡£¬È»ºón=0Ê¹Ñ­»·ÍË³ö¡£È»ºóÔÚÏÂÃæ¼¸ÐÐµÄif (cl) {ÀïÃæ¿ÉÒÔ¼ì²âµ½ÕâÖÖÇé¿ö
 //ÓÚÊÇÔÚÏÂÃæµÄifÀïÃæ»á½«Õâ¸öln´¦µÄÊý¾Ý·ÅÈëfree_raw_bufsµÄÍ·²¿¡£²»¹ýÕâÀï»áÓÐ¶à¸öÁ¬½ÓÂð? ¿ÉÄÜÓÐµÄ¡£
            }
        }

        if (cl) {
	//½«ÉÏÃæÃ»ÓÐÌîÂúÒ»¿éÄÚ´æ¿éµÄÊý¾ÝÁ´½Ó·Åµ½free_raw_bufsµÄÇ°Ãæ¡£×¢ÒâÉÏÃæÐÞ¸ÄÁËcl->buf->last£¬ºóÐøµÄ¶ÁÈëÊý¾Ý²»»á¸²¸ÇÕâÐ©Êý¾ÝµÄ¡£¿´ngx_readv_chain
            for (ln = cl; ln->next; ln = ln->next) { /* void */ }
            ln->next = p->free_raw_bufs;//Õâ¸ö²»ÊÇNULLÂð£¬ÉÏÃæ³õÊ¼»¯µÄ£¬²»¶Ô£¬ÒòÎªinput_filter¿ÉÄÜ»á½«ÄÇÐ©Ã»ÓÃdata²¿·ÖµÄfcgiÊý¾Ý°ü¿é·ÅÈëfree_raw_bufsÖ±½Ó½øÐÐ¸´ÓÃ¡£
            p->free_raw_bufs = cl;//ÕâÑùÔÚÏÂÒ»´ÎÑ­»·µÄÊ±ºò£¬Ò²¾ÍÊÇÉÏÃæ£¬»áÊ¹ÓÃfree_raw_bufsµÄ¡£
            //²¢ÇÒ£¬Èç¹ûÑ­»·½áÊøÁË£¬»áÔÚÏÂÃæÔÙ´¦ÀíÒ»ÏÂÕâ¸öÎ²²¿Ã»ÓÐÌîÂúÕû¸ö¿éµÄÊý¾Ý¡£
        }
    }//forÑ­»·½áÊø¡£

#if (NGX_DEBUG)
    for (cl = p->busy; cl; cl = cl->next) {
        ngx_log_debug8(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "pipe buf busy s:%d t:%d f:%d "
                       "%p, pos %p, size: %z "
                       "file: %O, size: %z",
                       (cl->buf->shadow ? 1 : 0),
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

    for (cl = p->out; cl; cl = cl->next) {
        ngx_log_debug8(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "pipe buf out  s:%d t:%d f:%d "
                       "%p, pos %p, size: %z "
                       "file: %O, size: %z",
                       (cl->buf->shadow ? 1 : 0),
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

    for (cl = p->in; cl; cl = cl->next) {
        ngx_log_debug8(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "pipe buf in   s:%d t:%d f:%d "
                       "%p, pos %p, size: %z "
                       "file: %O, size: %z",
                       (cl->buf->shadow ? 1 : 0),
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

    for (cl = p->free_raw_bufs; cl; cl = cl->next) {
        ngx_log_debug8(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "pipe buf free s:%d t:%d f:%d "
                       "%p, pos %p, size: %z "
                       "file: %O, size: %z",
                       (cl->buf->shadow ? 1 : 0),
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

#endif

    if ((p->upstream_eof || p->upstream_error) && p->free_raw_bufs) {//Ã»°ì·¨ÁË£¬¶¼¿ìµ½Í·ÁË£¬»òÕß³öÏÖ´íÎóÁË£¬ËùÒÔ´¦ÀíÒ»ÏÂÕâ¿é²»ÍêÕûµÄbuffer
        /* STUB */ p->free_raw_bufs->buf->num = p->num++;
		//Èç¹ûÊý¾Ý¶ÁÈ¡Íê±ÏÁË£¬»òÕßºó¶Ë³öÏÖÎÊÌâÁË£¬²¢ÇÒ£¬free_raw_bufs²»Îª¿Õ£¬ºóÃæ»¹ÓÐÒ»²¿·ÖÊý¾Ý£¬
		//µ±È»Ö»¿ÉÄÜÓÐÒ»¿é¡£ÄÇ¾Íµ÷ÓÃinput_filter´¦ÀíËü¡£FCGIÎªngx_http_fastcgi_input_filter ÔÚngx_http_fastcgi_handlerÀïÃæÉèÖÃµÄ

		//ÕâÀï¿¼ÂÇÒ»ÖÖÇé¿ö: ÕâÊÇ×îºóÒ»¿éÊý¾ÝÁË£¬Ã»Âú£¬ÀïÃæÃ»ÓÐdataÊý¾Ý£¬ËùÒÔngx_http_fastcgi_input_filter»áµ÷ÓÃngx_event_pipe_add_free_bufº¯Êý£¬
		//½«Õâ¿éÄÚ´æ·ÅÈëfree_raw_bufsµÄÇ°Ãæ£¬¿ÉÊÇ¾ý²»Öª£¬Õâ×îºóÒ»¿é²»´æÔÚÊý¾Ý²¿·ÖµÄÄÚ´æÕýºÃµÈÓÚfree_raw_bufs£¬ÒòÎªfree_raw_bufs»¹Ã»À´µÃ¼°¸Ä±ä¡£
		//ËùÒÔ£¬¾Í°Ñ×Ô¼º¸øÌæ»»µôÁË¡£ÕâÖÖÇé¿ö»á·¢ÉúÂð?
        if (p->input_filter(p, p->free_raw_bufs->buf) == NGX_ERROR) {
            return NGX_ABORT;
        }
        p->free_raw_bufs = p->free_raw_bufs->next;
        if (p->free_bufs && p->buf_to_file == NULL) {
            for (cl = p->free_raw_bufs; cl; cl = cl->next) {
                if (cl->buf->shadow == NULL) 
			//Õâ¸öshadow³ÉÔ±Ö¸ÏòÓÉÎÒÕâ¿ébuf²úÉúµÄÐ¡FCGIÊý¾Ý¿ébufµÄÖ¸ÕëÁÐ±í¡£Èç¹ûÎªNULL£¬¾ÍËµÃ÷Õâ¿ébufÃ»ÓÐdata£¬¿ÉÒÔÊÍ·ÅÁË¡£
                    ngx_pfree(p->pool, cl->buf->start);
                }
            }
        }
    }
    if (p->cacheable && p->in) {
        if (ngx_event_pipe_write_chain_to_temp_file(p) == NGX_ABORT) {
            return NGX_ABORT;
        }
    }
    return NGX_OK;
}


static ngx_int_t
ngx_event_pipe_write_to_downstream(ngx_event_pipe_t *p)
{//ngx_event_pipeµ÷ÓÃÕâÀï½øÐÐÊý¾Ý·¢ËÍ¸ø¿Í»§¶Ë£¬Êý¾ÝÒÑ¾­×¼±¸ÔÚp->out,p->inÀïÃæÁË¡£
    u_char            *prev;
    size_t             bsize;
    ngx_int_t          rc;
    ngx_uint_t         flush, flushed, prev_last_shadow;
    ngx_chain_t       *out, **ll, *cl, file;
    ngx_connection_t  *downstream;

    downstream = p->downstream;
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0,"pipe write downstream: %d", downstream->write->ready);
    flushed = 0;

    for ( ;; ) {
        if (p->downstream_error) {//Èç¹û¿Í»§¶ËÁ¬½Ó³ö´íÁË¡£drain=ÅÅË®£»Á÷¸É,
        //Çå¿Õupstream·¢¹ýÀ´µÄ£¬½âÎö¹ý¸ñÊ½ºóµÄHTMLÊý¾Ý¡£½«Æä·ÅÈëfree_raw_bufsÀïÃæ¡£
            return ngx_event_pipe_drain_chains(p);
        }
        if (p->upstream_eof || p->upstream_error || p->upstream_done) {
//Èç¹ûupstreamµÄÁ¬½ÓÒÑ¾­¹Ø±ÕÁË£¬»ò³öÎÊÌâÁË£¬»òÕß·¢ËÍÍê±ÏÁË£¬ÄÇ¾Í¿ÉÒÔ·¢ËÍÁË¡£
            /* pass the p->out and p->in chains to the output filter */
            for (cl = p->busy; cl; cl = cl->next) {
                cl->buf->recycled = 0;
            }

            if (p->out) {//Êý¾ÝÐ´µ½´ÅÅÌÁË
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write downstream flush out");
                for (cl = p->out; cl; cl = cl->next) {
                    cl->buf->recycled = 0;//²»ÐèÒª»ØÊÕÖØ¸´ÀûÓÃÁË£¬ÒòÎªupstream_doneÁË£¬²»»áÔÙ¸øÎÒ·¢ËÍÊý¾ÝÁË¡£
                }
				//ÏÂÃæ£¬ÒòÎªp->outµÄÁ´±íÀïÃæÒ»¿é¿é¶¼ÊÇ½âÎöºóµÄHTMLÊý¾Ý£¬ËùÒÔÖ±½Óµ÷ÓÃngx_http_output_filter½øÐÐHTMLÊý¾Ý·¢ËÍ¾ÍÐÐÁË¡£
                rc = p->output_filter(p->output_ctx, p->out);
                if (rc == NGX_ERROR) {
                    p->downstream_error = 1;
                    return ngx_event_pipe_drain_chains(p);
                }
                p->out = NULL;
            }

            if (p->in) {//¸úoutÍ¬Àí¡£¼òµ¥µ÷ÓÃngx_http_output_filter½øÈë¸÷¸öfilter·¢ËÍ¹ý³ÌÖÐ¡£
                ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write downstream flush in");
                for (cl = p->in; cl; cl = cl->next) {
                    cl->buf->recycled = 0;//ÒÑ¾­ÊÇ×îºóµÄÁË£¬²»ÐèÒª»ØÊÕÁË
                }
				//×¢ÒâÏÂÃæµÄ·¢ËÍ²»ÊÇÕæµÄwritevÁË£¬µÃ¿´¾ßÌåÇé¿ö±ÈÈçÊÇ·ñÐèÒªrecycled,ÊÇ·ñÊÇ×îºóÒ»¿éµÈ¡£ngx_http_write_filter»áÅÐ¶ÏÕâ¸öµÄ¡£
                rc = p->output_filter(p->output_ctx, p->in);//µ÷ÓÃngx_http_output_filter·¢ËÍ£¬×îºóÒ»¸öÊÇngx_http_write_filter
                if (rc == NGX_ERROR) {
                    p->downstream_error = 1;
                    return ngx_event_pipe_drain_chains(p);
                }
                p->in = NULL;
            }
			//Èç¹ûÒª»º´æ£¬ÄÇ¾ÍÐ´Èëµ½ÎÄ¼þÀïÃæÈ¥¡£
            if (p->cacheable && p->buf_to_file) {
                file.buf = p->buf_to_file;
                file.next = NULL;
                if (ngx_write_chain_to_temp_file(p->temp_file, &file) == NGX_ERROR){
                    return NGX_ABORT;
                }
            }

            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write downstream done");
            /* TODO: free unused bufs */
            p->downstream_done = 1;
            break;
        }

		//·ñÔòupstreamÊý¾Ý»¹Ã»ÓÐ·¢ËÍÍê±Ï¡£
        if (downstream->data != p->output_ctx || !downstream->write->ready || downstream->write->delayed) {
            break;
        }
        /* bsize is the size of the busy recycled bufs */
        prev = NULL;
        bsize = 0;
//ÕâÀï±éÀúÐèÒªbusyÕâ¸öÕýÔÚ·¢ËÍ£¬ÒÑ¾­µ÷ÓÃ¹ýoutput_filterµÄbufÁ´±í£¬¼ÆËãÒ»ÏÂÄÇÐ©¿ÉÒÔ»ØÊÕÖØ¸´ÀûÓÃµÄbuf
//¼ÆËãÕâÐ©bufµÄ×ÜÈÝÁ¿£¬×¢ÒâÕâÀï²»ÊÇ¼ÆËãbusyÖÐ»¹ÓÐ¶àÉÙÊý¾ÝÃ»ÓÐÕæÕýwritev³öÈ¥£¬¶øÊÇËûÃÇ×Ü¹²µÄ×î´óÈÝÁ¿
        for (cl = p->busy; cl; cl = cl->next) {
            if (cl->buf->recycled) {
                if (prev == cl->buf->start) {
                    continue;
                }
                bsize += cl->buf->end - cl->buf->start;
                prev = cl->buf->start;
            }
        }
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write busy: %uz", bsize);
        out = NULL;
		//busy_sizeÎªfastcgi_busy_buffers_size Ö¸ÁîÉèÖÃµÄ´óÐ¡£¬Ö¸×î´ó´ý·¢ËÍµÄbusy×´Ì¬µÄÄÚ´æ×Ü´óÐ¡¡£
		//Èç¹û´óÓÚÕâ¸ö´óÐ¡£¬nginx»á³¢ÊÔÈ¥·¢ËÍÐÂµÄÊý¾Ý²¢»ØÊÕÕâÐ©busy×´Ì¬µÄbuf¡£
        if (bsize >= (size_t) p->busy_size) {
            flush = 1;//Èç¹ûbusyÁ´±íÀïÃæµÄÊý¾ÝºÜ¶àÁË£¬³¬¹ýfastcgi_busy_buffers_size Ö¸Áî£¬ÄÇ¾Í¸Ï½ôÈ¥·¢ËÍ£¬»ØÊÕ°É£¬²»È»free_raw_bufsÀïÃæÃ»¿ÉÓÃ»º´æÁË¡£
            goto flush;
        }

        flush = 0;
        ll = NULL;
        prev_last_shadow = 1;//±ê¼ÇÉÏÒ»¸ö½ÚµãÊÇ²»ÊÇÕýºÃÊÇÒ»¿éFCGI bufferµÄ×îºóÒ»¸öÊý¾Ý½Úµã¡£
//±éÀúp->out,p->inÀïÃæµÄÎ´·¢ËÍÊý¾Ý£¬½«ËûÃÇ·Åµ½outÁ´±íºóÃæ£¬×¢ÒâÕâÀï·¢ËÍµÄÊý¾Ý²»³¬¹ýbusy_sizeÒòÎªÅäÖÃÏÞÖÆÁË¡£
        for ( ;; ) {
//Ñ­»·£¬Õâ¸öÑ­»·µÄÖÕÖ¹ºó£¬ÎÒÃÇ¾ÍÄÜ»ñµÃ¼¸¿éHTMLÊý¾Ý½Úµã£¬²¢ÇÒËûÃÇ¿çÔ½ÁË1¸öÒÔÉÏµÄFCGIÊý¾Ý¿éµÄ²¢ÒÔ×îºóÒ»¿é´øÓÐlast_shadow½áÊø¡£
            if (p->out) {//bufµ½tempfileµÄÊý¾Ý»á·Åµ½outÀïÃæ¡£
                cl = p->out;
                if (cl->buf->recycled && bsize + cl->buf->last - cl->buf->pos > p->busy_size) {
                    flush = 1;//ÅÐ¶ÏÊÇ·ñ³¬¹ýbusy_size
                    break;
                }
                p->out = p->out->next;
                ngx_event_pipe_free_shadow_raw_buf(&p->free_raw_bufs, cl->buf);
            } else if (!p->cacheable && p->in) {
                cl = p->in;
                ngx_log_debug3(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write buf ls:%d %p %z", cl->buf->last_shadow, cl->buf->pos, cl->buf->last - cl->buf->pos);
				//
                if (cl->buf->recycled && cl->buf->last_shadow && bsize + cl->buf->last - cl->buf->pos > p->busy_size)  {
					//1.¶ÔÓÚÔÚinÀïÃæµÄÊý¾Ý£¬Èç¹ûÆäÐèÒª»ØÊÕ;
					//2.²¢ÇÒÓÖÊÇÄ³Ò»¿é´óFCGI bufµÄ×îºóÒ»¸öÓÐÐ§htmlÊý¾Ý½Úµã£»
					//3.¶øÇÒµ±Ç°µÄÃ»·¨ËÍµÄ´óÐ¡´óÓÚbusy_size, ÄÇ¾ÍÐèÒª»ØÊÕÒ»ÏÂÁË£¬ÒòÎªÎÒÃÇÓÐbuffer»úÖÆ
                    if (!prev_last_shadow) {
		//Èç¹ûÇ°ÃæµÄÒ»¿é²»ÊÇÄ³¸ö´óFCGI bufferµÄ×îºóÒ»¸öÊý¾Ý¿é£¬ÄÇ¾Í½«µ±Ç°Õâ¿é·ÅÈëoutµÄºóÃæ£¬È»ºóÍË³öÑ­»·È¥flash
		//Ê²Ã´ÒâË¼ÄØ£¬¾ÍÊÇËµ£¬Èç¹ûµ±Ç°µÄÕâ¿é²»»áµ¼ÖÂoutÁ´±í¶à¼ÓÁËÒ»¸ö½Úµã£¬¶øµ¹ÊýµÚ¶þ¸ö½ÚµãÕýºÃÊÇÒ»¿éFCGI´óÄÚ´æµÄ½áÎ²¡£
		//ÆäÊµÊÇi×öÁË¸öÓÅ»¯,ÈÃnginx¾¡Á¿Ò»¿é¿éµÄ·¢ËÍ¡£
                        p->in = p->in->next;
                        cl->next = NULL;
                        if (out) {
                            *ll = cl;
                        } else {
                            out = cl;
                        }
                    }
                    flush = 1;//³¬¹ýÁË´óÐ¡£¬±ê¼ÇÒ»ÏÂ´ý»áÊÇÐèÒªÕæÕý·¢ËÍµÄ¡£²»¹ýÕâ¸öºÃÏñÃ»·¢»Ó¶àÉÙ×÷ÓÃ£¬ÒòÎªºóÃæ²»ÔõÃ´ÅÐ¶Ï¡¢
                    break;//Í£Ö¹´¦ÀíºóÃæµÄÄÚ´æ¿é£¬ÒòÎªÕâÀïÒÑ¾­´óÓÚbusy_sizeÁË¡£
                }
                prev_last_shadow = cl->buf->last_shadow;
                p->in = p->in->next;
            } else {
                break;//ºóÃæÃ»ÓÐÊý¾ÝÁË£¬ÄÇÃ»°ì·¨ÁË£¬·¢°É¡£²»¹ýÒ»°ãÇé¿ö¿Ï¶¨ÓÐlast_shadowÎª1µÄ¡£ÕâÀïºÜÄÑ½øÀ´µÄ¡£
            }
//clÖ¸Ïòµ±Ç°ÐèÒª´¦ÀíµÄÊý¾Ý£¬±ÈÈçcl = p->out»òÕßcl = p->in;
//ÏÂÃæ¾Í½«Õâ¿éÄÚ´æ·ÅÈëoutÖ¸ÏòµÄÁ´±íµÄ×îºó£¬llÖ¸Ïò×îºóÒ»¿éµÄnextÖ¸ÕëµØÖ·¡£
            if (cl->buf->recycled) {//Èç¹ûÕâ¿ébufÊÇÐèÒª»ØÊÕÀûÓÃµÄ£¬¾ÍÍ³¼ÆÆä´óÐ¡
                bsize += cl->buf->last - cl->buf->pos;
            }
            cl->next = NULL;
            if (out) {
                *ll = cl;
            } else {
                out = cl;//Ö¸ÏòµÚÒ»¿éÊý¾Ý
            }
            ll = &cl->next;
        }
//µ½ÕâÀïºó£¬outÖ¸ÕëÖ¸ÏòÒ»¸öÁ´±í£¬ÆäÀïÃæµÄÊý¾ÝÊÇ´Óp->out,p->inÀ´µÄÒª·¢ËÍµÄÊý¾Ý¡£
    flush:
//ÏÂÃæ½«outÖ¸ÕëÖ¸ÏòµÄÄÚ´æµ÷ÓÃoutput_filter£¬½øÈëfilter¹ý³Ì¡£
        ngx_log_debug2(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe write: out:%p, f:%d", out, flush);
        if (out == NULL) {
            if (!flush) {
                break;
            }
            /* a workaround for AIO */
            if (flushed++ > 10) {
                return NGX_BUSY;
            }
        }
        rc = p->output_filter(p->output_ctx, out);//¼òµ¥µ÷ÓÃngx_http_output_filter½øÈë¸÷¸öfilter·¢ËÍ¹ý³ÌÖÐ¡£
        if (rc == NGX_ERROR) {
            p->downstream_error = 1;
            return ngx_event_pipe_drain_chains(p);
        }
		//½«outµÄÊý¾ÝÒÆ¶¯µ½busy£¬busyÖÐ·¢ËÍÍê³ÉµÄÒÆ¶¯µ½free
        ngx_chain_update_chains(&p->free, &p->busy, &out, p->tag);
        for (cl = p->free; cl; cl = cl->next) {
            if (cl->buf->temp_file) {
                if (p->cacheable || !p->cyclic_temp_file) {
                    continue;
                }
                /* reset p->temp_offset if all bufs had been sent */
                if (cl->buf->file_last == p->temp_file->offset) {
                    p->temp_file->offset = 0;
                }
            }
            /* TODO: free buf if p->free_bufs && upstream done */
            /* add the free shadow raw buf to p->free_raw_bufs */
            if (cl->buf->last_shadow) {
	//Ç°ÃæËµ¹ýÁË£¬Èç¹ûÕâ¿éÄÚ´æÕýºÃÊÇÕû¸ö´óFCGIÂãÄÚ´æµÄ×îºóÒ»¸ödata½Úµã£¬ÔòÊÍ·ÅÕâ¿é´óFCGI buffer¡£
	//µ±last_shadowÎª1µÄÊ±ºò£¬buf->shadowÊµ¼ÊÉÏÖ¸ÏòÁËÕâ¿é´óµÄFCGIÂãbufµÄ¡£Ò²¾ÍÊÇÔ­Ê¼buf£¬ÆäËûbuf¶¼ÊÇ¸öÓ°×Ó£¬ËûÃÇÖ¸ÏòÄ³¿éÔ­Ê¼µÄbuf.
                if (ngx_event_pipe_add_free_buf(p, cl->buf->shadow) != NGX_OK) {
                    return NGX_ABORT;
                }
                cl->buf->last_shadow = 0;
            }
            cl->buf->shadow = NULL;
        }
    }
    return NGX_OK;
}


static ngx_int_t
ngx_event_pipe_write_chain_to_temp_file(ngx_event_pipe_t *p)
{
    ssize_t       size, bsize;
    ngx_buf_t    *b;
    ngx_chain_t  *cl, *tl, *next, *out, **ll, **last_free, fl;

    if (p->buf_to_file) {
        fl.buf = p->buf_to_file;
        fl.next = p->in;
        out = &fl;

    } else {
        out = p->in;
    }

    if (!p->cacheable) {

        size = 0;
        cl = out;
        ll = NULL;

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0,
                       "pipe offset: %O", p->temp_file->offset);

        do {
            bsize = cl->buf->last - cl->buf->pos;
            ngx_log_debug3(NGX_LOG_DEBUG_EVENT, p->log, 0, "pipe buf %p, pos %p, size: %z", cl->buf->start, cl->buf->pos, bsize);
            if ((size + bsize > p->temp_file_write_size)
               || (p->temp_file->offset + size + bsize > p->max_temp_file_size))
            {
                break;
            }

            size += bsize;
            ll = &cl->next;
            cl = cl->next;
        } while (cl);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "size: %z", size);

        if (ll == NULL) {
            return NGX_BUSY;
        }

        if (cl) {
           p->in = cl;
           *ll = NULL;

        } else {
           p->in = NULL;
           p->last_in = &p->in;
        }

    } else {
        p->in = NULL;
        p->last_in = &p->in;
    }

    if (ngx_write_chain_to_temp_file(p->temp_file, out) == NGX_ERROR) {
        return NGX_ABORT;
    }

    for (last_free = &p->free_raw_bufs;
         *last_free != NULL;
         last_free = &(*last_free)->next)
    {
        /* void */
    }

    if (p->buf_to_file) {
        p->temp_file->offset = p->buf_to_file->last - p->buf_to_file->pos;
        p->buf_to_file = NULL;
        out = out->next;
    }

    for (cl = out; cl; cl = next) {
        next = cl->next;
        cl->next = NULL;

        b = cl->buf;
        b->file = &p->temp_file->file;
        b->file_pos = p->temp_file->offset;
        p->temp_file->offset += b->last - b->pos;
        b->file_last = p->temp_file->offset;

        b->in_file = 1;
        b->temp_file = 1;

        if (p->out) {
            *p->last_out = cl;
        } else {
            p->out = cl;
        }
        p->last_out = &cl->next;

        if (b->last_shadow) {

            tl = ngx_alloc_chain_link(p->pool);
            if (tl == NULL) {
                return NGX_ABORT;
            }

            tl->buf = b->shadow;
            tl->next = NULL;

            *last_free = tl;
            last_free = &tl->next;

            b->shadow->pos = b->shadow->start;
            b->shadow->last = b->shadow->start;

            ngx_event_pipe_remove_shadow_links(b->shadow);
        }
    }

    return NGX_OK;
}


/* the copy input filter */
ngx_int_t ngx_event_pipe_copy_input_filter(ngx_event_pipe_t *p, ngx_buf_t *buf)
{
    ngx_buf_t    *b;
    ngx_chain_t  *cl;

    if (buf->pos == buf->last) {
        return NGX_OK;
    }
    if (p->free) {
        cl = p->free;
        b = cl->buf;
        p->free = cl->next;
        ngx_free_chain(p->pool, cl);
    } else {
        b = ngx_alloc_buf(p->pool);
        if (b == NULL) {
            return NGX_ERROR;
        }
    }
    ngx_memcpy(b, buf, sizeof(ngx_buf_t));
    b->shadow = buf;
    b->tag = p->tag;
    b->last_shadow = 1;
    b->recycled = 1;
    buf->shadow = b;

    cl = ngx_alloc_chain_link(p->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = b;
    cl->next = NULL;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, p->log, 0, "input buf #%d", b->num);

    if (p->in) {
        *p->last_in = cl;
    } else {
        p->in = cl;
    }
    p->last_in = &cl->next;

    return NGX_OK;
}


static ngx_inline void
ngx_event_pipe_remove_shadow_links(ngx_buf_t *buf)
{//É¾³ýÊý¾ÝµÄshadow£¬ÒÔ¼°recycledÉèÖÃÎª0£¬±íÊ¾²»ÐèÒªÑ­»·ÀûÓÃ£¬ÕâÀïÊµÏÖÁËbuffering¹¦ÄÜ
//ÒòÎªngx_http_write_filterº¯ÊýÀïÃæÅÐ¶ÏÈç¹ûÓÐrecycled±êÖ¾£¬¾Í»áÁ¢¼´½«Êý¾Ý·¢ËÍ³öÈ¥£¬
//Òò´ËÕâÀï½«ÕâÐ©±êÖ¾Çå¿Õ£¬µ½ngx_http_write_filterÄÇÀï¾Í»á¾¡Á¿»º´æµÄ¡£
    ngx_buf_t  *b, *next;

    b = buf->shadow;//Õâ¸öshadowÖ¸ÏòµÄÊÇbufÕâ¿éÂãFCGIÊý¾ÝµÄµÚÒ»¸öÊý¾Ý½Úµã
    if (b == NULL) {
        return;
    }
    while (!b->last_shadow) {//Èç¹û²»ÊÇ×îºóÒ»¸öÊý¾Ý½Úµã£¬²»¶ÏÍùºó±éÀú£¬
        next = b->shadow;
        b->temporary = 0;
        b->recycled = 0;//±ê¼ÇÎª»ØÊÕµÄ ·
        b->shadow = NULL;//°Ñshadow³ÉÔ±ÖÃ¿Õ¡£
        b = next;
    }

    b->temporary = 0;
    b->recycled = 0;
    b->last_shadow = 0;
    b->shadow = NULL;
    buf->shadow = NULL;
}


static ngx_inline void
ngx_event_pipe_free_shadow_raw_buf(ngx_chain_t **free, ngx_buf_t *buf)
{
    ngx_buf_t    *s;
    ngx_chain_t  *cl, **ll;

    if (buf->shadow == NULL) {
        return;
    }
	//ÏÂÃæ²»¶ÏµÄÑØ×Åµ±Ç°µÄbufÍùºó×ß£¬Ö±µ½×ßµ½ÁË±¾´óFCGIÂãÊý¾Ý¿éµÄ×îºóÒ»¸ö½ÚµãÊý¾Ý¿é¡£
    for (s = buf->shadow; !s->last_shadow; s = s->shadow) { /* void */ }

    ll = free;
    for (cl = *free; cl; cl = cl->next) {
        if (cl->buf == s) {//ÊÇ×îºóÒ»¿éµÄ»°
            *ll = cl->next;
            break;
        }
        if (cl->buf->shadow) {
            break;
        }

        ll = &cl->next;
    }
}


ngx_int_t
ngx_event_pipe_add_free_buf(ngx_event_pipe_t *p, ngx_buf_t *b)
{//½«²ÎÊýµÄb´ú±íµÄÊý¾Ý¿é¹ÒÈëfree_raw_bufsµÄ¿ªÍ·»òÕßµÚ¶þ¸öÎ»ÖÃ¡£bÎªÉÏ²ã¾õµÃÃ»ÓÃÁËµÄÊý¾Ý¿é¡£
    ngx_chain_t  *cl;
//ÕâÀï²»»á³öÏÖb¾ÍµÈÓÚfree_raw_bufs->bufµÄÇé¿öÂð
    cl = ngx_alloc_chain_link(p->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }
    b->pos = b->start;//ÖÃ¿ÕÕâÛçÊý¾Ý
    b->last = b->start;
    b->shadow = NULL;

    cl->buf = b;

    if (p->free_raw_bufs == NULL) {
        p->free_raw_bufs = cl;
        cl->next = NULL;
        return NGX_OK;
    }
	//¿´ÏÂÃæµÄ×¢ÊÍ£¬ÒâË¼ÊÇ£¬Èç¹û×îÇ°ÃæµÄfree_raw_bufsÖÐÃ»ÓÐÊý¾Ý£¬ÄÇ¾Í°Éµ±Ç°Õâ¿éÊý¾Ý·ÅÈëÍ·²¿¾ÍÐÐ¡£
	//·ñÔòÈç¹ûµ±Ç°free_raw_bufsÓÐÊý¾Ý£¬ÄÇ¾ÍµÃ·Åµ½ÆäºóÃæÁË¡£ÎªÊ²Ã´»áÓÐÊý¾ÝÄØ?±ÈÈç£¬¶ÁÈ¡Ò»Ð©Êý¾Ýºó£¬»¹Ê£ÏÂÒ»¸öÎ²°Í´æ·ÅÔÚfree_raw_bufs£¬È»ºó¿ªÊ¼Íù¿Í»§¶ËÐ´Êý¾Ý
	//Ð´Íêºó£¬×ÔÈ»Òª°ÑÃ»ÓÃµÄbuffer·ÅÈëµ½ÕâÀïÃæÀ´¡£Õâ¸öÊÇÔÚngx_event_pipe_write_to_downstreamÀïÃæ×öµÄ¡£»òÕß¸É´àÔÚngx_event_pipe_drain_chainsÀïÃæ×ö¡£
	//ÒòÎªÕâ¸öº¯ÊýÔÚinpupt_filterÀïÃæµ÷ÓÃÊÇ´ÓÊý¾Ý¿é¿ªÊ¼´¦Àí£¬È»ºóµ½ºóÃæµÄ£¬
	//²¢ÇÒÔÚµ÷ÓÃinput_filterÖ®Ç°ÊÇ»á½«free_raw_bufsÖÃ¿ÕµÄ¡£Ó¦¸ÃÊÇÆäËûµØ·½Ò²ÓÐµ÷ÓÃ¡£
    if (p->free_raw_bufs->buf->pos == p->free_raw_bufs->buf->last) {
        /* add the free buf to the list start */
        cl->next = p->free_raw_bufs;
        p->free_raw_bufs = cl;
        return NGX_OK;
    }
    /* the first free buf is partialy filled, thus add the free buf after it */
    cl->next = p->free_raw_bufs->next;
    p->free_raw_bufs->next = cl;
    return NGX_OK;
}


static ngx_int_t
ngx_event_pipe_drain_chains(ngx_event_pipe_t *p)
{//±éÀúp->in/out/busy£¬½«ÆäÁ´±íËùÊôµÄÂãFCGIÊý¾Ý¿éÊÍ·Å£¬·ÅÈëµ½free_raw_bufsÖÐ¼äÈ¥¡£Ò²¾ÍÊÇ£¬Çå¿Õupstream·¢¹ýÀ´µÄ£¬½âÎö¹ý¸ñÊ½ºóµÄHTMLÊý¾Ý¡£
    ngx_chain_t  *cl, *tl;

    for ( ;; ) {
        if (p->busy) {
            cl = p->busy;
            p->busy = NULL;
        } else if (p->out) {
            cl = p->out;
            p->out = NULL;
        } else if (p->in) {
            cl = p->in;
            p->in = NULL;
        } else {
            return NGX_OK;
        }
		//ÕÒµ½¶ÔÓ¦µÄÁ´±í
        while (cl) {/*ÒªÖªµÀ£¬ÕâÀïclÀïÃæ£¬±ÈÈçp->inÀïÃæµÄÕâÐ©ngx_buf_t½á¹¹ËùÖ¸ÏòµÄÊý¾ÝÄÚ´æÊµ¼ÊÉÏÊÇÔÚ
        ngx_event_pipe_read_upstreamÀïÃæµÄinput_filter½øÐÐÐ­Òé½âÎöµÄÊ±ºòÉèÖÃÎª¸ú´Ó¿Í»§¶Ë¶ÁÈ¡Êý¾ÝÊ±µÄbuf¹«ÓÃµÄ£¬Ò²¾ÍÊÇËùÎ½µÄÓ°×Ó¡£
		È»ºó£¬ËäÈ»p->inÖ¸ÏòµÄÁ´±íÀïÃæÓÐºÜ¶àºÜ¶à¸ö½Úµã£¬Ã¿¸ö½Úµã´ú±íÒ»¿éHTML´úÂë£¬µ«ÊÇËûÃÇ²¢²»ÊÇ¶ÀÕ¼Ò»¿éÄÚ´æµÄ£¬¶øÊÇ¿ÉÄÜ¹²ÏíµÄ£¬
		±ÈÈçÒ»¿é´óµÄbuffer£¬ÀïÃæÓÐ3¸öFCGIµÄSTDOUTÊý¾Ý°ü£¬¶¼ÓÐdata²¿·Ö£¬ÄÇÃ´½«´æÔÚ3¸öbµÄ½ÚµãÁ´½Óµ½p->inµÄÄ©Î²£¬ËûÃÇµÄshadow³ÉÔ±
		·Ö±ðÖ¸ÏòÏÂÒ»¸ö½Úµã£¬×îºóÒ»¸ö½Úµã¾ÍÖ¸ÏòÆäËùÊôµÄ´óÄÚ´æ½á¹¹¡£¾ßÌåÔÚngx_http_fastcgi_input_filterÊµÏÖ¡£
        */
            if (cl->buf->last_shadow) {//Åöµ½ÁËÄ³¸ö´óFCGIÊý¾Ý¿éµÄ×îºóÒ»¸ö½Úµã£¬ÊÍ·ÅÖ»£¬È»ºó½øÈëÏÂÒ»¸ö´ó¿éÀïÃæµÄÄ³¸öÐ¡html Êý¾Ý¿é¡£
                if (ngx_event_pipe_add_free_buf(p, cl->buf->shadow) != NGX_OK) {
                    return NGX_ABORT;
                }
                cl->buf->last_shadow = 0;
            }

            cl->buf->shadow = NULL;
            tl = cl->next;
			
            cl->next = p->free;//°ÑclÕâ¸öÐ¡buf½Úµã·ÅÈëp->free£¬¹©ngx_http_fastcgi_input_filter½øÐÐÖØ¸´Ê¹ÓÃ¡£
            p->free = cl;
			
            cl = tl;
        }
    }
}
