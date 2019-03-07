#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "ftree.h"
#include "hash.h"

#ifndef PORT
  #define PORT 30000
#endif


/* Add the new client with the filedescriptor fd as the first element of the 
 * struct client top, then return the adjusted top.
 */
static struct client *addclient(struct client *top, int fd) {
    struct client *p = malloc(sizeof(struct client));
    // Error check.
    if (!p) {
        perror("malloc");
        exit(1);
    }

	p->state = AWAITING_TYPE;
    p->fd = fd;
    p->next = top;
    top = p;
    
    return top;
}


/* Remove the client with the filedescriptor fd in the struct client top, then 
 * return the adjusted top.
 */
static struct client *removeclient(struct client *top, int fd) {
    struct client **p;
    
    // Finding the target client.
    for (p = &top; *p && (*p)->fd != fd; p = &(*p)->next);
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    
    // Found the target client.
    if (*p) {
        struct client *t = (*p)->next;
        printf("Removing client %d\n", fd);
        free(*p);
        *p = t;
    
    // Could not find the target client.
    } else {
        fprintf(stderr, "Error - Remove fd %d\n", fd);
    }
    
    return top;
}


/* Check if corresponding file/directory in the request need to be updated in 
 * the server. 
 * Return 0 if there is no need for a data transfer,
 *        1 if file contents need to be updated,
 *       -1 if there is a type mismatch or other error.
 */
int checkfile(struct request req) {
	char fullpath[MAXPATH];
    if (getcwd(fullpath, MAXPATH) == NULL) {
        perror("getcwd");
        fprintf(stderr, "Error - Get current working directory\n");
    };
	strncat(fullpath, "/", MAXPATH - strlen(fullpath) + 1);
	strncat(fullpath, req.path, MAXPATH - strlen(fullpath) + 1);
			
    // Case 1: if the request is of type-regular-file.
    if (req.type == 1) {
        FILE * fp;
        fp = fopen(fullpath, "r"); // Error check below.
        int copy_pass = 0; // copy_pass is set to 1 if the file is different.
	
        // Case 1.1: if the file doesn't exist, or there is an error.
        if (fp == NULL) {
            // Error check for fopen four lines above.
            if(errno != ENOENT && errno != EISDIR) {
                perror("fopen");
				fprintf(stderr, "Error - fopen: on file at path\n%s\n",
                        fullpath);
                return -1;
					
            // Mismatch between the file and existing directory.
            } else if (errno == EISDIR) {
				// Still need to change the permission.
                if (chmod(fullpath, req.mode & (0777))) {
					perror("chmod");
				}
                
                fprintf(stderr,
                        "Error - Mismatch between source and destination:\n%s\n",
                        req.path);
				return -1;
		
            // Create a new file, if it doesn't exist.
            } else {
                fp = fopen(fullpath, "w");
                if (fp == NULL) {
                    perror("fopen");
                    fprintf(stderr, "Error - fopen: on the file at path\n%s\n",
                            fullpath);
                }
                copy_pass += 1;
            }

        // Case 1.2: if the file already exists.
        } else {
            struct stat efilestat; // 'e' in the name stands for exist.
			
            if (lstat(fullpath, &efilestat) == -1) {
                perror("lstat");
                return -1;
            }
			
            // Check the file's size.
            if (efilestat.st_size != req.size) {
                copy_pass += 1;
			
            // Check the file's hash value.
            } else {
                char efilehash[9];
                hash(efilehash, fp);
                copy_pass += check_hash(req.hash, efilehash);
            }
        }
        
        // Files are different.
        if (copy_pass != 0) {
			if (fclose(fp) != 0) {
				perror("fclose");
				return -1;
			}
            
            // We need to overwrite the file.
            // Empty the file firstly.
			fp = fopen(fullpath, "w"); 
			if (fp == NULL) {
				perror("fopen");
                fprintf(stderr, "Error - fopen: on the file at path\n%s\n",
                        fullpath);
				return -1;
			}
        	return 1;
        
        // Files are the same.
        } else {
            if (chmod(fullpath, req.mode & 0777)) {
                perror("chmod");
				return -1; 
            }
			return 0;
        }
    
        if (fclose(fp) != 0) {
            perror("fclose");
            fprintf(stderr, "Error - fclose: on the file at path\n%s\n",
                    fullpath);
			return -1;
        }
				
    // Case 2: if the request is of type-directory.
	} else if(req.type == 2) {
        // Create a new directory if it was not in the dest.
		if (mkdir(fullpath, req.mode & (0777)) == -1) {
			if (errno != EEXIST) {
				perror("mkdir");
                return -1;

			//Check if it's a mismatch between directory and existing file.
			} else {
				struct stat edirstat;
				if (lstat(fullpath, &edirstat) == -1) {
					perror("lstat");
					return -1;
				}

				// If it is a directory then set it's mode appropriately.
				if (S_ISDIR(edirstat.st_mode)) {
                    if (chmod(fullpath, req.mode & (0777))) {
						perror("chmod");
                        return -1;
					}

				// Existing file is regular (or link), then it is a type
                // mismatch.
				} else {
					fprintf(stderr,
                            "Error - Mismatch betwen source and destination: \n%s\n",
                            req.path);
                    
                    // Still need to change the permission.
					if (chmod(fullpath, req.mode & (0777))) {
						perror("chmod");
					}
					return -1;
				}
			}
		}		
	}
	return 0;
}


