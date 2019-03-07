#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <libgen.h>
#include <limits.h>

#include "ftree.h"
#include "hash.h"

// Helper function.
char * get_filename(const char *fname);

/*
 * Return the FTree rooted at the path fname.
 */
struct TreeNode *generate_ftree(const char *fname) {
    struct stat st;
    
    if (lstat(fname, &st) == -1) {
        perror("lstat");
        exit(EXIT_FAILURE);
    }
    
    struct TreeNode *node_ptr = malloc(sizeof(struct TreeNode));
    
    // Get fname's name.
    int malloc_size = (strlen(get_filename(fname)) + 1) * sizeof(char);
    node_ptr->fname = malloc(malloc_size);
    strncpy(node_ptr->fname, get_filename(fname), malloc_size);
    
    // Get fname's octal chmod.
    node_ptr->permissions = st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
    
    // Case 1: fname is a file/link.
    if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
        // contents is NULL for a file/link node.
        // next might be connected to another node in the recursive call.
        node_ptr->contents = NULL;
        node_ptr->next = NULL;
        
        // Get the fname's hash.
        FILE *file;
        int error;
        
        file = fopen(fname, "r");
        if (file == NULL) {
            fprintf(stderr, "Error opening file\n");
            exit(1);
        }
        
        node_ptr->hash = hash(file);
        
        error = fclose(file);
        if (error != 0) {
            fprintf(stderr, "fclose failed\n");
            exit(1);
        }
    
    // Case 2: fname is a directory.
    } else if (S_ISDIR(st.st_mode)) {
        DIR* dir_ptr;
        struct dirent* dir_element;
        char full_path[PATH_MAX];
        int i = 0;
        // Collection of node(s) at the same level.
        struct TreeNode **node_lst = malloc(sizeof(struct TreeNode) * 4096);
        struct TreeNode *inner_node = malloc(sizeof(struct TreeNode));
        
        // hash is NULL for a directory.
        node_ptr->hash = NULL;
        
        // Get the directory's contents,
        // which might be NULL if it is a empty folder.
        dir_ptr = opendir(fname);
        if (dir_ptr == NULL) {
            perror("opendir");
            exit(1);
        }
        
        // Get contents of this node.
        while((dir_element = readdir(dir_ptr)) != NULL) {
            // No filename that starts with '.' should be included.
            if (dir_element->d_name[0] != '.') {
                // Use strcpy and strcat, since the length of string
                // full_path should not be larger than PATH_MAX.
                strcpy(full_path, fname);
                strcat(full_path, "/");
                strcat(full_path, dir_element->d_name);
                inner_node = generate_ftree(full_path);
                
                while(node_lst[i] != NULL) {
                    i++;
                }
                node_lst[i] = inner_node;
            }
        }
        
        closedir(dir_ptr);
        
        // Link nodes together.
        node_ptr->contents = node_lst[0];
        for(int j = 0; j < i; j++) {
            (node_lst[j])->next = node_lst[j + 1];
        }
        // Empty the array.
        for(int j = 0; j < 4096; j++) {
        		node_lst[j] = NULL;
        }
    }
    
    return node_ptr;
}


/*
 * Print the TreeNodes encountered on a preorder traversal of an FTree.
 */
void print_ftree(struct TreeNode *root) {
    // Here's a trick for remembering what depth (in the tree) you're at
    // and printing 2 * that many spaces at the beginning of the line.
    static int depth = 0;
    printf("%*s", depth * 2, "");
    
    // Case 1: root is a directory.
    if (root->hash == NULL) {
        printf("===== %s (%o) =====\n", root->fname, root->permissions);
        depth += 1;
        print_ftree(root->contents);
        depth -= 1;
    }
    
    // Case 2: root is a file/link.
    else {
        printf("%s (%o)\n", root->fname, root->permissions);
        
//        Check hash
//        for(int k = 0; k < 9; k++) {
//            printf("%.2hhx ", root->hash[k]);
//        }
    }
    
    // Check if root is followed by another node.
    if (root->next != NULL) {
        print_ftree(root->next);
    }
}

/*
 * Return the name of FTree rooted at the path fname.
 */
char * get_filename(const char *fname) {
    char *basec, *bname;
    basec = strdup(fname);
    bname = basename(basec);
    
    return bname;
}
