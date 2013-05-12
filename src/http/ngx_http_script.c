
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static ngx_int_t ngx_http_script_init_arrays(ngx_http_script_compile_t *sc);
static ngx_int_t ngx_http_script_done(ngx_http_script_compile_t *sc);
static ngx_int_t ngx_http_script_add_copy_code(ngx_http_script_compile_t *sc,
    ngx_str_t *value, ngx_uint_t last);
static ngx_int_t ngx_http_script_add_var_code(ngx_http_script_compile_t *sc,
    ngx_str_t *name);
static ngx_int_t ngx_http_script_add_args_code(ngx_http_script_compile_t *sc);
#if (NGX_PCRE)
static ngx_int_t ngx_http_script_add_capture_code(ngx_http_script_compile_t *sc,
     ngx_uint_t n);
#endif
static ngx_int_t
     ngx_http_script_add_full_name_code(ngx_http_script_compile_t *sc);
static size_t ngx_http_script_full_name_len_code(ngx_http_script_engine_t *e);
static void ngx_http_script_full_name_code(ngx_http_script_engine_t *e);


#define ngx_http_script_exit  (u_char *) &ngx_http_script_exit_code

static uintptr_t ngx_http_script_exit_code = (uintptr_t) NULL;


void ngx_http_script_flush_complex_value(ngx_http_request_t *r,  ngx_http_complex_value_t *val)
{//清楚变量的valid，not_found标识，这样就能自动的在下一次去获取变量值了。
    ngx_uint_t *index;

    index = val->flushes;
    if (index) {
        while (*index != (ngx_uint_t) -1) {
            if (r->variables[*index].no_cacheable) {
                r->variables[*index].valid = 0;
                r->variables[*index].not_found = 0;
            }
            index++;
        }
    }
}


ngx_int_t ngx_http_complex_value(ngx_http_request_t *r, ngx_http_complex_value_t *val, ngx_str_t *value)
{//根据val复杂表达式结构，获取其代表的目标值，存入value.
    size_t                        len;
    ngx_http_script_code_pt       code;
    ngx_http_script_len_code_pt   lcode;
    ngx_http_script_engine_t      e;

    if (val->lengths == NULL) {//没有lengths，那就是简单变量了，直接指向一下数据就行。
        *value = val->value;
        return NGX_OK;
    }

    ngx_http_script_flush_complex_value(r, val);
    ngx_memzero(&e, sizeof(ngx_http_script_engine_t));
    e.ip = val->lengths;
    e.request = r;
    e.flushed = 1;
    len = 0;
    while (*(uintptr_t *) e.ip) {//不断调用code.获取其长度
        lcode = *(ngx_http_script_len_code_pt *) e.ip;
        len += lcode(&e);
    }

    value->len = len;
    value->data = ngx_pnalloc(r->pool, len);
    if (value->data == NULL) {
        return NGX_ERROR;
    }
    e.ip = val->values;//调用values回调数组进行值的拷贝。
    e.pos = value->data;
    e.buf = *value;
    while (*(uintptr_t *) e.ip) {//获取内容。
        code = *(ngx_http_script_code_pt *) e.ip;
        code((ngx_http_script_engine_t *) &e);
    }
    *value = e.buf;
    return NGX_OK;
}


/*ngx_http_rewrite_return调用这里。解析return code [ text ] 后面的text或者URL。
结果存放在ccv->complex_value里面。为什么在这个里面呢
*/
ngx_int_t ngx_http_compile_complex_value(ngx_http_compile_complex_value_t *ccv)
{
    ngx_str_t                  *v;
    ngx_uint_t                  i, n, nv, nc;
    ngx_array_t                 flushes, lengths, values, *pf, *pl, *pv;
    ngx_http_script_compile_t   sc;

    v = ccv->value;
    if (v->len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, ccv->cf, 0, "empty parameter");
        return NGX_ERROR;
    }
    nv = 0;
    nc = 0;
    for (i = 0; i < v->len; i++) {
        if (v->data[i] == '$') {
            if (v->data[i + 1] >= '1' && v->data[i + 1] <= '9') {
                nc++;//统计匿名子模式的数目，这个会向上引用的。
            } else {
                nv++;//统计一般变量的数目。
            }
        }
    }
	//如果第一位为非变量，那就转变为绝对路径。
    if (v->data[0] != '$' && (ccv->conf_prefix || ccv->root_prefix)) {
        if (ngx_conf_full_name(ccv->cf->cycle, v, ccv->conf_prefix) != NGX_OK) {
            return NGX_ERROR;
        }
        ccv->conf_prefix = 0;
        ccv->root_prefix = 0;
    }
//下面计算初始化三个变量的数组。
    ccv->complex_value->value = *v;
    ccv->complex_value->flushes = NULL;
    ccv->complex_value->lengths = NULL;
    ccv->complex_value->values = NULL;
    if (nv == 0 && nc == 0) {//字符串和变量都没有，那就是简单的东东，上传肯定出bug了，不然这个简单字符串是不能正确处理的。
        return NGX_OK;
    }
    n = nv + 1;
    if (ngx_array_init(&flushes, ccv->cf->pool, n, sizeof(ngx_uint_t)) != NGX_OK){
        return NGX_ERROR;
    }
    n = nv * (2 * sizeof(ngx_http_script_copy_code_t) + sizeof(ngx_http_script_var_code_t)) + sizeof(uintptr_t);
    if (ngx_array_init(&lengths, ccv->cf->pool, n, 1) != NGX_OK) {
        return NGX_ERROR;
    }
    n = (nv * (2*sizeof(ngx_http_script_copy_code_t)+sizeof(ngx_http_script_var_code_t))+sizeof(uintptr_t)+v->len+sizeof(uintptr_t) - 1)
            & ~(sizeof(uintptr_t) - 1);
    if (ngx_array_init(&values, ccv->cf->pool, n, 1) != NGX_OK) {
        return NGX_ERROR;
    }
    pf = &flushes;
    pl = &lengths;
    pv = &values;
    ngx_memzero(&sc, sizeof(ngx_http_script_compile_t));
    sc.cf = ccv->cf;
    sc.source = v;
    sc.flushes = &pf;
    sc.lengths = &pl;
    sc.values = &pv;
    sc.complete_lengths = 1;
    sc.complete_values = 1;
    sc.zero = ccv->zero;
    sc.conf_prefix = ccv->conf_prefix;
    sc.root_prefix = ccv->root_prefix;
	//进行脚本解析编译，结果会反应到ccv->complex_value里面的。
    if (ngx_http_script_compile(&sc) != NGX_OK) {
        return NGX_ERROR;
    }
    if (flushes.nelts) {
        ccv->complex_value->flushes = flushes.elts;
        ccv->complex_value->flushes[flushes.nelts] = (ngx_uint_t) -1;
    }
    ccv->complex_value->lengths = lengths.elts;
    ccv->complex_value->values = values.elts;
    return NGX_OK;
}


