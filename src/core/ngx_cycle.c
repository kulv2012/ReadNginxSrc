
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


static void ngx_destroy_cycle_pools(ngx_conf_t *conf);
static ngx_int_t ngx_cmp_sockaddr(struct sockaddr *sa1, struct sockaddr *sa2);
static ngx_int_t ngx_init_zone_pool(ngx_cycle_t *cycle,
    ngx_shm_zone_t *shm_zone);
static ngx_int_t ngx_test_lockfile(u_char *file, ngx_log_t *log);
static void ngx_clean_old_cycles(ngx_event_t *ev);


volatile ngx_cycle_t  *ngx_cycle;
ngx_array_t            ngx_old_cycles;

static ngx_pool_t     *ngx_temp_pool;
static ngx_event_t     ngx_cleaner_event;

ngx_uint_t             ngx_test_config;
ngx_uint_t             ngx_quiet_mode;

#if (NGX_THREADS)
ngx_tls_key_t          ngx_core_tls_key;
#endif


/* STUB NAME */
static ngx_connection_t  dumb;
/* STUB */

static ngx_str_t  error_log = ngx_string(NGX_ERROR_LOG_PATH);

/*
加载配置文件，并回调create_conf,init_conf, init_module等；
初始化各个配置，并打开监听端口，设置优化选项；
*/
ngx_cycle_t * ngx_init_cycle(ngx_cycle_t *old_cycle)
{//整个程序的主要初始化，配置加载,解析，各个模块的初始化回调函数，指令的set回调函数调用，监听端口打开，共享内存申请等等都在这里.
    void                *rv;
    char               **senv, **env;
    ngx_uint_t           i, n;
    ngx_log_t           *log;
    ngx_time_t          *tp;
    ngx_conf_t           conf;
    ngx_pool_t          *pool;
    ngx_cycle_t         *cycle, **old;
    ngx_shm_zone_t      *shm_zone, *oshm_zone;
    ngx_list_part_t     *part, *opart;
    ngx_open_file_t     *file;
    ngx_listening_t     *ls, *nls;
    ngx_core_conf_t     *ccf, *old_ccf;
    ngx_core_module_t   *module;
    char                 hostname[NGX_MAXHOSTNAMELEN];

    ngx_timezone_update();

    /* force localtime update with a new timezone */

    tp = ngx_timeofday();//k : ngx_cached_time
    tp->sec = 0;//秒设置为0
    ngx_time_update();//迫使当前的缓存时间更新。
    log = old_cycle->log;
    pool = ngx_create_pool(NGX_CYCLE_POOL_SIZE, log);
    if (pool == NULL) {
        return NULL;
    }
    pool->log = log;
    cycle = ngx_pcalloc(pool, sizeof(ngx_cycle_t));//下一个过程吗
    if (cycle == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    cycle->pool = pool;//cycle也是包含在pool里面的! ，也就是，释放了cycle->pool后，cycle也释放了
    cycle->log = log;
    cycle->new_log.log_level = NGX_LOG_ERR;
    cycle->old_cycle = old_cycle;
//下面拷贝一下旧的配置路径什么的，参数等到新的cycle
    cycle->conf_prefix.len = old_cycle->conf_prefix.len;
    cycle->conf_prefix.data = ngx_pstrdup(pool, &old_cycle->conf_prefix);
    if (cycle->conf_prefix.data == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    cycle->prefix.len = old_cycle->prefix.len;
    cycle->prefix.data = ngx_pstrdup(pool, &old_cycle->prefix);
    if (cycle->prefix.data == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    cycle->conf_file.len = old_cycle->conf_file.len;
    cycle->conf_file.data = ngx_pnalloc(pool, old_cycle->conf_file.len + 1);
    if (cycle->conf_file.data == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    ngx_cpystrn(cycle->conf_file.data, old_cycle->conf_file.data,
                old_cycle->conf_file.len + 1);

    cycle->conf_param.len = old_cycle->conf_param.len;
    cycle->conf_param.data = ngx_pstrdup(pool, &old_cycle->conf_param);
    if (cycle->conf_param.data == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

//干嘛的?
    n = old_cycle->pathes.nelts ? old_cycle->pathes.nelts : 10;

    cycle->pathes.elts = ngx_pcalloc(pool, n * sizeof(ngx_path_t *));
    if (cycle->pathes.elts == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    cycle->pathes.nelts = 0;
    cycle->pathes.size = sizeof(ngx_path_t *);
    cycle->pathes.nalloc = n;
    cycle->pathes.pool = pool;

    if (old_cycle->open_files.part.nelts) {
        n = old_cycle->open_files.part.nelts;
        for (part = old_cycle->open_files.part.next; part; part = part->next) {
            n += part->nelts;
        }
    } else {
        n = 20;
    }
    if (ngx_list_init(&cycle->open_files, pool, n, sizeof(ngx_open_file_t)) != NGX_OK) {
        ngx_destroy_pool(pool);
        return NULL;
    }
//拷贝共享内存
    if (old_cycle->shared_memory.part.nelts) {
        n = old_cycle->shared_memory.part.nelts;
        for (part = old_cycle->shared_memory.part.next; part; part = part->next) {
            n += part->nelts;
        }
    } else {
        n = 1;
    }

    if (ngx_list_init(&cycle->shared_memory, pool, n, sizeof(ngx_shm_zone_t)) != NGX_OK)  {
        ngx_destroy_pool(pool);
        return NULL;
    }
//拷贝一下监听端口的信息；
    n = old_cycle->listening.nelts ? old_cycle->listening.nelts : 10;
    cycle->listening.elts = ngx_pcalloc(pool, n * sizeof(ngx_listening_t));
    if (cycle->listening.elts == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    cycle->listening.nelts = 0;
    cycle->listening.size = sizeof(ngx_listening_t);
    cycle->listening.nalloc = n;
    cycle->listening.pool = pool;
//为每个模块分配一个配置上下文指针，用来保存每个模块设置的配置数据
    cycle->conf_ctx = ngx_pcalloc(pool, ngx_max_module * sizeof(void *));
    if (cycle->conf_ctx == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
    if (gethostname(hostname, NGX_MAXHOSTNAMELEN) == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, "gethostname() failed");
        ngx_destroy_pool(pool);
        return NULL;
    }

    /* on Linux gethostname() silently truncates name that does not fit */

    hostname[NGX_MAXHOSTNAMELEN - 1] = '\0';
    cycle->hostname.len = ngx_strlen(hostname);

    cycle->hostname.data = ngx_pnalloc(pool, cycle->hostname.len);
    if (cycle->hostname.data == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    ngx_strlow(cycle->hostname.data, (u_char *) hostname, cycle->hostname.len);

	//k:for core modules.
    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_CORE_MODULE) {
            continue;
        }
//对于核心模块
        module = ngx_modules[i]->ctx;
        if (module->create_conf) {//如果核心模块设置了create_conf回调，则调用它们
            rv = module->create_conf(cycle);
            if (rv == NULL) {
                ngx_destroy_pool(pool);
                return NULL;
            }
            cycle->conf_ctx[ ngx_modules[i]->index ] = rv;//帮模块们保存create_conf返回的数据。后续可以方便取到
        }
    }
    senv = environ;//保留老的environ，这个环境变量已经被我们拷贝到了新地址的
    ngx_memzero(&conf, sizeof(ngx_conf_t));
    /* STUB: init array ? */
    conf.args = ngx_array_create(pool, 10, sizeof(ngx_str_t));
    if (conf.args == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }

    conf.temp_pool = ngx_create_pool(NGX_CYCLE_POOL_SIZE, log);
    if (conf.temp_pool == NULL) {
        ngx_destroy_pool(pool);
        return NULL;
    }
	

//切换到新的配置
    conf.ctx = cycle->conf_ctx;
    conf.cycle = cycle;
    conf.pool = pool;
    conf.log = log;
    conf.module_type = NGX_CORE_MODULE;
    conf.cmd_type = NGX_MAIN_CONF;
#if 0
    log->log_level = NGX_LOG_DEBUG_ALL;
#endif
    if (ngx_conf_param(&conf) != NGX_CONF_OK) {//调用ngx_conf_parse解析全局指令，貌似是做参数准备的。
        environ = senv;//还原老的environ，因为perl会改变它
        ngx_destroy_cycle_pools(&conf);
        return NULL;
    }
//解析配置文件，调用各个配置文件的set函数等。
    if (ngx_conf_parse(&conf, &cycle->conf_file) != NGX_CONF_OK) {
        environ = senv;
        ngx_destroy_cycle_pools(&conf);
        return NULL;
    }

    if (ngx_test_config && !ngx_quiet_mode) {//这就是经典的那个-t参数啦
        ngx_log_stderr(0, "the configuration file %s syntax is ok",  cycle->conf_file.data);
    }

    for (i = 0; ngx_modules[i]; i++) {
		//刚才各个配置指令已经调用了其set函数的。下面调用一下每个模块的init_conf，也就是每个模块的各个指令已经设置，现在开始模块本身了。
        if (ngx_modules[i]->type != NGX_CORE_MODULE) {
            continue;
        }
//对于NGX_CORE_MODULE，调用它他们的init_conf回调
        module = ngx_modules[i]->ctx;//得到模块初始化设置的数据，然后调用其init_conf
        if (module->init_conf) {
            if (module->init_conf(cycle, cycle->conf_ctx[ngx_modules[i]->index])== NGX_CONF_ERROR){ //第二个参数是在create_conf回调返回的东西
                environ = senv;
                ngx_destroy_cycle_pools(&conf);
                return NULL;
            }
        }
    }
    if (ngx_process == NGX_PROCESS_SIGNALLER) {
        return cycle;//如果是加-s reload等启动的，这里可以返回了。服务需要重启啥的，不是全新启动。在这里设置的ngx_get_options
    }
// 最核心的配置
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);//k conf_ctx[module.index]
    if (ngx_test_config) {
        if (ngx_create_pidfile(&ccf->pid, log) != NGX_OK) {
            goto failed;
        }
    } else if (!ngx_is_init_cycle(old_cycle)) {//第一次调用，old_cycle的配置相关的为空，第二次才是非空的。也就是下面的注释介绍的
        /*
         * we do not create the pid file in the first ngx_init_cycle() call
         * because we need to write the demonized process pid
         */
        old_ccf = (ngx_core_conf_t *) ngx_get_conf(old_cycle->conf_ctx, ngx_core_module);
        if (ccf->pid.len != old_ccf->pid.len || ngx_strcmp(ccf->pid.data, old_ccf->pid.data) != 0) {
            /* new pid file name */
            if (ngx_create_pidfile(&ccf->pid, log) != NGX_OK) {
                goto failed;
            }
            ngx_delete_pidfile(old_cycle);//删除旧的，这是啥原因，比如配置重新加载吗，文件变了啥的
        }
    }

//就打开文件，关闭，然后删除之
    if (ngx_test_lockfile(cycle->lock_file.data, log) != NGX_OK) {
        goto failed;
    }

//创建这些目录，并设置权限啥的
    if (ngx_create_pathes(cycle, ccf->user) != NGX_OK) {
        goto failed;
    }


    if (cycle->new_log.file == NULL) {//找到"logs/error.log"的ngx_open_file_t*结构，不open打开
        cycle->new_log.file = ngx_conf_open_file(cycle, &error_log);//"logs/error.log"
        if (cycle->new_log.file == NULL) {
            goto failed;
        }
    }

    /* open the new files */
    part = &cycle->open_files.part;
    file = part->elts;
//一个个打开这些文件，APPEND模式打开
    for (i = 0; /* void */ ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }
        if (file[i].name.len == 0) {
            continue;
        }
//真正打开这些文件，append模式
        file[i].fd = ngx_open_file(file[i].name.data,  NGX_FILE_APPEND, NGX_FILE_CREATE_OR_OPEN, NGX_FILE_DEFAULT_ACCESS);
        ngx_log_debug3(NGX_LOG_DEBUG_CORE, log, 0, "log: %p %d \"%s\"", &file[i], file[i].fd, file[i].name.data);
        if (file[i].fd == NGX_INVALID_FILE) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, ngx_open_file_n " \"%s\" failed",  file[i].name.data);
            goto failed;
        }
#if !(NGX_WIN32)
		// 这里设置为FD_CLOEXEC表示当程序执行exec函数时本fd将被系统自动关闭,表示不传递给exec创建的新进程。close-on-exec
        if (fcntl(file[i].fd, F_SETFD, FD_CLOEXEC) == -1) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, "fcntl(FD_CLOEXEC) \"%s\" failed", file[i].name.data);
            goto failed;
        }
#endif
    }

    cycle->log = &cycle->new_log;
    pool->log = &cycle->new_log;
    /* create shared memory */
    part = &cycle->shared_memory.part;
    shm_zone = part->elts;

    for (i = 0; /* void */ ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            shm_zone = part->elts;
            i = 0;
        }
        if (shm_zone[i].shm.size == 0) {
            ngx_log_error(NGX_LOG_EMERG, log, 0, "zero size shared memory zone \"%V\"", &shm_zone[i].shm.name);
            goto failed;
        }
        if (shm_zone[i].init == NULL) {
            /* unused shared zone */
            continue;
        }
        shm_zone[i].shm.log = cycle->log;
        opart = &old_cycle->shared_memory.part;
        oshm_zone = opart->elts;
        for (n = 0; /* void */ ; n++) {
            if (n >= opart->nelts) {
                if (opart->next == NULL) {
                    break;
                }
                opart = opart->next;
                oshm_zone = opart->elts;
                n = 0;
            }
            if (shm_zone[i].shm.name.len != oshm_zone[n].shm.name.len) {
                continue;
            }

            if (ngx_strncmp(shm_zone[i].shm.name.data,
                            oshm_zone[n].shm.name.data,
                            shm_zone[i].shm.name.len)
                != 0)
            {
                continue;
            }

            if (shm_zone[i].shm.size == oshm_zone[n].shm.size) {
                shm_zone[i].shm.addr = oshm_zone[n].shm.addr;

                if (shm_zone[i].init(&shm_zone[i], oshm_zone[n].data)
                    != NGX_OK)
                {
                    goto failed;
                }

                goto shm_zone_found;
            }

            ngx_shm_free(&oshm_zone[n].shm);

            break;
        }

        if (ngx_shm_alloc(&shm_zone[i].shm) != NGX_OK) {
            goto failed;
        }

        if (ngx_init_zone_pool(cycle, &shm_zone[i]) != NGX_OK) {
            goto failed;
        }

        if (shm_zone[i].init(&shm_zone[i], NULL) != NGX_OK) {
            goto failed;
        }

    shm_zone_found:

        continue;
    }


    /* handle the listening sockets */
    if (old_cycle->listening.nelts) {//如果设置了继承SOCK，就拷贝到cycle来
        ls = old_cycle->listening.elts;
        for (i = 0; i < old_cycle->listening.nelts; i++) {
            ls[i].remain = 0;//标记为全都不需要了，后面会清除
        }

        nls = cycle->listening.elts;
        for (n = 0; n < cycle->listening.nelts; n++) {

            for (i = 0; i < old_cycle->listening.nelts; i++) {
                if (ls[i].ignore) {
                    continue;
                }

                if (ngx_cmp_sockaddr(nls[n].sockaddr, ls[i].sockaddr) == NGX_OK)
                {//如果地址完全相同，那后面就不需要关闭，直接拷贝fd就行了
                    nls[n].fd = ls[i].fd;
                    nls[n].previous = &ls[i];
                    ls[i].remain = 1;

                    if (ls[n].backlog != nls[i].backlog) {
                        nls[n].listen = 1;
                    }

#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)

                    /*
                     * FreeBSD, except the most recent versions,
                     * could not remove accept filter
                     */
                    nls[n].deferred_accept = ls[i].deferred_accept;

                    if (ls[i].accept_filter && nls[n].accept_filter) {
                        if (ngx_strcmp(ls[i].accept_filter,
                                       nls[n].accept_filter)
                            != 0)
                        {
                            nls[n].delete_deferred = 1;
                            nls[n].add_deferred = 1;
                        }

                    } else if (ls[i].accept_filter) {
                        nls[n].delete_deferred = 1;

                    } else if (nls[n].accept_filter) {
                        nls[n].add_deferred = 1;
                    }
#endif

#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)

                    if (ls[n].deferred_accept && !nls[n].deferred_accept) {
                        nls[n].delete_deferred = 1;

                    } else if (ls[i].deferred_accept != nls[n].deferred_accept)
                    {
                        nls[n].add_deferred = 1;
                    }
#endif
                    break;
                }
            }

            if (nls[n].fd == -1) {
                nls[n].open = 1;
            }
        }

    } else {
        ls = cycle->listening.elts;
        for (i = 0; i < cycle->listening.nelts; i++) {
            ls[i].open = 1;
#if (NGX_HAVE_DEFERRED_ACCEPT && defined SO_ACCEPTFILTER)
            if (ls[i].accept_filter) {
                ls[i].add_deferred = 1;
            }
#endif
#if (NGX_HAVE_DEFERRED_ACCEPT && defined TCP_DEFER_ACCEPT)
            if (ls[i].deferred_accept) {
                ls[i].add_deferred = 1;
            }
#endif
        }
    }
