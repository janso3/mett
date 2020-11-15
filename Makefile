INCS =
LIBS = -lncurses
CFLAGS = $(INCS) -g -O2 -std=c11 -pedantic-errors
LDFLAGS = $(LIBS)

PREFIX = /usr/local

OBJ = mett.o

.c.o:
	$(CC) $(CFLAGS) -c $<

mett: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

clean:
	rm -f mett $(OBJ)

install: mett
	mkdir -p $(PREFIX)/bin
	cp -f mett $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/mett

uninstall:
	rm -f $(PREFIX)/bin/mett

.PHONY: clean install uninstall
