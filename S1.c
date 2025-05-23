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
#define CHUNK 4096

#define S1_FOLDER "S1" // local storage for .c

int connect_to(const char *host, int port);

void prcclient(int connfd);
void uploadf(int connfd, char *filename, char *dest);
void downlf(int connfd, char *path);
void removef(int connfd, char *path);
void downltar(int connfd, char *filetype);
void dispfnames(int connfd, char *path);

const char *get_file_extension(const char *filename) {
  const char *dot = strrchr(filename, '.');
  return dot ? dot : "";
}

int main() {
  mkdir(S1_FOLDER, 0777);

  int socketfd, con_sd;
  struct sockaddr_in servAdd;
  memset(&servAdd, 0, sizeof(servAdd));
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

// infinite loop reading commands from client and runs functions accordingly
void prcclient(int connfd) {
  while (1) {
    char *cmdline = recv_string(connfd);
    if (!cmdline) {
      // break out of loop if client disconnected or if socket error occurs
      break;
    }
    // parse command
    char *tok = strtok(cmdline, " ");
    if (!tok) {
      free(cmdline);
      break;
    }

    if (strcmp(tok, "uploadf") == 0) {
      char *filename = strtok(NULL, " ");
      char *dest = strtok(NULL, " ");
      if (filename && dest) {
        uploadf(connfd, filename, dest);
      }
    } else if (strcmp(tok, "downlf") == 0) {
      char *path = strtok(NULL, " ");
      if (path) {
        downlf(connfd, path);
      }
    } else if (strcmp(tok, "removef") == 0) {
      char *path = strtok(NULL, " ");
      if (path) {
        removef(connfd, path);
      }
    } else if (strcmp(tok, "downltar") == 0) {
      char *ft = strtok(NULL, " ");
      if (ft) {
        downltar(connfd, ft);
      }
    } else if (strcmp(tok, "dispfnames") == 0) {
      char *p = strtok(NULL, " ");
      if (p) {
        dispfnames(connfd, p);
      }
    }

    free(cmdline);
  }
}

// upload file to server
void uploadf(int connfd, char *filename, char *dest) {
  // read the file size from client in string format
  char *sz_s = recv_string(connfd);
  if (!sz_s)
    return;
  long fsize = atol(sz_s);
  free(sz_s);

  // read and discard data incase of incorrect file size
  // data still needs to be read from socket to keep it open for
  // any upcomming connections
  if (fsize <= 0) {
    char discard[1024];
    long remain = fsize;
    while (remain > 0) {
      long chunk = (remain > 1024) ? 1024 : remain;
      if (recv_all(connfd, discard, chunk) < 0)
        break;
      remain -= chunk;
    }
    printf("[S1] do_uploadf: zero/invalid file size.\n");
    return;
  }

  // Save the incoming data into a temporary file: "<basename>.tmp"
  // basename being the filename
  // this is done so absolute paths can be used and it would not create
  // the directories mentioned in the file path
  // e.g. if local user file is "/home/rohan/.../hello.c", parse out
  // "hello.c"
  // done  by finding last instance of / character, and taking everything after
  // that
  char *slashPos = strrchr(filename, '/');
  const char *baseName = (slashPos) ? slashPos + 1 : filename;

  char tmp_path[256];
  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", baseName);

  FILE *fp = fopen(tmp_path, "wb");
  // if there is an error creating the file, then read and discard data from
  // socket
  if (!fp) {
    perror("[S1] fopen tmp");
    char discard[1024];
    long remain = fsize;
    while (remain > 0) {
      long chunk = (remain > 1024) ? 1024 : remain;
      if (recv_all(connfd, discard, chunk) < 0)
        break;
      remain -= chunk;
    }
    return;
  }

  // Actually receive the file data this time
  long remain = fsize;

  // Recieve it in chunks of 4KB just to decrease number of IO operations, like
  // in clients program
  char buf[CHUNK];
  while (remain > 0) {
    long toread = (remain > CHUNK) ? CHUNK : remain;
    if (recv_all(connfd, buf, toread) < 0) {
      printf("[S1] Error receiving file data.\n");
      break;
    }

    // size_t is unsigned int, so better to use it in case of overflow
    size_t written = fwrite(buf, 1, toread, fp);
    // break if write error
    if (written != (size_t)toread) {
      perror("[S1] fwrite");
      break;
    }
    remain -= toread;
  }
  fclose(fp);

  // Decide which file extension gets stored where
  // c gets stored in S1
  // pdf gets stored in S2
  // txt gets stored in S3
  // zip gets stored in S4
  const char *ext = get_file_extension(baseName);
  if (strcmp(ext, ".c") == 0) {
    // If user typed something like "/hello.c" for `dest`, to store it
    // as "S1/hello.c". If user typed "/some/folder/", store it as
    // "S1/some/folder/<basename>"

    // => We'll handle the "auto-append" logic: if `dest` ends with '/' or is
    // just '/', append baseName
    char localpath[1024];
    memset(localpath, 0, sizeof(localpath));

    // "S1/" prefix
    snprintf(localpath, sizeof(localpath), "S1/%s", dest);
    // localpath might be "S1//hello.c" or "S1//some/folder/"

    // fix double slash
    // if localpath ends with '/', append the baseName
    size_t dlen = strlen(localpath);
    if (dlen == 0) {
      // edge case
      strcpy(localpath, "S1/hello.c");
    } else {
      if (localpath[dlen - 1] == '/') {
        strcat(localpath, baseName);
      }
    }

    // e.g. if dest="/hello.c", then localpath="S1//hello.c"

    // separate directory vs. filename
    char folder[1024];
    strcpy(folder, localpath);
    char *lastSlash = strrchr(folder, '/');
    if (lastSlash) {
      *lastSlash = '\0'; // now folder is just the directory portion
      // helper function to create directories within servers
      create_dirs_if_needed(folder);
    }

    // rename from tmp to final local path
    if (rename(tmp_path, localpath) != 0) {
      perror("[S1] rename failed");
      printf("From: %s\nTo: %s\n", tmp_path, localpath);
      remove(tmp_path); // fallback
    } else {
      printf("[S1] Stored .c => %s\n", localpath);
    }

  } else if (strcmp(ext, ".pdf") == 0) {
    // Connect to S2, forward
    int s2fd = connect_to(S2_HOST, S2_PORT);
    if (s2fd < 0) {
      printf("[S1] Cannot connect S2\n");
      remove(tmp_path);
      return;
    }

    // telling s2 that file is being uploaded so it needs to store the data
    send_string(s2fd, "STORE");
    // S2 expects path + size + data
    // We'll pass the same 'dest' (like "/folder1"), S2 will interpret
    // it as "/folder1"
    send_string(s2fd, dest);

    // find actual size of the tmp file on disk
    struct stat stt;
    stat(tmp_path, &stt);
    long real_size = stt.st_size;
    char stmp[64];
    sprintf(stmp, "%ld", real_size);
    // send file size to S2
    send_string(s2fd, stmp);

    // send file to S2
    FILE *fpp = fopen(tmp_path, "rb");
    if (fpp) {
      while (!feof(fpp)) {
        size_t r = fread(buf, 1, CHUNK, fpp);
        if (r > 0) {
          if (send_all(s2fd, buf, r) < 0) {
            printf("[S1] forward to S2 failed\n");
            break;
          }
        }
      }
      fclose(fpp);
    }
    close(s2fd);
    // remove local tmp
    remove(tmp_path);
    printf("[S1] .pdf forwarded to S2\n");
  } else if (strcmp(ext, ".txt") == 0) {
    // Connect to S3, do same thing
    int s3fd = connect_to(S3_HOST, S3_PORT);
    if (s3fd < 0) {
      printf("[S1] Cannot connect S3\n");
      remove(tmp_path);
      return;
    }
    send_string(s3fd, "STORE");
    send_string(s3fd, dest);
    struct stat stt;
    stat(tmp_path, &stt);
    long real_size = stt.st_size;
    char stmp[64];
    sprintf(stmp, "%ld", real_size);
    send_string(s3fd, stmp);

    FILE *fpp = fopen(tmp_path, "rb");
    if (fpp) {
      while (!feof(fpp)) {
        size_t r = fread(buf, 1, CHUNK, fpp);
        if (r > 0) {
          if (send_all(s3fd, buf, r) < 0) {
            printf("[S1] forward to S3 failed\n");
            break;
          }
        }
      }
      fclose(fpp);
    }
    close(s3fd);
    remove(tmp_path);
    printf("[S1] .txt forwarded to S3\n");
  } else if (strcmp(ext, ".zip") == 0) {
    // Connect to S4, do same thing
    int s4fd = connect_to(S4_HOST, S4_PORT);
    if (s4fd < 0) {
      printf("[S1] Cannot connect S4\n");
      remove(tmp_path);
      return;
    }
    send_string(s4fd, "STORE");
    send_string(s4fd, dest);
    struct stat stt;
    stat(tmp_path, &stt);
    long real_size = stt.st_size;
    char stmp[64];
    sprintf(stmp, "%ld", real_size);
    send_string(s4fd, stmp);

    FILE *fpp = fopen(tmp_path, "rb");
    if (fpp) {
      while (!feof(fpp)) {
        size_t r = fread(buf, 1, CHUNK, fpp);
        if (r > 0) {
          if (send_all(s4fd, buf, r) < 0) {
            printf("[S1] forward to S4 failed\n");
            break;
          }
        }
      }
      fclose(fpp);
    }
    close(s4fd);
    remove(tmp_path);
    printf("[S1] .zip forwarded to S4\n");
  } else {
    // Unknown extension => or discard?
    printf("[S1] Unrecognized extension: %s. Removing tmp.\n", ext);
    remove(tmp_path);
  }
}

// 2) downlf
void downlf(int connfd, char *path) {
  const char *ext = get_file_extension(path);
  int remoteSock = -1;
  int localFlag = 0;

  if (strcmp(ext, ".c") == 0) {
    localFlag = 1;
  } else if (strcmp(ext, ".pdf") == 0) {
    remoteSock = connect_to(S2_HOST, S2_PORT);
  } else if (strcmp(ext, ".txt") == 0) {
    remoteSock = connect_to(S3_HOST, S3_PORT);
  } else if (strcmp(ext, ".zip") == 0) {
    remoteSock = connect_to(S4_HOST, S4_PORT);
  }

  if (localFlag) {
    // read from S1 folder
    // e.g. "S1/testdir/S1.c"
    char localpath[1024];
    snprintf(localpath, sizeof(localpath), "S1/%s", path);

    FILE *fp = fopen(localpath, "rb");
    if (!fp) {
      // send "0" for file size, which means file doesn't exist, or there was an
      // error opening the file
      send_string(connfd, "0");
      return;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char stmp[64];
    sprintf(stmp, "%ld", sz);
    // send file size
    send_string(connfd, stmp);

    char buf[CHUNK];
    // send file in chunks
    while (!feof(fp)) {
      size_t r = fread(buf, 1, CHUNK, fp);
      if (r > 0) {
        if (send_all(connfd, buf, r) < 0) {
          printf("[S1] downlf send error\n");
          break;
        }
      }
    }
    fclose(fp);

  } else {
    // forward to S2, S3, S4

    // exit if there's a socket error
    if (remoteSock < 0) {
      send_string(connfd, "0");
      return;
    }

    // telling other servers that file is being downloaded so it needs to get
    // the data
    send_string(remoteSock, "GET");
    send_string(remoteSock, path);

    // server sends size
    char *sizestr = recv_string(remoteSock);

    // exit if we dont get size
    if (!sizestr) {
      close(remoteSock);
      send_string(connfd, "0");
      return;
    }

    // forward size to client
    send_string(connfd, sizestr);
    long sz = atol(sizestr);
    free(sizestr);

    // read file data from server, then send to client
    char buf[CHUNK];
    while (sz > 0) {
      long toread = (sz > CHUNK) ? CHUNK : sz;
      if (recv_all(remoteSock, buf, toread) < 0)
        break;
      if (send_all(connfd, buf, toread) < 0)
        break;
      sz -= toread;
    }
    close(remoteSock);
  }
}

// 3) removef
void removef(int connfd, char *path) {
  // if .c => remove local. else connect to S2/S3/S4
  const char *ext = get_file_extension(path);
  if (strcmp(ext, ".c") == 0) {
    char localpath[1024];
    if (strncmp(path, "~S1/", 4) == 0) {
      snprintf(localpath, sizeof(localpath), "S1/%s", path + 4);
    } else {
      snprintf(localpath, sizeof(localpath), "S1/%s", path);
    }
    remove(localpath);
  } else if (strcmp(ext, ".pdf") == 0) {
    int s2fd = connect_to(S2_HOST, S2_PORT);
    if (s2fd >= 0) {
      send_string(s2fd, "REMOVE");
      send_string(s2fd, path);
      close(s2fd);
    }
  } else if (strcmp(ext, ".txt") == 0) {
    int s3fd = connect_to(S3_HOST, S3_PORT);
    if (s3fd >= 0) {
      send_string(s3fd, "REMOVE");
      send_string(s3fd, path);
      close(s3fd);
    }
  } else if (strcmp(ext, ".zip") == 0) {
    int s4fd = connect_to(S4_HOST, S4_PORT);
    if (s4fd >= 0) {
      send_string(s4fd, "REMOVE");
      send_string(s4fd, path);
      close(s4fd);
    }
  } else {
    printf("Unsupported file format!\n");
    return;
  }
}

// downltar
void downltar(int connfd, char *filetype) {
  char buf[CHUNK];
  if (strcmp(filetype, ".c") == 0) {
    // remove tar file if it already exists
    system("rm -f cfiles.tar");
    // create new tar file of S1 folder, since it only contains .c files
    system("tar -cf cfiles.tar S1");
    // send cfiles.tar
    FILE *fp = fopen("cfiles.tar", "rb");
    if (!fp) {
      send_string(connfd, "0");
      return;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char stmp[64];
    sprintf(stmp, "%ld", sz);
    send_string(connfd, stmp);

    while (!feof(fp)) {
      size_t r = fread(buf, 1, CHUNK, fp);
      if (r > 0) {
        if (send_all(connfd, buf, r) < 0) {
          printf("[S1] downltar .c send error\n");
          break;
        }
      }
    }
    fclose(fp);
  } else if (strcmp(filetype, ".pdf") == 0) {
    // send instruction to S2 for creating tar, get the file and send it back to
    // client
    int s2fd = connect_to(S2_HOST, S2_PORT);
    if (s2fd < 0) {
      send_string(connfd, "0");
      return;
    }
    send_string(s2fd, "TAR");
    char *sizestr = recv_string(s2fd);
    if (!sizestr) {
      close(s2fd);
      send_string(connfd, "0");
      return;
    }
    // forward size to client
    send_string(connfd, sizestr);
    long sz = atol(sizestr);
    free(sizestr);

    while (sz > 0) {
      long toread = (sz > CHUNK) ? CHUNK : sz;
      if (recv_all(s2fd, buf, toread) < 0)
        break;
      if (send_all(connfd, buf, toread) < 0)
        break;
      sz -= toread;
    }
    close(s2fd);
  } else if (strcmp(filetype, ".txt") == 0) {
    // send instruction to S3 for creating tar, get the file and send it back to
    // client
    int s3fd = connect_to(S3_HOST, S3_PORT);
    if (s3fd < 0) {
      send_string(connfd, "0");
      return;
    }
    send_string(s3fd, "TAR");
    char *sizestr = recv_string(s3fd);
    if (!sizestr) {
      close(s3fd);
      send_string(connfd, "0");
      return;
    }
    send_string(connfd, sizestr);
    long sz = atol(sizestr);
    free(sizestr);

    while (sz > 0) {
      long toread = (sz > CHUNK) ? CHUNK : sz;
      if (recv_all(s3fd, buf, toread) < 0)
        break;
      if (send_all(connfd, buf, toread) < 0)
        break;
      sz -= toread;
    }
    close(s3fd);
  } else {
    // send 0 indicating unsupported file type
    send_string(connfd, "0");
  }
}

// 5) dispfnames
void dispfnames(int connfd, char *path) {
  // gather .c from local S1 folder, .pdf from S2, .txt from S3, .zip from S4
  // then combine in alphabetical order.
  // We'll do a simplistic approach: gather each list separately, then combine.
  // local .c:
  char localp[1024];
  snprintf(localp, sizeof(localp), "S1/%s", path);

  DIR *d = opendir(localp);
  char result[8192];
  memset(result, 0, sizeof(result));
  // if directory exists
  if (d) {
    // gather .c
    // dirent structs contain information about files in a directory
    struct dirent *dd;
    // store them in a temporary array to sort
    // first dimension is for file count, second, is to store the filename
    char lines[256][256];
    int count = 0;
    // loop through directory, get .c files and add them to lines array
    while ((dd = readdir(d))) {
      if (strstr(dd->d_name, ".c")) {
        strncpy(lines[count], dd->d_name, 255);
        lines[count][255] = 0;
        count++;
      } else {
        continue;
      }
    }
    closedir(d);
    // bubble sort lines
    for (int i = 0; i < count; i++) {
      for (int j = i + 1; j < count; j++) {
        if (strcmp(lines[i], lines[j]) > 0) {
          char temp[256];
          strcpy(temp, lines[i]);
          strcpy(lines[i], lines[j]);
          strcpy(lines[j], temp);
        }
      }
    }

    // append to result
    for (int i = 0; i < count; i++) {
      strcat(result, lines[i]);
      strcat(result, "\n");
    }
  }

  // gather from S2 => .pdf
  int s2fd = connect_to(S2_HOST, S2_PORT);
  if (s2fd >= 0) {
    // LIST tells server that client has entered dispfnames and
    // needs the list of files in a directory
    send_string(s2fd, "LIST");
    send_string(s2fd, path);
    char *pdfs = recv_string(s2fd);
    if (pdfs) {
      // pdfs is new-line separated
      // sort pdf files same way as c files
      char lines[256][256];
      int count = 0;
      char *tok = strtok(pdfs, "\n");
      while (tok) {
        strncpy(lines[count], tok, 255);
        lines[count][255] = 0;
        count++;
        tok = strtok(NULL, "\n");
      }
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
      // append to result
      for (int i = 0; i < count; i++) {
        strcat(result, lines[i]);
        strcat(result, "\n");
      }
      free(pdfs);
    }
    close(s2fd);
  }

  // gather .txt files from S3 in the same way
  int s3fd = connect_to(S3_HOST, S3_PORT);
  if (s3fd >= 0) {
    send_string(s3fd, "LIST");
    send_string(s3fd, path);
    char *txts = recv_string(s3fd);
    if (txts) {
      char lines[256][256];
      int count = 0;
      char *tok = strtok(txts, "\n");
      while (tok) {
        strncpy(lines[count], tok, 255);
        lines[count][255] = 0;
        count++;
        tok = strtok(NULL, "\n");
      }
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
      for (int i = 0; i < count; i++) {
        strcat(result, lines[i]);
        strcat(result, "\n");
      }
      free(txts);
    }
    close(s3fd);
  }

  // gather .zip files from S3 in the same way
  int s4fd = connect_to(S4_HOST, S4_PORT);
  if (s4fd >= 0) {
    send_string(s4fd, "LIST");
    send_string(s4fd, path);
    char *zips = recv_string(s4fd);
    if (zips) {
      char lines[256][256];
      int count = 0;
      char *tok = strtok(zips, "\n");
      while (tok) {
        strncpy(lines[count], tok, 255);
        lines[count][255] = 0;
        count++;
        tok = strtok(NULL, "\n");
      }
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
      for (int i = 0; i < count; i++) {
        strcat(result, lines[i]);
        strcat(result, "\n");
      }
      free(zips);
    }
    close(s4fd);
  }

  // now send result to client
  send_string(connfd, result);
}

// whole process needed to connect to other servers, which is why it is
// extracted to a function
int connect_to(const char *host, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;

  struct sockaddr_in servAdd;
  memset(&servAdd, 0, sizeof(servAdd));
  servAdd.sin_family = AF_INET;
  servAdd.sin_port = htons(port);
  inet_pton(AF_INET, host, &servAdd.sin_addr);

  if (connect(fd, (struct sockaddr *)&servAdd, sizeof(servAdd)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}