//下面一个个打开cycle->listening的listen的端口，并设置为listening端口
//这些监听端口的可读事件是在ngx_event_core_module模块中设置的，其进程初始化函数为ngx_event_process_init里面会放入epoll里面
    if (ngx_open_listening_sockets(cycle) != NGX_OK) {
        goto failed;
    }
    if (!ngx_test_config) {//如果不是测试配置，设置一下各个优化选项，比如发送，接收缓冲区大小，TCP_DEFER_ACCEPT等
        ngx_configure_listening_sockets(cycle);
    }

    /* commit the new cycle configuration */
    if (!ngx_use_stderr && cycle->log->file->fd != ngx_stderr) {
        if (ngx_set_stderr(cycle->log->file->fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno, ngx_set_stderr_n " failed");
        }
    }
    pool->log = cycle->log;
    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->init_module) {//对所有模块，调用其init_module回调
            if (ngx_modules[i]->init_module(cycle) != NGX_OK) {
                /* fatal */
                exit(1);
            }
        }
    }

    /* close and delete stuff that lefts from an old cycle */
    /* free the unnecessary shared memory */
    opart = &old_cycle->shared_memory.part;
    oshm_zone = opart->elts;
    for (i = 0; /* void */ ; i++) {
        if (i >= opart->nelts) {
            if (opart->next == NULL) {
                goto old_shm_zone_done;
            }
            opart = opart->next;
            oshm_zone = opart->elts;
            i = 0;
        }
        part = &cycle->shared_memory.part;
        shm_zone = part->elts;
        for (n = 0; /* void */ ; n++) {
            if (n >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }
                part = part->next;
                shm_zone = part->elts;
                n = 0;
            }
            if (oshm_zone[i].shm.name.len == shm_zone[n].shm.name.len
                && ngx_strncmp(oshm_zone[i].shm.name.data,
                               shm_zone[n].shm.name.data,
                               oshm_zone[i].shm.name.len)
                == 0){
                goto live_shm_zone;
            }
        }
        ngx_shm_free(&oshm_zone[i].shm);
    live_shm_zone:
        continue;
    }
