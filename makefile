all: serveur1-fabin

serveur1-fabin: server.o tcp.o
	gcc server.o tcp.o -o serveur1-fabin -Wall -lpthread

server.o: server.c data.h tcp.h
	gcc -c server.c -o server.o -Wall

tcp.o: tcp.c tcp.h data.h
	gcc -c tcp.c -o tcp.o -Wall

clean:
	rm *.o server copy_* serveur1-fabin
