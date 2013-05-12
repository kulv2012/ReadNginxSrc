
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>

#define NGX_CONF_BUFFER  4096

static ngx_int_t ngx_conf_handler(ngx_conf_t *cf, ngx_int_t last);
static ngx_int_t ngx_conf_read_token(ngx_conf_t *cf);
static char *ngx_conf_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_conf_test_full_name(ngx_str_t *name);
static void ngx_conf_flush_files(ngx_cycle_t *cycle);


static ngx_command_t  ngx_conf_commands[] = {

    { ngx_string("include"),
      NGX_ANY_CONF|NGX_CONF_TAKE1,
      ngx_conf_include,
      0,
      0,
      NULL },

      ngx_null_command
};


ngx_module_t  ngx_conf_module = {
    NGX_MODULE_V1,
    NULL,                                  /* module context */
    ngx_conf_commands,                     /* module directives */
    NGX_CONF_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_conf_flush_files,                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


/* The eight fixed arguments */

static ngx_uint_t argument_number[] = {
    NGX_CONF_NOARGS,
    NGX_CONF_TAKE1,
    NGX_CONF_TAKE2,
    NGX_CONF_TAKE3,
    NGX_CONF_TAKE4,
    NGX_CONF_TAKE5,
    NGX_CONF_TAKE6,
    NGX_CONF_TAKE7
};


char *
ngx_conf_param(ngx_conf_t *cf)
{//ngx_conf_param(&conf) 将conf需要的参数（可能没有就是空）存到conf中，ngx_conf_parse(&conf, &cycle->conf_file) 解析配置文件
    char             *rv;
    ngx_str_t        *param;
    ngx_buf_t         b;
    ngx_conf_file_t   conf_file;

    param = &cf->cycle->conf_param;

    if (param->len == 0) {
        return NGX_CONF_OK;
    }

    ngx_memzero(&conf_file, sizeof(ngx_conf_file_t));
    ngx_memzero(&b, sizeof(ngx_buf_t));
    b.start = param->data;
    b.pos = param->data;
    b.last = param->data + param->len;
    b.end = b.last;
    b.temporary = 1;
    conf_file.file.fd = NGX_INVALID_FILE;
    conf_file.file.name.data = NULL;
    conf_file.line = 0;
	//set to tmp var to parse
    cf->conf_file = &conf_file;
    cf->conf_file->buffer = &b;
	
    rv = ngx_conf_parse(cf, NULL);//传入空的文件名。
	//k:reset to null
    cf->conf_file = NULL;

    return rv;
}

/*
1.在解析整个配置文件的时候，cf等于顶层的配置，其ctx成员指向cycle->conf_ctx[]数组。
2.解析http{}里面的数据的时候，cf->ctx为ngx_http_conf_ctx_t结构。
*/

char * ngx_conf_parse(ngx_conf_t *cf, ngx_str_t *filename)
{
    char             *rv;
    ngx_fd_t          fd;
    ngx_int_t         rc;
    ngx_buf_t         buf;
    ngx_conf_file_t  *prev, conf_file;
    enum {//该函数存在三种运行方式，并非一定需要打开配置文件
        parse_file = 0,
        parse_block,
        parse_param
    } type;
//k: ngx_conf_parse have 3 type of parse ,some doesn't need to open file
#if (NGX_SUPPRESS_WARN)
    fd = NGX_INVALID_FILE;
    prev = NULL;
#endif
    if (filename) {//filename 的值为 nginx.conf 的路径
        /* open configuration file */
        fd = ngx_open_file(filename->data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
        if (fd == NGX_INVALID_FILE) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,  ngx_open_file_n " \"%s\" failed", filename->data);
            return NGX_CONF_ERROR;
        }
        prev = cf->conf_file;
        cf->conf_file = &conf_file;
		//k:get file info.ngx_fd_info = fstat
        if (ngx_fd_info(fd, &cf->conf_file->file.info) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, ngx_errno,
                          ngx_fd_info_n " \"%s\" failed", filename->data);
        }

        cf->conf_file->buffer = &buf;
        buf.start = ngx_alloc(NGX_CONF_BUFFER, cf->log);
        if (buf.start == NULL) {
            goto failed;
        }

        buf.pos = buf.start;
        buf.last = buf.start;
        buf.end = buf.last + NGX_CONF_BUFFER;
        buf.temporary = 1;

        cf->conf_file->file.fd = fd;
        cf->conf_file->file.name.len = filename->len;
        cf->conf_file->file.name.data = filename->data;
        cf->conf_file->file.offset = 0;
        cf->conf_file->file.log = cf->log;
        cf->conf_file->line = 1;
		//k:set parse type to parse_file.the file is specified in conf->conf_file.
		//and previous file is stored by prev = cf->conf_file;
		//cf->conf_file = prev;in done label restored to previous file.
        type = parse_file;//设置为解析文件模式
    } else if (cf->conf_file->file.fd != NGX_INVALID_FILE) {
        type = parse_block;
    } else {
        type = parse_param;
    }
	
    for ( ;; )//完成对配置文件信息的，初步设置之后，就开始对配置文件进行解析。
		//k:read a directive one time ,ended with \n or ```.store args in cf->args
        rc = ngx_conf_read_token(cf);
        /*
         * ngx_conf_read_token() may return
         *
         *    NGX_ERROR             there is error
         *    NGX_OK                the token terminated by ";" was found
         *    NGX_CONF_BLOCK_START  the token terminated by "{" was found
         *    NGX_CONF_BLOCK_DONE   the "}" was found
         *    NGX_CONF_FILE_DONE    the configuration file is done
         */

        if (rc == NGX_ERROR) {
            goto done;
        }

        if (rc == NGX_CONF_BLOCK_DONE) {
			//k:if current stat is not parsing block,we faced }.
            if (type != parse_block) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected \"}\"");
                goto failed;
            }
			//k:attention,if we faced with 'events{',
			//the call will go int hander()which will call ngx_conf_parse again recusive!!.
			//at the same time,local variable tpye will set to parse_block temporary 
            goto done;
        }

        if (rc == NGX_CONF_FILE_DONE) {

            if (type == parse_block) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected end of file, expecting \"}\"");
                goto failed;
            }
            goto done;
        }
        if (rc == NGX_CONF_BLOCK_START) {
            if (type == parse_param) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "block directives are not supported " "in -g option");
                goto failed;
            }
        }

        /* rc == NGX_OK || rc == NGX_CONF_BLOCK_START */
		//k NGX_CONF_BLOCK_DONE will goto done ,and again return by ngx_conf_handler
        if (cf->handler) {
            /*
             * the custom handler, i.e., that is used in the http's
             * "types { ... }" directive
             */
            rv = (*cf->handler)(cf, NULL, cf->handler_conf);
            if (rv == NGX_CONF_OK) {
                continue;
            }
            if (rv == NGX_CONF_ERROR) {
                goto failed;
            }
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, rv);
            goto failed;
        }
        rc = ngx_conf_handler(cf, rc);//找到了一条指令，调用这里进行指令的回调函数调用。
        if (rc == NGX_ERROR) {
            goto failed;
        }
    }