char *
ngx_http_set_complex_value_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t                          *value;
    ngx_http_complex_value_t          **cv;
    ngx_http_compile_complex_value_t    ccv;

    cv = (ngx_http_complex_value_t **) (p + cmd->offset);

    if (*cv != NULL) {
        return "duplicate";
    }

    *cv = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
    if (*cv == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = *cv;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


ngx_int_t
ngx_http_test_predicates(ngx_http_request_t *r, ngx_array_t *predicates)
{
    ngx_str_t                  val;
    ngx_uint_t                 i;
    ngx_http_complex_value_t  *cv;

    if (predicates == NULL) {
        return NGX_OK;
    }

    cv = predicates->elts;

    for (i = 0; i < predicates->nelts; i++) {
        if (ngx_http_complex_value(r, &cv[i], &val) != NGX_OK) {
            return NGX_ERROR;
        }

        if (val.len && val.data[0] != '0') {
            return NGX_DECLINED;
        }
    }

    return NGX_OK;
}


char *
ngx_http_set_predicate_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t                          *value;
    ngx_uint_t                          i;
    ngx_array_t                       **a;
    ngx_http_complex_value_t           *cv;
    ngx_http_compile_complex_value_t    ccv;

    a = (ngx_array_t **) (p + cmd->offset);

    if (*a == NGX_CONF_UNSET_PTR) {
        *a = ngx_array_create(cf->pool, 1, sizeof(ngx_http_complex_value_t));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    value = cf->args->elts;

    for (i = 1; i < cf->args->nelts; i++) {
        cv = ngx_array_push(*a);
        if (cv == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = &value[i];
        ccv.complex_value = cv;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


ngx_uint_t
ngx_http_script_variables_count(ngx_str_t *value)
{//以'$'开头的便是变量
    ngx_uint_t  i, n;
    for (n = 0, i = 0; i < value->len; i++) {
        if (value->data[i] == '$') {
            n++;
        }
    }
    return n;
}

/*下面解析如下几种脚本:
$1 , $2 : 	ngx_http_script_add_capture_code
$abc, $id : ngx_http_script_add_var_code
?a=va : 	ngx_http_script_add_args_code
abcd : 		ngx_http_script_add_copy_code
*/
ngx_int_t ngx_http_script_compile(ngx_http_script_compile_t *sc)
{//传入一个字符串，进行编译解析，比如http://$host/aaa.php;将计算长度的lcode放入sc->lengths,计算值的放入sc->values
    u_char       ch;
    ngx_str_t    name;
    ngx_uint_t   i, bracket;

    if (ngx_http_script_init_arrays(sc) != NGX_OK) {//根据variables变量数目，创建lengths，values等数组。
        return NGX_ERROR;
    }
    for (i = 0; i < sc->source->len; /* void */ ) {//一个个遍历参数里面的字符串，比如
        name.len = 0;
        if (sc->source->data[i] == '$') {//找到一个变量
            if (++i == sc->source->len) {//但是到头了，结尾
                goto invalid_variable;
            }
#if (NGX_PCRE)
            {
            ngx_uint_t  n;
            if (sc->source->data[i] >= '1' && sc->source->data[i] <= '9') {
				//碰到了一个正则的向上引用，比如$1 
                n = sc->source->data[i] - '0';
                if (sc->captures_mask & (1 << n)) {
                    sc->dup_capture = 1;
                }
                sc->captures_mask |= 1 << n;//增加这个引用代码到lengths,values数组里面
                if (ngx_http_script_add_capture_code(sc, n) != NGX_OK) {
                    return NGX_ERROR;
                }
                i++;
                continue;
            }
            }
#endif
            if (sc->source->data[i] == '{') {
                bracket = 1;
                if (++i == sc->source->len) {
                    goto invalid_variable;
                }
                name.data = &sc->source->data[i];
            } else {
                bracket = 0;
                name.data = &sc->source->data[i];
            }

            for ( /* void */ ; i < sc->source->len; i++, name.len++) {
                ch = sc->source->data[i];
                if (ch == '}' && bracket) {
                    i++;
                    bracket = 0;
                    break;
                }
                if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')|| ch == '_'){
                    continue; //接受字母，数组，下划线的变量
                }
                break;
            }
            if (bracket) {
                ngx_conf_log_error(NGX_LOG_EMERG, sc->cf, 0, "the closing bracket in \"%V\" variable is missing", &name);
                return NGX_ERROR;
            }
            if (name.len == 0) {
                goto invalid_variable;
            }
            sc->variables++;//找到了一个变量，将其加入到sc->lengths中，同事cmcf->varibles里面也会增加一个。
            if (ngx_http_script_add_var_code(sc, &name) != NGX_OK) {
                return NGX_ERROR;
            }
            continue;
        }
        if (sc->source->data[i] == '?' && sc->compile_args) {
            sc->args = 1;
            sc->compile_args = 0;//增加一个参数到sc->lengths 中
            if (ngx_http_script_add_args_code(sc) != NGX_OK) {
                return NGX_ERROR;
            }

            i++;

            continue;
        }

        name.data = &sc->source->data[i];
        while (i < sc->source->len) {//不是变量什么的，就直接往后找，一直找到一个变量，或者参数段开始。这中间的是简单字符串
            if (sc->source->data[i] == '$') {
                break;
            }
            if (sc->source->data[i] == '?') {
                sc->args = 1;
                if (sc->compile_args) {
                    break;
                }
            }
            i++;
            name.len++;
        }
        sc->size += name.len;//下面就是简单的copy了，简单字符串。
        if (ngx_http_script_add_copy_code(sc, &name, (i == sc->source->len)) != NGX_OK) {
            return NGX_ERROR;
        }
    }
    return ngx_http_script_done(sc);
	
invalid_variable:
    ngx_conf_log_error(NGX_LOG_EMERG, sc->cf, 0, "invalid variable name");
    return NGX_ERROR;
}


u_char *
ngx_http_script_run(ngx_http_request_t *r, ngx_str_t *value, void *code_lengths, size_t len, void *code_values)
{
    ngx_uint_t                    i;
    ngx_http_script_code_pt       code;
    ngx_http_script_len_code_pt   lcode;
    ngx_http_script_engine_t      e;
    ngx_http_core_main_conf_t    *cmcf;

    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
	//遍历每一个变量，将其初始化为无效状态，促使待会全部解析。
    for (i = 0; i < cmcf->variables.nelts; i++) {
        if (r->variables[i].no_cacheable) {
            r->variables[i].valid = 0;
            r->variables[i].not_found = 0;
        }
    }
    ngx_memzero(&e, sizeof(ngx_http_script_engine_t));
    e.ip = code_lengths;
    e.request = r;
    e.flushed = 1;//要刷新

    while (*(uintptr_t *) e.ip) {//一个个遍历这些回调句柄。获取变量的长度
        lcode = *(ngx_http_script_len_code_pt *) e.ip;
        len += lcode(&e);
    }
    value->len = len;
    value->data = ngx_pnalloc(r->pool, len);//申请一块大的内存。
    if (value->data == NULL) {
        return NULL;
    }
    e.ip = code_values;
    e.pos = value->data;

    while (*(uintptr_t *) e.ip) {//一步步遍历，设置变量的值。
        code = *(ngx_http_script_code_pt *) e.ip;
        code((ngx_http_script_engine_t *) &e);//里面会改变e的ip的
    }

    return e.pos;
}


void
ngx_http_script_flush_no_cacheable_variables(ngx_http_request_t *r, ngx_array_t *indices)
{
    ngx_uint_t  n, *index;
    if (indices) {
        index = indices->elts;
        for (n = 0; n < indices->nelts; n++) {
            if (r->variables[index[n]].no_cacheable) {
                r->variables[index[n]].valid = 0;
                r->variables[index[n]].not_found = 0;
            }
        }
    }
}


static ngx_int_t
ngx_http_script_init_arrays(ngx_http_script_compile_t *sc)
{//根据variables变量数目，创建lengths，values等数组。
    ngx_uint_t   n;

    if (sc->flushes && *sc->flushes == NULL) {
        n = sc->variables ? sc->variables : 1;
        *sc->flushes = ngx_array_create(sc->cf->pool, n, sizeof(ngx_uint_t));
        if (*sc->flushes == NULL) {
            return NGX_ERROR;
        }
    }
    if (*sc->lengths == NULL) {
		//下面是有多少个变量，就多少组。每一组包括2个copy_code_t,1个var_code_t，1个指针
        n = sc->variables * (2 * sizeof(ngx_http_script_copy_code_t) + sizeof(ngx_http_script_var_code_t)) + sizeof(uintptr_t);
        *sc->lengths = ngx_array_create(sc->cf->pool, n, 1);//申请这么多个指针。
        if (*sc->lengths == NULL) {
            return NGX_ERROR;
        }
    }
    if (*sc->values == NULL) {
        n = (sc->variables * (2 * sizeof(ngx_http_script_copy_code_t) + sizeof(ngx_http_script_var_code_t))
                + sizeof(uintptr_t) + sc->source->len + sizeof(uintptr_t) - 1)
                & ~(sizeof(uintptr_t) - 1);

        *sc->values = ngx_array_create(sc->cf->pool, n, 1);
        if (*sc->values == NULL) {
            return NGX_ERROR;
        }
    }

    sc->variables = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_http_script_done(ngx_http_script_compile_t *sc)
{//跟进不同的参数，在sc->lengths后面追加NULL元素。
    ngx_str_t    zero;
    uintptr_t   *code;

    if (sc->zero) {
        zero.len = 1;
        zero.data = (u_char *) "\0";
        if (ngx_http_script_add_copy_code(sc, &zero, 0) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (sc->conf_prefix || sc->root_prefix) {
        if (ngx_http_script_add_full_name_code(sc) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (sc->complete_lengths) {
        code = ngx_http_script_add_code(*sc->lengths, sizeof(uintptr_t), NULL);
        if (code == NULL) {
            return NGX_ERROR;
        }
        *code = (uintptr_t) NULL;
    }
    if (sc->complete_values) {
        code = ngx_http_script_add_code(*sc->values, sizeof(uintptr_t), &sc->main);
        if (code == NULL) {
            return NGX_ERROR;
        }
        *code = (uintptr_t) NULL;
    }
    return NGX_OK;
}


void *
ngx_http_script_start_code(ngx_pool_t *pool, ngx_array_t **codes, size_t size)
{//在指定的codes数组里面增加一项，大小为size
    if (*codes == NULL) {
        *codes = ngx_array_create(pool, 256, 1);
        if (*codes == NULL) {
            return NULL;
        }
    }
    return ngx_array_push_n(*codes, size);
}


void *
ngx_http_script_add_code(ngx_array_t *codes, size_t size, void *code)
{//在code里面增加一项，大小为size
    u_char  *elts, **p;
    void    *new;

    elts = codes->elts;
    new = ngx_array_push_n(codes, size);//codes->elts可能会变化的。如果数组已经满了需要申请一块大的内存
    if (new == NULL) {
        return NULL;
    }
    if (code) {
        if (elts != codes->elts) {//如果内存变化了，
            p = code;//因为code参数表的是&sc->main这种，也就是指向本数组的数据，因此需要更新一下位移信息。
            *p += (u_char *) codes->elts - elts;//这是什么意思，加上了新申请的内存的位移。
        }
    }

    return new;
}


static ngx_int_t
ngx_http_script_add_copy_code(ngx_http_script_compile_t *sc, ngx_str_t *value, ngx_uint_t last)
{
    u_char                       *p;
    size_t                        size, len, zero;
    ngx_http_script_copy_code_t  *code;

    zero = (sc->zero && last);
    len = value->len + zero;
    code = ngx_http_script_add_code(*sc->lengths, sizeof(ngx_http_script_copy_code_t), NULL);
    if (code == NULL) {
        return NGX_ERROR;
    }

    code->code = (ngx_http_script_code_pt) ngx_http_script_copy_len_code;
    code->len = len;//记录字符串长度
    size = (sizeof(ngx_http_script_copy_code_t) + len + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);
	
    code = ngx_http_script_add_code(*sc->values, size, &sc->main);
    if (code == NULL) {
        return NGX_ERROR;
    }
    code->code = ngx_http_script_copy_code;
    code->len = len;
    p = ngx_cpymem((u_char *) code + sizeof(ngx_http_script_copy_code_t), value->data, value->len);

    if (zero) {
        *p = '\0';
        sc->zero = 0;
    }
    return NGX_OK;
}


size_t ngx_http_script_copy_len_code(ngx_http_script_engine_t *e)
{//获取指令的长度。
    ngx_http_script_copy_code_t  *code;
    code = (ngx_http_script_copy_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_copy_code_t);//改变ip，让他指向下一个指令的地址。
    return code->len;//返回长度。
}
void ngx_http_script_copy_code(ngx_http_script_engine_t *e)
{
    u_char                       *p;
    ngx_http_script_copy_code_t  *code;
    code = (ngx_http_script_copy_code_t *) e->ip;
    p = e->pos;//存放的目标。
    if (!e->skip) {//拷贝从ip指针后面的部分。也就是数据部分。
        e->pos = ngx_copy(p, e->ip + sizeof(ngx_http_script_copy_code_t), code->len);
    }
    e->ip += sizeof(ngx_http_script_copy_code_t) 1) & ~(sizeof(uintptr_t) - 1));//指向下一个。
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script copy: \"%*s\"", e->pos - p, p);
}


static ngx_int_t
ngx_http_script_add_var_code(ngx_http_script_compile_t *sc, ngx_str_t *name)
{
    ngx_int_t                    index, *p;
    ngx_http_script_var_code_t  *code;
	//根据变量名字，获取其在&cmcf->variables里面的下标。如果没有，就新建它。
    index = ngx_http_get_variable_index(sc->cf, name);
    if (index == NGX_ERROR) {
        return NGX_ERROR;
    }
    if (sc->flushes) {
        p = ngx_array_push(*sc->flushes);
        if (p == NULL) {
            return NGX_ERROR;
        }
        *p = index;
    }
    code = ngx_http_script_add_code(*sc->lengths, sizeof(ngx_http_script_var_code_t), NULL);
    if (code == NULL) {
        return NGX_ERROR;
    }
    code->code = (ngx_http_script_code_pt) ngx_http_script_copy_var_len_code;
    code->index = (uintptr_t) index;
    code = ngx_http_script_add_code(*sc->values,  sizeof(ngx_http_script_var_code_t), &sc->main);
    if (code == NULL) {
        return NGX_ERROR;
    }
    code->code = ngx_http_script_copy_var_code;
    code->index = (uintptr_t) index;

    return NGX_OK;
}


size_t
ngx_http_script_copy_var_len_code(ngx_http_script_engine_t *e)
{//返回一个变量的长度。
    ngx_http_variable_value_t   *value;
    ngx_http_script_var_code_t  *code;

    code = (ngx_http_script_var_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_var_code_t);
    if (e->flushed) {
        value = ngx_http_get_indexed_variable(e->request, code->index);
    } else {
        value = ngx_http_get_flushed_variable(e->request, code->index);
    }
    if (value && !value->not_found) {
        return value->len;//返回这个变量的长度。
    }

    return 0;
}


void
ngx_http_script_copy_var_code(ngx_http_script_engine_t *e)
{//拷贝变量
    u_char                      *p;
    ngx_http_variable_value_t   *value;
    ngx_http_script_var_code_t  *code;

    code = (ngx_http_script_var_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_var_code_t);

    if (!e->skip) {
		//如果需要，就刷新，重新读取一下变量的值。
        if (e->flushed) {
            value = ngx_http_get_indexed_variable(e->request, code->index);
        } else {//flushed的区别就是会强制吧valid变为0，促使调用get_handler函数重新获取变量的值。
            value = ngx_http_get_flushed_variable(e->request, code->index);
        }
        if (value && !value->not_found) {
            p = e->pos;
            e->pos = ngx_copy(p, value->data, value->len);//拷贝数据到输出变量里面
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script var: \"%*s\"", e->pos - p, p);
        }
    }
}


static ngx_int_t
ngx_http_script_add_args_code(ngx_http_script_compile_t *sc)
{//添加一个参数 脚本
    uintptr_t   *code;

    code = ngx_http_script_add_code(*sc->lengths, sizeof(uintptr_t), NULL);
    if (code == NULL) {
        return NGX_ERROR;
    }
    *code = (uintptr_t) ngx_http_script_mark_args_code;
    code = ngx_http_script_add_code(*sc->values, sizeof(uintptr_t), &sc->main);
    if (code == NULL) {
        return NGX_ERROR;
    }
    *code = (uintptr_t) ngx_http_script_start_args_code;

    return NGX_OK;
}


size_t
ngx_http_script_mark_args_code(ngx_http_script_engine_t *e)
{
    e->is_args = 1;
    e->ip += sizeof(uintptr_t);
    return 1;
}


void
ngx_http_script_start_args_code(ngx_http_script_engine_t *e)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script args");
    e->is_args = 1;
    e->args = e->pos;
    e->ip += sizeof(uintptr_t);
}


#if (NGX_PCRE)
/*
1.	调用正则表达式引擎编译URL参数行，如果匹配失败，则e->ip += code->next;让调用方调到下一个表达式块进行解析。
2.如果成功，调用code->lengths，从而获取正则表达式替换后的字符串长度，以备在此函数返回后的code函数调用中能够存储新字符串长度。
*/
void ngx_http_script_regex_start_code(ngx_http_script_engine_t *e) {
	//匹配正则表达式，计算目标字符串长度并分配空间。这个函数是每条rewrite语句最先调用的解析函数，
	//本函数负责匹配，和目标字符串长度计算，依据lengths lcodes数组进行
    size_t                         len;
    ngx_int_t                      rc;
    ngx_uint_t                     n;
    ngx_http_request_t            *r;
    ngx_http_script_engine_t       le;
    ngx_http_script_len_code_pt    lcode;
    ngx_http_script_regex_code_t  *code;

    code = (ngx_http_script_regex_code_t *) e->ip;
    r = e->request;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"http script regex: \"%V\"", &code->name);
    if (code->uri) {
        e->line = r->uri;
    } else {
        e->sp--;
        e->line.len = e->sp->len;
        e->line.data = e->sp->data;
    }
	//下面用已经编译的regex 跟e->line去匹配，看看是否匹配成功。
    rc = ngx_http_regex_exec(r, code->regex, &e->line);
    if (rc == NGX_DECLINED) {//匹配失败
        if (e->log || (r->connection->log->log_level & NGX_LOG_DEBUG_HTTP)) {
            ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "\"%V\" does not match \"%V\"", &code->name, &e->line);
        }
        r->ncaptures = 0;//一个都没有成功
        if (code->test) {
            if (code->negative_test) {
                e->sp->len = 1;
                e->sp->data = (u_char *) "1";
            } else {
                e->sp->len = 0;
                e->sp->data = (u_char *) "";
            }
            e->sp++;//移动到下一个节点。返回。
            e->ip += sizeof(ngx_http_script_regex_code_t);
            return;
        }
        e->ip += code->next;//next的含义为;如果当前code匹配失败，那么下一个code的位移是在什么地方，这些东西全部放在一个数组里面的。
        return;
    }
    if (rc == NGX_ERROR) {
        e->ip = ngx_http_script_exit;
        e->status = NGX_HTTP_INTERNAL_SERVER_ERROR;
        return;
    }
    if (e->log || (r->connection->log->log_level & NGX_LOG_DEBUG_HTTP)) {
        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "\"%V\" matches \"%V\"", &code->name, &e->line);
    }
    if (code->test) {//如果匹配成功了，那设置一个标志吧，这样比如做if匹配的时候就能通过查看堆栈的值来知道是否成功。
        if (code->negative_test) {
            e->sp->len = 0;
            e->sp->data = (u_char *) "";
        } else {
            e->sp->len = 1;
            e->sp->data = (u_char *) "1";
        }
        e->sp++;
        e->ip += sizeof(ngx_http_script_regex_code_t);
        return;
    }

    if (code->status) {
        e->status = code->status;
        if (!code->redirect) {
            e->ip = ngx_http_script_exit;
            return;
        }
    }
    if (code->uri) {
        r->internal = 1;
        r->valid_unparsed_uri = 0;
        if (code->break_cycle) {//rewrite最后的参数是break，将rewrite后的地址在当前location标签中执行
            r->valid_location = 0;
            r->uri_changed = 0;//将uri_changed设置为0后，也就标志说URL没有变化，那么，
            //在ngx_http_core_post_rewrite_phase中就不会执行里面的if语句，也就不会再次走到find config的过程了，而是继续处理后面的。
            //不然正常情况，rewrite成功后是会重新来一次的，相当于一个全新的请求。
        } else {
            r->uri_changed = 1;
        }
    }

    if (code->lengths == NULL) {//如果后面部分是简单字符串比如 rewrite ^(.*)$ http://chenzhenianqing.cn break;
        e->buf.len = code->size;//下面只是求一下大小。那数据呢
        if (code->uri) {
            if (r->ncaptures && (r->quoted_uri || r->plus_in_uri)) {
                e->buf.len += 2 * ngx_escape_uri(NULL, r->uri.data, r->uri.len, NGX_ESCAPE_ARGS);
            }
        }
        for (n = 2; n < r->ncaptures; n += 2) {
            e->buf.len += r->captures[n + 1] - r->captures[n];
        }
    } else {
        ngx_memzero(&le, sizeof(ngx_http_script_engine_t));
        le.ip = code->lengths->elts;
        le.line = e->line;
        le.request = r;
        le.quote = code->redirect;
        len = 0;
        while (*(uintptr_t *) le.ip) {/*一个个去处理复杂表达式，但是这里其实只是算一下大小的，
        真正的数据拷贝在上层的code获取。比如 rewrite ^(.*)$ http://$http_host.mp4 break;
        //下面会分步的，拼装出后面的url,对于上面的例子，为
			ngx_http_script_copy_len_code		7
			ngx_http_script_copy_var_len_code 	18
			ngx_http_script_copy_len_code		4	=== 29 
		这里只是求一下长度，调用lengths求长度。数据拷贝在ngx_http_rewrite_handler中，本函数返回后就调用如下过程拷贝数据: 
			ngx_http_script_copy_code		拷贝"http://" 到e->buf
			ngx_http_script_copy_var_code	拷贝"115.28.34.175:8881"
			ngx_http_script_copy_code 		拷贝".mp4"
        */
            lcode = *(ngx_http_script_len_code_pt *) le.ip;
            len += lcode(&le);
        }

        e->buf.len = len;//记住总长度。
        e->is_args = le.is_args;
    }

    if (code->add_args && r->args.len) {//是否需要自动增加参数。如果配置行的后面显示的加上了?符号，则nginx不会追加参数。
        e->buf.len += r->args.len + 1;
    }
    e->buf.data = ngx_pnalloc(r->pool, e->buf.len);
    if (e->buf.data == NULL) {
        e->ip = ngx_http_script_exit;
        e->status = NGX_HTTP_INTERNAL_SERVER_ERROR;
        return;
    }
    e->quote = code->redirect;
    e->pos = e->buf.data;//申请了这么大的空间，用来装数据
    e->ip += sizeof(ngx_http_script_regex_code_t);//处理下一个。
}


