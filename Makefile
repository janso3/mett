INCS =
LIBS = -lncursesw -lm
CFLAGS = $(INCS) -g -O2 -std=c11 -Wall -Wextra -pedantic-errors
LDFLAGS = $(LIBS)

PREFIX = /usr/local

OBJ = mett.o

.c.o:
	$(CC) $(CFLAGS) -c $<

mett: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)
	strip $@

clean:
	rm -f mett $(OBJ)

install: mett
	mkdir -p $(PREFIX)/bin
	cp -f mett $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/mett
	mkdir -p ${PREFIX}/share/man/man1
	cp -f mett.1 ${PREFIX}/share/man/man1
	chmod 644 ${PREFIX}/share/man/man1/mett.1

uninstall:
	rm -f $(PREFIX)/bin/mett\
		${PREFIX}/share/man/man1/mett.1

.PHONY: clean install uninstall
