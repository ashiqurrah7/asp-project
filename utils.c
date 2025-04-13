#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Simple send/recv wrapper for fixed-length messages
// Returns 0 on success, or -1 on error/EOF
// send/recv are specifically designed for sockets, whereas read/write
// is for general use case. send recv provide better eror contexts
// since we're using send and recv, we need to send byte by byte, since there
// might be data loss
int send_all(int sock, const void *buf, size_t len) {
  size_t total = 0;
  const char *p = (const char *)buf;
  while (total < len) {
    ssize_t sent = send(sock, p + total, len - total, 0);
    if (sent <= 0) {
      perror("send error");

      return -1;
    }
    total += sent;
  }
  return 0;
}

// read exactly len bytes
int recv_all(int sock, void *buf, size_t len) {
  size_t total = 0;
  char *p = (char *)buf;
  while (total < len) {
    ssize_t got = recv(sock, p + total, len - total, 0);
    if (got <= 0) {
      perror("recv error");

      return -1;
    }
    total += got;
  }
  return 0;
}

// send a string (length + data)
int send_string(int sock, const char *s) {
  uint32_t length = (uint32_t)strlen(s);
  uint32_t nlength = htonl(length);
  if (send_all(sock, &nlength, 4) < 0)
    return -1;
  if (length > 0 && send_all(sock, s, length) < 0)
    return -1;
  return 0;
}

// receive a string (length + data). Caller must free.
char *recv_string(int sock) {
  uint32_t length = 0;
  if (recv_all(sock, &length, 4) < 0)
    return NULL;
  length = ntohl(length);
  // calloc initializes memory space with 0s to avoid existing garbage data
  char *buf = (char *)calloc(length + 1, 1);
  if (length > 0) {
    if (recv_all(sock, buf, length) < 0) {
      free(buf);
      return NULL;
    }
  }
  return buf;
}

void create_dirs_if_needed(const char *path) {
  char temp[1024];
  strncpy(temp, path, sizeof(temp) - 1);
  temp[sizeof(temp) - 1] = '\0';

  // loop through provided path and tokenizes folder names through /
  char build[1024] = "";
  char *p = strtok(temp, "/");

  while (p != NULL) {
    strcat(build, p);
    strcat(build, "/");

    // Try to create the directory; ignore if it already exists
    mkdir(build, 0777);

    p = strtok(NULL, "/");
  }
}