old_shm_zone_done:
    /* close the unnecessary listening sockets */
    ls = old_cycle->listening.elts;
    for (i = 0; i < old_cycle->listening.nelts; i++) {
        if (ls[i].remain || ls[i].fd == -1) {
            continue;//需要保留或者fd无效，不用删除，因为已经拷贝到cycle中了
        }
        if (ngx_close_socket(ls[i].fd) == -1) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,ngx_close_socket_n " listening socket on %V failed", &ls[i].addr_text);
        }

#if (NGX_HAVE_UNIX_DOMAIN)
        if (ls[i].sockaddr->sa_family == AF_UNIX) {
            u_char  *name;
            name = ls[i].addr_text.data + sizeof("unix:") - 1;
            ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                          "deleting socket %s", name);
            if (ngx_delete_file(name) == -1) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_socket_errno,
                              ngx_delete_file_n " %s failed", name);
            }
        }

#endif
    }
    /* close the unnecessary open files */
    part = &old_cycle->open_files.part;
    file = part->elts;
    for (i = 0; /* void */ ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }
        if (file[i].fd == NGX_INVALID_FILE || file[i].fd == ngx_stderr) {
            continue;
        }
        if (ngx_close_file(file[i].fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          ngx_close_file_n " \"%s\" failed",
                          file[i].name.data);
        }
    }

    ngx_destroy_pool(conf.temp_pool);
    if (ngx_process == NGX_PROCESS_MASTER || ngx_is_init_cycle(old_cycle)) {//ngx_is_init_cycle第一次调用false
//这是怎么进来的?
        /*
         * perl_destruct() frees environ, if it is not the same as it was at
         * perl_construct() time, therefore we save the previous cycle
         * environment before ngx_conf_parse() where it will be changed.
         */

        env = environ;
        environ = senv;//还原之前保存的环境变量地址

        ngx_destroy_pool(old_cycle->pool);
        cycle->old_cycle = NULL;

        environ = env;

        return cycle;
    }


    if (ngx_temp_pool == NULL) {
        ngx_temp_pool = ngx_create_pool(128, cycle->log);
        if (ngx_temp_pool == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "can not create ngx_temp_pool");
            exit(1);
        }

        n = 10;
        ngx_old_cycles.elts = ngx_pcalloc(ngx_temp_pool,
                                          n * sizeof(ngx_cycle_t *));
        if (ngx_old_cycles.elts == NULL) {
            exit(1);
        }
        ngx_old_cycles.nelts = 0;
        ngx_old_cycles.size = sizeof(ngx_cycle_t *);
        ngx_old_cycles.nalloc = n;
        ngx_old_cycles.pool = ngx_temp_pool;

        ngx_cleaner_event.handler = ngx_clean_old_cycles;
        ngx_cleaner_event.log = cycle->log;
        ngx_cleaner_event.data = &dumb;
        dumb.fd = (ngx_socket_t) -1;
    }

    ngx_temp_pool->log = cycle->log;

    old = ngx_array_push(&ngx_old_cycles);
    if (old == NULL) {
        exit(1);
    }
    *old = old_cycle;

    if (!ngx_cleaner_event.timer_set) {//如果还没有设置定时器，设置定时器，30秒后清除老的cycle?
        ngx_add_timer(&ngx_cleaner_event, 30000);
        ngx_cleaner_event.timer_set = 1;//标记此处已经设置过定时器了。
    }

    return cycle;


