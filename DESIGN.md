# Design Document

## 1. Introduction

The objective of this project is to develop an HTTP Proxy Server using the C programming language on Linux. The proxy acts as an intermediary between clients and destination web servers.

The system forwards HTTP requests, receives responses, supports multiple clients, caches eligible GET responses, blocks configured domains, and records request information.

## 2. System Architecture

The HTTP Proxy Server follows a modular architecture in which each component performs a specific task in processing client requests and delivering responses.

The main components are:

1. **Client Connection Handler:** Listens for incoming TCP connections on port 8080 and assigns each client connection to a separate POSIX thread, enabling concurrent request processing.

2. **HTTP Request Parser:** Receives the client's HTTP request and extracts essential information, including the request method, destination host, path, and port, to prepare the request for forwarding.

3. **Access Control Module:** Checks the requested domain against a configurable blocklist. Requests targeting blocked domains are rejected with an HTTP `403 Forbidden` response.

4. **Cache Management Module:** Checks whether an eligible GET request has a valid cached response. Cache hits are served directly to the client, while cache misses trigger a request to the destination server. Cached entries expire according to the configured time-to-live (TTL).

5. **Destination Connection Module:** Establishes a TCP connection to the destination server using the extracted hostname and port. Connection failures and timeouts are handled through the proxy's error-handling mechanisms.

6. **Request Forwarding Module:** Forwards the processed HTTP request to the destination server over the established TCP connection.

7. **Response Handling and Forwarding Module:** Receives the destination server's HTTP response, tracks the received data, forwards it to the client, and stores eligible responses in the cache for future requests.

8. **Logging and Monitoring Module:** Records useful information about proxy traffic, including the client address, requested host, HTTP method, path, response status, response size, request duration, and cache hit or miss status.

Together, these components provide HTTP request forwarding, concurrent client handling, domain-based access control, response caching, and traffic monitoring. The modular design makes the implementation easier to understand, test, and maintain.

## 3. Request Processing Workflow

1. The server creates a TCP socket and listens on port 8080.
2. When a client connects, a separate thread handles the connection.
3. The proxy receives and parses the HTTP request.
4. It checks the requested domain against the access-control list.
5. If the domain is blocked, the proxy returns HTTP 403 Forbidden.
6. For eligible GET requests, the proxy checks the cache.
7. If a valid cached response exists, the proxy returns it to the client.
8. Otherwise, the proxy connects to the destination server and forwards the request.
9. The proxy receives the response and forwards it to the client.
10. Eligible responses are stored in the cache for subsequent requests.
11. The proxy records request information in its logs.

## 4. Concurrency

The server uses POSIX threads (`pthread`) to handle client connections concurrently.

When a client connects, the server creates a separate thread to process its request. This allows multiple clients to be handled without requiring the main server loop to process every request sequentially.

## 5. Caching Strategy

The proxy maintains an in-memory cache for eligible GET responses.

- **Cache MISS:** The requested response is not available in the cache, so the proxy contacts the destination server.
- **Cache HIT:** A valid cached response is returned without fetching the same response again.
- **Cache expiration:** Responses have a default lifetime of 60 seconds, with eligible `Cache-Control: max-age` directives supported by the implementation.
- **Cache restrictions:** Responses with certain directives, such as `no-store` or `private`, are excluded from caching.

The cache reduces repeated network requests and can improve response time for repeated requests.

## 6. Access Control

The proxy uses a configurable list of blocked domains. When a request matches a blocked domain or its subdomain, the proxy rejects the request with an HTTP 403 Forbidden response.

This demonstrates basic domain-based access control.

## 7. Error Handling and Logging

The proxy provides error responses for supported error conditions, including malformed requests, blocked domains, unsupported HTTPS `CONNECT` requests, and destination connection failures.

HTTPS `CONNECT` requests receive an HTTP 501 Not Implemented response because HTTPS tunneling is not supported in the submitted implementation.

Logging records information such as the client address, requested host, HTTP method, path, response status, response size, duration, and cache result.

Socket timeouts are configured to help prevent connections from waiting indefinitely.

## 8. Design Trade-offs

### A. Multithreading

**Decision:** Use a separate thread for each client connection.

**Advantage:** Multiple clients can be processed concurrently.

**Trade-off:** Creating a thread for every connection consumes resources and may not scale well under a very high number of simultaneous connections.

### B. In-Memory Caching

**Decision:** Store eligible responses in memory.

**Advantage:** Cached responses can be retrieved quickly without contacting the destination server.

**Trade-off:** The cache is lost when the server stops, and the available cache capacity is limited.

### C. HTTP-Only Support

**Decision:** Focus on forwarding ordinary HTTP requests.

**Advantage:** Keeps the implementation focused on HTTP request parsing, forwarding, caching, and access control.

**Trade-off:** HTTPS `CONNECT` tunneling is not supported in the submitted implementation.

### D. Connection Timeouts

**Decision:** Configure socket send and receive timeouts.

**Advantage:** Helps prevent stalled connections from waiting indefinitely.

**Trade-off:** Slow destination servers may time out even when they might eventually respond.

### E. Basic Domain Blocking

**Decision:** Use a configurable domain list.

**Advantage:** Simple to implement and demonstrate.

**Trade-off:** The list must be maintained manually, and domain-based filtering alone does not provide comprehensive security.

## 9. Limitations

- HTTPS `CONNECT` tunneling is not supported.
- Large POST request bodies may not be processed completely.
- Response framing is not fully robust for every HTTP response scenario.
- The cache is stored in memory.
- The cache key does not include the destination port.
- The implementation is intended for educational purposes rather than production deployment.

## 10. Conclusion

The project demonstrates fundamental networking concepts through an HTTP proxy implemented in C. It combines TCP socket programming, multithreading, request forwarding, response caching, domain-based access control, timeout configuration, and logging.

Performance testing compares direct HTTP requests with requests sent through the proxy. Repeated requests can also demonstrate the effect of caching on response time. The measured results are documented separately in `PERFORMANCE.md`.