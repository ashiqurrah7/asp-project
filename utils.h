#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <dirent.h>


int send_all(int sock, const void *buf, size_t len);

int recv_all(int sock, void *buf, size_t len);

int send_string(int sock, const char *s);

char* recv_string(int sock);

void create_dirs_if_needed(const char* path);

#endif
