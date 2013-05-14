
/*
 * Copyright (C) Igor Sysoev
 */


#ifndef _NGX_HTTP_SCRIPT_H_INCLUDED_
#define _NGX_HTTP_SCRIPT_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    u_char                     *ip;
	/*关于pos && code: 每次调用code,都会将解析到的新的字符串放入pos指向的字符串处，
	然后将pos向后移动，下次进入的时候，会自动将数据追加到后面的。
	对于ip也是这个原理，code里面会将e->ip向后移动。移动的大小根据不同的变量类型相关。
	ip指向一快内存，其内容为变量相关的一个结构体，比如ngx_http_script_copy_capture_code_t，
	结构体之后，又是下一个ip的地址。比如移动时是这样的 :
	code = (ngx_http_script_copy_capture_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_copy_capture_code_t);//移动这么多位移。
	*/ 
    u_char                     *pos;//pos之前的数据就是解析成功的，后面的数据将追加到pos后面。
    ngx_http_variable_value_t  *sp;//这里貌似是用sp来保存中间结果，比如保存当前这一步的进度，到下一步好用e->sp--来找到上一步的结果。

    ngx_str_t                   buf;//存放结果，也就是buffer，pos指向其中。
    ngx_str_t                   line;//记录请求行URI  e->line = r->uri;

    /* the start of the rewritten arguments */
    u_char                     *args;

    unsigned                    flushed:1;
    unsigned                    skip:1;
    unsigned                    quote:1;
    unsigned                    is_args:1;
    unsigned                    log:1;

    ngx_int_t                   status;
    ngx_http_request_t         *request;//所属的请求
} ngx_http_script_engine_t;


typedef struct {
    ngx_conf_t                 *cf;
    ngx_str_t                  *source;//指向字符串，比如http://$http_host/aa.mp4

    ngx_array_t               **flushes;
    ngx_array_t               **lengths;//指向外部的编译结果数组&index->lengths;等
    ngx_array_t               **values;

    ngx_uint_t                  variables;//source指向的字符串中有几个变量
    ngx_uint_t                  ncaptures;//最大的一个$3 的数字
    ngx_uint_t                  captures_mask;
    ngx_uint_t                  size;

    void                       *main;

    unsigned                    compile_args:1;
    unsigned                    complete_lengths:1;
    unsigned                    complete_values:1;
    unsigned                    zero:1;
    unsigned                    conf_prefix:1;
    unsigned                    root_prefix:1;

    unsigned                    dup_capture:1;
    unsigned                    args:1;
} ngx_http_script_compile_t;


typedef struct {
    ngx_str_t                   value;//要解析的字符串。
    ngx_uint_t                 *flushes;
    void                       *lengths;
    void                       *values;
} ngx_http_complex_value_t;


typedef struct {
    ngx_conf_t                 *cf;
    ngx_str_t                  *value;//后面的参数字符串，要解析的字符串。
    ngx_http_complex_value_t   *complex_value;//复杂表达式的lcode,codes数组的结构，存储了复杂表达式的解析信息。

    unsigned                    zero:1;
    unsigned                    conf_prefix:1;
    unsigned                    root_prefix:1;
} ngx_http_compile_complex_value_t;


typedef void (*ngx_http_script_code_pt) (ngx_http_script_engine_t *e);
typedef size_t (*ngx_http_script_len_code_pt) (ngx_http_script_engine_t *e);


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   len;
} ngx_http_script_copy_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   index;//变量在cmcf->variables中的下标
} ngx_http_script_var_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    ngx_http_set_variable_pt    handler;
    uintptr_t                   data;
} ngx_http_script_var_handler_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   n;//第几个capture，翻倍了的值。就是数字$1,$2,用来寻找r->captures里面的下标，已2为单位。
} ngx_http_script_copy_capture_code_t;


#if (NGX_PCRE)

typedef struct {
    ngx_http_script_code_pt     code;//当前的code，第一个函数，为ngx_http_script_regex_start_code
    ngx_http_regex_t           *regex;//解析后的正则表达式。
    ngx_array_t                *lengths;//我这个正则表达式对应的lengths。依靠它来解析 第二部分 rewrite ^(.*)$ http://$http_host.mp4 break;
    									//lengths里面包含一系列code,用来求目标url的大小的。
    uintptr_t                   size;
    uintptr_t                   status;
    uintptr_t                   next;//next的含义为;如果当前code匹配失败，那么下一个code的位移是在什么地方，这些东西全部放在一个数组里面的。

    uintptr_t                   test:1;//我是要看看是否正则匹配成功，你待会匹配的时候记得放个变量到堆栈里。
    uintptr_t                   negative_test:1;
    uintptr_t                   uri:1;//是否是URI匹配。
    uintptr_t                   args:1;

    /* add the r->args to the new arguments */
    uintptr_t                   add_args:1;//是否自动追加参数到rewrite后面。如果目标结果串后面用问好结尾，则nginx不会拷贝参数到后面的

    uintptr_t                   redirect:1;//nginx判断，如果是用http://等开头的rewrite，就代表是垮域重定向。会做302处理。
    uintptr_t                   break_cycle:1;
	//rewrite最后的参数是break，将rewrite后的地址在当前location标签中执行。具体参考ngx_http_script_regex_start_code

    ngx_str_t                   name;
} ngx_http_script_regex_code_t;


