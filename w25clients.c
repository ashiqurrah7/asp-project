#include "utils.h"

#define S1_HOST "127.0.0.1"
#define S1_PORT 5001

int main() {
  int socketfd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in servAdd;
  socklen_t len;

  if (socketfd < 0) {
    perror("Socket error");
    exit(1);
  }

  servAdd.sin_family = AF_INET;
  servAdd.sin_port = htons((uint16_t)S1_PORT);

  if (inet_pton(AF_INET, S1_HOST, &servAdd.sin_addr) < 0) {
    fprintf(stderr, " inet_pton() has failed\n");
    exit(2);
  }

  if (connect(socketfd, (struct sockaddr *)&servAdd, sizeof(servAdd)) < 0) {
    fprintf(stderr, "connect() failed, exiting\n");
    exit(3);
  }

  printf("Connected to S1 at %s:%d\n", S1_HOST, S1_PORT);

  while (1) {
    printf("\nw25clients> ");
    fflush(stdout);

    char line[1024];

    if (!fgets(line, sizeof(line), stdin))
      break;

    char *command = strtok(line, " \t\r\n");
    if (!command)
      continue;

    if (strcmp(command, "uploadf") == 0) {
      // parse filename, dest
      char *filename = strtok(NULL, " \t\r\n");
      char *dest = strtok(NULL, " \t\r\n");
      if (!filename || !dest) {
        printf("uploadf needs a filename and a destination path\n");
        continue;
      }
      // send "uploadf filename dest"
      // since we need to send the command along with the file to s1,
      // we do send_string: "uploadf filename dest"
      // and S1 will parse it, then expect file data
      char combined[1024];
      snprintf(combined, sizeof(combined), "uploadf %s %s", filename, dest);
      send_string(socketfd, combined);

      // read local file
      // using fopen because we don't have to specify a file size
      FILE *fp = fopen(filename, "rb");
      if (!fp) {
        // we still need to send 0 bytes to s1 since s1 needs to know something
        // went wrong and doesn't expect the file
        send_string(socketfd, "0");
        printf("Cannot open local file.\n");
        continue;
      }
      // using fseek and ftell to get file size instead of lseek because of the
      // use of fopen instead of open
      fseek(fp, 0, SEEK_END);
      long sz = ftell(fp);
      fseek(fp, 0, SEEK_SET);

      char szbuf[64];
      sprintf(szbuf, "%ld", sz);
      send_string(socketfd, szbuf);

      char buf[4096];
      while (!feof(fp)) {
        size_t file_size = fread(buf, 1, 4096, fp);
        if (file_size > 0) {
          if (send_all(socketfd, buf, file_size) < 0) {
            printf("Send error\n");
            break;
          }
        }
      }
      fclose(fp);
      printf("Uploaded %s to %s\n", filename, dest);
    } else if (strcmp(command, "downlf") == 0) {
      // parse file path
      char *remotePath = strtok(NULL, " \t\r\n");
      if (!remotePath) {
        printf("downlf needs a file path\n");
        continue;
      }
      // send command
      char combined[1024];
      snprintf(combined, sizeof(combined), "downlf %s", remotePath);
      send_string(socketfd, combined);

      // S1 will respond with size, then data
      char *sizestr = recv_string(socketfd);
      if (!sizestr) {
        printf("downlf: no size\n");
        continue;
      }
      long sz = atol(sizestr);
      free(sizestr);
      if (sz <= 0) {
        printf("File not found or zero length.\n");
        continue;
      }
      // extract the filename from remotePath
      char *slash = strrchr(remotePath, '/');
      const char *fname = slash ? slash + 1 : remotePath;

      FILE *fp = fopen(fname, "wb");
      if (!fp) {
        printf("Cannot create local file %s\n", fname);
        // even if file cannot be created, data from S1 must be read and
        // discarded so that the communication channel is clear for future
        // operations
        long left = sz;
        char tmp[4096];
        while (left > 0) {
          // reading in chunks reduces the number of i/o operations
          long chunk = (left > 4096) ? 4096 : left;
          if (recv_all(socketfd, tmp, chunk) < 0)
            break;
          left -= chunk;
        }
        continue;
      }
      // get actual file if file can be created
      long left = sz;
      char buf[4096];
      while (left > 0) {
        long chunk = (left > 4096) ? 4096 : left;
        if (recv_all(socketfd, buf, chunk) < 0) {
          break;
        }
        // writing to file in chunks
        fwrite(buf, 1, chunk, fp);
        left -= chunk;
      }
      fclose(fp);
      printf("Downloaded %s (%ld bytes)\n", fname, sz);
    } else if (strcmp(command, "removef") == 0) {
      char *remotePath = strtok(NULL, " \t\r\n");
      if (!remotePath) {
        printf("removef needs file path\n");
        continue;
      }
      char combined[1024];
      snprintf(combined, sizeof(combined), "removef %s", remotePath);
      send_string(socketfd, combined);
      printf("Removing file %s\n", remotePath);

    } else if (strcmp(command, "downltar") == 0) {
      // extract file type
      char *ft = strtok(NULL, " \t\r\n");
      if (!ft) {
        printf("downltar needs file type .c|.txt|.pdf (no zips) \n");
        continue;
      }
      char combined[1024];
      snprintf(combined, sizeof(combined), "downltar %s", ft);
      send_string(socketfd, combined);

      // will return 0 if no files of type ft are available
      char *sizestr = recv_string(socketfd);
      if (!sizestr) {
        printf("No size returned.\n");
        continue;
      }
      long sz = atol(sizestr);
      free(sizestr);
      if (sz <= 0) {
        printf("No available files of type %s to archive.\n", ft);
        continue;
      }

      // decide local name
      char localfn[50];
      if (strcmp(ft, ".c") == 0)
        strcpy(localfn, "cfiles.tar");
      else if (strcmp(ft, ".pdf") == 0)
        strcpy(localfn, "pdf.tar");
      else if (strcmp(ft, ".txt") == 0)
        strcpy(localfn, "text.tar");
      else {
        printf("File type %s not supported for archiving\n", ft);
        continue;
      }

      FILE *fp = fopen(localfn, "wb");
      if (!fp) {
        printf("Cannot create %s\n", localfn);
        // discard data
        while (sz > 0) {
          long chunk = (sz > 4096) ? 4096 : sz;
          char tmp[4096];
          if (recv_all(socketfd, tmp, chunk) < 0)
            break;
          sz -= chunk;
        }
        continue;
      }

      // getting archive from S1
      while (sz > 0) {
        long chunk = (sz > 4096) ? 4096 : sz;
        char tmp[4096];
        if (recv_all(socketfd, tmp, chunk) < 0)
          break;
        fwrite(tmp, 1, chunk, fp);
        sz -= chunk;
      }
      fclose(fp);
      printf("Received %s\n", localfn);

    } else if (strcmp(command, "dispfnames") == 0) {
      char *p = strtok(NULL, " \t\r\n");
      if (!p) {
        printf("dispfnames needs pathname\n");
        continue;
      }
      char combined[1024];
      snprintf(combined, sizeof(combined), "dispfnames %s", p);
      send_string(socketfd, combined);

      // read list
      char *listing = recv_string(socketfd);
      if (!listing) {
        printf("No listing.\n");
        continue;
      }
      printf("Files:\n%s", listing);
      free(listing);
    } else if (strcmp(command, "exit") == 0) {
      // exit out of program
      break;
    } else {
      printf("Unrecognized command %s\n", command);
      continue;
    }
  }
  close(socketfd);
  return 0;
}
