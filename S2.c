#include "utils.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SERVER_PORT 6002
#define BASE_FOLDER "S2"
#define CHUNK 4096

void handle_client(int connfd);
void cmd_STORE(int connfd);
void cmd_GET(int connfd);
void cmd_REMOVE(int connfd);
void cmd_TAR(int connfd);
void cmd_LIST(int connfd);

int main(void) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("[S2] socket");
    exit(1);
  }
  int opt = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in servaddr;
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(SERVER_PORT);
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
    perror("[S2] bind");
    exit(1);
  }

  if (listen(sockfd, 10) < 0) {
    perror("[S2] listen");
    exit(1);
  }

  printf("[S2] Listening on port %d, storing .pdf files under '%s'...\n",
         SERVER_PORT, BASE_FOLDER);

  while (1) {
    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);
    int connfd = accept(sockfd, (struct sockaddr *)&caddr, &clen);
    if (connfd < 0) {
      perror("[S2] accept");
      continue;
    }
    if (fork() == 0) {
      close(sockfd);
      handle_client(connfd);
      close(connfd);
      _exit(0);
    } else {
      close(connfd);
    }
  }

  return 0;
}

// similar to prcclient() in the sense that it parses the command from the
// server and decides what function to run
void handle_client(int connfd) {
  while (1) {
    char *command = recv_string(connfd);
    if (!command) {
      break;
    }

    if (strcmp(command, "STORE") == 0) {
      free(command);
      cmd_STORE(connfd);
    } else if (strcmp(command, "GET") == 0) {
      free(command);
      cmd_GET(connfd);
    } else if (strcmp(command, "REMOVE") == 0) {
      free(command);
      cmd_REMOVE(connfd);
    } else if (strcmp(command, "TAR") == 0) {
      free(command);
      cmd_TAR(connfd);
    } else if (strcmp(command, "LIST") == 0) {
      free(command);
      cmd_LIST(connfd);
    } else {
      free(command);
      break;
    }
  }
}

void cmd_STORE(int connfd) {
  // get path from S1
  char *path = recv_string(connfd);
  if (!path)
    return;

  // get file size from S1
  char *sizestr = recv_string(connfd);
  if (!sizestr) {
    free(path);
    return;
  }
  long fsize = atol(sizestr);
  free(sizestr);

  if (fsize <= 0) {
    printf("[S2] cmd_STORE: file size <= 0. Discard.\n");
    // discard data if incorrect file size
    char discard[512];
    while (fsize > 0) {
      long chunk = (fsize > 512) ? 512 : fsize;
      if (recv_all(connfd, discard, chunk) < 0)
        break;
      fsize -= chunk;
    }
    free(path);
    return;
  }

  char localpath[CHUNK];
  memset(localpath, 0, sizeof(localpath));

  snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path);
  free(path);

  // handle when no filename has been provided
  size_t len = strlen(localpath);
  if (len == 0) {
    // Should never happen, but just in case
    strcpy(localpath, "S2/default.pdf");
  } else {
    if (localpath[len - 1] == '/') {
      // user didnt specift file name, so use default as filename
      strcat(localpath, "default.pdf");
    }
  }

  // Split off the directory from localpath so that only the folders can be sent
  // to create_dirs_if_needed(). If it had the filename, it would create a
  // folder with that filename like folder/hello.c would translate to
  // folder/hello.c/hello.c
  char folder[CHUNK];
  strncpy(folder, localpath, sizeof(folder) - 1);
  folder[sizeof(folder) - 1] = '\0';

  char *slash = strrchr(folder, '/');
  if (slash) {
    *slash = '\0'; // cut after slash => folder has "S2" or "S2/folder"
    // create folders specified by user if they don't exist
    create_dirs_if_needed(folder);
  }

  // Open final file for writing
  FILE *fp = fopen(localpath, "wb");
  if (!fp) {
    perror("[S2] fopen in STORE");
    // discard data if couldnt open file
    char discard[512];
    while (fsize > 0) {
      long chunk = (fsize > 512) ? 512 : fsize;
      if (recv_all(connfd, discard, chunk) < 0)
        break;
      fsize -= chunk;
    }
    return;
  }

  // get data from S1
  long remain = fsize;
  char buf[CHUNK];
  while (remain > 0) {
    long chunk = (remain > CHUNK) ? CHUNK : remain;
    if (recv_all(connfd, buf, chunk) < 0) {
      printf("[S2] Error receiving file data.\n");
      break;
    }

    // write file locally
    size_t written = fwrite(buf, 1, chunk, fp);
    if (written != (size_t)chunk) {
      perror("[S2] fwrite error");
      break;
    }
    remain -= chunk;
  }
  fclose(fp);

  printf("[S2] Stored .pdf => %s\n", localpath);
}