failed:
    rc = NGX_ERROR;
done:
    if (filename) {
        if (cf->conf_file->buffer->start) {
            ngx_free(cf->conf_file->buffer->start);
        }
        if (ngx_close_file(fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ALERT, cf->log, ngx_errno, ngx_close_file_n " %s failed",  filename->data);
            return NGX_CONF_ERROR;
        }
        cf->conf_file = prev;
    }
    if (rc == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }
    return NGX_CONF_OK;
}


static ngx_int_t
ngx_conf_handler(ngx_conf_t *cf, ngx_int_t last)
{//读取到一条指令后，调用这里，然后遍历所有的模块，得到这些模块的指令集合，看看是不是对应的，然后调用其set函数
    char           *rv;
    void           *conf, **confp;
    ngx_uint_t      i, multi;
    ngx_str_t      *name;
    ngx_command_t  *cmd;

    name = cf->args->elts;
    multi = 0;
    for (i = 0; ngx_modules[i]; i++) {//找到一条指令后，遍历所有的模块，尝试找到对应的模块处理函数
        /* look up the directive in the appropriate modules */
        if (ngx_modules[i]->type != NGX_CONF_MODULE && ngx_modules[i]->type != cf->module_type) {
            continue;//
        }
        cmd = ngx_modules[i]->commands;//拿到这个模块的所有指令集合
        if (cmd == NULL) {
            continue;
        }
        for ( /* void */ ; cmd->name.len; cmd++) {//遍历这个模块的所有指令。比如ngx_core_commands
            if (name->len != cmd->name.len) {
                continue;
            }
            if (ngx_strcmp(name->data, cmd->name.data) != 0) {
                continue;//如果当前指令不等于，那就不用care了
            }
            /* is the directive's location right ? */
            if (!(cmd->type & cf->cmd_type)) {
                if (cmd->type & NGX_CONF_MULTI) {
                    multi = 1;
                    continue;
                }
                goto not_allowed;
            }
            if (!(cmd->type & NGX_CONF_BLOCK) && last != NGX_OK) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "directive \"%s\" is not terminated by \";\"", name->data);
                return NGX_ERROR;
            }
            if ((cmd->type & NGX_CONF_BLOCK) && last != NGX_CONF_BLOCK_START) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "directive \"%s\" has no opening \"{\"", name->data);
                return NGX_ERROR;
            }
            /* is the directive's argument count right ? */
            if (!(cmd->type & NGX_CONF_ANY)) {//参数判断
                if (cmd->type & NGX_CONF_FLAG) {
                    if (cf->args->nelts != 2) {
                        goto invalid;
                    }
                } else if (cmd->type & NGX_CONF_1MORE) {
                    if (cf->args->nelts < 2) {
                        goto invalid;
                    }
                } else if (cmd->type & NGX_CONF_2MORE) {
                    if (cf->args->nelts < 3) {
                        goto invalid;
                    }
                } else if (cf->args->nelts > NGX_CONF_MAX_ARGS) {
                    goto invalid;
                } else if (!(cmd->type & argument_number[cf->args->nelts - 1])) {
                    goto invalid;
                }
            }
            /* set up the directive's configuration context */
            conf = NULL
            if (cmd->type & NGX_DIRECT_CONF) {
                conf = ((void **) cf->ctx)[ngx_modules[i]->index];
            } else if (cmd->type & NGX_MAIN_CONF) {//处理顶层的配置，直接取某个模块的下标。
                conf = &(((void **) cf->ctx)[ngx_modules[i]->index]);
            } else if (cf->ctx) {
                confp = *(void **) ((char *) cf->ctx + cmd->conf);//cmd->conf代表这个命令使用的是哪一个conf。
                if (confp) {//去除其ctx[*]的某项配置，然后拿到该配置列表中当前模块下标的配置。
                    conf = confp[ngx_modules[i]->ctx_index];//从对应模块中的序号得到该模块在所属模块组内的配置
                }
            }
            rv = cmd->set(cf, cmd, conf);//调用这个指令的set函数，比如ngx_conf_set_flag_slot等。
            if (rv == NGX_CONF_OK) {
                return NGX_OK;
            }
            if (rv == NGX_CONF_ERROR) {
                return NGX_ERROR;
            }
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "\"%s\" directive %s", name->data, rv);
            return NGX_ERROR;
        }
    }
    if (multi == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unknown directive \"%s\"", name->data);
        return NGX_ERROR;
    }
