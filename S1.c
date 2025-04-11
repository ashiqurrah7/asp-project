/* S1.c – Main Server for Distributed File System */
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
#include <sys/wait.h>

#define PORT_S1 9000
#define PORT_S2 9001
#define PORT_S3 9002
#define PORT_S4 9003
#define BUFFER_SIZE 1024

// Function prototypes
void prcclient(int sockfd);
void error(const char *msg) {
    perror(msg);
    exit(1);
}
void create_directory_if_not_exists(const char *path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        mkdir(path, 0700);
    }
}
// Returns: 0 for .c, 1 for .pdf, 2 for .txt, 3 for .zip, -1 otherwise
int getFileExtensionType(const char *filename) {
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return -1;
    if(strcmp(dot, ".c") == 0) return 0;
    if(strcmp(dot, ".pdf") == 0) return 1;
    if(strcmp(dot, ".txt") == 0) return 2;
    if(strcmp(dot, ".zip") == 0) return 3;
    return -1;
}
// Open a connection to a specified port (target server)
int connect_to_server(int port) {
    int sockfd;
    struct sockaddr_in serv_addr;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return -1;
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connect failed");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// Receives file data from socket and saves it to a local file
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

// Sends file data over a socket
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

// Handles each client connection (forked child process)
void prcclient(int sockfd) {
    char command[BUFFER_SIZE];
    while(1) {
        memset(command, 0, BUFFER_SIZE);
        int n = recv(sockfd, command, BUFFER_SIZE-1, 0);
        if(n <= 0) break;
        // Remove newline characters
        command[strcspn(command, "\r\n")] = 0;
        printf("Received command: %s\n", command);
        
        if(strncmp(command, "uploadf", 7) == 0) {
            char filename[256], dest_path[256];
            // Expected command: uploadf filename destination_path
            sscanf(command, "uploadf %s %s", filename, dest_path);
            int ftype = getFileExtensionType(filename);
            if(ftype == -1) {
                char msg[] = "Unsupported file type.\n";
                send(sockfd, msg, strlen(msg), 0);
                continue;
            }
            // Receive file size (sent as a string)
            char sizeStr[64];
            recv(sockfd, sizeStr, sizeof(sizeStr)-1, 0);
            long filesize = atol(sizeStr);
            // Create destination directory in S1 (dest_path begins with "~S1")
            char fullPath[512];
            snprintf(fullPath, sizeof(fullPath), "S1%s", dest_path+3);
            create_directory_if_not_exists(fullPath);
            char filePath[600];
            snprintf(filePath, sizeof(filePath), "%s/%s", fullPath, filename);
            // Receive file data from the client
            receive_file(sockfd, filePath, filesize);
            printf("File received: %s\n", filePath);
            // For .c files, store the file; otherwise forward it to the appropriate server
            if(ftype == 0) {
                char msg[] = "File stored on S1.\n";
                send(sockfd, msg, strlen(msg), 0);
            } else {
                int targetPort = (ftype == 1) ? PORT_S2 : (ftype == 2) ? PORT_S3 : PORT_S4;
                int s = connect_to_server(targetPort);
                if(s < 0) {
                    char errMsg[] = "Error connecting to target server.\n";
                    send(sockfd, errMsg, strlen(errMsg), 0);
                } else {
                    // Change destination to reflect target server (e.g. ~S2, ~S3 or ~S4)
                    char target_dest[256];
                    if(ftype == 1)
                        strcpy(target_dest, "~S2");
                    else if(ftype == 2)
                        strcpy(target_dest, "~S3");
                    else if(ftype == 3)
                        strcpy(target_dest, "~S4");
                    strcat(target_dest, dest_path+3);  // Maintain folder structure
                    // Send the uploadf command to the target server
                    char target_cmd[BUFFER_SIZE];
                    snprintf(target_cmd, sizeof(target_cmd), "uploadf %s %s", filename, target_dest);
                    send(s, target_cmd, strlen(target_cmd), 0);
                    sleep(1);
                    // Forward the file size then file data
                    char fsizeStr[64];
                    snprintf(fsizeStr, sizeof(fsizeStr), "%ld", filesize);
                    send(s, fsizeStr, strlen(fsizeStr), 0);
                    sleep(1);
                    send_file(s, filePath);
                    close(s);
                    // Delete temporary file from S1
                    remove(filePath);
                    char msg[] = "File forwarded to appropriate server.\n";
                    send(sockfd, msg, strlen(msg), 0);
                }
            }
        } else if(strncmp(command, "downlf", 6) == 0) {
            // Command: downlf <filepath>
            char filepath[256];
            sscanf(command, "downlf %s", filepath);
            int ftype = getFileExtensionType(filepath);
            char localPath[512];
            if(ftype == 0) {
                // Process download locally for .c file
                snprintf(localPath, sizeof(localPath), "S1%s", filepath+3);
                FILE *fp = fopen(localPath, "rb");
                if(fp == NULL) {
                    char msg[] = "File not found on S1.\n";
                    send(sockfd, msg, strlen(msg), 0);
                    continue;
                }
                fseek(fp, 0, SEEK_END);
                long fsize = ftell(fp);
                rewind(fp);
                char fsizeStr[64];
                snprintf(fsizeStr, sizeof(fsizeStr), "%ld", fsize);
                send(sockfd, fsizeStr, strlen(fsizeStr), 0);
                sleep(1);
                send_file(sockfd, localPath);
                fclose(fp);
            } else {
                // Forward download to appropriate server for .pdf, .txt, .zip
                int targetPort = (ftype == 1) ? PORT_S2 : (ftype == 2) ? PORT_S3 : PORT_S4;
                int s = connect_to_server(targetPort);
                if(s < 0) {
                    char errMsg[] = "Error connecting to target server for download.\n";
                    send(sockfd, errMsg, strlen(errMsg), 0);
                } else {
                    char target_cmd[BUFFER_SIZE];
                    snprintf(target_cmd, sizeof(target_cmd), "downlf %s", filepath);
                    send(s, target_cmd, strlen(target_cmd), 0);
                    sleep(1);
                    char fsizeStr[64];
                    recv(s, fsizeStr, sizeof(fsizeStr)-1, 0);
                    long fsize = atol(fsizeStr);
                    send(sockfd, fsizeStr, strlen(fsizeStr), 0);
                    sleep(1);
                    char buffer[BUFFER_SIZE];
                    long remaining = fsize;
                    while(remaining > 0) {
                        int r = recv(s, buffer, (remaining>BUFFER_SIZE?BUFFER_SIZE:remaining), 0);
                        if(r <= 0) break;
                        send(sockfd, buffer, r, 0);
                        remaining -= r;
                    }
                    close(s);
                }
            }
        } else if(strncmp(command, "removef", 7) == 0) {
            // Command: removef <filepath>
            char filepath[256];
            sscanf(command, "removef %s", filepath);
            int ftype = getFileExtensionType(filepath);
            char localPath[512];
            if(ftype == 0) {
                snprintf(localPath, sizeof(localPath), "S1%s", filepath+3);
                if(remove(localPath) == 0) {
                    char msg[] = "File removed from S1.\n";
                    send(sockfd, msg, strlen(msg), 0);
                } else {
                    char msg[] = "Error removing file from S1.\n";
                    send(sockfd, msg, strlen(msg), 0);
                }
            } else {
                // Forward removal command to appropriate server
                int targetPort = (ftype == 1) ? PORT_S2 : (ftype == 2) ? PORT_S3 : PORT_S4;
                int s = connect_to_server(targetPort);
                if(s < 0) {
                    char errMsg[] = "Error connecting to target server for removal.\n";
                    send(sockfd, errMsg, strlen(errMsg), 0);
                } else {
                    send(s, command, strlen(command), 0);
                    char response[BUFFER_SIZE];
                    recv(s, response, sizeof(response)-1, 0);
                    send(sockfd, response, strlen(response), 0);
                    close(s);
                }
            }
        } else if(strncmp(command, "downltar", 8) == 0) {
            // Command: downltar <filetype>
            char filetype[16];
            sscanf(command, "downltar %s", filetype);
            if(strcmp(filetype, ".c") == 0) {
                system("tar -cf cfiles.tar S1");
                FILE *fp = fopen("cfiles.tar", "rb");
                if(fp == NULL) {
                    char msg[] = "Error creating tar file.\n";
                    send(sockfd, msg, strlen(msg), 0);
                    continue;
                }
                fseek(fp, 0, SEEK_END);
                long fsize = ftell(fp);
                rewind(fp);
                char fsizeStr[64];
                snprintf(fsizeStr, sizeof(fsizeStr), "%ld", fsize);
                send(sockfd, fsizeStr, strlen(fsizeStr), 0);
                sleep(1);
                send_file(sockfd, "cfiles.tar");
                fclose(fp);
                remove("cfiles.tar");
            } else if(strcmp(filetype, ".pdf") == 0 || strcmp(filetype, ".txt") == 0) {
                int targetPort = (strcmp(filetype, ".pdf")==0)? PORT_S2 : PORT_S3;
                int s = connect_to_server(targetPort);
                if(s < 0) {
                    char errMsg[] = "Error connecting to target server for tar download.\n";
                    send(sockfd, errMsg, strlen(errMsg), 0);
                } else {
                    char tar_cmd[BUFFER_SIZE];
                    snprintf(tar_cmd, sizeof(tar_cmd), "downltar %s", filetype);
                    send(s, tar_cmd, strlen(tar_cmd), 0);
                    sleep(1);
                    char fsizeStr[64];
                    recv(s, fsizeStr, sizeof(fsizeStr)-1, 0);
                    long fsize = atol(fsizeStr);
                    send(sockfd, fsizeStr, strlen(fsizeStr), 0);
                    sleep(1);
                    char buffer[BUFFER_SIZE];
                    long remaining = fsize;
                    while(remaining > 0) {
                        int r = recv(s, buffer, (remaining>BUFFER_SIZE?BUFFER_SIZE:remaining), 0);
                        if(r <= 0) break;
                        send(sockfd, buffer, r, 0);
                        remaining -= r;
                    }
                    close(s);
                }
            } else {
                char msg[] = "Unsupported filetype for tar download.\n";
                send(sockfd, msg, strlen(msg), 0);
            }
        } else if(strncmp(command, "dispfnames", 10) == 0) {
            // Command: dispfnames <pathname>
            char pathname[256];
            sscanf(command, "dispfnames %s", pathname);
            char listBuffer[4096] = "";
            // List .c files from S1
            char localPath[512];
            snprintf(localPath, sizeof(localPath), "S1%s", pathname+3);
            DIR *d = opendir(localPath);
            if(d) {
                struct dirent *dir;
                while((dir = readdir(d)) != NULL) {
                    if(dir->d_type == DT_REG) {
                        const char *dot = strrchr(dir->d_name, '.');
                        if(dot && strcmp(dot, ".c") == 0) {
                            strcat(listBuffer, dir->d_name);
                            strcat(listBuffer, "\n");
                        }
                    }
                }
                closedir(d);
            }
            // Obtain filenames from S2, S3 and S4
            int ports[3] = {PORT_S2, PORT_S3, PORT_S4};
            for(int i = 0; i < 3; i++){
                int s = connect_to_server(ports[i]);
                if(s >= 0) {
                    char cmd[256];
                    snprintf(cmd, sizeof(cmd), "dispfnames %s", pathname);
                    send(s, cmd, strlen(cmd), 0);
                    sleep(1);
                    char response[1024];
                    int r = recv(s, response, sizeof(response)-1, 0);
                    if(r > 0) {
                        response[r] = '\0';
                        strcat(listBuffer, response);
                    }
                    close(s);
                }
            }
            send(sockfd, listBuffer, strlen(listBuffer), 0);
        } else {
            char msg[] = "Invalid command.\n";
            send(sockfd, msg, strlen(msg), 0);
        }
    }
    close(sockfd);
}

int main() {
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0)
        error("ERROR opening socket");
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT_S1);
    
    if(bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    
    printf("S1 server running on port %d\n", PORT_S1);
    while(1) {
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if(newsockfd < 0)
            error("ERROR on accept");
        if(fork() == 0) {
            close(sockfd);
            prcclient(newsockfd);
            exit(0);
        }
        close(newsockfd);
        while(waitpid(-1, NULL, WNOHANG) > 0);
    }
    close(sockfd);
    return 0;
}