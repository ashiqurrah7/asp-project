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

// Simple send/recv wrapper for fixed-length messages
// Returns 0 on success, or -1 on error/EOF
int send_all(int sock, const void *buf, size_t len)
{
    size_t total = 0;
    const char *p = (const char*)buf;
    while (total < len) {
        ssize_t sent = send(sock, p + total, len - total, 0);
        if (sent <= 0) return -1;
        total += sent;
    }
    return 0;
}

// read exactly len bytes
int recv_all(int sock, void *buf, size_t len)
{
    size_t total = 0;
    char *p = (char*)buf;
    while (total < len) {
        ssize_t got = recv(sock, p + total, len - total, 0);
        if (got <= 0) return -1;
        total += got;
    }
    return 0;
}

// send a string (length + data)
int send_string(int sock, const char *s)
{
    uint32_t length = (uint32_t)strlen(s);
    uint32_t nlength = htonl(length);
    if (send_all(sock, &nlength, 4) < 0) return -1;
    if (length > 0 && send_all(sock, s, length) < 0) return -1;
    return 0;
}

// receive a string (length + data). Caller must free.
char* recv_string(int sock)
{
    uint32_t length = 0;
    if (recv_all(sock, &length, 4) < 0) return NULL;
    length = ntohl(length);
    char *buf = (char *)calloc(length+1, 1);
    if (length > 0) {
        if (recv_all(sock, buf, length) < 0) {
            free(buf);
            return NULL;
        }
    }
    return buf;
}

// create directories recursively
void create_dirs_if_needed(const char* path)
{
    // E.g., path ~S2/folder1/folder2 => we skip til /S2 then treat rest as relative
    // or you can do a naive approach: split by '/', call mkdir progressively
    char temp[1024];
    strcpy(temp, path);

    // Convert something like "~S2/folder1/folder2" => "S2/folder1/folder2" or custom logic
    // For simplicity, skip the leading "~S2". We assume the next slash is index 3 or 4. 
    // In real code, parse carefully based on your approach.

    char *p = strstr(temp, "/"); // find first slash
    if(!p) return;
    p++; // skip slash
    char build[1024];
    memset(build, 0, sizeof(build));
    while (*p) {
        char *slash = strchr(p, '/');
        if (!slash) {
            // final part
            strcat(build, p);
            break;
        }
        // partial
        size_t chunkLen = (size_t)(slash - p);
        strncat(build, p, chunkLen);
        mkdir(build, 0777);
        strcat(build, "/");
        slash++;
        p = slash;
    }
}
