# Proxy Server Performance Analysis

## 1. Overview

This document evaluates the performance of the HTTP Proxy Server implemented in C using TCP sockets and POSIX threads.

The main objective is to compare direct HTTP communication with communication through the proxy server.

The evaluation considers:

* Response time
* HTTP response status
* Response size
* Proxy performance
* Cache performance

## 2. Testing Environment

| Parameter              | Details                   |
| ---------------------- | ------------------------- |
| Operating System       | Linux                     |
| Programming Language   | C                         |
| Communication Protocol | HTTP over TCP             |
| Proxy Server Port      | 8080                      |
| Compiler               | GCC                       |
| Threading              | POSIX threads (`pthread`) |
| Testing Tool           | `curl`                    |
| Destination Website    | `httpforever.com`         |

## 3. Direct HTTP Request

A direct request connects to the destination website without using the proxy.

**Command used:**

```bash
curl -s -o /dev/null -w "%{http_code} %{time_total} %{size_download}\n" http://httpforever.com/
```

**Measured result:**

```text
HTTP Status: 200
Response Time: 0.824077 seconds
Response Size: 11513 bytes
```

The destination returned HTTP status `200`, indicating a successful request.

## 4. HTTP Request Through the Proxy

The proxy server was started on port `8080`. A request was then sent through the proxy.

**Command used:**

```bash
curl -s -o /dev/null -w "%{http_code} %{time_total} %{size_download}\n" -x http://127.0.0.1:8080 http://httpforever.com/
```

**Measured result:**

```text
HTTP Status: 200
Response Time: 0.485904 seconds
Response Size: 11513 bytes
```

The proxy returned the same HTTP status and response size as the direct request.

## 5. Cache Performance

The proxy implements an in-memory cache that can serve eligible stored GET responses without contacting the destination server again.

### 5.1 Cache Miss

The measured initial proxy request produced the following result:

```text
HTTP Status: 200
Response Time: 0.485904 seconds
Response Size: 11513 bytes
```

This result represents a cache miss only if the proxy log confirms `CACHE MISS`.

### 5.2 Cache Hit

The repeated proxy request produced the following result:

```text
HTTP Status: 200
Response Time: 0.002886 seconds
Response Size: 11513 bytes
```

This result represents a cache hit only if the proxy log confirms `CACHE HIT`.

The substantially lower response time is consistent with the expected benefit of returning a cached response rather than fetching it again from the destination server.

## 6. Performance Results

| Test                                  | HTTP Status | Response Time (seconds) | Response Size (bytes) |
| ------------------------------------- | ----------: | ----------------------: | --------------------: |
| Direct HTTP request                   |         200 |                0.824077 |                 11513 |
| HTTP through proxy — initial request  |         200 |                0.485904 |                 11513 |
| HTTP through proxy — repeated request |         200 |                0.002886 |                 11513 |

The cache classifications should be confirmed using the proxy logs before presenting the initial and repeated requests as verified cache-miss and cache-hit measurements.

## 7. Proxy Performance Analysis

The measured difference between the direct request time and the initial proxy request time is:

**Formula:**

```text
Time Difference = Direct Request Time - Proxy Request Time
```

Using the measured values:

```text
Time Difference = 0.824077 - 0.485904
                = 0.338173 seconds
```

The initial proxy request was approximately `0.338173` seconds faster than the direct request in this individual measurement.

This result does not establish that the proxy reduces network latency or has negative processing overhead. Differences in network conditions, destination-server response times, and measurement variability may affect individual results.

Repeated measurements under comparable conditions are needed to estimate typical proxy overhead reliably.

## 8. Cache Improvement

Cache improvement is estimated by comparing the measured initial proxy request with the repeated proxy request.

### Time Saved

```text
Time Saved = Initial Proxy Time - Repeated Proxy Time

           = 0.485904 - 0.002886

           = 0.483018 seconds
```

### Percentage Improvement

```text
Percentage Improvement =
((Initial Proxy Time - Repeated Proxy Time)
 / Initial Proxy Time) * 100
```

Using the measured values:

```text
= ((0.485904 - 0.002886) / 0.485904) * 100

≈ 99.41%
```

The repeated request was approximately `99.41%` faster than the initial proxy request based on these measurements.

This calculation demonstrates the observed difference in response time. It represents a cache-hit improvement only if the proxy logs confirm that the first request was a cache miss and the second was a cache hit.

## 9. Observations

* All three requests returned HTTP status `200`.
* Each request reported a response size of `11513` bytes.
* The direct request took `0.824077` seconds.
* The initial proxy request took `0.485904` seconds.
* The repeated proxy request took `0.002886` seconds.
* The repeated proxy request was substantially faster than the initial proxy request.
* Cache logs can help establish whether the repeated response was served from memory.
* Observed performance can vary with network conditions, destination-server response time, cache state, and system load.

These results represent individual measurements rather than averages from repeated trials.

## 10. Conclusion

The tests demonstrate successful HTTP communication both directly and through the proxy, with matching HTTP status codes and reported response sizes.

The measured repeated proxy request was significantly faster than the initial proxy request. If the proxy logs confirm a cache hit, this result demonstrates the performance benefit of serving a response from the in-memory cache.

The repeated request took approximately `2.886` milliseconds, compared with `485.904` milliseconds for the initial proxy request.

These measurements illustrate the potential benefit of caching. Additional trials would provide a more reliable comparison of typical response times and proxy overhead.
