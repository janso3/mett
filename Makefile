INCS =
LIBS = -lncurses
CFLAGS = $(INCS)
LDFLAGS = $(LIBS)

OBJ = mett.o

.c.o:
	$(CC) $(CFLAGS) -c $<

mett: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)
