
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_array_t  *codes;/* uintptr_t *///此结构保存着code_t的实现函数数组，用来做重定向的。
						//这里是句柄数组组成的，逻辑上分为一组一组的，每条rewrite语句占用一组，每一组可能包含好几条code()函数指针等数据。
						//如果匹配失败就通过next跳过本组。
    ngx_uint_t    stack_size;//这个是什么?代码里面找不到。看样子是e->sp[]数组的大小，其用来存储正则简析时，存放类似堆栈的临时值。

    ngx_flag_t    log;
    ngx_flag_t    uninitialized_variable_warn;
} ngx_http_rewrite_loc_conf_t;


static void *ngx_http_rewrite_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_rewrite_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_rewrite_init(ngx_conf_t *cf);
static char *ngx_http_rewrite(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_rewrite_return(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_rewrite_break(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_http_rewrite_if(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char * ngx_http_rewrite_if_condition(ngx_conf_t *cf,
    ngx_http_rewrite_loc_conf_t *lcf);
static char *ngx_http_rewrite_variable(ngx_conf_t *cf,
    ngx_http_rewrite_loc_conf_t *lcf, ngx_str_t *value);
static char *ngx_http_rewrite_set(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char * ngx_http_rewrite_value(ngx_conf_t *cf,
    ngx_http_rewrite_loc_conf_t *lcf, ngx_str_t *value);


static ngx_command_t  ngx_http_rewrite_commands[] = {

    { ngx_string("rewrite"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                       |NGX_CONF_TAKE23,
      ngx_http_rewrite,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("return"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                       |NGX_CONF_TAKE12,
      ngx_http_rewrite_return,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("break"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                       |NGX_CONF_NOARGS,
      ngx_http_rewrite_break,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("if"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_BLOCK|NGX_CONF_1MORE,
      ngx_http_rewrite_if,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("set"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                       |NGX_CONF_TAKE2,
      ngx_http_rewrite_set,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("rewrite_log"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF
                        |NGX_HTTP_LIF_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_rewrite_loc_conf_t, log),
      NULL },

    { ngx_string("uninitialized_variable_warn"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF
                        |NGX_HTTP_LIF_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_rewrite_loc_conf_t, uninitialized_variable_warn),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_rewrite_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_rewrite_init,                 /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_rewrite_create_loc_conf,      /* create location configration */
    ngx_http_rewrite_merge_loc_conf        /* merge location configration */
};


ngx_module_t  ngx_http_rewrite_module = {
    NGX_MODULE_V1,
    &ngx_http_rewrite_module_ctx,          /* module context */
    ngx_http_rewrite_commands,             /* module directives */
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

/*ngx_http_rewrite_init函数在初始化时将这个函数加入SERVER_REWRITE_PHASE 和 REWRITE_PHASE过程中。
//这样每次进入ngx_core_run_phrases()后会调用这个地方进行重定向。
重定向完毕后，如果不是break，就将进入下一次find config 阶段，
后者成功后又将进行重新的rewrite，就像有个新的请求到来一样。
*/
static ngx_int_t ngx_http_rewrite_handler(ngx_http_request_t *r)
{
    ngx_http_script_code_pt       code;
    ngx_http_script_engine_t     *e;
    ngx_http_rewrite_loc_conf_t  *rlcf;

    rlcf = ngx_http_get_module_loc_conf(r, ngx_http_rewrite_module);
    if (rlcf->codes == NULL) {//如果没有处理函数，直接返回，因为这个模块肯定没有一条rewrite。也就是不需要
        return NGX_DECLINED;//如果返回OK就代表处理完毕，不用处理i后面的其他过程了。
    }
	//新建一个脚本引擎，开始进行codes的解析。
    e = ngx_pcalloc(r->pool, sizeof(ngx_http_script_engine_t));
    if (e == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
	//下面的stack_size到底在哪里设置的/代码里面都找不到。
	//功能是用来存放计算的中间结果。
    e->sp = ngx_pcalloc(r->pool,  rlcf->stack_size * sizeof(ngx_http_variable_value_t));
    if (e->sp == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    e->ip = rlcf->codes->elts;
    e->request = r;
    e->quote = 1;
    e->log = rlcf->log;
    e->status = NGX_DECLINED;
	/*对于这样的: rewrite ^(.*)$ http://$http_host.mp4 break; 下面的循环是i这样走的
	ngx_http_rewrite_handler
		1. ngx_http_script_regex_start_code 解析完了正则表达式。并求出总长度，设置到了e上了
			1.1 ngx_http_script_copy_len_code		7
			1.2 ngx_http_script_copy_var_len_code 	18
			1.3 ngx_http_script_copy_len_code		4	=== 29 
			
		2. ngx_http_script_copy_code		拷贝"http://" 到e->buf
		3. ngx_http_script_copy_var_code	拷贝"115.28.34.175:8881"
		4. ngx_http_script_copy_code 		拷贝".mp4"
		5. ngx_http_script_regex_end_code
	*/

    while (*(uintptr_t *) e->ip) {//遍历每一个函数指针，分别调用他们。
        code = *(ngx_http_script_code_pt *) e->ip;
        code(e);//执行对应指令的函数，比如if等，
    }
    if (e->status == NGX_DECLINED) {
        return NGX_DECLINED;
    }
    if (r->err_status == 0) {
        return e->status;
    }
    return r->err_status;
}


static ngx_int_t ngx_http_rewrite_var(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data)
{//这只是一个默认的get_handler，实际上会设置为对应的函数比如ngx_http_get_indexed_variable
//ngx_http_rewrite_set函数会将这个设置为初始的get_handler
    ngx_http_variable_t          *var;
    ngx_http_core_main_conf_t    *cmcf;
    ngx_http_rewrite_loc_conf_t  *rlcf;

    rlcf = ngx_http_get_module_loc_conf(r, ngx_http_rewrite_module);
    if (rlcf->uninitialized_variable_warn == 0) {
        *v = ngx_http_variable_null_value;
        return NGX_OK;
    }
    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    var = cmcf->variables.elts;
    /*
     * the ngx_http_rewrite_module sets variables directly in r->variables,
     * and they should be handled by ngx_http_get_indexed_variable(),
     * so the handler is called only if the variable is not initialized
     */
    ngx_log_error(NGX_LOG_WARN, r->connection->log, 0, "using uninitialized \"%V\" variable", &var[data].name);
    *v = ngx_http_variable_null_value;
    return NGX_OK;
}


static void *
ngx_http_rewrite_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_rewrite_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_rewrite_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    conf->stack_size = NGX_CONF_UNSET_UINT;
    conf->log = NGX_CONF_UNSET;
    conf->uninitialized_variable_warn = NGX_CONF_UNSET;
    return conf;
}


static char *
ngx_http_rewrite_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_rewrite_loc_conf_t *prev = parent;
    ngx_http_rewrite_loc_conf_t *conf = child;

    uintptr_t  *code;
    ngx_conf_merge_value(conf->log, prev->log, 0);
    ngx_conf_merge_value(conf->uninitialized_variable_warn,  prev->uninitialized_variable_warn, 1);
    ngx_conf_merge_uint_value(conf->stack_size, prev->stack_size, 10);

    if (conf->codes == NULL) {
        return NGX_CONF_OK;
    }
    if (conf->codes == prev->codes) {
        return NGX_CONF_OK;
    }
    code = ngx_array_push_n(conf->codes, sizeof(uintptr_t));
    if (code == NULL) {
        return NGX_CONF_ERROR;
    }
    *code = (uintptr_t) NULL;//最后追加一个code，这样可以结束。
    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_rewrite_init(ngx_conf_t *cf)
{//将ngx_http_rewrite_handler句柄设置到SERVER_REWRITE_PHASE 和 REWRITE_PHASE过程中去。
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_SERVER_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_rewrite_handler;

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_rewrite_handler;
    return NGX_OK;
}

/*
1. 解析正则表达式，提取子模式，命名子模式存入variables等；
2.	解析第四个参数last,break等。
3.调用ngx_http_script_compile将目标字符串解析为结构化的codes句柄数组，以便解析时进行计算；
4.根据第三步的结果，生成lcf->codes 组，后续rewrite时，一组组的进行匹配即可。失败自动跳过本组，到达下一组rewrite
*/
static char * ngx_http_rewrite(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//碰到"rewrite"指令时调用这里。
//比如rewrite ^(/xyz/aa.*)$ http://$http_host/aa.mp4 break; 
    ngx_http_rewrite_loc_conf_t  *lcf = conf;

    ngx_str_t                         *value;
    ngx_uint_t                         last;
    ngx_regex_compile_t                rc;
    ngx_http_script_code_pt           *code;
    ngx_http_script_compile_t          sc;
    ngx_http_script_regex_code_t      *regex;
    ngx_http_script_regex_end_code_t  *regex_end;
    u_char                             errstr[NGX_MAX_CONF_ERRSTR];
	//在本模块的codes的尾部，这里应该算一块新的指令组的头部，增加一个开始回调ngx_http_script_regex_start_code
	//这里申请的是ngx_http_script_regex_code_t，其第一个成员code为经常被e->ip指向的函数指针，被当做code调用的。
    regex = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_regex_code_t));
    if (regex == NULL) {
        return NGX_CONF_ERROR;
    }
    ngx_memzero(regex, sizeof(ngx_http_script_regex_code_t));
    value = cf->args->elts;
    ngx_memzero(&rc, sizeof(ngx_regex_compile_t));

    rc.pattern = value[1];//记录 ^(/xyz/aa.*)$
    rc.err.len = NGX_MAX_CONF_ERRSTR;
    rc.err.data = errstr;

    /* TODO: NGX_REGEX_CASELESS */
	//解析正则表达式，填写ngx_http_regex_t结构并返回。正则句柄，命名子模式等都在里面了。
    regex->regex = ngx_http_regex_compile(cf, &rc);
    if (regex->regex == NULL) {
        return NGX_CONF_ERROR;
    }
	//ngx_http_script_regex_start_code函数匹配正则表达式，计算目标字符串长度并分配空间。
	//将其设置为第一个code函数，求出目标字符串大小。尾部还有ngx_http_script_regex_end_code
    regex->code = ngx_http_script_regex_start_code;
    regex->uri = 1;
    regex->name = value[1];//记录正则表达式

    if (value[2].data[value[2].len - 1] == '?') {//如果目标结果串后面用问好结尾，则nginx不会拷贝参数到后面的
        /* the last "?" drops the original arguments */
        value[2].len--;
    } else {
        regex->add_args = 1;//自动追加参数。
    }

    last = 0;
    if (ngx_strncmp(value[2].data, "http://", sizeof("http://") - 1) == 0
        || ngx_strncmp(value[2].data, "https://", sizeof("https://") - 1) == 0
        || ngx_strncmp(value[2].data, "$scheme", sizeof("$scheme") - 1) == 0)
    {//nginx判断，如果是用http://等开头的rewrite，就代表是垮域重定向。会做302处理。
        regex->status = NGX_HTTP_MOVED_TEMPORARILY;
        regex->redirect = 1;//标记要做302重定向。
        last = 1;
    }

    if (cf->args->nelts == 4) {//处理后面的参数。
        if (ngx_strcmp(value[3].data, "last") == 0) {
            last = 1;
        } else if (ngx_strcmp(value[3].data, "break") == 0) {
            regex->break_cycle = 1;//需要break，这里体现了跟last的区别，参考ngx_http_script_regex_start_code。
            //这个标志会影响正则解析成功之后的代码，让其设置了一个url_changed=0,也就骗nginx说，URL没有变化，
            //你不用重新来跑find config phrase了。不然还得像个新连接一样跑一遍。
            last = 1;
        } else if (ngx_strcmp(value[3].data, "redirect") == 0) {
            regex->status = NGX_HTTP_MOVED_TEMPORARILY;
            regex->redirect = 1;
            last = 1;
        } else if (ngx_strcmp(value[3].data, "permanent") == 0) {
            regex->status = NGX_HTTP_MOVED_PERMANENTLY;
            regex->redirect = 1;
            last = 1;
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid parameter \"%V\"", &value[3]);
            return NGX_CONF_ERROR;
        }
    }

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
    sc.cf = cf;
    sc.source = &value[2];//字符串 http://$http_host/aa.mp4
    sc.lengths = &regex->lengths;//输出参数，里面会包含一些如何求目标字符串长度的函数回调。如上会包含三个: 常量 变量 常量
    sc.values = &lcf->codes;//将子模式存入这里。
    sc.variables = ngx_http_script_variables_count(&value[2]);
    sc.main = regex;//这是顶层的表达式，里面包含了lengths等。
    sc.complete_lengths = 1;
    sc.compile_args = !regex->redirect;

    if (ngx_http_script_compile(&sc) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    regex = sc.main;//这里这么做的原因是可能上面会改变内存地址。
    regex->size = sc.size;
    regex->args = sc.args;

    if (sc.variables == 0 && !sc.dup_capture) {//如果没有变量，那就将lengths置空，这样就不用做多余的正则解析而直接进入字符串拷贝codes
        regex->lengths = NULL;
    }
    regex_end = ngx_http_script_add_code(lcf->codes, sizeof(ngx_http_script_regex_end_code_t), &regex);
    if (regex_end == NULL) {
        return NGX_CONF_ERROR;
    }
	/*经过上面的处理，后面的rewrite会解析出如下的函数结构: rewrite ^(.*)$ http://$http_host.mp4 break;
	ngx_http_script_regex_start_code 解析完了正则表达式。根据lengths求出总长度，申请空间。
			ngx_http_script_copy_len_code		7
			ngx_http_script_copy_var_len_code 	18
			ngx_http_script_copy_len_code		4	=== 29 

	ngx_http_script_copy_code		拷贝"http://" 到e->buf
	ngx_http_script_copy_var_code	拷贝"115.28.34.175:8881"
	ngx_http_script_copy_code 		拷贝".mp4"
	ngx_http_script_regex_end_code
	*/

    regex_end->code = ngx_http_script_regex_end_code;//结束回调。对应前面的开始。
    regex_end->uri = regex->uri;
    regex_end->args = regex->args;
    regex_end->add_args = regex->add_args;//是否添加参数。
    regex_end->redirect = regex->redirect;

    if (last) {//参考上面，如果rewrite 末尾有last,break,等，就不会再次解析后面的数据了，那么，就将code设置为空。
        code = ngx_http_script_add_code(lcf->codes, sizeof(uintptr_t), &regex);
        if (code == NULL) {
            return NGX_CONF_ERROR;
        }
        *code = NULL;
    }
	//下一个解析句柄组的地址。
    regex->next = (u_char *) lcf->codes->elts + lcf->codes->nelts - (u_char *) regex;
    return NGX_CONF_OK;
}

/*Syntax:	return code [ text ]
			return code URL 
			return URL

解析完后的表达式为: 
	ngx_http_script_return_code 就挂着一个，如果后面除了状态码还有第二个参数
					那么久需要调用脚本引擎进行解析了。return和rewrite指令的脚本应用数据结够稍微有点不同。
*/
static char * ngx_http_rewrite_return(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_rewrite_loc_conf_t  *lcf = conf;

    u_char                            *p;
    ngx_str_t                         *value, *v;
    ngx_http_script_return_code_t     *ret;
    ngx_http_compile_complex_value_t   ccv;

    ret = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_return_code_t));
    if (ret == NULL) {
        return NGX_CONF_ERROR;
    }
    value = cf->args->elts;
    ngx_memzero(ret, sizeof(ngx_http_script_return_code_t));
    ret->code = ngx_http_script_return_code;
    p = value[1].data;
    ret->status = ngx_atoi(p, value[1].len);//将第一个参数简单当个返回状态码解析，如果失败，那就是个url
    if (ret->status == (uintptr_t) NGX_ERROR) {
        if (cf->args->nelts == 2
            && (ngx_strncmp(p, "http://", sizeof("http://") - 1) == 0
                || ngx_strncmp(p, "https://", sizeof("https://") - 1) == 0
                || ngx_strncmp(p, "$scheme", sizeof("$scheme") - 1) == 0))
        {//第二个参数为URL，那就直接302重定向吧
            ret->status = NGX_HTTP_MOVED_TEMPORARILY;
            v = &value[1];
        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid return code \"%V\"", &value[1]);
            return NGX_CONF_ERROR;
        }
    } else {
        if (cf->args->nelts == 2) {//就1个参数，形如return code。直接返回，返回码一件设置了。
            return NGX_CONF_OK;
        }
        v = &value[2];
    }
	//后面还有一个参数，那这个不好搞了。需要解析表达式了。不过下面lengths都没有
    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
    ccv.cf = cf;
    ccv.value = v;//后面的参数字符串，要解析的字符串。
    ccv.complex_value = &ret->text;
	//进行复杂表达式的解析，里面会调用ngx_http_script_compile脚本引擎进行编译。
	//进行语法等解析，计算其lengths,codes等。然后反应到ccv.complex_value = &ret->text;
    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_rewrite_break(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{//这个最简单了，没什么条件，就光秃秃的一个break;语句在那；
//处理方式很简单: 增加一个code到lcf->codes里面。然后设置其回调为ngx_http_script_break_code。
//其实这个函数也就设置一个变量e->request->uri_changed = 0;你懂的。break和last的区别在此。
    ngx_http_rewrite_loc_conf_t *lcf = conf;

    ngx_http_script_code_pt  *code;
    code = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(uintptr_t));
    if (code == NULL) {
        return NGX_CONF_ERROR;
    }
    *code = ngx_http_script_break_code;

    return NGX_CONF_OK;
}

/* Syntax:	if ( condition ) { ... }


*/
static char * ngx_http_rewrite_if(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
//碰到if语句就调用这里。
    ngx_http_rewrite_loc_conf_t  *lcf = conf;

    void                         *mconf;
    char                         *rv;
    u_char                       *elts;
    ngx_uint_t                    i;
    ngx_conf_t                    save;
    ngx_http_module_t            *module;
    ngx_http_conf_ctx_t          *ctx, *pctx;
    ngx_http_core_loc_conf_t     *clcf, *pclcf;
    ngx_http_script_if_code_t    *if_code;
    ngx_http_rewrite_loc_conf_t  *nlcf;
	//ngx_http_conf_ctx_t，这个眼熟了，其实if语句就类似于location的作用，因此这里又建立了配置的树形节点。
    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_conf_ctx_t));
    if (ctx == NULL) {//申请一个新的上下文结构。
        return NGX_CONF_ERROR;
    }

    pctx = cf->ctx;//备份之前的上线问结构
    ctx->main_conf = pctx->main_conf;//公用其父节点的main_conf
    ctx->srv_conf = pctx->srv_conf;//公用其父节点的srv_conf
    //其实if就只有loc_conf不一样，类似location

    ctx->loc_conf = ngx_pcalloc(cf->pool, sizeof(void *) * ngx_http_max_module);
    if (ctx->loc_conf == NULL) {
        return NGX_CONF_ERROR;
    }
    for (i = 0; ngx_modules[i]; i++) {
        if (ngx_modules[i]->type != NGX_HTTP_MODULE) {
            continue;
        }
        module = ngx_modules[i]->ctx;
        if (module->create_loc_conf) {//调用每个HTTP模块的create_loc_conf
            mconf = module->create_loc_conf(cf);
            if (mconf == NULL) {
                 return NGX_CONF_ERROR;
            }
            ctx->loc_conf[ngx_modules[i]->ctx_index] = mconf;
        }
    }

    pclcf = pctx->loc_conf[ngx_http_core_module.ctx_index];//父节点的核心core loc配置

    clcf = ctx->loc_conf[ngx_http_core_module.ctx_index];//if{}块的核心core loc配置。
    clcf->loc_conf = ctx->loc_conf;//将loc配置设置为新的
    clcf->name = pclcf->name;//名字拷贝就行
    clcf->noname = 1;
	//加location的原因是什么?为什么要加这个?其实就是增加一个虚拟的loction配置节点吧。
	//在父节点中增加一个虚拟的location节点。
    if (ngx_http_add_location(cf, &pclcf->locations, clcf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }
	//做个脚本解析
    if (ngx_http_rewrite_if_condition(cf, lcf) != NGX_CONF_OK) {
        return NGX_CONF_ERROR;
    }
	//在lcf->codes中增加一个项，lcf=conf，也就是if节点的配置。增加一个回调。
    if_code = ngx_array_push_n(lcf->codes, sizeof(ngx_http_script_if_code_t));
    if (if_code == NULL) {
        return NGX_CONF_ERROR;
    }
	//追加一个code，用来收尾，也就是: 如果匹配成功，替换loc_conf,
    if_code->code = ngx_http_script_if_code;//然后调用ngx_http_update_location_config更新各项配置。
    elts = lcf->codes->elts;
    /* the inner directives must be compiled to the same code array */
    nlcf = ctx->loc_conf[ngx_http_rewrite_module.ctx_index];
    nlcf->codes = lcf->codes;//这里比较有意思，将if里面的所有rewrite等一系列的codes合并到父节点的codes，
    //然后在后面设置next指针的时候，if_code->next跳的跨度是整个if语句。当然，if里面的那些指令其实也是有结构的。
    //其里面也有next，只是指向内部。

    save = *cf;
    cf->ctx = ctx;//临时替换为if{}块的ctx，这样就可以进入ngx_conf_parse解析配置了。

    if (pclcf->name.len == 0) {
        if_code->loc_conf = NULL;
        cf->cmd_type = NGX_HTTP_SIF_CONF;
    } else {
        if_code->loc_conf = ctx->loc_conf;
        cf->cmd_type = NGX_HTTP_LIF_CONF;
    }
    rv = ngx_conf_parse(cf, NULL);
    *cf = save;//还原为父节点的配置。
    if (rv != NGX_CONF_OK) {
        return rv;
    }

	//下面这个next跳转比较有意思，它将整个if里面的codes整体往又移动了一层，类似代码缩进了。
	//从而达到一个效果: 如果if 没有匹配通过，一次next跳转就能跳过父节点的该if块的所有codes项而进入下面处理。
    if (elts != lcf->codes->elts) {
        if_code = (ngx_http_script_if_code_t *) ((u_char *) if_code + ((u_char *) lcf->codes->elts - elts));
    }
    if_code->next = (u_char *) lcf->codes->elts + lcf->codes->nelts - (u_char *) if_code;
    /* the code array belong to parent block */
    nlcf->codes = NULL;

    return NGX_CONF_OK;
}

/*Syntax:	if ( condition ) { ... }
	eg: if ($request_method = POST ) 
	这个函数做了很多功能: 
	1.对于相等匹配 ， ngx_http_rewrite_variable解析出变量的值后，再用ngx_http_rewrite_value解析计算出=/!=运算符号后面的脚本复杂表达式的值；
		然后设置ngx_http_script_equal_code回调做相等匹配。
	2.对于正则匹配，同样ngx_http_rewrite_variable解析出变量的值后，挂载一个ngx_http_script_regex_start_code做正则匹配；
	3.对文件存在性判断，挂载ngx_http_script_file_code做目录，文件等判断；

	以上三个挂载的函数，判断完成后都将会将结果存在堆栈上面，供上层挂载的ngx_http_script_if_code调用判断是否if匹配成功。
	如果成功，将替换堆栈。更新各项loc_conf配置。
*/
static char * ngx_http_rewrite_if_condition(ngx_conf_t *cf, ngx_http_rewrite_loc_conf_t *lcf)
{
    u_char                        *p;
    size_t                         len;
    ngx_str_t                     *value;
    ngx_uint_t                     cur, last;
    ngx_regex_compile_t            rc;
    ngx_http_script_code_pt       *code;
    ngx_http_script_file_code_t   *fop;
    ngx_http_script_regex_code_t  *regex;
    u_char                         errstr[NGX_MAX_CONF_ERRSTR];

    value = cf->args->elts;
    last = cf->args->nelts - 1;
//下面做一系列参数合法性校验。
    if (value[1].len < 1 || value[1].data[0] != '(') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid condition \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }
    if (value[1].len == 1) {//第一个是(，且就一个括号
        cur = 2;
    } else {//括号和字符相连。左右缩短。
        cur = 1;//当前处理的是第一个参数。
        value[1].len--;
        value[1].data++;
    }
    if (value[last].len < 1 || value[last].data[value[last].len - 1] != ')') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid condition \"%V\"", &value[last]);
        return NGX_CONF_ERROR;
    }
    if (value[last].len == 1) {
        last--;
    } else {
        value[last].len--;
        value[last].data[value[last].len] = '\0';
    }

    len = value[cur].len;
    p = value[cur].data;

    if (len > 1 && p[0] == '$') {
        if (cur != last && cur + 2 != last) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid condition \"%V\"", &value[cur]);
            return NGX_CONF_ERROR;
        }
		//根据名字获取一个变量在varialbes[]中的下标。然后为其设置一个code到lcf->codes。
		//code=ngx_http_script_var_code，注意这个函数，这个函数会增加堆栈值的。也就是会保存堆栈的。
        if (ngx_http_rewrite_variable(cf, lcf, &value[cur]) != NGX_CONF_OK) {
            return NGX_CONF_ERROR;//这样就在lcf->codes[]数组里面增加了一个code，进行匹配解析的时候会先调用这个值。
        }
        if (cur == last) {//就一个参数。
            return NGX_CONF_OK;
        }

        cur++;//处理下一个参数

        len = value[cur].len;
        p = value[cur].data;

        if (len == 1 && p[0] == '=') {//如果这个是个等于号，那么最后一个参数必须是个值。用来做相等比较的。
        	//解析字符串，如果里面有变量，则调用ngx_http_script_compile进行脚本解析。
        	//解析字符串，如果里面有变量，则调用ngx_http_script_compile进行脚本解析。
			//做了2件事: 1.设置code为ngx_http_script_complex_value_code；2.计算编译了value的符合表达式，结果存入lengths,values,codes
            if (ngx_http_rewrite_value(cf, lcf, &value[last]) != NGX_CONF_OK) {
                return NGX_CONF_ERROR;
            }
            code = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(uintptr_t));
            if (code == NULL) {
                return NGX_CONF_ERROR;
            }
			/*然后在后面挂载1个比较操作函数，如果成功，就改变标志就行了。
			注意，当解析到这个函数的时候，堆栈已经放入了2个值了，从底向上为:ngx_http_rewrite_variable存入变量的值，
			ngx_http_script_complex_value_code存入复杂表达式匹配出来的值。因此这个code正好可以取堆栈上的1,2个位置就是要比较的2个字符串。
			*/
            *code = ngx_http_script_equal_code;//挂载一个相等匹配。
            return NGX_CONF_OK;
        }
        if (len == 2 && p[0] == '!' && p[1] == '=') {
            if (ngx_http_rewrite_value(cf, lcf, &value[last]) != NGX_CONF_OK) {
                return NGX_CONF_ERROR;
            }
            code = ngx_http_script_start_code(cf->pool, &lcf->codes,  sizeof(uintptr_t));
            if (code == NULL) {
                return NGX_CONF_ERROR;
            }//同上，只是挂在的是一个不等匹配
            *code = ngx_http_script_not_equal_code;
            return NGX_CONF_OK;
        }

        if ((len == 1 && p[0] == '~')
            || (len == 2 && p[0] == '~' && p[1] == '*')
            || (len == 2 && p[0] == '!' && p[1] == '~')
            || (len == 3 && p[0] == '!' && p[1] == '~' && p[2] == '*'))
        {
        	//这里不好玩了，因为是要做正则匹配神马的，那么里面得挂点其他的函数了。
        	//注意，这里不是要做正则匹配的rewrite，不同，这里只要知道是否匹配通过就行。不用求字符串。
        	//因此，这里可以没有lengths,
            regex = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_regex_code_t));
            if (regex == NULL) {
                return NGX_CONF_ERROR;
            }
            ngx_memzero(regex, sizeof(ngx_http_script_regex_code_t));
            ngx_memzero(&rc, sizeof(ngx_regex_compile_t));
            rc.pattern = value[last];//最后一个就是正则表达式。
            rc.options = (p[len - 1] == '*') ? NGX_REGEX_CASELESS : 0;//不区分大小写
            rc.err.len = NGX_MAX_CONF_ERRSTR;
            rc.err.data = errstr;
            regex->regex = ngx_http_regex_compile(cf, &rc);//编译这个正则表达式，但是没有处理codes的。
            //注意这个跟ngx_http_script_compile的区别，后者是做脚本解析计算的，ngx_http_regex_compile是做正则匹配的。别混淆了。
            if (regex->regex == NULL) {
                return NGX_CONF_ERROR;
            }
			///挂载这个正则匹配节点，但是却不做具体工作，因为没有lengths,后面没有codes做拷贝了的。
			//这里挂在这个正则匹配的节点就够了，因为设置了test=1后，函数会在堆栈里面设置一个值，告诉ngx_http_script_if_code匹配结果的
            regex->code = ngx_http_script_regex_start_code;
            regex->next = sizeof(ngx_http_script_regex_code_t);
            regex->test = 1;//我是要看看是否正则匹配成功，你待会匹配的时候记得放个变量到堆栈里。
            if (p[0] == '!') {
                regex->negative_test = 1;
            }
            regex->name = value[last];
            return NGX_CONF_OK;
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected \"%V\" in condition", &value[cur]);
        return NGX_CONF_ERROR;

    } else if ((len == 2 && p[0] == '-') || (len == 3 && p[0] == '!' && p[1] == '-'))
    {//处理!-f operators;等
        if (cur + 1 != last) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid condition \"%V\"", &value[cur]);
            return NGX_CONF_ERROR;
        }

        value[last].data[value[last].len] = '\0';
        value[last].len++;
		//后面肯定是个变量了，要做存在性判断，比如文件，目录是否存在，是否有权限，所以需要求出最后一个参数的值。
		//这个value参数可以是复杂表达式，反正这里就是挂在复杂表达式的相关code.
		//做了2件事: 1.设置code为ngx_http_script_complex_value_code；2.计算编译了value的符合表达式，结果存入lengths,values,codes
        if (ngx_http_rewrite_value(cf, lcf, &value[last]) != NGX_CONF_OK) {
            return NGX_CONF_ERROR;//上面也有介绍了。
        }
        fop = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_file_code_t));
        if (fop == NULL) {
            return NGX_CONF_ERROR;
        }
        fop->code = ngx_http_script_file_code;//挂载一个文件是否存在的判断。
        if (p[1] == 'f') {
            fop->op = ngx_http_script_file_plain;
            return NGX_CONF_OK;
        }
        if (p[1] == 'd') {
            fop->op = ngx_http_script_file_dir;
            return NGX_CONF_OK;
        }
        if (p[1] == 'e') {
            fop->op = ngx_http_script_file_exists;
            return NGX_CONF_OK;
        }
        if (p[1] == 'x') {
            fop->op = ngx_http_script_file_exec;
            return NGX_CONF_OK;
        }
        if (p[0] == '!') {
            if (p[2] == 'f') {
                fop->op = ngx_http_script_file_not_plain;
                return NGX_CONF_OK;
            }
            if (p[2] == 'd') {
                fop->op = ngx_http_script_file_not_dir;
                return NGX_CONF_OK;
            }
            if (p[2] == 'e') {
                fop->op = ngx_http_script_file_not_exists;
                return NGX_CONF_OK;
            }
            if (p[2] == 'x') {
                fop->op = ngx_http_script_file_not_exec;
                return NGX_CONF_OK;
            }
        }
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "invalid condition \"%V\"", &value[cur]);
        return NGX_CONF_ERROR;
    }
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "invalid condition \"%V\"", &value[cur]);
    return NGX_CONF_ERROR;
}


