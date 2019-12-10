AR = ar
CC = gcc
CFLAGS = -g
LIBRARIES =

all:
	${CC} ${CFLAGS} goldelox_programmer.c -o goldelox_programmer
	${CC} ${CFLAGS} goldelox_media.c -o goldelox_media
	${CC} ${CFLAGS} switch_movie.c -o switch_movie

clean:
	rm -rf goldelox 
