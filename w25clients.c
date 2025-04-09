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
      // using fseek and ftell to get file size instead of lseek because of the use of fopen instead of open
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

    } else if (strcmp(command, "removef") == 0) {

    } else if (strcmp(command, "downltar") == 0) {

    } else if (strcmp(command, "dispfnames") == 0) {
    }
  }

  return 0;
}