static char *
ngx_http_rewrite_variable(ngx_conf_t *cf, ngx_http_rewrite_loc_conf_t *lcf, ngx_str_t *value)
{//根据名字获取一个变量在varialbes[]中的下标。然后为其设置一个code到lcf->codes。
//句柄为变量读取函数ngx_http_script_var_code。
    ngx_int_t                    index;
    ngx_http_script_var_code_t  *var_code;

    value->len--;
    value->data++;
    index = ngx_http_get_variable_index(cf, value);
    if (index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }
    var_code = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_var_code_t));
    if (var_code == NULL) {
        return NGX_CONF_ERROR;
    }
    var_code->code = ngx_http_script_var_code;
    var_code->index = index;
    return NGX_CONF_OK;
}

/*Syntax:	set $variable value
1. 将$variable加入到变量系统中，cmcf->variables_keys->keys和cmcf->variables。


a. 如果value是简单字符串，那么解析之后，lcf->codes就会追加这样的到后面: 
	ngx_http_script_value_code  直接简单字符串指向一下就行，都不用拷贝了。
b. 如果value是复杂的包含变量的串，那么lcf->codes就会追加如下的进去 :
	ngx_http_script_complex_value_code  调用lengths的lcode获取组合字符串的总长度，并且申请内存
		lengths
	values，这里根据表达式的不同而不同。 分别将value代表的复杂表达式拆分成语法单元，进行一个个求值，并合并在一起。
	ngx_http_script_set_var_code		负责将上述合并出的最终结果设置到variables[]数组中去。

*/
static char * ngx_http_rewrite_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_rewrite_loc_conf_t  *lcf = conf;
    ngx_int_t                            index;
    ngx_str_t                           *value;
    ngx_http_variable_t                 *v;
    ngx_http_script_var_code_t          *vcode;
    ngx_http_script_var_handler_code_t  *vhcode;

    value = cf->args->elts;
    if (value[1].data[0] != '$') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid variable name \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }
    value[1].len--;
    value[1].data++;
	//下面根据这个变量名，将其加入到cmcf->variables_keys->keys里面。
    v = ngx_http_add_variable(cf, &value[1], NGX_HTTP_VAR_CHANGEABLE);
    if (v == NULL) {
        return NGX_CONF_ERROR;
    }
	//将其加入到cmcf->variables里面，并返回其下标
    index = ngx_http_get_variable_index(cf, &value[1]);
    if (index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    if (v->get_handler == NULL
        && ngx_strncasecmp(value[1].data, (u_char *) "http_", 5) != 0
        && ngx_strncasecmp(value[1].data, (u_char *) "sent_http_", 10) != 0
        && ngx_strncasecmp(value[1].data, (u_char *) "upstream_http_", 14) != 0)
    {//如果变量名称以如上开头，则其get_handler为ngx_http_rewrite_var，data为index 。
        v->get_handler = ngx_http_rewrite_var;//设置一个默认的handler。在ngx_http_variables_init_vars里面其实是会将著名的变量设置好的。
        v->data = index;
    }
	//解析字符串，如果里面有变量，则调用ngx_http_script_compile进行脚本解析。
	//做了2件事: 1.设置code为ngx_http_script_complex_value_code；2.计算编译了value的符合表达式，结果存入lengths,values,codes
    if (ngx_http_rewrite_value(cf, lcf, &value[2]) != NGX_CONF_OK) {
        return NGX_CONF_ERROR;
    }

    if (v->set_handler) {//好像从没有见过set_handler被设置过
        vhcode = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_var_handler_code_t));
        if (vhcode == NULL) {
            return NGX_CONF_ERROR;
        }
        vhcode->code = ngx_http_script_var_set_handler_code;
        vhcode->handler = v->set_handler;
        vhcode->data = v->data;
        return NGX_CONF_OK;
    }

    vcode = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_var_code_t));
    if (vcode == NULL) {
        return NGX_CONF_ERROR;
    }
	//在set $variable value指令的最后一个code会调用这里保存值。这样就能将这个值保存起来了。
    vcode->code = ngx_http_script_set_var_code;
    vcode->index = (uintptr_t) index;
    return NGX_CONF_OK;
}

