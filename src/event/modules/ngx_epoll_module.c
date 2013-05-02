
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


typedef struct {
    ngx_uint_t  events;//控制一次返回的事件数These directives specify how many events may be passed to/from kernel, using appropriate method.
} ngx_epoll_conf_t;


static ngx_int_t ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer);
static void ngx_epoll_done(ngx_cycle_t *cycle);
static ngx_int_t ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_add_connection(ngx_connection_t *c);
static ngx_int_t ngx_epoll_del_connection(ngx_connection_t *c,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer,
    ngx_uint_t flags);

#if (NGX_HAVE_FILE_AIO)
static void ngx_epoll_eventfd_handler(ngx_event_t *ev);
#endif

static void *ngx_epoll_create_conf(ngx_cycle_t *cycle);
static char *ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf);

static int                  ep = -1;//epoll句柄
static struct epoll_event  *event_list;//k epoll的返回事件集合数组
static ngx_uint_t           nevents;//上面的数组大小

#if (NGX_HAVE_FILE_AIO)

int                         ngx_eventfd = -1;
aio_context_t               ngx_aio_ctx = 0;

static ngx_event_t          ngx_eventfd_event;
static ngx_connection_t     ngx_eventfd_conn;

#endif

static ngx_str_t      epoll_name = ngx_string("epoll");

static ngx_command_t  ngx_epoll_commands[] = {
	//控制一次返回的事件数These directives specify how many events may be passed to/from kernel, using appropriate method.
    { ngx_string("epoll_events"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,//一个数字。
      0,
      offsetof(ngx_epoll_conf_t, events),
      NULL },

      ngx_null_command
};


ngx_event_module_t  ngx_epoll_module_ctx = {
    &epoll_name,
    ngx_epoll_create_conf,               /* create configuration */
    ngx_epoll_init_conf,                 /* init configuration */

    {
        ngx_epoll_add_event,             /* add an event */
        ngx_epoll_del_event,             /* delete an event */
        ngx_epoll_add_event,             /* enable an event */
        ngx_epoll_del_event,             /* disable an event */
        ngx_epoll_add_connection,        /* add an connection */
        ngx_epoll_del_connection,        /* delete an connection */
        NULL,                            /* process the changes */
        ngx_epoll_process_events,        /* process the events */
        ngx_epoll_init,                  /* init the events */
        ngx_epoll_done,                  /* done the events */
    }
};

ngx_module_t  ngx_epoll_module = {
    NGX_MODULE_V1,
    &ngx_epoll_module_ctx,               /* module context */
    ngx_epoll_commands,                  /* module directives */
    NGX_EVENT_MODULE,                    /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    NULL,                                /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    NULL,                                /* exit process */
    NULL,                                /* exit master */
    NGX_MODULE_V1_PADDING
};


#if (NGX_HAVE_FILE_AIO)

/*
 * We call io_setup(), io_destroy() io_submit(), and io_getevents() directly
 * as syscalls instead of libaio usage, because the library header file
 * supports eventfd() since 0.3.107 version only.
 *
 * Also we do not use eventfd() in glibc, because glibc supports it
 * since 2.8 version and glibc maps two syscalls eventfd() and eventfd2()
 * into single eventfd() function with different number of parameters.
 */

static long
io_setup(u_int nr_reqs, aio_context_t *ctx)
{
    return syscall(SYS_io_setup, nr_reqs, ctx);
}


static int
io_destroy(aio_context_t ctx)
{
    return syscall(SYS_io_destroy, ctx);
}


static long
io_getevents(aio_context_t ctx, long min_nr, long nr, struct io_event *events,
    struct timespec *tmo)
{
    return syscall(SYS_io_getevents, ctx, min_nr, nr, events, tmo);
}

#endif


static ngx_int_t
ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer)
{//干了一件大事: 设置所有事件模型的函数指针 ngx_event_actions。以后的事件相关的调用都会使用epoll的。
    ngx_epoll_conf_t  *epcf;
    epcf = ngx_event_get_conf(cycle->conf_ctx, ngx_epoll_module);//得到我在event模块集合里面的配置。
    if (ep == -1) {//如果epoll句柄为无效状态，则创建之
        ep = epoll_create(cycle->connection_n / 2);
        if (ep == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno, "epoll_create() failed");
            return NGX_ERROR;
        }
#if (NGX_HAVE_FILE_AIO)//nginx对AIO的支持再此。需要2.6.22版本以上。
        {
        int                 n;
        struct epoll_event  ee;
        ngx_eventfd = syscall(SYS_eventfd, 0);
        if (ngx_eventfd == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,  "eventfd() failed");
            return NGX_ERROR;
        }
        n = 1;
        if (ioctl(ngx_eventfd, FIONBIO, &n) == -1) {//允许套接字的非阻塞模式
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,  "ioctl(eventfd, FIONBIO) failed");
        }
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,  "eventfd: %d", ngx_eventfd);
        n = io_setup(1024, &ngx_aio_ctx);
        if (n != 0) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, -n, "io_setup() failed");
            return NGX_ERROR;
        }
        ngx_eventfd_event.data = &ngx_eventfd_conn;
        ngx_eventfd_event.handler = ngx_epoll_eventfd_handler;//有事件的回调。
        ngx_eventfd_event.log = cycle->log;
        ngx_eventfd_event.active = 1;
        ngx_eventfd_conn.fd = ngx_eventfd;
        ngx_eventfd_conn.read = &ngx_eventfd_event;
        ngx_eventfd_conn.log = cycle->log;

        ee.events = EPOLLIN|EPOLLET;
        ee.data.ptr = &ngx_eventfd_conn;
        if (epoll_ctl(ep, EPOLL_CTL_ADD, ngx_eventfd, &ee) == -1) {//将这个eventfd加入epoll里面。将AIO和epoll结合起来，避免获取状态的阻塞操作。
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,  "epoll_ctl(EPOLL_CTL_ADD, eventfd) failed");
            return NGX_ERROR;
        }
        }