failed:

    if (!ngx_is_init_cycle(old_cycle)) {
        old_ccf = (ngx_core_conf_t *) ngx_get_conf(old_cycle->conf_ctx,
                                                   ngx_core_module);
        if (old_ccf->environment) {
            environ = old_ccf->environment;
        }
    }
    /* rollback the new cycle configuration */
    part = &cycle->open_files.part;
    file = part->elts;
    for (i = 0; /* void */ ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }
            part = part->next;
            file = part->elts;
            i = 0;
        }

        if (file[i].fd == NGX_INVALID_FILE || file[i].fd == ngx_stderr) {
            continue;
        }

        if (ngx_close_file(file[i].fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                          ngx_close_file_n " \"%s\" failed",
                          file[i].name.data);
        }
    }

    if (ngx_test_config) {
        ngx_destroy_cycle_pools(&conf);
        return NULL;
    }

    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        if (ls[i].fd == -1 || !ls[i].open) {
            continue;
        }

        if (ngx_close_socket(ls[i].fd) == -1) {
            ngx_log_error(NGX_LOG_EMERG, log, ngx_socket_errno,
                          ngx_close_socket_n " %V failed",
                          &ls[i].addr_text);
        }
    }

    ngx_destroy_cycle_pools(&conf);

    return NULL;
}


