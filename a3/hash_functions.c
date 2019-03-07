#include <stdio.h>
#include <stdlib.h>
#include "hash.h"

#define BLOCK_SIZE 8

/* Return the hash value hash_val of FILE *f. */
char *hash(FILE *f) {
    // Initialize all bytes of hash_val.
    char *hash_val = malloc(sizeof(char) * 9);
    for(int i = 0; i < 9; i++) {
        hash_val[i] = '\0';
    }
    
    char c;
    int k = 0;
    
    while(fread(&c, sizeof(char), 1, f)) {
        if (k == BLOCK_SIZE) {
            k = 0;
        }
        hash_val[k] = hash_val[k] ^ c;
        k++;
    }
    
    rewind(f);
    return hash_val;
}