#endif
    }

    if (nevents < epcf->events) {
        if (event_list) {
            ngx_free(event_list);
        }
        event_list = ngx_alloc(sizeof(struct epoll_event) * epcf->events,  cycle->log);
        if (event_list == NULL) {
            return NGX_ERROR;
        }
    }

    nevents = epcf->events;
    ngx_io = ngx_os_io;
    ngx_event_actions = ngx_epoll_module_ctx.actions;

#if (NGX_HAVE_CLEAR_EVENT)
    ngx_event_flags = NGX_USE_CLEAR_EVENT
#else
    ngx_event_flags = NGX_USE_LEVEL_EVENT
#endif
                      |NGX_USE_GREEDY_EVENT
                      |NGX_USE_EPOLL_EVENT; //使用epoll方式，这里决定事件的选取

    return NGX_OK;
}


static void
ngx_epoll_done(ngx_cycle_t *cycle)
{
    if (close(ep) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno, "epoll close() failed");
    }
    ep = -1;
#if (NGX_HAVE_FILE_AIO)
    if (io_destroy(ngx_aio_ctx) != 0) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,  "io_destroy() failed");
    }
    ngx_aio_ctx = 0;
#endif
    ngx_free(event_list);
    event_list = NULL;
    nevents = 0;
}


static ngx_int_t
ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    int                  op;
    uint32_t             events, prev;
    ngx_event_t         *e;
    ngx_connection_t    *c;
    struct epoll_event   ee;

    c = ev->data;

    events = (uint32_t) event;
//如果要注册POLLIN事件，就是用c->write，啥意思
    if (event == NGX_READ_EVENT) {//k POLLIN
        e = c->write;//这只是为了判断一下这个链接的另外一个事件是否是active，如果是，那说明已经在epoll中了，我们只能EPOLL_CTL_MOD
        prev = EPOLLOUT;//但你怎么就确定，prev是EPOLLOUT呢··
#if (NGX_READ_EVENT != EPOLLIN)
        events = EPOLLIN;
#endif

    } else {//我们要增加的是写事件，那我下面需要判断一下读事件，如果读事件是active的，那说明需要MOD修改，而不是增加
        e = c->read;
        prev = EPOLLIN;
#if (NGX_WRITE_EVENT != EPOLLOUT)
        events = EPOLLOUT;
#endif
    }

    if (e->active) {//已经在使用中，已经在epoll中，那说明之前肯定有读或者写事件的，那么，推断上面的判断是正确的
        op = EPOLL_CTL_MOD;
        events |= prev;

    } else {
        op = EPOLL_CTL_ADD;//否则，操作时增加，不是修改
    }

    ee.events = events | (uint32_t) flags;
    ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0, "epoll add event: fd:%d op:%d ev:%08XD", c->fd, op, ee.events);

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno, "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }
    ev->active = 1;