/* Receive client's info. based on its status, and update its status after read.
 * Return 0 for success and no further actions needed, 
 *       -1 for error and stop reading,
 *        1 for TRANSFILE request to be sent to this client,
 *        2 for OK request to be sent,
 *        3 if client has closed.
 */
int handleclient(struct client *p, struct client *top) {
    // Get info. in the order: type, path, mode, hash, size.
    
    if (p->state == AWAITING_TYPE) {
		int buf;
		int numread = read(p->fd, &buf, sizeof(p->req.type));
		
        // Client has closed the socket, it should be removed.
        if (numread == 0) {
			return 3;
        } else if (numread <0) {
            perror("read");
            fprintf(stderr, "Error - read: Read initial request\n");
        }
		
        p->req.type = ntohs(buf);
		p->state = AWAITING_PATH;

	} else if (p->state == AWAITING_PATH) {
        if (read(p->fd, &p->req.path, MAXPATH) < 0) {
            perror("read");
            fprintf(stderr, "Error - read: Read path from client\n");
        }
		p->state = AWAITING_PERM;

	} else if (p->state == AWAITING_PERM) {
        if (read(p->fd, &p->req.mode, sizeof(p->req.mode)) < 0) {
            perror("read");
            fprintf(stderr, "Error - read: Read mode for relative path %s\n",
                    p->req.path);
        }
		p->state = AWAITING_HASH;

	} else if (p->state == AWAITING_HASH) {
        if (read(p->fd, &p->req.hash, BLOCKSIZE) < 0) {
            perror("read");
            fprintf(stderr, "Error - read: Read hash for relative path %s\n",
                    p->req.path);
        }
		p->state = AWAITING_SIZE;

	} else if (p->state == AWAITING_SIZE) {
		int buf;
        
		if (read(p->fd, &buf, sizeof(p->req.size)) < 0 ) {
			perror("read");
            fprintf(stderr, "Error - read: Read size for relative path %s\n",
                    p->req.path);
			return -1; 
		}
        
		p->req.size = ntohs(buf);

		// Move to accept Data state if type is TRANSFILE.
        if (p->req.type == TRANSFILE) {
			p->state = AWAITING_DATA;

		//restart the state machine if it's not a TRANSFILE request.
		} else {
			int check = checkfile(p->req);
			p->state = AWAITING_TYPE;
			
			// Return different value depending on check's return value.
            // 0: OK request to be sent.
            if (check == 0) {
				return 2;
                
            // -1: error and stop reading.
			} else if (check == -1) {
				return -1;
                
            // 1: TRANSFILE request to be sent to this client.
			} else if (check == 1) {
				return 1;
			}
		}
    
    // Transfer data and update the file.
	} else if (p->state == AWAITING_DATA) {
		char fullpath[MAXPATH];
		getcwd(fullpath, MAXPATH);
		strncat(fullpath, "/", MAXPATH - strlen(fullpath) + 1);
		strncat(fullpath, p->req.path, MAXPATH - strlen(fullpath) + 1);
			
		// "fpow" stands for "file pointer (to be) over writen."
		FILE * fpow = fopen(fullpath, "a");
		if (fpow == NULL) {
			perror("fopen");
			fprintf(stderr, "Error - opening file for appending %s \n",
                    fullpath);
			return -1;
		}
        
		char bufdata[MAXDATA];
		int num_read = read(p->fd, bufdata, sizeof(bufdata)); // Error check below.
		int num_written = 0; // Set to 0 as default.

		if (num_read < 0) {
			perror("read");
			fprintf(stderr, "Error: Reading data for file %s\n", fullpath);
			return -1;
        
        // Check if it is the last data transfer.
		} else if (num_read < 256) {
			if (fwrite(bufdata, sizeof(char), num_read, fpow) != num_read) {
				perror("fwrite");
				fprintf(stderr, "Error - writing data for file %s\n", fullpath);
				return -1;
			}
            
			p->state = AWAITING_TYPE;
            
            if (chmod(fullpath, p->req.mode & (0777))) {
				perror("chmod");
				fprintf(stderr, "Error - chmod: for file %s\n", fullpath);
				return -1;
			}
         
        // All data is not yet transfered.
		} else {
			num_written = fwrite(bufdata, sizeof(char), MAXDATA, fpow);
		}
		
        if (fclose(fpow) != 0) {
			perror("fclose");
			return -1;
		}
		
        // Error check for fwrite in else block above.
		if (num_written < 0) {
			perror("fwrite");
			return -1;
		}
        // Return 2 so that an OK request is sent.
		return 2;
	}
    // Return 0 so that no request is sent.
	return 0;
}


