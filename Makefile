CC=gcc
LIB=-pthread
BIN=dsm

all:
	make binary

dsm: dsm.c
	$(CC) $(LIB) -c $<

test: test.c
	$(CC) -c $<

binary: dsm.c test.c
	make dsm
	make test
	$(CC) $(LIB) -o $(BIN) dsm.o test.o

.PHONY: clean
clean:
	rm -f *.o $(BIN)
