exec: user1 user2

user1: user1.c libsocket.a
	gcc -o user1 -L. user1.c -lsocket

user2: user2.c libsocket.a
	gcc -o user2 -L. user2.c -lsocket
