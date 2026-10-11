#define _GNU_SOURCE
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
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_BLOCKED_DOMAINS 3
#define CACHE_SIZE 10
#define CACHE_TTL 60

struct CacheEntry
{
    char host[256];
    char path[2048];
    char *response;
    size_t response_size;
    time_t timestamp;
    int valid;
    int ttl;
};
struct CacheEntry cache[CACHE_SIZE];
pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static __thread char tl_client_ip[INET_ADDRSTRLEN] = "unknown";
static __thread char tl_path[2048] = "-";
static __thread const char *tl_cache = "-";

const char *blocked_domains[MAX_BLOCKED_DOMAINS] = {
    "example.com",
    "httpforever.com",
    "textfiles.com"};

int http(char *request, char *method, char *path, char *host)
{
    char *host_start;
    if (sscanf(request, "%19s %2047s", method, path) != 2)
    {
        return -1;
    }
    if (strlen(path) >= 2047)
    {
        return -1;
    }
    host_start = strstr(request, "Host:");
    if (host_start == NULL)
    {
        return -1;
    }
    if (sscanf(host_start, "Host: %199s", host) != 1)
    {
        return -1;
    }
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

void set_timeout(int sockfd)
{
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

int connect_server(char *host, int port)
{
    struct addrinfo hints, *res, *rp;
    char port_str[8];
    int socketfd = -1;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0)
    {
        printf("Host not found: %s\n", host);
        return -1;
    }
    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        socketfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socketfd < 0)
            continue;
        set_timeout(socketfd);
        if (connect(socketfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(socketfd);
        socketfd = -1;
    }
    freeaddrinfo(res);
    if (socketfd < 0)
    {
        printf("Connection to destination server failed\n");
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
    if (length > 0 && (size_t)length < sizeof(response))
    {
        send_all(client_sockfd, response, length);
    }
}

void bad_request(int client_sockfd)
{
    const char *body = "400 Bad Request";
    char response[256];
    int length = snprintf(response, sizeof(response), "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
    if (length > 0 && (size_t)length < sizeof(response))
    {
        send_all(client_sockfd, response, (size_t)length);
    }
}

void bad_gateway(int client_sockfd)
{
    const char *body = "502 Bad Gateway";
    char response[256];
    int length = snprintf(response, sizeof(response), "HTTP/1.1 502 Bad Gateway\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
    if (length > 0 && (size_t)length < sizeof(response))
    {
        send_all(client_sockfd, response, (size_t)length);
    }
}

void not_implemented(int client_sockfd)
{
    const char *body = "501 Not Implemented";
    char response[256];
    int length = snprintf(response, sizeof(response), "HTTP/1.1 501 Not Implemented\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
    if (length > 0 && (size_t)length < sizeof(response))
    {
        send_all(client_sockfd, response, (size_t)length);
    }
}


int build_request(const char *method, const char *req_path, const char *buf, int length, char *out, int out_size)
{
    const char *end = buf + length;
    const char *line = strstr(buf, "\r\n");
    int o;
    if (line == NULL)
        return -1;
    line += 2;
    o = snprintf(out, out_size, "%s %s HTTP/1.1\r\n", method, req_path);
    if (o < 0 || o >= out_size)
        return -1;
    while (line < end)
    {
        const char *eol = strstr(line, "\r\n");
        if (eol == NULL)
            return -1;
        int line_len = (int)(eol - line) + 2;
        if (line_len == 2)
        {
            const char *add = "Connection: close\r\n\r\n";
            int add_len = (int)strlen(add);
            int rest = (int)(end - (line + 2));
            if (o + add_len + rest > out_size)
                return -1;
            memcpy(out + o, add, add_len);
            o += add_len;
            memcpy(out + o, line + 2, rest);
            return o + rest;
        }
        int skip = strncasecmp(line, "Connection:", 11) == 0 || strncasecmp(line, "Proxy-Connection:", 17) == 0 || strncasecmp(line, "Keep-Alive:", 11) == 0;
        if (!skip)
        {
            if (o + line_len > out_size)
                return -1;
            memcpy(out + o, line, line_len);
            o += line_len;
        }
        line = eol + 2;
    }
    return -1;
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
    fprintf(logfile, "[%s] Client: %s | Host: %s | Method: %s | Path: %s | Status: %d | Bytes: %ld | Duration: %ld ms | Cache: %s\n", timestamp, tl_client_ip, host, method, tl_path, status, bytes, duration_ms, tl_cache);
    fclose(logfile);
}

int cache_ttl(const char *resp, size_t len)
{
    const char *end = memmem(resp, len, "\r\n\r\n", 4);
    if (end == NULL || (size_t)(end - resp) >= 8192)
        return 0;
    size_t hlen = (size_t)(end - resp);
    char headers[8192];
    memcpy(headers, resp, hlen);
    headers[hlen] = '\0';
    if (strcasestr(headers, "\r\nSet-Cookie:") != NULL || strcasestr(headers, "no-store") != NULL ||
        strcasestr(headers, "no-cache") != NULL || strcasestr(headers, "private") != NULL)
        return 0;
    const char *ma = strcasestr(headers, "max-age=");
    if (ma != NULL)
    {
        int age = atoi(ma + 8);
        if (age <= 0)
            return 0;
        return age > 3600 ? 3600 : age;
    }
    return CACHE_TTL;
}

int get_cached_response(const char *host, const char *path, char **response, size_t *response_size)
{
    int found = 0;
    time_t now = time(NULL);
    pthread_mutex_lock(&cache_mutex);
    for (int i = 0; i < CACHE_SIZE; i++)
    {
        if (cache[i].valid && strcmp(cache[i].host, host) == 0 && strcmp(cache[i].path, path) == 0)
        {
            if (now - cache[i].timestamp < cache[i].ttl)
            {
                *response = malloc(cache[i].response_size);
                if (*response != NULL)
                {
                    memcpy(*response, cache[i].response, cache[i].response_size);
                    *response_size = cache[i].response_size;
                    found = 1;
                }
            }
            else
            {
                free(cache[i].response);
                cache[i].response = NULL;
                cache[i].valid = 0;
            }
            break;
        }
    }
    pthread_mutex_unlock(&cache_mutex);
    return found;
}

void store_cached_response(const char *host, const char *path, const char *response, size_t response_size, int ttl)
{
    if (response_size == 0)
        return;
    char *copy = malloc(response_size);
    if (copy == NULL)
        return;
    memcpy(copy, response, response_size);
    pthread_mutex_lock(&cache_mutex);
    int index = -1;
    for (int i = 0; i < CACHE_SIZE; i++)
    {
        if (cache[i].valid && strcmp(cache[i].host, host) == 0 && strcmp(cache[i].path, path) == 0)
        {
            index = i;
            break;
        }
        if (!cache[i].valid && index == -1)
            index = i;
    }
    if (index == -1)
    {
        index = 0;
        for (int i = 1; i < CACHE_SIZE; i++)
        {
            if (cache[i].timestamp < cache[index].timestamp)
                index = i;
        }
    }
    free(cache[index].response);
    snprintf(cache[index].host, sizeof(cache[index].host), "%s", host);
    snprintf(cache[index].path, sizeof(cache[index].path), "%s", path);
    cache[index].response = copy;
    cache[index].response_size = response_size;
    cache[index].timestamp = time(NULL);
    cache[index].ttl = ttl;
    cache[index].valid = 1;
    pthread_mutex_unlock(&cache_mutex);
}

void *handle_client(void *arg)
{
    int client_sockfd = *(int *)arg;
    free(arg);
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    set_timeout(client_sockfd);
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    if (getpeername(client_sockfd, (struct sockaddr *)&peer, &peer_len) == 0)
        inet_ntop(AF_INET, &peer.sin_addr, tl_client_ip, sizeof(tl_client_ip));
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
        char path[2048];
        char host[256];
        if (http(buf, method, path, host) == 0)
        {
            snprintf(log_host, sizeof(log_host), "%.199s", host);
            snprintf(log_method, sizeof(log_method), "%s", method);
            snprintf(tl_path, sizeof(tl_path), "%s", path);
            int port = 80;
            char *colon = strchr(host, ':');
            if (colon != NULL)
            {
                *colon = '\0';
                port = atoi(colon + 1);
                if (port <= 0 || port > 65535)
                    port = 80;
            }
            if (strcmp(method, "CONNECT") == 0)
            {
                printf("CONNECT not supported (HTTPS)\n");
                log_status = 501;
                log_bytes = 19;
                gettimeofday(&end_time, NULL);
                long duration_ms = (end_time.tv_sec - start_time.tv_sec) * 1000L + (end_time.tv_usec - start_time.tv_usec) / 1000L;
                not_implemented(client_sockfd);
                log_traffic(log_host, log_method, log_status, log_bytes, duration_ms);
                close(client_sockfd);
                return NULL;
            }
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
            if (strcmp(method, "GET") == 0)
            {
                char *cached_response = NULL;
                size_t cached_size = 0;
                if (get_cached_response(host, path, &cached_response, &cached_size))
                {
                    tl_cache = "HIT";
                    printf("CACHE HIT: %s%s\n", host, path);
                    int sent = send_all(client_sockfd, cached_response, (int)cached_size);
                    if (sent > 0)
                    {
                        log_status = 200;
                        log_bytes = cached_size;
                    }
                    free(cached_response);
                    gettimeofday(&end_time, NULL);
                    long duration_ms = (end_time.tv_sec - start_time.tv_sec) * 1000L + (end_time.tv_usec - start_time.tv_usec) / 1000L;
                    log_traffic(log_host, log_method, log_status, log_bytes, duration_ms);
                    close(client_sockfd);
                    return NULL;
                }
                tl_cache = "MISS";
                printf("CACHE MISS: %s%s\n", host, path);
            }
            int destination_socket = connect_server(host, 80);
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
                char request[BUFFER_SIZE];
                const char *req_path = path;
                if (strncasecmp(path, "http://", 7) == 0)
                {
                    const char *slash = strchr(path + 7, '/');
                    req_path = slash != NULL ? slash : "/";
                }
                int request_length = build_request(method, req_path, buf, rbytes, request, sizeof(request));
                if (request_length < 0)
                {
                    printf("Failed to build HTTP request\n");
                    bad_request(client_sockfd);
                    close(destination_socket);
                    close(client_sockfd);
                    return NULL;
                }
                int sbytes = send_all(destination_socket, request, request_length);
                if (sbytes < 0)
                {
                    printf("Failed to send HTTP request\n");
                    log_status = 502;
                    bad_gateway(client_sockfd);
                    gettimeofday(&end_time, NULL);
                    long duration_ms = (end_time.tv_sec - start_time.tv_sec) * 1000L + (end_time.tv_usec - start_time.tv_usec) / 1000L;
                    log_traffic(log_host, log_method, log_status, 15, duration_ms);
                }
                else
                {
                    printf("HTTP request forwarded to destination server\n");
                    char response[BUFFER_SIZE];
                    int response_bytes;
                    long total_received = 0;
                    char *full_response = NULL;
                    size_t full_response_size = 0;
                    int cacheable = (strcmp(method, "GET") == 0);
                    while (1)
                    {
                        response_bytes = recv(destination_socket, response, sizeof(response), 0);
                        if (response_bytes == 0)
                        {
                            if (cacheable && full_response != NULL && full_response_size > 0 && log_status == 200)
                            {
                                int ttl = cache_ttl(full_response, full_response_size);
                                if (ttl > 0)
                                {
                                    store_cached_response(host, path, full_response, full_response_size, ttl);
                                    printf("Response saved in cache (ttl %d s)\n", ttl);
                                }
                                else
                                {
                                    tl_cache = "UNCACHEABLE";
                                    printf("Response not cached (Cache-Control / Set-Cookie)\n");
                                }
                            }
                            free(full_response);
                            full_response = NULL;
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
                            free(full_response);
                            full_response = NULL;
                            if (log_status == 0)
                            {
                                log_status = 502;
                                bad_gateway(client_sockfd);
                            }
                            printf("Failed to receive HTTP response\n");
                            gettimeofday(&end_time, NULL);
                            long duration_ms = (end_time.tv_sec - start_time.tv_sec) * 1000L + (end_time.tv_usec - start_time.tv_usec) / 1000L;
                            log_traffic(log_host, log_method, log_status, total_received, duration_ms);
                            break;
                        }
                        response[response_bytes] = '\0';
                        total_received += response_bytes;
                        if (cacheable)
                        {
                            if (full_response_size + (size_t)response_bytes <= 5 * 1024 * 1024)
                            {
                                char *temp = realloc(full_response, full_response_size + (size_t)response_bytes);
                                if (temp != NULL)
                                {
                                    full_response = temp;
                                    memcpy(full_response + full_response_size, response, (size_t)response_bytes);
                                    full_response_size += (size_t)response_bytes;
                                }
                                else
                                {
                                    free(full_response);
                                    full_response = NULL;
                                    full_response_size = 0;
                                    cacheable = 0;
                                }
                            }
                            else
                            {
                                free(full_response);
                                full_response = NULL;
                                full_response_size = 0;
                                cacheable = 0;
                            }
                        }
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
                    free(full_response);
                }
                close(destination_socket);
            }
            else
            {
                printf("Destination connection failed\n");
                log_status = 502;
                const char *body = "502 Bad Gateway";
                log_bytes = strlen(body);
                bad_gateway(client_sockfd);
                gettimeofday(&end_time, NULL);
                long duration_ms = (end_time.tv_sec - start_time.tv_sec) * 1000L + (end_time.tv_usec - start_time.tv_usec) / 1000L;
                log_traffic(log_host, log_method, log_status, log_bytes, duration_ms);
            }
        }
        else
        {
            printf("HTTP request parsing failed\n");
            log_status = 400;
            const char *body = "400 Bad Request";
            log_bytes = strlen(body);
            bad_request(client_sockfd);
            gettimeofday(&end_time, NULL);
            long duration_ms = (end_time.tv_sec - start_time.tv_sec) * 1000L + (end_time.tv_usec - start_time.tv_usec) / 1000L;
            log_traffic(log_host, log_method, log_status, log_bytes, duration_ms);
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
    int opt = 1;
    setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
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