/* Setup the socket. Return the file-descriptor, which is an endpoint for 
 * communication. Return 1 if there is an error.
 */
int setup_server(void) {
	int on = 1;
  	struct sockaddr_in self;
  	int listenfd;
  	
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    	perror("socket");
    	exit(1);
  	}

  	// Make sure to reuse the port immediately after the server terminates.
  	if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
    	             &on, sizeof(on)) == -1) {
    	perror("setsockopt -- REUSEADDR");
  	}

  	memset(&self.sin_zero, '\0', sizeof(self));
  	self.sin_family = AF_INET;
  	self.sin_addr.s_addr = INADDR_ANY;
  	self.sin_port = htons(PORT);
  	printf("Listening on %d\n", PORT);

  	if (bind(listenfd, (struct sockaddr *)&self, sizeof(self)) < 0) {
    	perror("bind");
		close(listenfd);
    	exit(1);
  	}

  	if (listen(listenfd, 5) < 0) {
    	perror("listen");
		close(listenfd);
    	exit(1);
  	}
  
  	return listenfd;
}


/* Listen for and handle all requests from clients.
 */
void rcopy_server(unsigned short port) {
	// The fd for the socket.
	int sockfd;

	// Setup the socket for the server.
 	sockfd = setup_server();

	// The fd for the client pulled from the queue in the socket.
 	int clientfd;

	// Maximum fd as upper bound for select function.
	int maxfd;

	// allfds is master list of connections, listen_fds is copy for select function.
	fd_set allfds, listenfds;

	// Empty allfds so that it doesn't contain any garbage.
	FD_ZERO(&allfds);

	// Add the socket to allfds.
	FD_SET(sockfd, &allfds);
	
	struct client *p;
	struct client *head = NULL; 
		
 	struct sockaddr_in peer;
 	socklen_t socklen;

	// Set the initial upper bound to the value of the only fd in the set.
	maxfd = sockfd;

 	while (1) {
		// Copy allfds into listenfds so select doesn't manipulate master set of fds.
		listenfds = allfds;

		int nready = select(maxfd + 1, &listenfds, NULL, NULL, NULL);
		// Error check select.
		if (nready == -1) {
			perror("select");
			exit(1);
		}
	
		// Check if the original socket is in the set, then create a new connection.
		if (FD_ISSET(sockfd, &listenfds)) {
			socklen = sizeof(peer);
			// Pass in valid pointers for the second and third arguments to accept 
    		// here to store and use client information.
    		if ((clientfd = accept(sockfd,
                                   (struct sockaddr *)&peer, &socklen)) < 0) {
      			perror("accept");
    		}
      		
			// Update maxfd if necessary.
			if (clientfd > maxfd) {
				maxfd = clientfd;
			}
			// Add new fd to master set of all fds.
			FD_SET(clientfd, &allfds);
			// Report successful connection to stdout.
			printf("New connection on port %d\n", ntohs(peer.sin_port));
			head = addclient(head, clientfd);
		}

		for (int i = 0; i <= maxfd; i++) {
			if (FD_ISSET(i, &listenfds)) {
				for (p = head; p != NULL; p = p->next) {
					if (p->fd == i) {
						
                        // Read and manage data sent from the client.
                        int result = handleclient(p,head);
						
                        // Remove the client.
                        if (result == 3) {
							int tmpfd = p->fd;
							head = removeclient(head, p->fd);
							FD_CLR(tmpfd, &allfds);
							close(tmpfd);
						
                        } else if (result != 0) {
							// Make the type appropriate given result from
							// function checkfile.
							if (result == 1) {
								p->req.type = SENDFILE;
							} else if (result == 2) {
								p->req.type = OK;
							} else {
								p->req.type = ERROR;
							}

							// Write request to socket.
							int typebuf = htons(p->req.type);
        					if (write(p->fd, &typebuf, sizeof(typebuf)) == -1) {
								perror("write");
							}
							printf("Request of type %d\n", p->req.type);
						}
						break;
					}
				}
			}
		}			    	
  	}
}