#if 0
    ev->oneshot = (flags & NGX_ONESHOT_EVENT) ? 1 : 0;
#endif
    return NGX_OK;
}


static ngx_int_t
ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    int                  op;
    uint32_t             prev;
    ngx_event_t         *e;
    ngx_connection_t    *c;
    struct epoll_event   ee;
    /*
     * when the file descriptor is closed, the epoll automatically deletes
     * it from its queue, so we do not need to delete explicity the event
     * before the closing the file descriptor
     */
    if (flags & NGX_CLOSE_EVENT) {
        ev->active = 0;
        return NGX_OK;
    }

    c = ev->data;

    if (event == NGX_READ_EVENT) {//如果要删除的是读事件
        e = c->write;//为什么要进行相反的判断，是为了确保一个连接的读，写事件是否已经加入epoll，从而决定是修改还是删除EPOLL_CTL_DEL
        prev = EPOLLOUT;//则留下写事件

    } else {//如果要删除的是写事件，则留下读事件
        e = c->read;
        prev = EPOLLIN;
    }

    if (e->active) {//如果已经在epoll中，变为MOD操作
        op = EPOLL_CTL_MOD;
        ee.events = prev | (uint32_t) flags;
        ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);

    } else {//否则变为删除操作
        op = EPOLL_CTL_DEL;
        ee.events = 0;
        ee.data.ptr = NULL;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "epoll del event: fd:%d op:%d ev:%08XD",
                   c->fd, op, ee.events);

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    ev->active = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_add_connection(ngx_connection_t *c)
{
    struct epoll_event  ee;
    ee.events = EPOLLIN|EPOLLOUT|EPOLLET;//读写事件都需要关注，采用边缘触发机制
    ee.data.ptr = (void *) ((uintptr_t) c | c->read->instance);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0, "epoll add connection: fd:%d ev:%08XD", c->fd, ee.events);
