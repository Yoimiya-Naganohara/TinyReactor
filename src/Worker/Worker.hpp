#pragma once
#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

struct Connection {
  std::vector<char> read_buf;
  std::string write_buf;
  size_t write_offset{0};
  bool keep_alive{false};
};
struct FileCache {
  std::string content;
  std::filesystem::file_time_type mtime;
  size_t hits{0};
};

class Worker {
  int epfd_{-1};
  int notify_fd_{-1};
  std::queue<int> pending_fds_;
  std::mutex mtx_;
  std::atomic_bool stop_{false};
  std::jthread thread_;
  std::string document_root_;
  int max_epoll_events_;
  size_t max_cache_size_;
  std::unordered_map<std::string, std::string> mime_types_;
  std::unordered_map<int, Connection> connections_;
  std::unordered_map<std::string, FileCache> file_cache_;
  std::multimap<size_t, std::string> cache_order_;
  void run();
  void handle_client_read(int fd);
  void handle_client_write(int fd);
  void handle_request(int fd);
  void finish_request(int fd);
  void close_fd(int fd);
  static bool write_all(int fd, const char *data, size_t len);
  std::string read_file(const std::string &path);

public:
  Worker(std::string document_root,
         std::unordered_map<std::string, std::string> mime_types,
         int max_epoll_events,
         size_t max_cache_size);
  void add_fd(int fd);
  ~Worker();
};