//Code below this comment handles the client.


/* Generate the information of file/directory rooted at parents concatenated
 * with path, and return the corresponding struct request.
 */
struct request request_generator(const char *parent, char *path) {
    struct request req;
    struct stat st;
    
    // Get the fullpath of the file/directory.
    // fullpath = parent + path
    char fullpath[MAXPATH];
    strncpy(fullpath, parent, MAXPATH);
    strncat(fullpath, "/", 2);
    strncat(fullpath, path, strlen(path) + 1);
    
    // Check if fullpath if valid.
    if (lstat(fullpath, &st) == -1) {
        perror("lstat");
        exit(EXIT_FAILURE);
    }
    
    // Case 0: If fullpath is a link, we will set request's type as 0. Then it
    // will not be copied.
    if (S_ISLNK(st.st_mode)){
        req.type = 0;
        return req;
    }
    
    // Pass in the file/directory's path, size and mode.
    strncpy(req.path, path, MAXPATH);
    req.size = st.st_size;
    req.mode = st.st_mode;
    
    // Case 1: If fullpath is a regular file.
    // We need to get the hash value of the file.
    if(S_ISREG(st.st_mode)) {
        req.type = 1;
        
        FILE *fp = fopen(fullpath, "r");
        if (fp == NULL) {
            perror("fopen");
            fprintf(stderr, "Error - fopen: opening file %s\n", fullpath);
            exit(1);
            // This could cause problems exiting stopping the recursion, but
            // we did not have time to implement it as a return error in order
            // to continue on with the recursion and close the socket properly.
            
        }
        
        hash(req.hash, fp);
        
    // Case 2: If fullpath is a directory.
    } else if (S_ISDIR(st.st_mode)) {
        req.type = 2;
    }
    
    return req;
}


/* Return the basename of file rooted at the path fname.
 */
char *get_basename(const char *fname) {
    char *basec, *bname;
    basec = strdup(fname);
    bname = basename(basec);
    
    return bname;
}


/* Traverse file tree rooted at fullpath(parent + path). Return 0 on success, 
 * or 1 on failure.
 */
