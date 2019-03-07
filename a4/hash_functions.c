#include <stdio.h>
#include <stdlib.h>

#define BLOCK_SIZE 8


/* Build the hash of size block_size, and save it at hash_val.
 */
char *hash(char *hash_val, FILE *f) {
    char ch;
    int hash_index = 0;

    for (int index = 0; index < BLOCK_SIZE; index++) {
        hash_val[index] = '\0';
    }

    while(fread(&ch, 1, 1, f) != 0) {
        hash_val[hash_index] ^= ch;
        hash_index = (hash_index + 1) % BLOCK_SIZE;
    }

    return hash_val;
}


/* Check two Hashes. Return 1 and print the first index where two Hashes do not 
 * match, or return 0 if every value matches.
 */
int check_hash(const char *hash1, const char *hash2) {
    for (long i = 0; i < BLOCK_SIZE; i++) {
        if (hash1[i] != hash2[i]) {
            printf("Index %ld: %c\n", i, hash1[i]);
            return 1;
        }
    }
    return 0;
}