//就一个epoll句柄吗，多点呗
    if (epoll_ctl(ep, EPOLL_CTL_ADD, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno, "epoll_ctl(EPOLL_CTL_ADD, %d) failed", c->fd);
        return NGX_ERROR;
    }
    c->read->active = 1;//标记在使用中
    c->write->active = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_del_connection(ngx_connection_t *c, ngx_uint_t flags)
{
    int                 op;
    struct epoll_event  ee;

    /*
     * when the file descriptor is closed the epoll automatically deletes
     * it from its queue so we do not need to delete explicity the event
     * before the closing the file descriptor
     */

    if (flags & NGX_CLOSE_EVENT) {
        c->read->active = 0;
        c->write->active = 0;
        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll del connection: fd:%d", c->fd);

    op = EPOLL_CTL_DEL;
    ee.events = 0;
    ee.data.ptr = NULL;

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    c->read->active = 0;
    c->write->active = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer, ngx_uint_t flags)
{//工作进程不断的调用这个，问有没有事件发生。包括计时器啥的。如果有，可能放入队列或者进行处理
//工作进程的循环调用了这个宏:ngx_process_events，其实就是指向这里。进行是否有事件的判断。
    int                events;
    uint32_t           revents;
    ngx_int_t          instance, i;
    ngx_uint_t         level;
    ngx_err_t          err;
    ngx_log_t         *log;
    ngx_event_t       *rev, *wev, **queue;
    ngx_connection_t  *c;

    /* NGX_TIMER_INFINITE == INFTIM */
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "epoll timer: %M", timer);
    events = epoll_wait(ep, event_list, (int) nevents, timer);
//带超时的等待,这个时间是当前红黑树里面最小的还要多久超时，也就是说，如果超时，再次就定时器到了或者有东西了
    err = (events == -1) ? ngx_errno : 0;
    if (flags & NGX_UPDATE_TIME || ngx_event_timer_alarm) {
//如果上面要求更新时间，或者ngx_event_timer_alarm是啥意思,ngx_timer_signal_handler,定时器到了，需要更新时间。
        ngx_time_update();//需要更新时间
    }

    if (err) {//有错
        if (err == NGX_EINTR) {//错在定时器到了，而啥都没有返回。被中断了
            if (ngx_event_timer_alarm) {
                ngx_event_timer_alarm = 0;//我已经更新了，下回再通知我，白袍一趟
                return NGX_OK;
            }
            level = NGX_LOG_INFO;
        } else {
            level = NGX_LOG_ALERT;
        }//怎么不大于错误信息
        ngx_log_error(level, cycle->log, err, "epoll_wait() failed");
        return NGX_ERROR;
    }

    if (events == 0) {//还是一个事情都没有
        if (timer != NGX_TIMER_INFINITE) {//没有用精确时间，timer等于刚刚等待之前，红黑树里面最快超时的那个还要多久，现在到了
            return NGX_OK;//真的是超时了，而且是红黑树里面的最小的超时了。不过返回值也没有用的
        }
		//事情也没有，又不是超时
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,  "epoll_wait() returned no events without timeout");
        return NGX_ERROR;
    }
    ngx_mutex_lock(ngx_posted_events_mutex);//多线程才有用
    log = cycle->log;

    for (i = 0; i < events; i++) {//遍历返回数组的每个元素
        c = event_list[i].data.ptr;//取得保存进去的连接
        instance = (uintptr_t) c & 1;//这是啥意思，去掉所有高位，保留第一个位,设置的时候就是这么设置的
        c = (ngx_connection_t *) ((uintptr_t) c & (uintptr_t) ~1);//ee.data.ptr = (void *) ((uintptr_t) c | c->read->instance);
//在设置进epoll的时候，设置为c|read->instance了，现在去掉这个位，还原成连接。因为明知道连接的地址最低位不会为1是吗
        rev = c->read;//获取其可读事件结构
		/*http://blog.csdn.net/dingyujie/article/details/7531498
		   fd在当前处理时变成-1，意味着在之前的事件处理时，把当前请求关闭了，
		   即close fd并且当前事件对应的连接已被还回连接池，此时该次事件就不应该处理了，作废掉。
		   其次，如果fd > 0,那么是否本次事件就可以正常处理，就可以认为是一个合法的呢？答案是否定的。
		   这里我们给出一个情景：
		   当前的事件序列是： A ... B ... C ...
		   其中A,B,C是本次epoll上报的其中一些事件，但是他们此时却相互牵扯：
		   A事件是向客户端写的事件，B事件是新连接到来，C事件是A事件中请求建立的upstream连接，此时需要读源数据，
		   然后A事件处理时，由于种种原因将C中upstream的连接关闭了(比如客户端关闭，此时需要同时关闭掉取源连接)，自然
		   C事件中请求对应的连接也被还到连接池(注意，客户端连接与upstream连接使用同一连接池)，
		   而B事件中的请求到来，获取连接池时，刚好拿到了之前C中upstream还回来的连接结构，当前需要处理C事件的时候，
		   c->fd != -1，因为该连接被B事件拿去接收请求了，而rev->instance在B使用时，已经将其值取反了，所以此时C事件epoll中
		   携带的instance就不等于rev->instance了，因此我们也就识别出该stale event，跳过不处理了。
		  */
        if (c->fd == -1 || rev->instance != instance) {//处理stale event ，
            /*
             * the stale event from a file descriptor that was just closed in this iteration
             */
            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,  "epoll: stale event %p", c);
            continue;
        }
        revents = event_list[i].events;//得到发生的事件
        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, log, 0, "epoll: fd:%d ev:%04XD d:%p",  c->fd, revents, event_list[i].data.ptr);
        if (revents & (EPOLLERR|EPOLLHUP)) {//如果出现错误，打个日志，debug的时候
            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, log, 0,  "epoll_wait() error on fd:%d ev:%04XD", c->fd, revents);
        }

         if ((revents & (EPOLLERR|EPOLLHUP)) && (revents & (EPOLLIN|EPOLLOUT)) == 0)
        {//如果有错误，而且没有读写事件，那么增加读写事件? 
            /*
             * if the error events were returned without EPOLLIN or EPOLLOUT,
             * then add these flags to handle the events at least in one
             * active handler
             */
            revents |= EPOLLIN|EPOLLOUT;
        }

        if ((revents & EPOLLIN) && rev->active) {
            if ((flags & NGX_POST_THREAD_EVENTS) && !rev->accept) {//不是监听SOCK
                rev->posted_ready = 1;

            } else {
                rev->ready = 1;
            }

            if (flags & NGX_POST_EVENTS) {
                 queue = (ngx_event_t **) (rev->accept ? &ngx_posted_accept_events : &ngx_posted_events);
//如果设置了NGX_POST_EVENTS，我们把新连接和一般连接可读事件放入不同的链表，待会好优先处理新连接事件，尽快释放锁
                ngx_locked_post_event(rev, queue);//根据是否是监听sock，放入不同的队列里面

            } else {//不需要后处理事件，那我们就直接回调得了
                rev->handler(rev);//带上这个连接的结构，里面可以找到这个fd.当然，为什么我们这里不用判断是否是监听sock呢，因为这个注册的handle不同
//新连接为ngx_event_accept，自然就调用了监听的函数了
            }
        }

        wev = c->write;
