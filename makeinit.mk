initksocket: initksocket.c ksocket.h libsocket.a
	gcc -o initksocket -L. initksocket.c -lsocket