static void
ngx_destroy_cycle_pools(ngx_conf_t *conf)
{
    ngx_destroy_pool(conf->temp_pool);
    ngx_destroy_pool(conf->pool);
}


static ngx_int_t
ngx_cmp_sockaddr(struct sockaddr *sa1, struct sockaddr *sa2)
{
    struct sockaddr_in   *sin1, *sin2;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6  *sin61, *sin62;
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
    struct sockaddr_un   *saun1, *saun2;
#endif

    if (sa1->sa_family != sa2->sa_family) {
        return NGX_DECLINED;
    }

    switch (sa1->sa_family) {

#if (NGX_HAVE_INET6)
    case AF_INET6:
        sin61 = (struct sockaddr_in6 *) sa1;
        sin62 = (struct sockaddr_in6 *) sa2;

        if (sin61->sin6_port != sin62->sin6_port) {
            return NGX_DECLINED;
        }

        if (ngx_memcmp(&sin61->sin6_addr, &sin62->sin6_addr, 16) != 0) {
            return NGX_DECLINED;
        }

        break;
#endif

#if (NGX_HAVE_UNIX_DOMAIN)
    case AF_UNIX:
       saun1 = (struct sockaddr_un *) sa1;
       saun2 = (struct sockaddr_un *) sa2;

       if (ngx_memcmp(&saun1->sun_path, &saun2->sun_path,
                      sizeof(saun1->sun_path))
           != 0)
       {
           return NGX_DECLINED;
       }

       break;
#endif

    default: /* AF_INET */

        sin1 = (struct sockaddr_in *) sa1;
        sin2 = (struct sockaddr_in *) sa2;

        if (sin1->sin_port != sin2->sin_port) {
            return NGX_DECLINED;
        }

        if (sin1->sin_addr.s_addr != sin2->sin_addr.s_addr) {
            return NGX_DECLINED;
        }

        break;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_init_zone_pool(ngx_cycle_t *cycle, ngx_shm_zone_t *zn)
{
    u_char           *file;
    ngx_slab_pool_t  *sp;

    sp = (ngx_slab_pool_t *) zn->shm.addr;

    if (zn->shm.exists) {

        if (sp == sp->addr) {
            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "shared zone \"%V\" has no equal addresses: %p vs %p",
                      &zn->shm.name, sp->addr, sp);
        return NGX_ERROR;
    }

    sp->end = zn->shm.addr + zn->shm.size;
    sp->min_shift = 3;
    sp->addr = zn->shm.addr;

#if (NGX_HAVE_ATOMIC_OPS)

    file = NULL;

#else

    file = ngx_pnalloc(cycle->pool, cycle->lock_file.len + zn->shm.name.len);
    if (file == NULL) {
        return NGX_ERROR;
    }

    (void) ngx_sprintf(file, "%V%V%Z", &cycle->lock_file, &zn->shm.name);

#endif

    if (ngx_shmtx_create(&sp->mutex, (void *) &sp->lock, file) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_slab_init(sp);

    return NGX_OK;
}


ngx_int_t
ngx_create_pidfile(ngx_str_t *name, ngx_log_t *log)
{
    size_t      len;
    ngx_uint_t  create;
    ngx_file_t  file;
    u_char      pid[NGX_INT64_LEN + 2];

    if (ngx_process > NGX_PROCESS_MASTER) {
        return NGX_OK;
    }

    ngx_memzero(&file, sizeof(ngx_file_t));

    file.name = *name;
    file.log = log;

    create = ngx_test_config ? NGX_FILE_CREATE_OR_OPEN : NGX_FILE_TRUNCATE;

    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDWR,
                            create, NGX_FILE_DEFAULT_ACCESS);

    if (file.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file.name.data);
        return NGX_ERROR;
    }

    if (!ngx_test_config) {
        len = ngx_snprintf(pid, NGX_INT64_LEN + 2, "%P%N", ngx_pid) - pid;

        if (ngx_write_file(&file, pid, len, 0) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    if (ngx_close_file(file.fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", file.name.data);
    }

    return NGX_OK;
}


void
ngx_delete_pidfile(ngx_cycle_t *cycle)
{
    u_char           *name;
    ngx_core_conf_t  *ccf;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    name = ngx_new_binary ? ccf->oldpid.data : ccf->pid.data;

    if (ngx_delete_file(name) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      ngx_delete_file_n " \"%s\" failed", name);
    }
}


ngx_int_t
ngx_signal_process(ngx_cycle_t *cycle, char *sig)
{
    ssize_t           n;
    ngx_int_t         pid;
    ngx_file_t        file;
    ngx_core_conf_t  *ccf;
    u_char            buf[NGX_INT64_LEN + 2];

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "signal process started");
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    file.name = ccf->pid;
    file.log = cycle->log;
//打开pid文件，过滤掉里面的空格，找出pid进程id
    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDONLY,NGX_FILE_OPEN, NGX_FILE_DEFAULT_ACCESS);
    if (file.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file.name.data);
        return 1;
    }
    n = ngx_read_file(&file, buf, NGX_INT64_LEN + 2, 0);
    if (ngx_close_file(file.fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", file.name.data);
    }
    if (n == NGX_ERROR) {
        return 1;
    }
    while (n-- && (buf[n] == CR || buf[n] == LF)) { /* void */ }

    pid = ngx_atoi(buf, ++n);

    if (pid == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                      "invalid PID number \"%*s\" in \"%s\"",
                      n, buf, file.name.data);
        return 1;
    }

    return ngx_os_signal_process(cycle, sig, pid);

}