void
ngx_http_script_regex_end_code(ngx_http_script_engine_t *e)
{//貌似没干什么事情，如果是redirect，急设置了一下头部header的location，该302了。
    u_char                            *dst, *src;
    ngx_http_request_t                *r;
    ngx_http_script_regex_end_code_t  *code;

    code = (ngx_http_script_regex_end_code_t *) e->ip;
    r = e->request;
    e->quote = 0;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,"http script regex end");

    if (code->redirect) {
        dst = e->buf.data;
        src = e->buf.data;
        ngx_unescape_uri(&dst, &src, e->pos - e->buf.data, NGX_UNESCAPE_REDIRECT);
        if (src < e->pos) {
            dst = ngx_copy(dst, src, e->pos - src);
        }
        e->pos = dst;
        if (code->add_args && r->args.len) {
            *e->pos++ = (u_char) (code->args ? '&' : '?');
            e->pos = ngx_copy(e->pos, r->args.data, r->args.len);
        }

        e->buf.len = e->pos - e->buf.data;

        if (e->log || (r->connection->log->log_level & NGX_LOG_DEBUG_HTTP)) {
            ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0, "rewritten redirect: \"%V\"", &e->buf);
        }

        r->headers_out.location = ngx_list_push(&r->headers_out.headers);
        if (r->headers_out.location == NULL) {
            e->ip = ngx_http_script_exit;
            e->status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            return;
        }

        r->headers_out.location->hash = 1;
        ngx_str_set(&r->headers_out.location->key, "Location");
        r->headers_out.location->value = e->buf;

        e->ip += sizeof(ngx_http_script_regex_end_code_t);
        return;
    }

    if (e->args) {//如果请求有参数，那么可能需要拷贝一下参数
        e->buf.len = e->args - e->buf.data;
        if (code->add_args && r->args.len) {//需要拷贝参数，且参数不为空。下面拷贝一下参数部分。
            *e->pos++ = '&';
            e->pos = ngx_copy(e->pos, r->args.data, r->args.len);
        }
        r->args.len = e->pos - e->args;
        r->args.data = e->args;
        e->args = NULL;
    } else {
        e->buf.len = e->pos - e->buf.data;
        if (!code->add_args) {
            r->args.len = 0;
        }
    }

    if (e->log || (r->connection->log->log_level & NGX_LOG_DEBUG_HTTP)) {
        ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,  "rewritten data: \"%V\", args: \"%V\"", &e->buf, &r->args);
    }
    if (code->uri) {
        r->uri = e->buf;
        if (r->uri.len == 0) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "the rewritten URI has a zero length");
            e->ip = ngx_http_script_exit;
            e->status = NGX_HTTP_INTERNAL_SERVER_ERROR;
            return;
        }
        ngx_http_set_exten(r);
    }
    e->ip += sizeof(ngx_http_script_regex_end_code_t);
}


