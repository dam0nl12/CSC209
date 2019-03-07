#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "ftree.h"
#include "hash.h"


// Helper functions.
char *hash(FILE *f);
char * get_basename(const char *fname);
int check_hash(char *hash1, char *hash2, int block_size);

// Global variable.
int error_flag = 1;

int copy_ftree(const char *src, const char *dest) {
    struct stat src_st, dest_st;
    
    // Check if src is a valid path.
    if (lstat(src, &src_st) == -1) {
        perror("lstat");
        exit(EXIT_FAILURE);
    }
    
    // Check if dest is a valid path, and it should not be regualr file.
    if (lstat(dest, &dest_st) == -1) {
        perror("lstat");
        exit(EXIT_FAILURE);
    }
    if (S_ISREG(dest_st.st_mode)) {
        fprintf(stderr, "Destination should be a directory, not be a file.\n");
        exit(-1);
    }
    
    // Number of process.
    int process_num = 1;
    
    // Get the basename of src, and make a new path where the file should be
    // copied to.
    char new_path[PATH_MAX];
    strcpy(new_path, dest);
    strcat(new_path, "/");
    strcat(new_path, get_basename(src));
    
    // Check that if there is a file with the same name in the dest.
    FILE *fp_dest;
    fp_dest = fopen(new_path, "r");
    
    // Case 1: if src is a regualr file.
    if (S_ISREG(src_st.st_mode)) {
        char c;
        int error;
        FILE *fp_src;
        
        fp_src = fopen(src, "r");
        if (fp_src == NULL) {
            perror("fopen");
            exit(-1);
        }
        
        // Case 1.1: There is not a file with the same name is in the dest.
        if (fp_dest == NULL) {
            // Create a copy of src in the dest.
            fp_dest = fopen(new_path, "w");
            
            while(fread(&c, sizeof(char), 1, fp_src)) {
                fwrite(&c, sizeof(char), 1, fp_dest);
            }
            
            error = fclose(fp_dest);
            error += fclose(fp_src);
            if (error != 0) {
                perror("fclose");
                exit(-1);
            }
        
        // Case 1.2: There is a file with the same name is in the dest.
        } else {
            struct stat new_copy_st;

            if (lstat(new_path, &new_copy_st) == -1) {
                perror("lstat");
                exit(EXIT_FAILURE);
            }
            
            // If the file and the directory have the same name, then there is
            // a mismatch error.
            if(S_ISDIR(new_copy_st.st_mode)) {
                fprintf(stderr,
                        "Error Mismatch between source and destination:\n%s\n%s\n",
                        src, new_path);
                exit(-1);
            }
            
            int copy_pass = 0;
            
            // Check difference of sizes.
            if(new_copy_st.st_size != src_st.st_size) {
                copy_pass += 1;
            }
            
            // Check difference of hash value.
            if (copy_pass == 0) {
                char hash_src[9];
                strncpy(hash_src, hash(fp_src), 9);
                char hash_dest[9];
                strncpy(hash_dest, hash(fp_dest), 9);
                copy_pass += check_hash(hash_src, hash_dest, 8);
            }
            
            // If size or hash value is different, then overwriting the old
            // file.
            if (copy_pass != 0) {
                fp_dest = freopen(new_path, "w", fp_dest);
                
                if (fp_dest == NULL) {
                    perror("freopen");
                    exit(-1);
                }
                
                while(fread(&c, sizeof(char), 1, fp_src)) {
                    fwrite(&c, sizeof(char), 1, fp_dest);
                }
                
                error = fclose(fp_src);
                error += fclose(fp_dest);
                if (error != 0) {
                    perror("fclose");
                    exit(-1);
                }
            }
            
            // Update chmod.
            if (chmod(new_path, (src_st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)
                                 ))) {
                perror("chmod");
                exit(-1);
            }
        }
        
    // Case 2: if src is a direcoty.
    } else if (S_ISDIR(src_st.st_mode)) {
        DIR* src_ptr;
        DIR* new_dir_ptr;
        struct dirent* src_element;
        char src_element_path[PATH_MAX];
        struct stat new_copy_st;
        
        src_ptr = opendir(src);
        if (src_ptr == NULL) {
            perror("opendir");
            exit(-1);
        }
        
        // If the file and the directory have the same name, then there is a
        // mismatch error.
        if (lstat(new_path, &new_copy_st) != -1) {
            if(S_ISREG(new_copy_st.st_mode)) {
                fprintf(stderr,
                        "Error Mismatch between source and destination:\n%s\n%s\n",
                        src, new_path);
                exit(-1);
            }
        }

        // Case 2.1: Make a new directory in dest, if it doesn't exist.
        new_dir_ptr = opendir(new_path);
        if (new_dir_ptr == NULL) {
            mkdir(new_path, (src_st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)));
        
        // Case 2.2: The directory is already there, then change the chmod.
        } else {
            closedir(new_dir_ptr);
            
            // Update chmod.
            if (chmod(new_path, (src_st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO)
                                 ))) {
                perror("chmod");
                exit(-1);
            }
        }
        
        // Get contents in the src.
        while((src_element = readdir(src_ptr)) != NULL) {
            // No filename that starts with '.' should be included.
            if (src_element->d_name[0] != '.') {
                // Get the path of element in src.
                strcpy(src_element_path, src);
                strcat(src_element_path, "/");
                strcat(src_element_path, src_element->d_name);
                
                if (lstat(src_element_path, &new_copy_st) == -1) {
                    perror("lstat");
                    exit(EXIT_FAILURE);
                }
                
                // If src_element_path is a regular file, then copying it
                // without calling fork.
                if (S_ISREG(new_copy_st.st_mode)) {
                    copy_ftree(src_element_path, new_path);
                    
                // If src_element_path is a sub-directory, then call fork to
                // copy it.
                } else if (S_ISDIR(new_copy_st.st_mode)) {
                    int pid = fork();
                    
                    // Child process.
                    if (pid == 0) {
                        int child_result;
                        child_result = copy_ftree(src_element_path, new_path);
                        exit(child_result);
                        
                    // Original process.
                    } else if (pid > 0) {
                        // Check int status to get the result of child process.
                        int status;
                        
                        // Child process copies successfully.
                        if (wait(&status) != -1) {
                            if (WIFEXITED(status)) {
                                // Add the number of process, which the child
                                // process used.
                                char cvalue = WEXITSTATUS(status);
                                
                                // Errors encountered in the child process.
                                if (cvalue < 0){
                                    process_num += -1 * cvalue;
                                    error_flag = -1;
                                
                                // Update number of processes.
                                } else {
                                    process_num += cvalue;
                                }
                            }
                        
                        // Error with wait.
                        } else {
                            perror("wait");
                            exit(-1);
                        }
                        
                    // Error with fork.
                    } else {
                        perror("fork");
                        exit(-1);
                    }
                }
            }
        }
    
    // Case 3: if src is a soft link.
    } else if (S_ISLNK(src_st.st_mode)) {
        // Skip the link, and do nothing.
    }
    
    return error_flag * process_num;
}


/*
 * Return the basename of file rooted at the path fname.
 */
char *get_basename(const char *fname) {
    char *basec, *bname;
    basec = strdup(fname);
    bname = basename(basec);
    
    return bname;
}


/* Check two Hashes. Return the first index where two Hashes do not match,
 * or return the block_size if every value matches.
 */
int check_hash(char *hash1, char *hash2, int block_size) {
    int result = 0;
    
    for(int i = 0; i < block_size; i++) {
        // Hash value are different;
        if (hash1[i] != hash2[i]) {
            result += 1;
            return result;
        }
    }
    
    // Hash value are same.
    return result;
}