static ngx_int_t
ngx_test_lockfile(u_char *file, ngx_log_t *log)
{
#if !(NGX_HAVE_ATOMIC_OPS)
    ngx_fd_t  fd;

    fd = ngx_open_file(file, NGX_FILE_RDWR, NGX_FILE_CREATE_OR_OPEN,
                       NGX_FILE_DEFAULT_ACCESS);

    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file);
        return NGX_ERROR;
    }

    if (ngx_close_file(fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", file);
    }

    if (ngx_delete_file(file) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      ngx_delete_file_n " \"%s\" failed", file);
    }

#endif

    return NGX_OK;
}


void
ngx_reopen_files(ngx_cycle_t *cycle, ngx_uid_t user)
{
    ssize_t           n, len;
    ngx_fd_t          fd;
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_open_file_t  *file;

    part = &cycle->open_files.part;
    file = part->elts;

    for (i = 0; /* void */ ; i++) {
        if (i >= part->nelts) {//大于等于总文件数
            if (part->next == NULL) {
                break;//如果没有了，就返回
            }
            part = part->next;//如果还有，标记i = 0,重新来
            file = part->elts;
            i = 0;
        }
        if (file[i].name.len == 0) {
            continue;
        }
        len = file[i].pos - file[i].buffer;
        if (file[i].buffer && len != 0) {//要写进去吗，确认?
            n = ngx_write_fd(file[i].fd, file[i].buffer, len);
            if (n == NGX_FILE_ERROR) {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno, ngx_write_fd_n " to \"%s\" failed", file[i].name.data);
            } else if (n != len) {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, 0, ngx_write_fd_n " to \"%s\" was incomplete: %z of %uz", file[i].name.data, n, len);
            }

            file[i].pos = file[i].buffer;
        }
