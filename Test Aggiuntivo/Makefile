
DIR_PATH = ../data/*
SOCKET_PATH = ../object.store

CC = gcc
AR = ar
CFLAGS += -std=c99 -std=gnu99 -Wall -pedantic -g
ARFLAGS = rvs
INCLUDES = -I..
LDFLAGS  = -L..
LIBS = -pthread
NAME_LIB = 


.PHONY: all clean cleanall real_test test
.SUFFIXES: .c .h


%: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -o $@ $< $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) -c -o $@ $<

all: img_test

img_test: img_test.o
	make -C ..
	$(CC) $(CFLAGS) $(INCLUDES) $(OPTFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS) -lostore


clean:
	rm -f img_test.o
	rm -f img_test

cleanall: clean
	make -C .. cleanall
	rm -fr ./data/*


test:
	make img_test
	../objectstore &
	./img_test
	killall -QUIT objectstore