INCS =
LIBS = -lncurses
CFLAGS = $(INCS) -g -O2 -std=c99 -pedantic-errors
LDFLAGS = $(LIBS)

OBJ = mett.o

.c.o:
	$(CC) $(CFLAGS) -c $<

mett: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)
