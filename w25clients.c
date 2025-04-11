/* w25clients.c – Client Program for Distributed File System */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>

#define PORT_S1 9000
#define BUFFER_SIZE 1024

// Connect to S1 server on port 5000
int connect_to_S1() {
    int sockfd;
    struct sockaddr_in serv_addr;
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT_S1);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if(connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection error");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// Send file data over the socket
int send_file(int sock, const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if(fp == NULL) {
        perror("File open error");
        return -1;
    }
    char buffer[BUFFER_SIZE];
    int n;
    while((n = fread(buffer, sizeof(char), BUFFER_SIZE, fp)) > 0) {
        send(sock, buffer, n, 0);
    }
    fclose(fp);
    return 0;
}

// Receive file data from socket and save it locally
int receive_file(int sock, const char *filepath, long filesize) {
    FILE *fp = fopen(filepath, "wb");
    if(fp == NULL) {
        perror("File open error");
        return -1;
    }
    char buffer[BUFFER_SIZE];
    long remaining = filesize;
    while(remaining > 0) {
        int n = recv(sock, buffer, (remaining > BUFFER_SIZE ? BUFFER_SIZE : remaining), 0);
        if(n <= 0) break;
        fwrite(buffer, sizeof(char), n, fp);
        remaining -= n;
    }
    fclose(fp);
    return 0;
}
int main() {
    char command[BUFFER_SIZE];
    while(1) {
        printf("w25clients$ ");
        fflush(stdout);
        if(!fgets(command, sizeof(command), stdin))
            break;
        command[strcspn(command, "\r\n")] = 0;
        if(strlen(command) == 0) continue;
        
        if(strncmp(command, "uploadf", 7) == 0) {
            char filename[256], dest_path[256];
            sscanf(command, "uploadf %s %s", filename, dest_path);
            FILE *fp = fopen(filename, "rb");
            if(fp == NULL) {
                printf("File %s not found in current directory.\n", filename);
                continue;
            }
            fseek(fp, 0, SEEK_END);
            long fsize = ftell(fp);
            fclose(fp);
            int sock = connect_to_S1();
            if(sock < 0) continue;
            // Send command, then file size, then file data
            send(sock, command, strlen(command), 0);
            sleep(1);
            char fsizeStr[64];
            snprintf(fsizeStr, sizeof(fsizeStr), "%ld", fsize);
            send(sock, fsizeStr, strlen(fsizeStr), 0);
            sleep(1);
            send_file(sock, filename);
            char response[BUFFER_SIZE];
            int n = recv(sock, response, sizeof(response)-1, 0);
            if(n > 0) {
                response[n] = '\0';
                printf("%s", response);
            }
            close(sock);
        } else if(strncmp(command, "downlf", 6) == 0) {
            char filepath[256];
            sscanf(command, "downlf %s", filepath);
            int sock = connect_to_S1();
            if(sock < 0) continue;
            send(sock, command, strlen(command), 0);
            char fsizeStr[64];
            int n = recv(sock, fsizeStr, sizeof(fsizeStr)-1, 0);
            if(n <= 0) {
                printf("Error receiving file size.\n");
                close(sock);
                continue;
            }
            fsizeStr[n] = '\0';
            long fsize = atol(fsizeStr);
            // Determine local filename from filepath
            char *token = strrchr(filepath, '/');
            char local_filename[256];
            if(token)
                strcpy(local_filename, token+1);
            else
                strcpy(local_filename, filepath);
            receive_file(sock, local_filename, fsize);
            printf("File %s downloaded.\n", local_filename);
            close(sock);
        } else if(strncmp(command, "removef", 7) == 0) {
            int sock = connect_to_S1();
            if(sock < 0) continue;
            send(sock, command, strlen(command), 0);
            char response[BUFFER_SIZE];
            int n = recv(sock, response, sizeof(response)-1, 0);
            if(n > 0) {
                response[n] = '\0';
                printf("%s", response);
            }
            close(sock);
        } else if(strncmp(command, "downltar", 8) == 0) {
            int sock = connect_to_S1();
            if(sock < 0) continue;
            send(sock, command, strlen(command), 0);
            char fsizeStr[64];
            int n = recv(sock, fsizeStr, sizeof(fsizeStr)-1, 0);
            if(n <= 0) {
                printf("Error receiving tar file size.\n");
                close(sock);
                continue;
            }
            fsizeStr[n] = '\0';
            long fsize = atol(fsizeStr);
            char tar_filename[256] = "downloaded.tar";
            receive_file(sock, tar_filename, fsize);
            printf("Tar file %s downloaded.\n", tar_filename);
            close(sock);
        } else if(strncmp(command, "dispfnames", 10) == 0) {
            int sock = connect_to_S1();
            if(sock < 0) continue;
            send(sock, command, strlen(command), 0);
            char listBuffer[4096];
            int n = recv(sock, listBuffer, sizeof(listBuffer)-1, 0);
            if(n > 0) {
                listBuffer[n] = '\0';
                printf("Files:\n%s", listBuffer);
            }
            close(sock);
        } else {
            printf("Invalid command.\n");
        }
    }
    return 0;
}