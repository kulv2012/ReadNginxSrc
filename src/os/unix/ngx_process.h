
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_PROCESS_H_INCLUDED_
#define _NGX_PROCESS_H_INCLUDED_


#include <ngx_setproctitle.h>


typedef pid_t       ngx_pid_t;

#define NGX_INVALID_PID  -1

typedef void (*ngx_spawn_proc_pt) (ngx_cycle_t *cycle, void *data);

typedef struct {
    ngx_pid_t           pid;
    int                 status;
    ngx_socket_t        channel[2];//socketpair创建的socket句柄.0写，1读，用于给进程间通信的

    ngx_spawn_proc_pt   proc;//设置工作进程的处理函数ngx_worker_process_cycle，会fork之后调用的
    void               *data;//执行函数的参数
    char               *name;//本进程的名字

    unsigned            respawn:1;//重新创建
    unsigned            just_spawn:1; //第一次创建的
    unsigned            detached:1;//热代码替换
    unsigned            exiting:1;//正在退出的
    unsigned            exited:1;
} ngx_process_t;


typedef struct {
    char         *path;
    char         *name;
    char *const  *argv;
    char *const  *envp;
} ngx_exec_ctx_t;


#define NGX_MAX_PROCESSES         1024

#define NGX_PROCESS_NORESPAWN     -1
#define NGX_PROCESS_JUST_SPAWN    -2	
#define NGX_PROCESS_RESPAWN       -3	//重新创建进程
#define NGX_PROCESS_JUST_RESPAWN  -4	//重新加载配置
#define NGX_PROCESS_DETACHED      -5   //如果类型为NGX_PROCESS_DETACHED，则说明是热代码替换(热代码替换也是通过这个函数进行处理的)，因此不需要新建socketpair。 


#define ngx_getpid   getpid

#ifndef ngx_log_pid
#define ngx_log_pid  ngx_pid
#endif


ngx_pid_t ngx_spawn_process(ngx_cycle_t *cycle,
    ngx_spawn_proc_pt proc, void *data, char *name, ngx_int_t respawn);
ngx_pid_t ngx_execute(ngx_cycle_t *cycle, ngx_exec_ctx_t *ctx);
ngx_int_t ngx_init_signals(ngx_log_t *log);
void ngx_debug_point(void);


#if (NGX_HAVE_SCHED_YIELD)
#define ngx_sched_yield()  sched_yield()
#else
#define ngx_sched_yield()  usleep(1)
#endif


extern int            ngx_argc;
extern char         **ngx_argv;
extern char         **ngx_os_argv;

extern ngx_pid_t      ngx_pid;
extern ngx_socket_t   ngx_channel;
extern ngx_int_t      ngx_process_slot;
extern ngx_int_t      ngx_last_process;
extern ngx_process_t  ngx_processes[NGX_MAX_PROCESSES];


#endif /* _NGX_PROCESS_H_INCLUDED_ */
