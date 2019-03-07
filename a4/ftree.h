#ifndef _FTREE_H_
#define _FTREE_H_

#include "hash.h"
#include <sys/stat.h>

#define MAXPATH 128
#define MAXDATA 256

// Input states
#define AWAITING_TYPE 0
#define AWAITING_PATH 1
#define AWAITING_SIZE 2
#define AWAITING_PERM 3
#define AWAITING_HASH 4
#define AWAITING_DATA 5

// Request types
#define REGFILE 1
#define REGDIR 2
#define TRANSFILE 3

#define OK 0
#define SENDFILE 1
#define ERROR 2

#ifndef PORT
    #define PORT 30100
#endif


struct request {
    int type;           // Request type is REGFILE, REGDIR, TRANSFILE
    char path[MAXPATH];
    mode_t mode;
    char hash[BLOCKSIZE];
    int size;
};


struct client {
	int fd;
	int state;
	struct client *next;    
    struct request req;
};


// Functions for rcopy_server.
void rcopy_server(unsigned short port);
int setup_server(void);
int checkfile(struct request req);
int setup_client(char *host, unsigned short port);
int handleclient(struct client *p, struct client *top);

// Functions for rcopy_client.
int rcopy_client(char *source, char *host, unsigned short port);
struct request request_generator(const char *parent, char *path);
char *get_basename(const char *fname);
int traverse_ftree(const char *parent, char *path, int soc, char *host);
int setup_client(char *host, unsigned short port);

#endif // _FTREE_H_