/* 对一个字符串做脚本语法解析，其结果为给lcf->codes插入项，插入的为: 
a. 如果value是简单字符串，那么解析之后，lcf->codes就会追加这样的到后面: 
	ngx_http_script_value_code  直接简单字符串指向一下就行，都不用拷贝了。
b. 如果value是复杂的包含变量的串，那么lcf->codes就会追加如下的进去 :
	ngx_http_script_complex_value_code  调用lengths的lcode获取组合字符串的总长度，并且申请内存
		lengths
	values，这里根据表达式的不同而不同。 分别将value代表的复杂表达式拆分成语法单元，进行一个个求值，并合并在一起。
	至于后面怎么办，看具体应用，是比较值呢，还是做其他的挂在 。
	比如set 指令胡i挂载ngx_http_script_set_var_code函数设置变量值。if指令会挂在比较函数ngx_http_script_equal_code
*/
static char * ngx_http_rewrite_value(ngx_conf_t *cf, ngx_http_rewrite_loc_conf_t *lcf, ngx_str_t *value)
{//解析字符串，如果里面有变量，则调用ngx_http_script_compile进行脚本解析。
//自结构中lengths存放在complex->lengths，values()计算函数放入当前location的codes中。
//ngx_http_script_complex_value_code函数在进行rewrite匹配的时候会遍历lengths中的句柄，求出目标字符串长度的。
    ngx_int_t                              n;
    ngx_http_script_compile_t              sc;
    ngx_http_script_value_code_t          *val;
    ngx_http_script_complex_value_code_t  *complex;

    n = ngx_http_script_variables_count(value);//或者这个字符串的变量数目
    if (n == 0) {//如果没有变量，是个简单字符串，那就简单了。
        val = ngx_http_script_start_code(cf->pool, &lcf->codes,  sizeof(ngx_http_script_value_code_t));
        if (val == NULL) {
            return NGX_CONF_ERROR;
        }
        n = ngx_atoi(value->data, value->len);
        if (n == NGX_ERROR) {//不是字符串是么
            n = 0;
        }
		//简单的字符串处理函数，直接指向一下就行了，什么内存分配，都不需要，因为这里压根就不需要分配内存。
        val->code = ngx_http_script_value_code;//简单字符串的code.
        val->value = (uintptr_t) n;
        val->text_len = (uintptr_t) value->len;
        val->text_data = (uintptr_t) value->data;
        return NGX_CONF_OK;
    }
	//带有$的变量，nginx里面称作“complex value”.这里申请一个新的codes，
    complex = ngx_http_script_start_code(cf->pool, &lcf->codes, sizeof(ngx_http_script_complex_value_code_t));
    if (complex == NULL) {
        return NGX_CONF_ERROR;
    }
	//申请一个start code,的指令数组。类似于ngx_http_script_regex_start_code
    complex->code = ngx_http_script_complex_value_code;
    complex->lengths = NULL;

    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));

    sc.cf = cf;
    sc.source = value;
    sc.lengths = &complex->lengths;
    sc.values = &lcf->codes;
    sc.variables = n;
    sc.complete_lengths = 1;
	//下面进入编译阶段，这样会用不同的code填充complex->lengths，&lcf->codes
    if (ngx_http_script_compile(&sc) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