//再次打开以下这个文件
        fd = ngx_open_file(file[i].name.data, NGX_FILE_APPEND, NGX_FILE_CREATE_OR_OPEN, NGX_FILE_DEFAULT_ACCESS);
        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "reopen file \"%s\", old:%d new:%d", file[i].name.data, file[i].fd, fd);
        if (fd == NGX_INVALID_FILE) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,  ngx_open_file_n " \"%s\" failed", file[i].name.data);
            continue;
        }

#if !(NGX_WIN32)//如果是unix系的操作系统，需要设置文件拥有者啥的
        if (user != (ngx_uid_t) NGX_CONF_UNSET_UINT) {
            ngx_file_info_t  fi;
//得到文件的信息
            if (ngx_file_info((const char *) file[i].name.data, &fi) == NGX_FILE_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, ngx_file_info_n " \"%s\" failed",  file[i].name.data);
                if (ngx_close_file(fd) == NGX_FILE_ERROR) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, ngx_close_file_n " \"%s\" failed", file[i].name.data);
                }
            }
//修改文件所有者
            if (fi.st_uid != user) {
                if (chown((const char *) file[i].name.data, user, -1) == -1) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, "chown(\"%s\", %d) failed", file[i].name.data, user);
                    if (ngx_close_file(fd) == NGX_FILE_ERROR) {
                        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, ngx_close_file_n " \"%s\" failed", file[i].name.data);
                    }
                }
            }