not_allowed:
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "\"%s\" directive is not allowed here", name->data);
    return NGX_ERROR;
invalid:
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "invalid number of arguments in \"%s\" directive",  name->data);
    return NGX_ERROR;
}


static ngx_int_t
ngx_conf_read_token(ngx_conf_t *cf)
{
    u_char      *start, ch, *src, *dst;

    off_t        file_size;
    size_t       len;
    ssize_t      n, size;
    ngx_uint_t   found, need_space, last_space, sharp_comment, variable;
    ngx_uint_t   quoted, s_quoted, d_quoted, start_line;
    ngx_str_t   *word;
    ngx_buf_t   *b;

    found = 0;
    need_space = 0;//k:we expect space or ;end of line
    last_space = 1;//k:just now we conquered space ,expect chars.
    sharp_comment = 0;//k: conquered # comment beginer
    variable = 0;//k:we find $,mean next is a variable
    quoted = 0;
    s_quoted = 0;//k:single quoted.the next string is single quoted 
    d_quoted = 0;//k:double quoted.the next string is double quoted 

    cf->args->nelts = 0;//init to 0,when return ,set to args of directive
    b = cf->conf_file->buffer;//conf file buffer ,may be filled already in previous call
    start = b->pos;//k:start of current token.last end of position.now from here
    start_line = cf->conf_file->line;//current line 

    file_size = ngx_file_size(&cf->conf_file->file.info);
    for ( ;; ) {
        if (b->pos >= b->last) {//k: we got to the last of buf,need to read more
            if (cf->conf_file->file.offset >= file_size) {
				//k:unexpected end or reach the end of file
                if (cf->args->nelts > 0) {//k:parsing args
                    if (cf->conf_file->file.fd == NGX_INVALID_FILE) {
                        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected end of parameter, " "expecting \";\"");
                        return NGX_ERROR;
                    }
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected end of file, " "expecting \";\" or \"}\"");
                    return NGX_ERROR;
                }
                return NGX_CONF_FILE_DONE;//k:we got to the end .good
            }
            len = b->pos - start;
            if (len == NGX_CONF_BUFFER) {//k:all buf is used for a line.param is too long
                cf->conf_file->line = start_line;
                if (d_quoted) {
                    ch = '"';//just now we encounter a ",we need to tell user he may need "
                } else if (s_quoted) {
                    ch = '\'';
                } else {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "too long parameter \"%*s...\" started",10, start);
                    return NGX_ERROR;
                }
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,  "too long parameter, probably " "missing terminating \"%c\" character", ch);
                return NGX_ERROR;
            }
            if (len) {//k:copy it to the very start of totall buf.so we got some empty space
                ngx_memcpy(b->start, start, len);
            }
			//left size charactors 
            size = (ssize_t) (file_size - cf->conf_file->file.offset);
            if (size > b->end - (b->start + len)) {
                size = b->end - (b->start + len);//read as most as possable bytes
            }
            n = ngx_read_file(&cf->conf_file->file, b->start + len, size,  cf->conf_file->file.offset);
            if (n == NGX_ERROR) {
                return NGX_ERROR;
            }
            if (n != size) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, ngx_read_file_n " returned " "only %z bytes instead of %z", n, size);
                return NGX_ERROR;
            }
            b->pos = b->start + len;
            b->last = b->pos + n;
            start = b->start;
        }

        ch = *b->pos++;//k: forward the next word
        if (ch == LF) {
            cf->conf_file->line++;
            if (sharp_comment) {//comment end if conqualed \n
                sharp_comment = 0;
            }
        }
        if (sharp_comment) {//k ignore comment
            continue;
        }
        if (quoted) {//k what's mean?
            quoted = 0;
            continue;
        }
        if (need_space) {
            if (ch == ' ' || ch == '\t' || ch == CR || ch == LF) {
                last_space = 1;//k:find space ,good ,next i need charactors 
                need_space = 0;
                continue;
            }
            if (ch == ';') {//k:we need space ,but ';' means end of line ,good,return 
                return NGX_OK;
            }
            if (ch == '{') {//k:http { , start of block
                return NGX_CONF_BLOCK_START;
            }
            if (ch == ')') {
                last_space = 1;
                need_space = 0;
            } else {
                 ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected \"%c\"", ch);
                 return NGX_ERROR;
            }
        }
        if (last_space) {//k: we just now conquered space,next expect space or chars or end
            if (ch == ' ' || ch == '\t' || ch == CR || ch == LF) {
                continue;
            }
            start = b->pos - 1;//k:ch is not space,mean we have find a word.
            //k: the word is stored at line 686 .begin for the new
            start_line = cf->conf_file->line;
            switch (ch) {
            case ';':
            case '{':
                if (cf->args->nelts == 0) { ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected \"%c\"", ch);
                    return NGX_ERROR;
                }
                if (ch == '{') {//k cf->args->nelts>0,mean we have args,like 'server {'
                    return NGX_CONF_BLOCK_START;
                }
                return NGX_OK;//ch == ;,end of line.normal
            case '}'://eg:"   }",
                if (cf->args->nelts != 0) {
					//k: conquer } ,but no ; invalidate
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "unexpected \"}\"");
                    return NGX_ERROR;
                }
                return NGX_CONF_BLOCK_DONE;
            case '#'://#eg:   #asdasdf.if conquer # at the middle of directive ,continue next line
                sharp_comment = 1;
                continue;
            case '\\':
                quoted = 1;
                last_space = 0;
                continue;
            case '"':
                start++;//K;ignore " in string arg
                d_quoted = 1;//k:double quoted.
                last_space = 0;
                continue;
            case '\'':
                start++;//K;ignore " in string arg
                s_quoted = 1;//k:next char is single quoted by ch
                last_space = 0;
                continue;
            default:
                last_space = 0;
            }
        } else {
            if (ch == '{' && variable) {
                continue;
            }
            variable = 0;
            if (ch == '\\') {
                quoted = 1;
                continue;
            }
            if (ch == '$') {
                variable = 1;//k:we encounter variable,like:$document_root$fastcgi_script_name;
                continue;
            }
            if (d_quoted) {
                if (ch == '"') {
                    d_quoted = 0;//k:end of double quoted
                    need_space = 1;
                    found = 1;
                }
            } else if (s_quoted) {
                if (ch == '\'') {//end of single quote
                    s_quoted = 0;
                    need_space = 1;
                    found = 1;
                }
            } else if (ch == ' ' || ch == '\t' || ch == CR || ch == LF
            		|| ch == ';' || ch == '{')
            {
                last_space = 1;
                found = 1;//we find a token.
            }
            if (found) {
                word = ngx_array_push(cf->args);//we got a param,store it in args,for uplayer to use
                if (word == NULL) {
                    return NGX_ERROR;
                }
                word->data = ngx_pnalloc(cf->pool, b->pos - start + 1);
                if (word->data == NULL) {
                    return NGX_ERROR;
                }
                for (dst = word->data, src = start, len = 0; src < b->pos - 1;  len++) {
                    if (*src == '\\') {
                        switch (src[1]) {
                        case '"':
                        case '\'':
                        case '\\':
                            src++;
                            break;

                        case 't':
                            *dst++ = '\t';
                            src += 2;
                            continue;

                        case 'r':
                            *dst++ = '\r';
                            src += 2;
                            continue;

                        case 'n':
                            *dst++ = '\n';
                            src += 2;
                            continue;
                        }

                    }
                    *dst++ = *src++;
                }
                *dst = '\0';
                word->len = len;

                if (ch == ';') {
                    return NGX_OK;
                }

                if (ch == '{') {
                    return NGX_CONF_BLOCK_START;
                }

                found = 0;//k again to search the next param
            }
        }
    }
}


