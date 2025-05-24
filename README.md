# ASP-Project: Distributed file server

## Instructions on how to compile
 1) gcc S1.c utils.c -o s1
 2) gcc S2.c utils.c -o s2
 3) gcc S3.c utils.c -o s3
 4) gcc S4.c utils.c -o s4
 5) gcc w25clients.c utils.c -o w25clients

## Run in different terminal instances
 1) ./s1
 2) ./s2
 3) ./s3
 4) ./s4
 5) ./w25clients

## Instructions
 - Servers S1, S2, S3, S4 will only show logs and errors
 - User can interact with servers with w25clients
 - uploadf <source path> <destination path> (eg. uploadf hello.c /hello.c)
    - only supported file types are .c, .pdf, .txt and .zip
 - downlf <file path> (eg. downlf /hello.c or /folder/hello.c)
 - removef <file path> (eg. removef /hello.c or /folder/hello.c)
 - downltar <file type> (eg. downltar .c | .pdf | .txt | .zip)
 - dispfnames <path> (eg. dispfnames / or /folder/)