//修改文件权限
            if ((fi.st_mode & (S_IRUSR|S_IWUSR)) != (S_IRUSR|S_IWUSR)) { fi.st_mode |= (S_IRUSR|S_IWUSR);
                if (chmod((const char *) file[i].name.data, fi.st_mode) == -1) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, "chmod() \"%s\" failed", file[i].name.data);
                    if (ngx_close_file(fd) == NGX_FILE_ERROR) {
                        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, ngx_close_file_n " \"%s\" failed",  file[i].name.data);
                    }
                }
            }
        }

        if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, "fcntl(FD_CLOEXEC) \"%s\" failed", file[i].name.data);
            if (ngx_close_file(fd) == NGX_FILE_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, ngx_close_file_n " \"%s\" failed", file[i].name.data);
            }
            continue;
        }
#endif
        if (ngx_close_file(file[i].fd) == NGX_FILE_ERROR) {//关闭掉老的文件句柄
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, ngx_close_file_n " \"%s\" failed",file[i].name.data);
        }
//赋值为新的文件句柄。放心吧，没有别的地方使用这个fd的，就我一个进程，多线程版本不在此
        file[i].fd = fd;
    }

#if !(NGX_WIN32)
    if (cycle->log->file->fd != STDERR_FILENO) {//int dup2(int oldfd, int newfd);
        if (dup2(cycle->log->file->fd, STDERR_FILENO) == -1) {//标准错误输出也设置为日志.那程序的fprint(stderr,)将输出到日志文件
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,"dup2(STDERR) failed");
        }
    }
#endif
}


ngx_shm_zone_t *
ngx_shared_memory_add(ngx_conf_t *cf, ngx_str_t *name, size_t size, void *tag)
{
    ngx_uint_t        i;
    ngx_shm_zone_t   *shm_zone;
    ngx_list_part_t  *part;

    part = &cf->cycle->shared_memory.part;
    shm_zone = part->elts;
    for (i = 0; /* void */ ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;//没有了。
            }
            part = part->next;//指向下一块buffer，重新从0开始查找。
            shm_zone = part->elts;
            i = 0;
        }
        if (name->len != shm_zone[i].shm.name.len) {
            continue;
        }
        if (ngx_strncmp(name->data, shm_zone[i].shm.name.data, name->len) != 0) {
            continue;
        }
        if (size && size != shm_zone[i].shm.size) {//名字相同，大小不一致，错误。
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                            "the size %uz of shared memory zone \"%V\" conflicts with already declared size %uz",
                            size, &shm_zone[i].shm.name, shm_zone[i].shm.size);
            return NULL;
        }
        if (tag != shm_zone[i].tag) {//tag不一致。这个是上层的数据，比如&ngx_http_limit_req_module
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "the shared memory zone \"%V\" is already declared for a different use", &shm_zone[i].shm.name);
            return NULL;
        }
        return &shm_zone[i];//找到了一个相同名字的，返回其在cf->cycle->shared_memory.part中的下标。
    }

    shm_zone = ngx_list_push(&cf->cycle->shared_memory);//没有找到已有名字的，因此新建一个。
    if (shm_zone == NULL) {
        return NULL;
    }

    shm_zone->data = NULL;
    shm_zone->shm.log = cf->cycle->log;
    shm_zone->shm.size = size;//该块共享内存的大小
    shm_zone->shm.name = *name;//共享内存名字。
    shm_zone->shm.exists = 0;
    shm_zone->init = NULL;
    shm_zone->tag = tag;//用来做标志，比如为&ngx_http_limit_req_module

    return shm_zone;//返回现在申请的新的shm_zone
}


static void
ngx_clean_old_cycles(ngx_event_t *ev)
{
    ngx_uint_t     i, n, found, live;
    ngx_log_t     *log;
    ngx_cycle_t  **cycle;

    log = ngx_cycle->log;
    ngx_temp_pool->log = log;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, log, 0, "clean old cycles");

    live = 0;

    cycle = ngx_old_cycles.elts;
    for (i = 0; i < ngx_old_cycles.nelts; i++) {

        if (cycle[i] == NULL) {
            continue;
        }

        found = 0;

        for (n = 0; n < cycle[i]->connection_n; n++) {
            if (cycle[i]->connections[n].fd != (ngx_socket_t) -1) {
                found = 1;

                ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "live fd:%d", n);

                break;
            }
        }

        if (found) {
            live = 1;
            continue;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "clean old cycle: %d", i);

        ngx_destroy_pool(cycle[i]->pool);
        cycle[i] = NULL;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, log, 0, "old cycles status: %d", live);

    if (live) {
        ngx_add_timer(ev, 30000);

    } else {
        ngx_destroy_pool(ngx_temp_pool);
        ngx_temp_pool = NULL;
        ngx_old_cycles.nelts = 0;
    }
}
