# HTTP Proxy Server

## 1. Project Overview

This project implements an HTTP Proxy Server using the C programming language on Linux. A proxy server acts as an intermediary between a client and a destination web server. It receives HTTP requests from clients, forwards them to the destination server, receives the responses, and sends them back to the clients.

The project demonstrates TCP socket programming, HTTP request handling, concurrent client processing, caching, access control, and traffic logging.

## 2. Project Team Members

This project was developed collaboratively by the following team members:

| S. No. | Name             | Roll Number |
| ------ | ---------------- | ----------- |
| 1      | Atul Singh       | 24156010    |
| 2      | Ritwika Dasgupta | 24155995    |
| 3      | Agradeep Das     | 24158036    |

**Project:** HTTP Proxy Server in C  
**Course:** Computer Networks 
**Institution:** Kalinga Institute of Industrial Technology (KIIT), Bhubaneswar

## 3. Features

* **HTTP Request Forwarding:** Receives HTTP requests and forwards them to destination servers.
* **Concurrent Client Handling:** Uses POSIX threads to handle multiple clients simultaneously.
* **Response Caching:** Stores eligible GET responses in memory to reduce repeated network requests.
* **Cache Hit and Miss:** Returns cached responses when available and fetches fresh responses when necessary.
* **Cache Expiration:** Uses a default cache time of 60 seconds and supports eligible `Cache-Control: max-age` directives.
* **Access Control:** Blocks configured domains and their subdomains.
* **Custom Port Support:** Supports destination ports specified in the HTTP `Host` header.
* **Timeout Handling:** Configures socket send and receive timeouts.
* **Traffic Logging:** Records client information, requested host, HTTP method, path, status, response size, and request duration.
* **Error Handling:** Generates HTTP error responses for invalid requests, blocked domains, unsupported methods, and destination connection failures.

## 4. Technologies Used

* Programming Language: C
* Operating System: Linux
* Networking: TCP Sockets
* Concurrency: POSIX Threads (`pthread`)
* Protocol: HTTP
* Tools: GCC, curl, Git

## 5. Requirements

Before running the project, ensure that the following are installed:

* Linux operating system or a compatible POSIX environment
* GCC compiler
* curl command-line tool
* Internet connection

## 6. Compilation

Compile the source code using:

```bash
gcc server.c -o server -pthread
```

Compiler options:

* `gcc` compiles the C program.
* `-o server` names the executable `server`.
* `-pthread` enables POSIX thread support.

## 7. Running the Proxy Server

Start the proxy server:

```bash
./server
```

The server listens on port `8080` by default.

Keep the terminal running while testing the proxy.

## 8. Testing the Proxy Server

### Test 1: HTTP Request Forwarding

Open another terminal and execute:

```bash
curl -v -x http://127.0.0.1:8080 http://example.org/
```

This sends an HTTP request through the proxy server.

### Test 2: Response Caching

Run the same command twice:

```bash
curl -v -x http://127.0.0.1:8080 http://example.org/
```

Expected behavior:

* First request: `CACHE MISS` — the proxy fetches the response from the destination server.
* Second request: `CACHE HIT` — the proxy returns the cached response if it is still valid.

### Test 3: Domain Blocking

Execute:

```bash
curl -v -x http://127.0.0.1:8080 http://example.com/
```

If `example.com` is present in the configured block list, the proxy should reject the request with an HTTP `403 Forbidden` response.

### Test 4: View Proxy Logs

Check the terminal output while making requests. The proxy logs information about client connections, requested hosts, HTTP methods, paths, response sizes, status codes, and cache activity.

## 9. Configuration

The following constants can be modified in the source code:

* `PORT`: Port on which the proxy server listens.
* `BUFFER_SIZE`: Buffer size used when processing network data.
* `CACHE_SIZE`: Maximum number of cache entries.
* `CACHE_TTL`: Default cache lifetime in seconds.
* `MAX_BLOCKED_DOMAINS`: Maximum number of configured blocked domains.

The blocked domain list can also be modified in the source code.

## 10. How the Proxy Works

1. The proxy server creates a TCP socket and binds it to port `8080`.
2. It listens for incoming client connections.
3. When a client connects, the server creates a separate thread to handle the connection.
4. The proxy receives and parses the client's HTTP request.
5. It checks whether the requested domain is blocked.
6. For eligible GET requests, it checks the cache for a valid response.
7. If a valid cached response exists, the proxy sends it to the client.
8. If no valid cached response exists, the proxy connects to the destination server and forwards the HTTP request.
9. The destination server returns an HTTP response, which the proxy forwards to the client.
10. Eligible responses are stored in the cache for subsequent requests.
11. The proxy records request information for monitoring and debugging.

## 11. Limitations

* HTTPS `CONNECT` tunneling is not supported; such requests return `501 Not Implemented`.
* Large POST request bodies may not be handled completely because the implementation does not continuously read the entire request body.
* Response completion relies on the destination server closing the connection, so some HTTP response-framing scenarios may not be handled robustly.
* The cache is stored in memory and is lost when the proxy server stops.
* The cache key does not include the destination port, which can cause incorrect cache reuse when the same host and path are requested on different ports.
* The implementation is intended for educational purposes and is not production-ready.

## 12. Learning Outcomes

Through this project, we gained practical experience with:

* TCP socket programming in C
* Client-server communication
* HTTP request parsing and forwarding
* Multithreaded programming
* Response caching and cache expiration
* Domain-based access control
* Network error handling
* Traffic logging and performance observation
* Testing and debugging network applications

## 13. Conclusion

The HTTP Proxy Server demonstrates how an intermediary server processes HTTP requests and responses using TCP sockets. The project implements request forwarding, concurrent client handling, response caching, domain blocking, timeout configuration, and traffic logging.

It provides practical experience in computer networking and helps develop an understanding of how proxy servers manage client requests and communication with destination web servers.