static ngx_int_t
ngx_http_script_add_capture_code(ngx_http_script_compile_t *sc, ngx_uint_t n)
{//增加一个向上引用比如$1， 参数n是这个$1的1字。
    ngx_http_script_copy_capture_code_t  *code;

    code = ngx_http_script_add_code(*sc->lengths, sizeof(ngx_http_script_copy_capture_code_t),  NULL);
    if (code == NULL) {//从sc->lengths数组中申请一块内存，并返回其地址。
        return NGX_ERROR;
    }
    code->code = (ngx_http_script_code_pt) ngx_http_script_copy_capture_len_code;
    code->n = 2 * n;//2倍的原因是PCRE保存结果的关系

	code = ngx_http_script_add_code(*sc->values, sizeof(ngx_http_script_copy_capture_code_t), &sc->main);
    if (code == NULL) {//在values里面增加一项
        return NGX_ERROR;
    }
    code->code = ngx_http_script_copy_capture_code;
    code->n = 2 * n;

    if (sc->ncaptures < n) {
        sc->ncaptures = n;
    }

    return NGX_OK;
}


size_t
ngx_http_script_copy_capture_len_code(ngx_http_script_engine_t *e)
{//跟ngx_http_script_copy_capture_code对应，这里是求长度，后者是i拷贝值。
    int                                  *cap;
    u_char                               *p;
    ngx_uint_t                            n;
    ngx_http_request_t                   *r;
    ngx_http_script_copy_capture_code_t  *code;

    r = e->request;
    code = (ngx_http_script_copy_capture_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_copy_capture_code_t);
    n = code->n;
    if (n < r->ncaptures) {
        cap = r->captures;
        if ((e->is_args || e->quote)  && (e->request->quoted_uri || e->request->plus_in_uri)) {
            p = r->captures_data;
            return cap[n + 1] - cap[n] + 2 * ngx_escape_uri(NULL, &p[cap[n]], cap[n + 1] - cap[n], NGX_ESCAPE_ARGS);
        } else {
            return cap[n + 1] - cap[n];
        }
    }
    return 0;
}


