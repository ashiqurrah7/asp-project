/* S4.c – Server for ZIP Files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

#define PORT_S4 5003
#define BUFFER_SIZE 1024

void create_directory_if_not_exists(const char *path) {
    struct stat st = {0};
    if(stat(path, &st)==-1) {
        mkdir(path,0700);
    }
}
int receive_file(int sock, const char *filepath, long filesize) {
    FILE *fp = fopen(filepath, "wb");
    if(fp==NULL){ perror("File open error"); return -1; }
    char buffer[BUFFER_SIZE];
    long remaining = filesize;
    while(remaining > 0){
        int n = recv(sock, buffer, (remaining>BUFFER_SIZE?BUFFER_SIZE:remaining), 0);
        if(n<=0) break;
        fwrite(buffer, sizeof(char), n, fp);
        remaining -= n;
    }
    fclose(fp);
    return 0;
}
int send_file(int sock, const char *filepath) {
    FILE *fp = fopen(filepath, "rb");
    if(fp==NULL){ perror("File open error"); return -1; }
    char buffer[BUFFER_SIZE];
    int n;
    while((n=fread(buffer,sizeof(char),BUFFER_SIZE,fp))>0){
        send(sock, buffer, n, 0);
    }
    fclose(fp);
    return 0;
}
int main(){
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd<0){ perror("Socket creation error"); exit(1); }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family=AF_INET;
    serv_addr.sin_addr.s_addr=INADDR_ANY;
    serv_addr.sin_port=htons(PORT_S4);
    
    if(bind(sockfd,(struct sockaddr *)&serv_addr, sizeof(serv_addr))<0){
        perror("Bind error");
        exit(1);
    }
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    printf("S4 server (ZIP) running on port %d\n", PORT_S4);
    while(1){
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if(newsockfd<0){ perror("Accept error"); continue; }
        char command[BUFFER_SIZE];
        memset(command, 0, BUFFER_SIZE);
        recv(newsockfd, command, BUFFER_SIZE-1, 0);
        if(strncmp(command, "uploadf", 7)==0){
            char filename[256], dest_path[256];
            sscanf(command, "uploadf %s %s", filename, dest_path);
            char fullPath[512];
            snprintf(fullPath, sizeof(fullPath), "S4%s", dest_path+3);
            create_directory_if_not_exists(fullPath);
            char filePath[600];
            snprintf(filePath, sizeof(filePath), "%s/%s", fullPath, filename);
            char sizeStr[64];
            recv(newsockfd, sizeStr, sizeof(sizeStr)-1, 0);
            long filesize = atol(sizeStr);
            receive_file(newsockfd, filePath, filesize);
            send(newsockfd, "File stored on S4.\n", 20, 0);
        } else if(strncmp(command, "downlf", 6)==0){
            char filepath[256];
            sscanf(command, "downlf %s", filepath);
            char localPath[512];
            snprintf(localPath, sizeof(localPath), "S4%s", filepath+3);
            FILE *fp = fopen(localPath, "rb");
            if(fp==NULL){
                send(newsockfd, "File not found on S4.\n", 23, 0);
                close(newsockfd);
                continue;
            }
            fseek(fp,0,SEEK_END);
            long fsize = ftell(fp);
            rewind(fp);
            char fsizeStr[64];
            snprintf(fsizeStr, sizeof(fsizeStr), "%ld", fsize);
            send(newsockfd, fsizeStr, strlen(fsizeStr), 0);
            sleep(1);
            send_file(newsockfd, localPath);
            fclose(fp);
        } else if(strncmp(command, "removef", 7)==0){
            char filepath[256];
            sscanf(command, "removef %s", filepath);
            char localPath[512];
            snprintf(localPath, sizeof(localPath), "S4%s", filepath+3);
            if(remove(localPath)==0)
                send(newsockfd, "File removed from S4.\n", 23, 0);
            else
                send(newsockfd, "Error removing file from S4.\n", 29, 0);
        } else if(strncmp(command, "downltar",8)==0){
            // Tar download is not supported for .zip files.
            send(newsockfd, "Tar download not supported for .zip files.\n", 45, 0);
        } else if(strncmp(command, "dispfnames", 10)==0){
            char pathname[256];
            sscanf(command, "dispfnames %s", pathname);
            char listBuffer[1024]="";
            char localPath[512];
            snprintf(localPath, sizeof(localPath), "S4%s", pathname+3);
            DIR *d = opendir(localPath);
            if(d){
                struct dirent *dir;
                while((dir = readdir(d)) != NULL){
                    if(dir->d_type==DT_REG){
                        const char *dot = strrchr(dir->d_name, '.');
                        if(dot && strcmp(dot, ".zip")==0){
                            strcat(listBuffer, dir->d_name);
                            strcat(listBuffer, "\n");
                        }
                    }
                }
                closedir(d);
            }
            send(newsockfd, listBuffer, strlen(listBuffer), 0);
        }
        close(newsockfd);
    }
    close(sockfd);
    return 0;
}