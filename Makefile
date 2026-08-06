CFLAGS=-Wall -Wextra -std=gnu99 -Iinclude -ggdb -MMD -MP
LDFLAGS=-lpthread

UTILS_OBJ:=utils/sockets.o\
		   utils/workers.o\
		   utils/http.o \
		   utils/config.o \
		   utils/server.o \
		   utils/routing.o

BINDIR=bin
FOXTAIL_SRC=foxtails/src

include foxtails/foxtails.make

$(UTILS_OBJ):%.o: %.c
	$(CC) -o$@ $(CFLAGS) -c $<

$(BINDIR):
	mkdir -p $@

-include $(UTILS_OBJ:.o=.d)