void
ngx_http_script_copy_capture_code(ngx_http_script_engine_t *e)
{//拷贝一下正则表达式解析后的 变量 的值。比如命名子模式$var或者匿名子模式$2
    int                                  *cap;
    u_char                               *p, *pos;
    ngx_uint_t                            n;
    ngx_http_request_t                   *r;
    ngx_http_script_copy_capture_code_t  *code;

    r = e->request;
    code = (ngx_http_script_copy_capture_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_copy_capture_code_t);
    n = code->n;
    pos = e->pos;
    if (n < r->ncaptures) {
        cap = r->captures;//得到刚才的正则表达式解析的结果，放在这里。其内容为2个单位元素的数组，分别代表匹配的开始结束。
        p = r->captures_data;
        if ((e->is_args || e->quote)  && (e->request->quoted_uri || e->request->plus_in_uri)) {
            e->pos = (u_char *) ngx_escape_uri(pos, &p[cap[n]],  cap[n + 1] - cap[n], NGX_ESCAPE_ARGS);
        } else {
        	//将数据拷贝到目标地址，然后返回尾部，直接修改e->pos
            e->pos = ngx_copy(pos, &p[cap[n]], cap[n + 1] - cap[n]);
        }
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script capture: \"%*s\"", e->pos - pos, pos);
}
#endif


