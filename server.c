#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>

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

int connect_server(char *host)
{
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd < 0)
    {
        printf("Destination socket creation failed\n");
        return -1;
    }
    struct hostent *server = gethostbyname(host);
    if (server == NULL)
    {
        printf("Host not found: %s\n", host);
        close(socketfd);
        return -1;
    }
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(80);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    if (connect(socketfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("Connection to destination server failed\n");
        close(socketfd);
        return -1;
    }
    printf("Connected to destination server\n");
    return socketfd;
}
int send_all(int sockfd, const char *data, int length){
    int total_sent = 0;
    while (total_sent < length){
        int sbytes = send(sockfd, data + total_sent, length - total_sent, 0);
        if (sbytes <= 0){
            return -1;
        }
        total_sent += sbytes;
    }
    return total_sent;
}

void *handle_client(void *arg){
    int client_sockfd = *(int *)arg;
    free(arg);
    printf("Client connected: thread %lu\n",(unsigned long)pthread_self());
    char buf[BUFFER_SIZE];
    int rbytes = recv(client_sockfd, buf, BUFFER_SIZE - 1, 0);
    if (rbytes > 0){
        buf[rbytes] = '\0';
        printf("rbytes: %d\n", rbytes);
        printf("Message from client:\n%s\n", buf);
        char method[20];
        char path[500];
        char host[200];
        if (http(buf, method, path, host) == 0){
            printf("\nHTTP Request Details:\n");
            printf("Method: %s\n", method);
            printf("Path: %s\n", path);
            printf("Host: %s\n", host);
            int destination_socket = connect_server(host);
            if (destination_socket >= 0){
                printf("Destination connection successful\n");
                char *header_end = strstr(buf, "\r\n\r\n");
                if (header_end == NULL){
                    printf("Invalid HTTP request headers\n");
                    close(destination_socket);
                    close(client_sockfd);
                    return NULL;
                }
                int request_length = rbytes;
                int sbytes = send_all(destination_socket, buf, request_length);
                if (sbytes < 0){
                    printf("Failed to send HTTP request\n");
                }
                else{
                    printf("HTTP request forwarded to destination server\n");
                    char response[BUFFER_SIZE];
                    int response_bytes;
                    long total_received = 0;
                    while (1){
                        response_bytes = recv(destination_socket, response, sizeof(response), 0);
                        if (response_bytes == 0){
                            printf("Destination server closed the connection\n");
                            printf("Total response bytes: %ld\n",total_received);
                            printf("HTTP response forwarded to client\n");
                            break;
                        }
                        if (response_bytes < 0){
                            printf("Failed to receive HTTP response\n");
                            break;
                        }
                        total_received += response_bytes;
                        printf("HTTP response received from destination server\n");
                        printf("Response size: %d bytes\n",response_bytes);
                        sbytes = send_all(client_sockfd, response, response_bytes);
                        if (sbytes < 0){
                            printf("Failed to forward response to client\n");
                            break;
                        }
                    }
                }
                close(destination_socket);
            }
            else{
                printf("Destination connection failed\n");
            }
        }
        else{
            printf("HTTP request parsing failed\n");
        }
    }
    else{
        printf("Client recv failed or connection closed\n");
    }
    close(client_sockfd);
    printf("Client disconnected: thread %lu\n",
           (unsigned long)pthread_self());

    return NULL;
}

int main()
{
    signal(SIGPIPE, SIG_IGN);
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
    while (1){
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sockfd = accept(socketfd, (struct sockaddr *)&client_addr, &client_len);

        if (client_sockfd < 0)
        {
            close(socketfd);
            perror("accept");
            continue;
        }
        int *client_arg = malloc(sizeof(int));

        if (client_arg == NULL)
        {
            printf("Memory allocation failed\n");
            close(client_sockfd);
            continue;
        }

        *client_arg = client_sockfd;
        pthread_t thread;
        int result = pthread_create(&thread,NULL,handle_client,client_arg);
        if (result != 0){
            printf("Thread creation failed: %s\n",strerror(result));
            free(client_arg);
            close(client_sockfd);
            continue;
        }
        pthread_detach(thread);
    }
    close(socketfd);
    return 0;
}