static char *
ngx_conf_include(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char        *rv;
    ngx_int_t    n;
    ngx_str_t   *value, file, name;
    ngx_glob_t   gl;

    value = cf->args->elts;
    file = value[1];

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

    if (ngx_conf_full_name(cf->cycle, &file, 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (strpbrk((char *) file.data, "*?[") == NULL) {

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

        return ngx_conf_parse(cf, &file);
    }

    ngx_memzero(&gl, sizeof(ngx_glob_t));

    gl.pattern = file.data;
    gl.log = cf->log;
    gl.test = 1;

    if (ngx_open_glob(&gl) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           ngx_open_glob_n " \"%s\" failed", file.data);
        return NGX_CONF_ERROR;
    }

    rv = NGX_CONF_OK;

    for ( ;; ) {
        n = ngx_read_glob(&gl, &name);

        if (n != NGX_OK) {
            break;
        }

        file.len = name.len++;
        file.data = ngx_pstrdup(cf->pool, &name);

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cf->log, 0, "include %s", file.data);

        rv = ngx_conf_parse(cf, &file);

        if (rv != NGX_CONF_OK) {
            break;
        }
    }

    ngx_close_glob(&gl);

    return rv;
}


ngx_int_t
ngx_conf_full_name(ngx_cycle_t *cycle, ngx_str_t *name, ngx_uint_t conf_prefix)
{//将name参数转为绝对路径。
    size_t      len;
    u_char     *p, *n, *prefix;
    ngx_int_t   rc;

    rc = ngx_conf_test_full_name(name);//看看路径是不是绝对路径。
    if (rc == NGX_OK) {
        return rc;//是绝对路径直接返回
    }
    if (conf_prefix) {
        len = cycle->conf_prefix.len;
        prefix = cycle->conf_prefix.data;
    } else {//k:if not specified prefix path ,use the global.
        len = cycle->prefix.len;
        prefix = cycle->prefix.data;
    }
#if (NGX_WIN32)
    if (rc == 2) {
        len = rc;
    }
#endif
    n = ngx_pnalloc(cycle->pool, len + name->len + 1);
    if (n == NULL) {
        return NGX_ERROR;
    }
    p = ngx_cpymem(n, prefix, len);
    ngx_cpystrn(p, name->data, name->len + 1);
    name->len += len;
    name->data = n;
    return NGX_OK;
}


