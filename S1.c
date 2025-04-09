/* S1.c */
#include "utils.h"
#include <asm-generic/socket.h>
#include <sys/wait.h>

// Hardcode the S2, S3, S4 IP/port or get them from argv
#define S1_PORT 5001
#define S2_HOST "127.0.0.1"
#define S2_PORT 6002
#define S3_HOST "127.0.0.1"
#define S3_PORT 6003
#define S4_HOST "127.0.0.1"
#define S4_PORT 6004

#define S1_FOLDER "S1" // local storage for .c

int connect_to(const char *host, int port);

void prcclient(int cfd);
void uploadf(int cfd, char *filename, char *dest);
void downlf(int cfd, char *path);
void removef(int cfd, char *path);
void downltar(int cfd, char *filetype);
void dispfnames(int cfd, char *path);

const char *get_file_extension(const char *filename) {
  const char *dot = strrchr(filename, '.');
  return dot ? dot : "";
}

int main() {
  mkdir(S1_FOLDER, 0777);

  int socketfd, con_sd;
  struct sockaddr_in servAdd;
  socketfd = socket(AF_INET, SOCK_STREAM, 0);

  if (socketfd < 0) {
    perror("Socket error");
    exit(1);
  }

  servAdd.sin_family = AF_INET;
  servAdd.sin_addr.s_addr = htonl(INADDR_ANY);
  servAdd.sin_port = htons((uint16_t)S1_PORT);

  if (bind(socketfd, (struct sockaddr *)&servAdd, sizeof(servAdd)) < 0) {
    perror("Bind error");
    exit(1);
  }

  if (listen(socketfd, 6) < 0) {
    perror("Listen error");
    exit(1);
  }

  printf("S1 listening on port %d...\n", S1_PORT);

  while (1) {
    struct sockaddr_in clientaddr;
    socklen_t clen = sizeof(clientaddr);
    int connfd = accept(socketfd, (struct sockaddr *)&clientaddr, &clen);
    if (connfd < 0) {
      perror("Accept error");
      continue;
    }
    if (fork() == 0) {
      close(socketfd);
      prcclient(connfd);
      close(connfd);
      exit(0);
    } else {
      close(connfd);
    }
  }
  return 0;
}