static ngx_int_t
ngx_http_script_add_full_name_code(ngx_http_script_compile_t *sc)
{
    ngx_http_script_full_name_code_t  *code;

    code = ngx_http_script_add_code(*sc->lengths,
                                    sizeof(ngx_http_script_full_name_code_t),
                                    NULL);
    if (code == NULL) {
        return NGX_ERROR;
    }

    code->code = (ngx_http_script_code_pt) ngx_http_script_full_name_len_code;
    code->conf_prefix = sc->conf_prefix;

    code = ngx_http_script_add_code(*sc->values,
                                    sizeof(ngx_http_script_full_name_code_t),
                                    &sc->main);
    if (code == NULL) {
        return NGX_ERROR;
    }

    code->code = ngx_http_script_full_name_code;
    code->conf_prefix = sc->conf_prefix;

    return NGX_OK;
}


static size_t
ngx_http_script_full_name_len_code(ngx_http_script_engine_t *e)
{
    ngx_http_script_full_name_code_t  *code;

    code = (ngx_http_script_full_name_code_t *) e->ip;

    e->ip += sizeof(ngx_http_script_full_name_code_t);

    return code->conf_prefix ? ngx_cycle->conf_prefix.len:
                               ngx_cycle->prefix.len;
}


static void
ngx_http_script_full_name_code(ngx_http_script_engine_t *e)
{
    ngx_http_script_full_name_code_t  *code;

    ngx_str_t  value;

    code = (ngx_http_script_full_name_code_t *) e->ip;

    value.data = e->buf.data;
    value.len = e->pos - e->buf.data;

    if (ngx_conf_full_name((ngx_cycle_t *) ngx_cycle, &value, code->conf_prefix)
        != NGX_OK)
    {
        e->ip = ngx_http_script_exit;
        e->status = NGX_HTTP_INTERNAL_SERVER_ERROR;
        return;
    }

    e->buf = value;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0,
                   "http script fullname: \"%V\"", &value);

    e->ip += sizeof(ngx_http_script_full_name_code_t);
}


