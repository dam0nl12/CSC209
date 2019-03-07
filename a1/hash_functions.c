#include <stdio.h>

// Build the Hash of size block_size, and save it at hash_val

void hash(char *hash_val, long block_size) {
    // Initialize block_size bytes of hash_val
    for(int i = 0; i < block_size; i++) {
        hash_val[i] = '\0';
    }
    
    char ch;
    int j = 0;
    
    while(scanf("%c", &ch) != EOF) {
        // The computation needs wrap around if j reaches block_size
        if(j == block_size) {
            j = 0;
        }
        hash_val[j] = hash_val[j] ^ ch;
        j++;
    }
}

/* Check two Hashes. Return the first index where two Hashes do not match,
 * or return the block_size if every value matches.
 */
 
int check_hash(const char *hash1, const char *hash2, long block_size) {
    // Case 1: Two hashes do not match
    for(int i = 0; i < block_size; i++) {
        if(hash1[i] != hash2[i]) {
            printf("Two hashes do not match firstly at index %d\n", i);
            return i;
        }
    }
    
    // Case 2: Two hashes match
    printf("Two hashes match, block_size is %ld\n", block_size);
    return block_size;
}
