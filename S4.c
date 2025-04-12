/***************************************************************
 * S4.c (Option B fix)
 *
 *  - Listens on port 6004 for "STORE", "GET", "REMOVE", "TAR", "LIST"
 *  - If the path from S1 ends in '/', or is something like "/",
 *    we auto-append "default.zip" so we don't get "Is a directory."
 *  - That means the final file is stored as e.g. "S4//default.zip"
 *    or "S4/myfolder/default.zip".
 *
 *  NOTE: This loses the original filename if S1 didn't include it
 *  in the path. If you *do* want the original name, you'd have S1
 *  pass "folder/hello.zip" to S4. But this code at least avoids
 *  the "Is a directory" error.
 ***************************************************************/
#include "utils.h" // your send_all, recv_all, create_dirs_if_needed, etc.
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
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

#define SERVER_PORT 6004
#define BASE_FOLDER "S4"

void handle_client(int cfd);
void cmd_STORE(int cfd);
void cmd_GET(int cfd);
void cmd_REMOVE(int cfd);
void cmd_LIST(int cfd);

int main(void) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("[S4] socket");
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
    perror("[S4] bind");
    exit(1);
  }

  if (listen(sockfd, 10) < 0) {
    perror("[S4] listen");
    exit(1);
  }

  printf("[S4] Listening on port %d, storing .zip files under '%s'...\n",
         SERVER_PORT, BASE_FOLDER);

  while (1) {
    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);
    int cfd = accept(sockfd, (struct sockaddr *)&caddr, &clen);
    if (cfd < 0) {
      perror("[S4] accept");
      continue;
    }
    if (fork() == 0) {
      close(sockfd);
      handle_client(cfd);
      close(cfd);
      _exit(0);
    } else {
      close(cfd);
    }
  }

  return 0;
}

void handle_client(int cfd) {
  while (1) {
    char *cmd = recv_string(cfd);
    if (!cmd) {
      break;
    }

    if (strcmp(cmd, "STORE") == 0) {
      free(cmd);
      cmd_STORE(cfd);
    } else if (strcmp(cmd, "GET") == 0) {
      free(cmd);
      cmd_GET(cfd);
    } else if (strcmp(cmd, "REMOVE") == 0) {
      free(cmd);
      cmd_REMOVE(cfd);
    } else if (strcmp(cmd, "LIST") == 0) {
      free(cmd);
      cmd_LIST(cfd);
    } else {
      free(cmd);
      break;
    }
  }
}

/***************************************************************
 * cmd_STORE:
 *   - S1 calls "STORE", then sends:
 *         1) path (e.g. "/")
 *         2) file size
 *         3) file data
 *
 *   - We convert "~S1/folder" => "S4/folder"
 *   - If the final path ends with '/', we append "default.zip"
 *   - Create directories only for the folder portion, then fopen.
 ***************************************************************/
