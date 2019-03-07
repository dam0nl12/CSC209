PORT = 52672
CFLAGS = -DPORT=$(PORT) -g -Wall -std=gnu99
DEPENDENCIES = ftree.h hash.h


all: rcopy_client rcopy_server

rcopy_client: rcopy_client.o hash_functions.o ftree.o
	gcc ${CFLAGS} -o $@ $^

rcopy_server: rcopy_server.o hash_functions.o ftree.o
	gcc ${CFLAGS} -o $@ $^

%.o: %.c ${DEPENDENCIES}
	gcc ${CFLAGS} -c $<

clean: 
	rm *.o rcopy_client rcopy_server