int traverse_ftree(const char *parent, char *path, int soc, char *host) {
    struct request req = request_generator(parent, path);
   
	char fullpath[MAXPATH];
    // Set fullpath to be the absolute path of the file or directory.
    strncpy(fullpath, parent, MAXPATH);
    strncat(fullpath, "/", 2);
    strncat(fullpath, path, strlen(path) + 1);
 
    // Case 1: If fullpath is not a link, then it might be a
    // file/direcoty. Need transfer the info. to the server.
    if (req.type != 0) {
        // Translate any numeric types to network order in case the systems
        // have different endians.
        int typebuf = htons(req.type);
        if (write(soc, &typebuf, sizeof(typebuf)) == -1) {
			perror("write");
			exit(1);
            // Exit instead of return because if returned the server would be
            // out of sync with the client anyway and so incorrect data could be
            // written to the client.
		}
        
        if (write(soc, &req.path, MAXPATH) == -1) {
			perror("write");
			exit(1);
		}
        
        if (write(soc, &req.mode, sizeof(req.mode)) == -1) {
			perror("write");
			exit(1);
		}
        
        if (write(soc, &req.hash, BLOCKSIZE) == -1) {
			perror("write");
			exit(1);
		}
        
        typebuf = htons(req.size);
        if (write(soc, &typebuf, sizeof(typebuf))== -1) {
			perror("write");
			exit(1);
		}
	
		// Wait for and get response from the server.
		int response_type;
		if (read(soc, &response_type, sizeof(int)) < 0) {
			perror("read");
			exit(1);
		}
		response_type = ntohs(response_type);

		// Child process should be made to send TRANSFILE request.
		if (response_type == SENDFILE) {
			int pid = fork();
			
            if (pid < 0) {
				perror("fork");
				close(soc);
				exit(1);
			}
            
			// If it's a child process send data through a new socket.
			if (pid == 0) {
				soc = setup_client(host, PORT);
				req.type = TRANSFILE;

				FILE *fp = fopen(fullpath, "r");
				if (fp == NULL) {
					perror("fopen");
					fprintf(stderr, "Error - fopen %s\n", fullpath);
					exit(1);
				}
                
				// Send request to server for file overwrite.
				int typebuf = htons(req.type);
        		if (write(soc, &typebuf, sizeof(typebuf)) == -1) {
					perror("write");
					fprintf(stderr, "Error - Write request for %s to socket\n",
                            fullpath);
					exit(1);
				}
                
       			if (write(soc, &req.path, MAXPATH) == -1) {
					perror("write");
					fprintf(stderr, "Erroe - Write request for %s to socket\n",
                            fullpath);
					exit(1);

				}
                
        		if (write(soc, &req.mode, sizeof(req.mode)) == -1) {
					perror("write");
					fprintf(stderr, "Failed to write request for %s to socket\n", fullpath);
					exit(1);

				}
                
        		if (write(soc, &req.hash, BLOCKSIZE) == -1) {
					perror("write");
					fprintf(stderr, "Error - write request for %s to socket\n",
                            fullpath);
					exit(1);

				}
                
        		typebuf = htons(req.size);
        		if (write(soc, &typebuf, sizeof(typebuf)) == -1) {
					perror("write");
					fprintf(stderr, "Error - Write request for %s to socket\n",
                            fullpath);
					exit(1);

				}
				
				// Send file's data.
				char databuf[MAXDATA];
                int num_read = fread(&databuf, sizeof(char), MAXDATA, fp);
                if (write(soc, &databuf, num_read)== -1) {
                    perror("write");
                    fprintf(stderr, "Error - Write request for %s to socket\n",
                            fullpath);
                    exit(1);
                }
                
				while (num_read == 256) {
                    num_read = fread(&databuf, sizeof(char), MAXDATA, fp);
                    if (num_read <0) {
                        perror("fread");
                        fprintf(stderr, "Error - Read file %s\n", fullpath);
                        exit(1);
                    }
					if (write(soc, &databuf, num_read) == -1) {
						perror("write");
						fprintf(stderr, "Error - Write request for %s to socket\n",
                                fullpath);
						exit(1);
					}
				}
                
				if (read(soc, &response_type, sizeof(int)) < 0) {
					perror("read");
					exit(1);
				}
                
				response_type = ntohs(response_type);
				if (response_type == ERROR) {					
					fprintf(stderr, "Error - %s\n", fullpath);
					close(soc);
					exit(1);
				} else {
					close(soc);
					exit(0);
				}
			}
            
    	} else if (response_type == ERROR) {
			fprintf(stderr, "Error - %s\n", fullpath);
			return 1;
		}
	}
    
    // Case 2: If fullpath is a directory, then recurse to lower levels.
    if (req.type == 2) {
        DIR *dir_ptr;
        struct dirent *content;
        if ((dir_ptr = opendir(fullpath)) == NULL) {
            perror("opendir");
            fprintf(stderr, "Error - opendir: On directory\n%s\n", fullpath);
            return 1;
        }
        
        // Go through contents of Directory.
        while ((content = readdir(dir_ptr)) != NULL) {
            // A file starting with ?.? should be skipped.
            if (content->d_name[0] != '.') {
                // Get the new subpath of a innner file/directory.
                char subpath[MAXPATH];
                strncpy(subpath, path, MAXPATH);
                strncat(subpath, "/", 2);
                strncat(subpath, content->d_name, strlen(content->d_name));
                
                traverse_ftree(parent, subpath, soc, host);
            }
        }
    }
    return 0;
}


/* Set up the socket for the client side. Return the file-descriptor, which is
 * an endpoint for communication. Teturn 1 if there is an error.
 */
int setup_client(char *host, unsigned short port) {
	int soc;
	struct sockaddr_in peer;
	
	peer.sin_family = AF_INET;
  	peer.sin_port = htons(port);

	if ((soc = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    	perror("socket");
    	exit(1);
  	}
		
  	if (inet_pton(AF_INET, host, &peer.sin_addr) < 1) {
    	perror("inet_pton");
    	close(soc);
    	exit(1);
  	}
	
	if (connect(soc, (struct sockaddr *)&peer, sizeof(peer)) == -1) {
   		perror("connect");
   		exit(1);
  	}
	return soc;
}


/* Initiate a connection with rcopy_server, and send data of file or 
 * directory rooted at source.*/
int rcopy_client(char *source, char *host, unsigned short port) {

  	char path[MAXPATH];
	strncpy(path, source, MAXPATH);
	
	int soc = setup_client(host, port);	
	
	// Split source into it's basename and it's parent folders.
	char* fname = get_basename(source);
	char srccpy[strlen(source) + 1];
	strncpy(srccpy, source, strlen(source) + 1);
	srccpy[strlen(srccpy) - strlen(fname) - 1] = '\0';

	// Call recursive function to traverse the file tree. TODO: deal with responses
	traverse_ftree(srccpy, fname, soc, host);
  	
  	close(soc);
    
  	return 0;
}