void cmd_STORE(int cfd) {
  // 1) read path
  char *path = recv_string(cfd);
  if (!path)
    return;

  // 2) read file size
  char *sizestr = recv_string(cfd);
  if (!sizestr) {
    free(path);
    return;
  }
  long fsize = atol(sizestr);
  free(sizestr);

  if (fsize <= 0) {
    printf("[S4] cmd_STORE: file size <= 0. Discard.\n");
    // discard data
    char discard[512];
    while (fsize > 0) {
      long chunk = (fsize > 512) ? 512 : fsize;
      if (recv_all(cfd, discard, chunk) < 0)
        break;
      fsize -= chunk;
    }
    free(path);
    return;
  }

  // 3) Convert path from "~S1/..." => "S4/..."
  char localpath[1024];
  memset(localpath, 0, sizeof(localpath));

  // e.g. "~S1/folder/testdir" => "S4/folder/testdir"
  if (strncmp(path, "~S1/", 4) == 0) {
    snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path + 4);
  } else {
    // if path is just "/", or some other partial
    snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path);
  }
  free(path);

  // If localpath ends in '/', or if it's "S4/", we have no filename =>
  // let's auto-append "default.zip"
  size_t len = strlen(localpath);
  if (len == 0) {
    // Should never happen, but just in case
    strcpy(localpath, "S4/default.zip");
  } else {
    if (localpath[len - 1] == '/') {
      // user gave a directory only => append a default name
      strcat(localpath, "default.zip");
    }
  }

  // e.g. if user said path="/", then localpath = "S4//" => we fix by appending
  // "default.zip"
  // => "S4//default.zip"
  // That works but you might want to avoid double slash. It's harmless though.

  // 4) Split off the directory from localpath
  char folder[1024];
  strncpy(folder, localpath, sizeof(folder) - 1);
  folder[sizeof(folder) - 1] = '\0';

  char *slash = strrchr(folder, '/');
  if (slash) {
    *slash = '\0'; // cut after slash => folder has "S4" or "S4/folder"
    create_dirs_if_needed(folder);
  }

  // 5) Open final file for writing
  FILE *fp = fopen(localpath, "wb");
  if (!fp) {
    perror("[S4] fopen in STORE");
    // discard data
    char discard[512];
    while (fsize > 0) {
      long chunk = (fsize > 512) ? 512 : fsize;
      if (recv_all(cfd, discard, chunk) < 0)
        break;
      fsize -= chunk;
    }
    return;
  }

  // 6) read and write
  long remain = fsize;
  char buf[1024];
  while (remain > 0) {
    long chunk = (remain > 1024) ? 1024 : remain;
    if (recv_all(cfd, buf, chunk) < 0) {
      printf("[S4] Error receiving file data.\n");
      break;
    }
    size_t written = fwrite(buf, 1, chunk, fp);
    if (written != (size_t)chunk) {
      perror("[S4] fwrite error");
      break;
    }
    remain -= chunk;
  }
  fclose(fp);

  printf("[S4] Stored .zip => %s\n", localpath);
}

/***************************************************************
 * cmd_GET: S1 requests a file path, we read from S4, send size + data
 ***************************************************************/
void cmd_GET(int cfd) {
  char *path = recv_string(cfd);
  if (!path)
    return;

  // e.g. path "~S1/folder/abc.zip" => "S4/folder/abc.zip"
  char localpath[1024];
  if (strncmp(path, "~S1/", 4) == 0) {
    snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path + 4);
  } else {
    snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path);
  }
  free(path);

  FILE *fp = fopen(localpath, "rb");
  if (!fp) {
    // file doesn't exist or can't open
    send_string(cfd, "0");
    return;
  }
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  char sizebuf[64];
  snprintf(sizebuf, sizeof(sizebuf), "%ld", sz);
  send_string(cfd, sizebuf);

  char buf[1024];
  while (!feof(fp)) {
    size_t r = fread(buf, 1, 1024, fp);
    if (r > 0) {
      if (send_all(cfd, buf, r) < 0) {
        printf("[S4] GET send error\n");
        break;
      }
    }
  }
  fclose(fp);
}

/***************************************************************
 * cmd_REMOVE: S1 says remove <path> => we remove local
 ***************************************************************/
void cmd_REMOVE(int cfd) {
  char *path = recv_string(cfd);
  if (!path)
    return;

  char localpath[1024];
  if (strncmp(path, "~S1/", 4) == 0) {
    snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path + 4);
  } else {
    snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path);
  }
  free(path);

  remove(localpath);
}

/***************************************************************
 * cmd_LIST:
 *   - S1 wants a directory listing under S4
 ***************************************************************/
void cmd_LIST(int cfd) {
  char *path = recv_string(cfd);
  if (!path)
    return;

  char localpath[1024];
  if (strncmp(path, "~S1/", 4) == 0) {
    snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path + 4);
  } else {
    snprintf(localpath, sizeof(localpath), "%s/%s", BASE_FOLDER, path);
  }
  free(path);

  DIR *d = opendir(localpath);
  if (!d) {
    // no directory => send empty list
    send_string(cfd, "");
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

  // sort
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

  // build final
  char result[8192];
  result[0] = '\0';
  for (int i = 0; i < count; i++) {
    strcat(result, lines[i]);
    strcat(result, "\n");
  }
  send_string(cfd, result);
}
