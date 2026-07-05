# TinyReactor Benchmarks

HTTP benchmark results using Apache Bench (`ab`) on a Fedora 43 WSL instance.

## Configuration

- **CPU:** Fedora 43 (WSL)
- **Threads:** 16 (`num_threads = 16`)
- **Port:** 8080
- **Document root:** `.` (build directory)
- **Build type:** Release (`-O3 -DNDEBUG`)
- **Server binary:** Built with CMake, C++23

## Results

### test.html (53 bytes) — Keep-Alive, Concurrency 100

| Metric | Value |
|---|---|
| Requests | 100,000 |
| Concurrency | 100 |
| Keep-Alive | Yes |
| Failed requests | 0 |
| **Requests/sec** | **~20,627** |
| Transfer rate | 3,042 KB/s |
| Avg latency | 5 ms |
| P99 latency | 12 ms |

### test.html (53 bytes) — Keep-Alive, Concurrency 500

| Metric | Value |
|---|---|
| Requests | 50,000 |
| Concurrency | 500 |
| Keep-Alive | Yes |
| Failed requests | 0 |
| **Requests/sec** | **~21,438** |
| Transfer rate | 3,161 KB/s |
| Avg latency | 23 ms |
| P99 latency | 43 ms |

### test.html (53 bytes) — No Keep-Alive, Concurrency 100

| Metric | Value |
|---|---|
| Requests | 20,000 |
| Concurrency | 100 |
| Keep-Alive | No |
| Failed requests | 0 |
| **Requests/sec** | **~20,772** |
| Transfer rate | 3,063 KB/s |
| Avg latency | 5 ms |
| P99 latency | 12 ms |

### index.html (157 bytes) — Keep-Alive, Concurrency 200

| Metric | Value |
|---|---|
| Requests | 50,000 |
| Concurrency | 200 |
| Keep-Alive | Yes |
| Failed requests | 0 |
| **Requests/sec** | **~22,474** |
| Transfer rate | 5,619 KB/s |
| Avg latency | 9 ms |
| P99 latency | 18 ms |

## Key Takeaways

- **~21,000–22,000 req/s** sustained throughput across all scenarios.
- **Scales well** with concurrency; at 500 concurrent connections it delivers ~21,438 RPS.
- **Zero failed requests** across all tests — the server handles connections cleanly.
- No-Keep-Alive performance is on par with Keep-Alive in this environment (WSL loopback), suggesting the connection overhead is minimal.
- Larger payloads (index.html) achieve higher transfer rates (~5,619 KB/s) while maintaining high RPS.
