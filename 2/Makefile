# Makefile for
# Project: mastermind
# Author: Christian Fiedler <e1363562@student.tuwien.ac.at>

CC=gcc
CFLAGS=-O3 -std=c99 -pedantic -Wall -c -g -D_XOPEN_SOURCE=500 -D_POSIX_C_SOURCE=200112L 
LFLAGS=-lm
DOXYFILE=./Doxyfile
DOXY_TRASH=html latex doxygen_sqlite3.db

# all = release target
all: encr

# debug -> debug mode 
debug: CFLAGS+= -DENDEBUG -g 
debug: encr
 
encr: encr.o
	$(CC) $(LFLAGS) encr.o -o encr

%.o: %.c
	$(CC) $(CFLAGS) $^

clean:
	rm -f *.o encr
	rm -Rf $(DOXY_TRASH)

docs:
	doxygen $(DOXYFILE)

.PHONY: all docs debug clean