void
ngx_http_script_return_code(ngx_http_script_engine_t *e)
{
    ngx_http_script_return_code_t  *code;

    code = (ngx_http_script_return_code_t *) e->ip;
    if (code->status < NGX_HTTP_BAD_REQUEST  || code->text.value.len || code->text.lengths) {
		//是400，或者是个简单字符串，或者是复杂表达式，那么需要正规的好好的处理一下才行。
		//根据复杂表达式获取其值，然后确定是重定向还是要发送响应，将头部数据和body数据发送给客户端。
        e->status = ngx_http_send_response(e->request, code->status, NULL, &code->text);
    } else {//一般正常请求，正常结束就行。顶多终止后面的过程处理
        e->status = code->status;
    }
    e->ip = ngx_http_script_exit;//修改为空，上层会结束的。
}


void
ngx_http_script_break_code(ngx_http_script_engine_t *e)
{//Syntax:	break
    e->request->uri_changed = 0;//待会就算有重定向成功了，我也不重新find config 了。就这么简单。
    e->ip = ngx_http_script_exit;
}


void
ngx_http_script_if_code(ngx_http_script_engine_t *e)
{//if匹配完后，在此做location替换神马的。
    ngx_http_script_if_code_t  *code;
    code = (ngx_http_script_if_code_t *) e->ip;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0,  "http script if");
    e->sp--;//回退一下栈
    if (e->sp->len && e->sp->data[0] != '0') {//根据栈上的值是不是"1"来判断刚才的if匹配是否成功。这个是跟上面的code协商的。
        if (code->loc_conf) {//成功了的话，就换一个location !!!!!
            e->request->loc_conf = code->loc_conf;//替换为if里面的location
            ngx_http_update_location_config(e->request);//更新各种配置。
        }
        e->ip += sizeof(ngx_http_script_if_code_t);//转到下一个code处理。
        return;
    }
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script if: false");
    e->ip += code->next;//如果刚才的if匹配失败了，那么，就跳到下一个code块处理。
}


void
ngx_http_script_equal_code(ngx_http_script_engine_t *e)
{
	/*注意，当解析到这个函数的时候，堆栈已经放入了2个值了，从底向上为:ngx_http_rewrite_variable存入变量的值，
	ngx_http_script_complex_value_code存入复杂表达式匹配出来的值。因此这个code正好可以取堆栈上的1,2个位置就是要比较的2个字符串。
	*/
    ngx_http_variable_value_t  *val, *res;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script equal");
    e->sp--;//得到栈里面的上一个值，其实就是ngx_http_rewrite_value挂载的那些句柄，
    //在rewrite phrase过程中执行时设置的值，存放在堆栈里面供大家分享，呵呵
    val = e->sp;//
    res = e->sp - 1;
    e->ip += sizeof(uintptr_t);//往后移动1个指针就行。
	//脚本解析出来的字符串放在e->sp中，其正好跟上上一个字符串相等，就OK。这是啥意思。
    if (val->len == res->len && ngx_strncmp(val->data, res->data, res->len) == 0) {
        *res = ngx_http_variable_true_value;//将堆栈上面第二个字符串改为"1"
        //这里改为1是跟ngx_http_script_if_code这个判断整个if是否成功的code配合的，对方也是这么判断是否成功的。
        //也就是，我这个比较操作符，跟结果判断操作符(就是下一个code)，沟通用这种方式判断是否匹配成功。
        return;
    }
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script equal: no");
    *res = ngx_http_variable_null_value;//不成功。
}


void
ngx_http_script_not_equal_code(ngx_http_script_engine_t *e)
{//同ngx_http_script_equal_code，做不等匹配。
    ngx_http_variable_value_t  *val, *res;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script not equal");
    e->sp--;
    val = e->sp;
    res = e->sp - 1;
    e->ip += sizeof(uintptr_t);
    if (val->len == res->len  && ngx_strncmp(val->data, res->data, res->len) == 0){
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script not equal: no");
        *res = ngx_http_variable_null_value;
        return;
    }
    *res = ngx_http_variable_true_value;
}


