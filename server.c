#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_BLOCKED_DOMAINS 2
const char *blocked_domains[MAX_BLOCKED_DOMAINS] = {
    "example.com",
    "httpforever.com"};

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
int is_blocked(char *host)
{
    for (int i = 0; i < MAX_BLOCKED_DOMAINS; i++)
    {
        size_t host_len = strlen(host);
        size_t blocked_len = strlen(blocked_domains[i]);
        if (strcasecmp(host, blocked_domains[i]) == 0)
        {
            return 1;
        }
        if (host_len > blocked_len && host[host_len - blocked_len - 1] == '.' && strcasecmp(host + host_len - blocked_len, blocked_domains[i]) == 0)
        {
            return 1;
        }
    }
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
int send_all(int sockfd, const char *data, int length)
{
    int total_sent = 0;
    while (total_sent < length)
    {
        int sbytes = send(sockfd, data + total_sent, length - total_sent, 0);
        if (sbytes <= 0)
        {
            return -1;
        }
        total_sent += sbytes;
    }
    return total_sent;
}
void send_forbidden(int client_sockfd)
{
    const char *body = "Access Denied by Proxy";
    char response[256];
    int length = snprintf(response, sizeof(response), "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
    if (length > 0 && length < sizeof(response))
    {
        send_all(client_sockfd, response, length);
    }
}

void log_traffic(const char *host, const char *method, int status, long bytes, long duration_ms)
{
    FILE *logfile = fopen("proxy.log", "a");
    if (logfile == NULL)
    {
        perror("Unable to open proxy.log");
        return;
    }
    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    char timestamp[32];
    if (local_time != NULL)
    {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", local_time);
    }
    else
    {
        snprintf(timestamp, sizeof(timestamp), "unknown-time");
    }
    fprintf(logfile, "[%s] Host: %s | Method: %s | Status: %d | Bytes: %ld | Duration: %ld ms\n", timestamp, host, method, status, bytes, duration_ms);
    fclose(logfile);
}

void *handle_client(void *arg)
{
    int client_sockfd = *(int *)arg;
    free(arg);
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    char log_host[200] = "unknown";
    char log_method[20] = "unknown";
    int log_status = 0;
    long log_bytes = 0;
    printf("Client connected: thread %lu\n", (unsigned long)pthread_self());
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
            snprintf(log_host, sizeof(log_host), "%s", host);
            snprintf(log_method, sizeof(log_method), "%s", method);
            printf("\nHTTP Request Details:\n");
            printf("Method: %s\n", method);
            printf("Path: %s\n", path);
            printf("Host: %s\n", host);
            if (is_blocked(host))
            {
                printf("Access denied: %s is blocked\n", host);
                log_status = 403;
                const char *body = "Access Denied by Proxy";
                log_bytes = strlen(body);
                gettimeofday(&end_time, NULL);
                long duration_ms = (end_time.tv_sec - start_time.tv_sec) * 1000L + (end_time.tv_usec - start_time.tv_usec) / 1000L;
                send_forbidden(client_sockfd);
                log_traffic(log_host, log_method, log_status, log_bytes, duration_ms);
                close(client_sockfd);
                return NULL;
            }
            printf("Access allowed: %s\n", host);
            int destination_socket = connect_server(host);
            if (destination_socket >= 0)
            {
                printf("Destination connection successful\n");
                char *header_end = strstr(buf, "\r\n\r\n");
                if (header_end == NULL)
                {
                    printf("Invalid HTTP request headers\n");
                    close(destination_socket);
                    close(client_sockfd);
                    return NULL;
                }
                int request_length = rbytes;
                int sbytes = send_all(destination_socket, buf, request_length);
                if (sbytes < 0)
                {
                    printf("Failed to send HTTP request\n");
                }
                else
                {
                    printf("HTTP request forwarded to destination server\n");
                    char response[BUFFER_SIZE];
                    int response_bytes;
                    long total_received = 0;
                    while (1)
                    {
                        response_bytes = recv(destination_socket, response, sizeof(response), 0);
                        if (response_bytes == 0)
                        {
                            printf("Destination server closed the connection\n");
                            printf("Total response bytes: %ld\n", total_received);
                            printf("HTTP response forwarded to client\n");
                            log_bytes = total_received;
                            gettimeofday(&end_time, NULL);
                            long duration_ms = (end_time.tv_sec - start_time.tv_sec) * 1000L + (end_time.tv_usec - start_time.tv_usec) / 1000L;
                            log_traffic(log_host, log_method, log_status, log_bytes, duration_ms);
                            break;
                        }
                        if (response_bytes < 0)
                        {
                            printf("Failed to receive HTTP response\n");
                            break;
                        }
                        total_received += response_bytes;
                        if (log_status == 0)
                        {
                            char *status_start = strstr(response, "HTTP/");
                            if (status_start != NULL)
                            {
                                char *status_space = strchr(status_start, ' ');
                                if (status_space != NULL && status_space[1] >= '0' && status_space[1] <= '9')
                                {
                                    log_status = atoi(status_space + 1);
                                }
                            }
                        }
                        printf("HTTP response received from destination server\n");
                        printf("Response size: %d bytes\n", response_bytes);
                        sbytes = send_all(client_sockfd, response, response_bytes);
                        if (sbytes < 0)
                        {
                            printf("Failed to forward response to client\n");
                            break;
                        }
                    }
                }
                close(destination_socket);
            }
            else
            {
                printf("Destination connection failed\n");
            }
        }
        else
        {
            printf("HTTP request parsing failed\n");
        }
    }
    else
    {
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
    while (1)
    {
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
        int result = pthread_create(&thread, NULL, handle_client, client_arg);
        if (result != 0)
        {
            printf("Thread creation failed: %s\n", strerror(result));
            free(client_arg);
            close(client_sockfd);
            continue;
        }
        pthread_detach(thread);
    }
    close(socketfd);
    return 0;
}