static ngx_int_t
ngx_conf_test_full_name(ngx_str_t *name)
{//监测一下路径是不是绝对路径。win不太好判断。linux很简单，第一个字符就行
#if (NGX_WIN32)
    u_char  c0, c1;
    c0 = name->data[0];
    if (name->len < 2) {
        if (c0 == '/') {
            return 2;
        }
        return NGX_DECLINED;
    }
    c1 = name->data[1];
    if (c1 == ':') {
        c0 |= 0x20;
        if ((c0 >= 'a' && c0 <= 'z')) {
            return NGX_OK;
        }
        return NGX_DECLINED;
    }
    if (c1 == '/') {
        return NGX_OK;
    }
    if (c0 == '/') {
        return 2;
    }
    return NGX_DECLINED;
#else
    if (name->data[0] == '/') {
        return NGX_OK;
    }
    return NGX_DECLINED;
#endif
}

//找到这个文件的ngx_open_file_t结构，如果没有，新建，但不会打开文件的
ngx_open_file_t *
ngx_conf_open_file(ngx_cycle_t *cycle, ngx_str_t *name)
{
    ngx_str_t         full;
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_open_file_t  *file;

#if (NGX_SUPPRESS_WARN)
    ngx_str_null(&full);
#endif

    if (name->len) {
        full = *name;

        if (ngx_conf_full_name(cycle, &full, 0) != NGX_OK) {
            return NULL;
        }

        part = &cycle->open_files.part;
        file = part->elts;//打开的文件列表，要在这里面找

        for (i = 0; /* void */ ; i++) {//一个个找

            if (i >= part->nelts) {
                if (part->next == NULL) {
                    break;
                }
                part = part->next;
                file = part->elts;
                i = 0;//啥意思，再来一次吗，直道头
            }

            if (full.len != file[i].name.len) {//先砍长度
                continue;
            }

            if (ngx_strcmp(full.data, file[i].name.data) == 0) {
                return &file[i];//如果找到了，就返回这个结构
            }
        }
    }
// 到这里说明没有找到已经打开的，那就新建一个
    file = ngx_list_push(&cycle->open_files);
    if (file == NULL) {
        return NULL;
    }

    if (name->len) {
        file->fd = NGX_INVALID_FILE;
        file->name = full;

    } else {
        file->fd = ngx_stderr;
        file->name = *name;
    }

    file->buffer = NULL;

    return file;
}