void
ngx_http_script_file_code(ngx_http_script_engine_t *e)
{//看看文件是否存在。并设置相关的标识，供if语句判断是否存在
    ngx_str_t                     path;
    ngx_http_request_t           *r;
    ngx_open_file_info_t          of;
    ngx_http_core_loc_conf_t     *clcf;
    ngx_http_variable_value_t    *value;
    ngx_http_script_file_code_t  *code;

    value = e->sp - 1;

    code = (ngx_http_script_file_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_file_code_t);

    path.len = value->len - 1;
    path.data = value->data;

    r = e->request;
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "http script file op %p \"%V\"", code->op, &path);
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    ngx_memzero(&of, sizeof(ngx_open_file_info_t));
	
    of.read_ahead = clcf->read_ahead;
    of.directio = clcf->directio;
    of.valid = clcf->open_file_cache_valid;
    of.min_uses = clcf->open_file_cache_min_uses;
    of.test_only = 1;
    of.errors = clcf->open_file_cache_errors;
    of.events = clcf->open_file_cache_events;

    if (ngx_open_cached_file(clcf->open_file_cache, &path, &of, r->pool)
        != NGX_OK)
    {
        if (of.err != NGX_ENOENT
            && of.err != NGX_ENOTDIR
            && of.err != NGX_ENAMETOOLONG)
        {
            ngx_log_error(NGX_LOG_CRIT, r->connection->log, of.err, "%s \"%s\" failed", of.failed, value->data);
        }

        switch (code->op) {

        case ngx_http_script_file_plain:
        case ngx_http_script_file_dir:
        case ngx_http_script_file_exists:
        case ngx_http_script_file_exec:
             goto false_value;

        case ngx_http_script_file_not_plain:
        case ngx_http_script_file_not_dir:
        case ngx_http_script_file_not_exists:
        case ngx_http_script_file_not_exec:
             goto true_value;
        }

        goto false_value;
    }

    switch (code->op) {
    case ngx_http_script_file_plain:
        if (of.is_file) {
             goto true_value;
        }
        goto false_value;

    case ngx_http_script_file_not_plain:
        if (of.is_file) {
            goto false_value;
        }
        goto true_value;

    case ngx_http_script_file_dir:
        if (of.is_dir) {
             goto true_value;
        }
        goto false_value;

    case ngx_http_script_file_not_dir:
        if (of.is_dir) {
            goto false_value;
        }
        goto true_value;

    case ngx_http_script_file_exists:
        if (of.is_file || of.is_dir || of.is_link) {
             goto true_value;
        }
        goto false_value;

    case ngx_http_script_file_not_exists:
        if (of.is_file || of.is_dir || of.is_link) {
            goto false_value;
        }
        goto true_value;

    case ngx_http_script_file_exec:
        if (of.is_exec) {
             goto true_value;
        }
        goto false_value;

    case ngx_http_script_file_not_exec:
        if (of.is_exec) {
            goto false_value;
        }
        goto true_value;
    }

false_value:

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http script file op false");

    *value = ngx_http_variable_null_value;
    return;

true_value:

    *value = ngx_http_variable_true_value;
    return;
}


void ngx_http_script_complex_value_code(ngx_http_script_engine_t *e)
{//依据lengths里面的指令数组，遍历调用里面的codes， 获取一个变量的内容。会增加堆栈值。
    size_t                                 len;
    ngx_http_script_engine_t               le;
    ngx_http_script_len_code_pt            lcode;
    ngx_http_script_complex_value_code_t  *code;

    code = (ngx_http_script_complex_value_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_complex_value_code_t);//移动下一个
    
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script complex value");
    ngx_memzero(&le, sizeof(ngx_http_scbreak ript_engine_t));

    le.ip = code->lengths->elts;//复杂指令里面嵌套了其他code
    le.line = e->line;
    le.request = e->request;
    le.quote = e->quote;

    for (len = 0; *(uintptr_t *) le.ip; len += lcode(&le)) {//处理这个复杂数据的codes，获取其长度。
        lcode = *(ngx_http_script_len_code_pt *) le.ip;
    }

    e->buf.len = len;//然后申请内存
    e->buf.data = ngx_pnalloc(e->request->pool, len);
    if (e->buf.data == NULL) {
        e->ip = ngx_http_script_exit;
        e->status = NGX_HTTP_INTERNAL_SERVER_ERROR;
        return;
    }

    e->pos = e->buf.data;//指向这块新申请的内存。其内容应该是空的吧
    e->sp->len = e->buf.len;
    e->sp->data = e->buf.data;//这是啥意思，申请的新内存，啥数据也没有
    e->sp++;//这里貌似是用sp来保存中间结果，比如保存当前这一步的进度，到下一步好用e->sp--来找到上一步的结果。
}


void
ngx_http_script_value_code(ngx_http_script_engine_t *e)
{//简单的字符串处理函数，直接指向一下就行了，什么内存分配，都不需要，因为这里压根就不需要分配内存。
    ngx_http_script_value_code_t  *code;

    code = (ngx_http_script_value_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_value_code_t);
	
    e->sp->len = code->text_len;
    e->sp->data = (u_char *) code->text_data;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script value: \"%v\"", e->sp);
    e->sp++;//这里貌似是用sp来保存中间结果，比如保存当前这一步的进度，到下一步好用e->sp--来找到上一步的结果。
}


void
ngx_http_script_set_var_code(ngx_http_script_engine_t *e)
{//根据e指向的内容填充r->variables[code->index]。设置i这个变量。一般在set $variable value指令的最后一个code会调用这里保存值。
    ngx_http_request_t          *r;
    ngx_http_script_var_code_t  *code;
	// e->ip就是之前在解析时设置的各种结构体  
    code = (ngx_http_script_var_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_var_code_t);
    r = e->request;
    e->sp--;
    r->variables[code->index].len = e->sp->len;
    r->variables[code->index].valid = 1;
    r->variables[code->index].no_cacheable = 0;
    r->variables[code->index].not_found = 0;
    r->variables[code->index].data = e->sp->data;

#if (NGX_DEBUG)
    {
    ngx_http_variable_t        *v;
    ngx_http_core_main_conf_t  *cmcf;
    cmcf = ngx_http_get_module_main_conf(r, ngx_http_core_module);
    v = cmcf->variables.elts;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script set $%V", &v[code->index].name);
    }
#endif
}


void
ngx_http_script_var_set_handler_code(ngx_http_script_engine_t *e)
{
    ngx_http_script_var_handler_code_t  *code;
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script set var handler");

    code = (ngx_http_script_var_handler_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_var_handler_code_t);
    e->sp--;
    code->handler(e->request, e->sp, code->data);
}


void
ngx_http_script_var_code(ngx_http_script_engine_t *e)
{//获取一个变量的植。
    ngx_http_variable_value_t   *value;
    ngx_http_script_var_code_t  *code;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script var");
    code = (ngx_http_script_var_code_t *) e->ip;
    e->ip += sizeof(ngx_http_script_var_code_t);
    value = ngx_http_get_flushed_variable(e->request, code->index);
    if (value && !value->not_found) {
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, e->request->connection->log, 0, "http script var: \"%v\"", value);
        *e->sp = *value;//设置值
        e->sp++;//这里增加堆栈，保存这个值。后续可以取出来。
        return;
    }
    *e->sp = ngx_http_variable_null_value;
    e->sp++;
}


void
ngx_http_script_nop_code(ngx_http_script_engine_t *e)
{
    e->ip += sizeof(uintptr_t);
}
