libsocket.a: ksocket.o
	ar rcs libsocket.a ksocket.o

ksocket.o: ksocket.c ksocket.h
	gcc -c ksocket.c