static void
ngx_conf_flush_files(ngx_cycle_t *cycle)
{
    ssize_t           n, len;
    ngx_uint_t        i;
    ngx_list_part_t  *part;
    ngx_open_file_t  *file;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, cycle->log, 0, "flush files");

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

        len = file[i].pos - file[i].buffer;

        if (file[i].buffer == NULL || len == 0) {
            continue;
        }

        n = ngx_write_fd(file[i].fd, file[i].buffer, len);

        if (n == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_write_fd_n " to \"%s\" failed",
                          file[i].name.data);

        } else if (n != len) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          ngx_write_fd_n " to \"%s\" was incomplete: %z of %uz",
                          file[i].name.data, n, len);
        }
    }
}


void ngx_cdecl
ngx_conf_log_error(ngx_uint_t level, ngx_conf_t *cf, ngx_err_t err,
    const char *fmt, ...)
{
    u_char   errstr[NGX_MAX_CONF_ERRSTR], *p, *last;
    va_list  args;

    last = errstr + NGX_MAX_CONF_ERRSTR;

    va_start(args, fmt);
    p = ngx_vslprintf(errstr, last, fmt, args);
    va_end(args);

    if (err) {
        p = ngx_log_errno(p, last, err);
    }

    if (cf->conf_file == NULL) {
        ngx_log_error(level, cf->log, 0, "%*s", p - errstr, errstr);
        return;
    }

    if (cf->conf_file->file.fd == NGX_INVALID_FILE) {
        ngx_log_error(level, cf->log, 0, "%*s in command line",
                      p - errstr, errstr);
        return;
    }

    ngx_log_error(level, cf->log, 0, "%*s in %s:%ui",
                  p - errstr, errstr,
                  cf->conf_file->file.name.data, cf->conf_file->line);
}


