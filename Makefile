#

PROG= echoc
SRCS= echoc.c

OBJS= $(SRCS:%.c=%.o)

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) -o $@ $(OBJS)

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<
