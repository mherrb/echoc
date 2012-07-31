#

PROG= echoc
SRCS= echoc.c

OBJS= $(SRCS:%.c=%.o)

LIBS=	-lrt

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS) $(PROG)

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<