char *
ngx_conf_set_flag_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t        *value;
    ngx_flag_t       *fp;
    ngx_conf_post_t  *post;

    fp = (ngx_flag_t *) (p + cmd->offset);

    if (*fp != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
        *fp = 1;

    } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
        *fp = 0;

    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                     "invalid value \"%s\" in \"%s\" directive, "
                     "it must be \"on\" or \"off\"",
                     value[1].data, cmd->name.data);
        return NGX_CONF_ERROR;
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, fp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_str_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t        *field, *value;
    ngx_conf_post_t  *post;

    field = (ngx_str_t *) (p + cmd->offset);

    if (field->data) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *field = value[1];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, field);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_str_array_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t         *value, *s;
    ngx_array_t      **a;
    ngx_conf_post_t   *post;

    a = (ngx_array_t **) (p + cmd->offset);

    if (*a == NGX_CONF_UNSET_PTR) {
        *a = ngx_array_create(cf->pool, 4, sizeof(ngx_str_t));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    s = ngx_array_push(*a);
    if (s == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    *s = value[1];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, s);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_keyval_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_str_t         *value;
    ngx_array_t      **a;
    ngx_keyval_t      *kv;
    ngx_conf_post_t   *post;

    a = (ngx_array_t **) (p + cmd->offset);

    if (*a == NULL) {
        *a = ngx_array_create(cf->pool, 4, sizeof(ngx_keyval_t));
        if (*a == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    kv = ngx_array_push(*a);
    if (kv == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    kv->key = value[1];
    kv->value = value[2];

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, kv);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_num_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_int_t        *np;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    np = (ngx_int_t *) (p + cmd->offset);

    if (*np != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;
    *np = ngx_atoi(value[1].data, value[1].len);
    if (*np == NGX_ERROR) {
        return "invalid number";
    }
    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, np);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_size_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    size_t           *sp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    sp = (size_t *) (p + cmd->offset);
    if (*sp != NGX_CONF_UNSET_SIZE) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *sp = ngx_parse_size(&value[1]);
    if (*sp == (size_t) NGX_ERROR) {
        return "invalid value";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, sp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_off_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    off_t            *op;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    op = (off_t *) (p + cmd->offset);
    if (*op != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *op = ngx_parse_offset(&value[1]);
    if (*op == (off_t) NGX_ERROR) {
        return "invalid value";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, op);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_msec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_msec_t       *msp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    msp = (ngx_msec_t *) (p + cmd->offset);
    if (*msp != NGX_CONF_UNSET_MSEC) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *msp = ngx_parse_time(&value[1], 0);
    if (*msp == (ngx_msec_t) NGX_ERROR) {
        return "invalid value";
    }

    if (*msp == (ngx_msec_t) NGX_PARSE_LARGE_TIME) {
        return "value must be less than 597 hours";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, msp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_sec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    time_t           *sp;
    ngx_str_t        *value;
    ngx_conf_post_t  *post;


    sp = (time_t *) (p + cmd->offset);
    if (*sp != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    *sp = ngx_parse_time(&value[1], 1);
    if (*sp == NGX_ERROR) {
        return "invalid value";
    }

    if (*sp == NGX_PARSE_LARGE_TIME) {
        return "value must be less than 68 years";
    }

    if (cmd->post) {
        post = cmd->post;
        return post->post_handler(cf, post, sp);
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_bufs_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char *p = conf;

    ngx_str_t   *value;
    ngx_bufs_t  *bufs;


    bufs = (ngx_bufs_t *) (p + cmd->offset);
    if (bufs->num) {
        return "is duplicate";
    }

    value = cf->args->elts;

    bufs->num = ngx_atoi(value[1].data, value[1].len);
    if (bufs->num == NGX_ERROR || bufs->num == 0) {
        return "invalid value";
    }

    bufs->size = ngx_parse_size(&value[2]);
    if (bufs->size == (size_t) NGX_ERROR || bufs->size == 0) {
        return "invalid value";
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_set_enum_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_uint_t       *np, i;
    ngx_str_t        *value;
    ngx_conf_enum_t  *e;

    np = (ngx_uint_t *) (p + cmd->offset);

    if (*np != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    value = cf->args->elts;
    e = cmd->post;

    for (i = 0; e[i].name.len != 0; i++) {
        if (e[i].name.len != value[1].len
            || ngx_strcasecmp(e[i].name.data, value[1].data) != 0)
        {
            continue;
        }

        *np = e[i].value;

        return NGX_CONF_OK;
    }

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "invalid value \"%s\"", value[1].data);

    return NGX_CONF_ERROR;
}


char *
ngx_conf_set_bitmask_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char  *p = conf;

    ngx_uint_t          *np, i, m;
    ngx_str_t           *value;
    ngx_conf_bitmask_t  *mask;


    np = (ngx_uint_t *) (p + cmd->offset);
    value = cf->args->elts;
    mask = cmd->post;

    for (i = 1; i < cf->args->nelts; i++) {
        for (m = 0; mask[m].name.len != 0; m++) {

            if (mask[m].name.len != value[i].len
                || ngx_strcasecmp(mask[m].name.data, value[i].data) != 0)
            {
                continue;
            }

            if (*np & mask[m].mask) {
                ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                                   "duplicate value \"%s\"", value[i].data);

            } else {
                *np |= mask[m].mask;
            }

            break;
        }

        if (mask[m].name.len == 0) {
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                               "invalid value \"%s\"", value[i].data);

            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


char *
ngx_conf_unsupported(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    return "unsupported on this platform";
}


char *
ngx_conf_deprecated(ngx_conf_t *cf, void *post, void *data)
{
    ngx_conf_deprecated_t  *d = post;

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "the \"%s\" directive is deprecated, "
                       "use the \"%s\" directive instead",
                       d->old_name, d->new_name);

    return NGX_CONF_OK;
}


char *
ngx_conf_check_num_bounds(ngx_conf_t *cf, void *post, void *data)
{
    ngx_conf_num_bounds_t  *bounds = post;
    ngx_int_t  *np = data;

    if (bounds->high == -1) {
        if (*np >= bounds->low) {
            return NGX_CONF_OK;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "value must be equal or more than %i", bounds->low);

        return NGX_CONF_ERROR;
    }

    if (*np >= bounds->low && *np <= bounds->high) {
        return NGX_CONF_OK;
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "value must be between %i and %i",
                       bounds->low, bounds->high);

    return NGX_CONF_ERROR;
}
