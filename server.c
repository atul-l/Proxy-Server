#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 4096

int http(char *request, char *method, char *path, char *host)
{
    char *host_start;
    if (sscanf(request, "%s %s", method, path) != 2)
    {
        return -1;
    }
    host_start = strstr(request, "Host:");
    if (host_start == NULL)
    {
        return -1;
    }
    sscanf(host_start, "Host: %s", host);
    return 0;
}

int main()
{
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd < 0)
    {
        printf("Socket creation failed...\n");
        return 1;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(socketfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(socketfd);
        printf("Socket binding failed\n");
        return 1;
    }
    if (listen(socketfd, 5) < 0)
    {
        close(socketfd);
        printf("listen failed\n");
        return 1;
    }
    printf("Proxy server is listening on port %d..\n", PORT);
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sockfd = accept(socketfd, (struct sockaddr *)&client_addr, &client_len);

    if (client_sockfd < 0)
    {
        close(socketfd);
        printf("accept() failed\n");
        return 1;
    }
    printf("Client connected\n");
    char buf[BUFFER_SIZE];
    int rbytes = recv(client_sockfd, buf, BUFFER_SIZE - 1, 0);
    if (rbytes > 0)
    {
        buf[rbytes] = '\0';
        printf("rbytes: %d\n", rbytes);
        printf("Message from client:\n%s\n", buf);
        char method[20];
        char path[500];
        char host[200];
        if (http(buf, method, path, host) == 0)
        {
            printf("\nHTTP Request Details:\n");
            printf("Method: %s\n", method);
            printf("Path: %s\n", path);
            printf("Host: %s\n", host);
        }
        else
        {
            printf("HTTP request parsing failed...\n");
        }
    }
    else
    {
        printf("recv() failed\n");
    }
    close(client_sockfd);
    close(socketfd);
    return 0;
}