//这里有个bug，就是没有判断instance了，在1.0.9b版本已经修复了。具体见这里:http://forum.nginx.org/read.php?29,217919,217919#msg-217919
        if ((revents & EPOLLOUT) && wev->active) {//可写
            if (flags & NGX_POST_THREAD_EVENTS) {
                wev->posted_ready = 1;

            } else {
                wev->ready = 1;
            }

            if (flags & NGX_POST_EVENTS) {
                ngx_locked_post_event(wev, &ngx_posted_events);

            } else {
                wev->handler(wev);
	//差不多读写事件可以放一起闭着眼睛处理的，因为不同的地方在handler，但是因为NGX_POST_EVENTS在accept上有点区别，所以分开了
            }
        }
    }
    ngx_mutex_unlock(ngx_posted_events_mutex);//多线程才有用
    return NGX_OK;
}


#if (NGX_HAVE_FILE_AIO)

static void
ngx_epoll_eventfd_handler(ngx_event_t *ev)
{
    int               n;
    long              i, events;
    uint64_t          ready;
    ngx_err_t         err;
    ngx_event_t      *e;
    ngx_event_aio_t  *aio;
    struct io_event   event[64];
    struct timespec   ts;

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd handler");
    n = read(ngx_eventfd, &ready, 8);
    err = ngx_errno;
    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd: %d", n);
    if (n != 8) {
        if (n == -1) {
            if (err == NGX_EAGAIN) {
                return;
            }
            ngx_log_error(NGX_LOG_ALERT, ev->log, err, "read(eventfd) failed");
            return;
        }
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "read(eventfd) returned only %d bytes", n);
        return;
    }
    ts.tv_sec = 0;
    ts.tv_nsec = 0;

    while (ready) {//超时0秒查询epoll的状态。实际上是不等待。
        events = io_getevents(ngx_aio_ctx, 1, 64, event, &ts);
        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0, "io_getevents: %l", events);
        if (events > 0) {
            ready -= events;
            for (i = 0; i < events; i++) {
                ngx_log_debug4(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                               "io_event: %uXL %uXL %L %L",  event[i].data, event[i].obj,  event[i].res, event[i].res2);

                e = (ngx_event_t *) (uintptr_t) event[i].data;
                e->complete = 1;
                e->active = 0;
                e->ready = 1;
                aio = e->data;
                aio->res = event[i].res;
                ngx_post_event(e, &ngx_posted_events);//有事件发生了。挂入后处理事件队列中。读写已经完成了的。
            }
            continue;
        }
        if (events == 0) {
            return;
        }
        /* events < 0 */
        ngx_log_error(NGX_LOG_ALERT, ev->log, -events, "io_getevents() failed");
        return;
    }
}

#endif


static void *
ngx_epoll_create_conf(ngx_cycle_t *cycle)
{//啥也没有
    ngx_epoll_conf_t  *epcf;
    epcf = ngx_palloc(cycle->pool, sizeof(ngx_epoll_conf_t));
    if (epcf == NULL) {
        return NULL;
    }
    epcf->events = NGX_CONF_UNSET;
    return epcf;
}


static char *
ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_epoll_conf_t *epcf = conf;
    ngx_conf_init_uint_value(epcf->events, 512);
    return NGX_CONF_OK;
}
