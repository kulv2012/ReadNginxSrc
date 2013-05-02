
default:	build

clean:
	rm -rf Makefile objs

build:
	$(MAKE) -f objs/Makefile

install:
	$(MAKE) -f objs/Makefile install

upgrade:
	/home/haiwen/nginx/sbin/nginx -t

	kill -USR2 `cat /home/haiwen/nginx/logs/nginx.pid`
	sleep 1
	test -f /home/haiwen/nginx/logs/nginx.pid.oldbin

	kill -QUIT `cat /home/haiwen/nginx/logs/nginx.pid.oldbin`