void cmd_GET(int connfd) {
  char *path = recv_string(connfd);
  if (!path)
    return;

  char localpath[CHUNK];
  snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path);

  free(path);

  FILE *fp = fopen(localpath, "rb");
  if (!fp) {
    // send 0 file size to S1 indicating there's an error opening the file,
    // or file doesn't exist
    send_string(connfd, "0");
    return;
  }
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  char sizebuf[64];
  snprintf(sizebuf, sizeof(sizebuf), "%ld", sz);
  // send file size
  send_string(connfd, sizebuf);

  // send file
  char buf[CHUNK];
  while (!feof(fp)) {
    size_t r = fread(buf, 1, CHUNK, fp);
    if (r > 0) {
      if (send_all(connfd, buf, r) < 0) {
        printf("[S2] GET send error\n");
        break;
      }
    }
  }
  fclose(fp);
}

void cmd_REMOVE(int connfd) {
  char *path = recv_string(connfd);
  if (!path)
    return;

  char localpath[CHUNK];
  snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path);

  free(path);

  remove(localpath);
}

void cmd_TAR(int connfd) {
  // works same way as S1
  system("rm -f s2pdf.tar");
  system("tar -cf s2pdf.tar S2");

  FILE *fp = fopen("s2pdf.tar", "rb");
  if (!fp) {
    send_string(connfd, "0");
    return;
  }
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  char sizebuf[64];
  snprintf(sizebuf, sizeof(sizebuf), "%ld", sz);
  send_string(connfd, sizebuf);

  char buf[CHUNK];
  while (!feof(fp)) {
    size_t r = fread(buf, 1, CHUNK, fp);
    if (r > 0) {
      if (send_all(connfd, buf, r) < 0) {
        printf("[S2] TAR send error\n");
        break;
      }
    }
  }
  fclose(fp);
}

void cmd_LIST(int connfd) {
  // works same way as S1
  char *path = recv_string(connfd);
  if (!path)
    return;

  char localpath[CHUNK];
  snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path);

  free(path);

  DIR *d = opendir(localpath);
  if (!d) {
    // no directory => send empty list
    send_string(connfd, "");
    return;
  }
  char lines[256][256];
  int count = 0;
  struct dirent *dd;
  while ((dd = readdir(d))) {
    if (dd->d_name[0] == '.')
      continue;
    strncpy(lines[count], dd->d_name, 255);
    lines[count][255] = '\0';
    count++;
    if (count >= 256)
      break;
  }
  closedir(d);

  // bubble sort
  for (int i = 0; i < count; i++) {
    for (int j = i + 1; j < count; j++) {
      if (strcmp(lines[i], lines[j]) > 0) {
        char tmp[256];
        strcpy(tmp, lines[i]);
        strcpy(lines[i], lines[j]);
        strcpy(lines[j], tmp);
      }
    }
  }

  char result[8192];
  result[0] = '\0';
  for (int i = 0; i < count; i++) {
    strcat(result, lines[i]);
    strcat(result, "\n");
  }
  send_string(connfd, result);
}