typedef struct {
    ngx_http_script_code_pt     code;

    uintptr_t                   uri:1;
    uintptr_t                   args:1;

    /* add the r->args to the new arguments */
    uintptr_t                   add_args:1;

    uintptr_t                   redirect:1;
} ngx_http_script_regex_end_code_t;

#endif


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   conf_prefix;
} ngx_http_script_full_name_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   status;//返回的状态码。return code [ text ]
    ngx_http_complex_value_t    text;//ccv.complex_value = &ret->text;后面的参数的脚本引擎地址。
} ngx_http_script_return_code_t;


typedef enum {
    ngx_http_script_file_plain = 0,
    ngx_http_script_file_not_plain,
    ngx_http_script_file_dir,
    ngx_http_script_file_not_dir,
    ngx_http_script_file_exists,
    ngx_http_script_file_not_exists,
    ngx_http_script_file_exec,
    ngx_http_script_file_not_exec
} ngx_http_script_file_op_e;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   op;
} ngx_http_script_file_code_t;


typedef struct {
    ngx_http_script_code_pt     code;
    uintptr_t                   next;
    void                      **loc_conf;//新的location配置。
} ngx_http_script_if_code_t;


typedef struct {
    ngx_http_script_code_pt     code;//ngx_http_script_complex_value_code
    ngx_array_t                *lengths;//复杂指令里面嵌套了其他code
} ngx_http_script_complex_value_code_t;


typedef struct {
    ngx_http_script_code_pt     code;//可以为ngx_http_script_value_code
    uintptr_t                   value;//数字大小，或者如果text_data不是数字串，就为0.
    uintptr_t                   text_len;//简单字符串的长度。
    uintptr_t                   text_data;//记录字符串地址value->data;
} ngx_http_script_value_code_t;


void ngx_http_script_flush_complex_value(ngx_http_request_t *r,
    ngx_http_complex_value_t *val);
ngx_int_t ngx_http_complex_value(ngx_http_request_t *r,
    ngx_http_complex_value_t *val, ngx_str_t *value);
ngx_int_t ngx_http_compile_complex_value(ngx_http_compile_complex_value_t *ccv);
char *ngx_http_set_complex_value_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


ngx_int_t ngx_http_test_predicates(ngx_http_request_t *r,
    ngx_array_t *predicates);
char *ngx_http_set_predicate_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

ngx_uint_t ngx_http_script_variables_count(ngx_str_t *value);
ngx_int_t ngx_http_script_compile(ngx_http_script_compile_t *sc);
u_char *ngx_http_script_run(ngx_http_request_t *r, ngx_str_t *value,
    void *code_lengths, size_t reserved, void *code_values);
void ngx_http_script_flush_no_cacheable_variables(ngx_http_request_t *r,
    ngx_array_t *indices);

void *ngx_http_script_start_code(ngx_pool_t *pool, ngx_array_t **codes,
    size_t size);
void *ngx_http_script_add_code(ngx_array_t *codes, size_t size, void *code);

size_t ngx_http_script_copy_len_code(ngx_http_script_engine_t *e);
void ngx_http_script_copy_code(ngx_http_script_engine_t *e);
size_t ngx_http_script_copy_var_len_code(ngx_http_script_engine_t *e);
void ngx_http_script_copy_var_code(ngx_http_script_engine_t *e);
size_t ngx_http_script_copy_capture_len_code(ngx_http_script_engine_t *e);
void ngx_http_script_copy_capture_code(ngx_http_script_engine_t *e);
size_t ngx_http_script_mark_args_code(ngx_http_script_engine_t *e);
void ngx_http_script_start_args_code(ngx_http_script_engine_t *e);
#if (NGX_PCRE)
void ngx_http_script_regex_start_code(ngx_http_script_engine_t *e);
void ngx_http_script_regex_end_code(ngx_http_script_engine_t *e);
#endif
void ngx_http_script_return_code(ngx_http_script_engine_t *e);
void ngx_http_script_break_code(ngx_http_script_engine_t *e);
void ngx_http_script_if_code(ngx_http_script_engine_t *e);
void ngx_http_script_equal_code(ngx_http_script_engine_t *e);
void ngx_http_script_not_equal_code(ngx_http_script_engine_t *e);
void ngx_http_script_file_code(ngx_http_script_engine_t *e);
void ngx_http_script_complex_value_code(ngx_http_script_engine_t *e);
void ngx_http_script_value_code(ngx_http_script_engine_t *e);
void ngx_http_script_set_var_code(ngx_http_script_engine_t *e);
void ngx_http_script_var_set_handler_code(ngx_http_script_engine_t *e);
void ngx_http_script_var_code(ngx_http_script_engine_t *e);
void ngx_http_script_nop_code(ngx_http_script_engine_t *e);


#endif /* _NGX_HTTP_SCRIPT_H_INCLUDED_ */
