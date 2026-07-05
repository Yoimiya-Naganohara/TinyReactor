#include "Worker.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../HttpParser/HttpParser.hpp"

Worker::Worker(std::string document_root,
             std::unordered_map<std::string, std::string> mime_types,
             int max_epoll_events,
             size_t max_cache_size)
    : document_root_(std::move(document_root)),
      mime_types_(std::move(mime_types)),
      max_epoll_events_(max_epoll_events),
      max_cache_size_(max_cache_size),
      epfd_(epoll_create1(EPOLL_CLOEXEC)),
      notify_fd_(eventfd(0, EFD_NONBLOCK)) {
  struct epoll_event ev{.events = EPOLLIN, .data = {.fd = notify_fd_}};
  epoll_ctl(epfd_, EPOLL_CTL_ADD, notify_fd_, &ev);
  thread_ = std::jthread([this] { Worker::run(); });
}

Worker::~Worker() {
  stop_.store(true);
  uint64_t val = 1;
  if (write(notify_fd_, &val, sizeof(val)) == -1) {
    perror("write");
  }
  if (thread_.joinable())
    thread_.join();
  if (epfd_ != -1)
    close(epfd_);
  if (notify_fd_ != -1)
    close(notify_fd_);
}

void Worker::run() {
  std::vector<struct epoll_event> events(max_epoll_events_);

  while (!stop_) {
    const int n = epoll_wait(epfd_, events.data(), max_epoll_events_, -1);
    if (n == -1)
      throw std::system_error(errno, std::generic_category(), "epoll_wait");

    for (int i = 0; i < n; i++) {
      const int fd = events[i].data.fd;
      const uint32_t flags = events[i].events;

      if (fd == notify_fd_) {
        uint64_t val;
        if (read(notify_fd_, &val, sizeof(val)) == -1)
          throw std::system_error(errno, std::generic_category(), "read");
        std::queue<int> fds;
        {
          const std::scoped_lock lock(mtx_);
          fds.swap(pending_fds_);
        }
        while (!fds.empty()) {
          const int pfd = fds.front();
          fds.pop();
          struct epoll_event client_ev{
              .events = EPOLLIN | EPOLLET,
              .data = {.fd = pfd},
          };
          if (epoll_ctl(epfd_, EPOLL_CTL_ADD, pfd, &client_ev) == -1) {
            close_fd(pfd);
          }
          connections_.emplace(pfd, Connection{});
        }
      } else if (flags & (EPOLLERR | EPOLLHUP)) {
        connections_.erase(fd);
        close_fd(fd);
      } else if (flags & EPOLLIN) {
        handle_client_read(fd);
      } else if (flags & EPOLLOUT) {
        handle_client_write(fd);
      }
    }
  }
}

void Worker::add_fd(int fd) {
  {
    const std::scoped_lock lock(mtx_);
    pending_fds_.push(fd);
  }
  uint64_t val = 1;
  if (write(notify_fd_, &val, sizeof(val)) == -1)
    throw std::system_error(errno, std::generic_category(), "write");
}

void Worker::close_fd(int fd) {
  connections_.erase(fd);
  epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
  close(fd);
}

void Worker::handle_client_read(int fd) {
  auto &conn = connections_[fd];
  std::array<char, 4096> buf;
  while (true) {
    const ssize_t len = read(fd, buf.data(), buf.size());
    if (len == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      close_fd(fd);
      return;
    }
    if (len == 0) {
      close_fd(fd);
      return;
    }
    conn.read_buf.insert(conn.read_buf.end(), buf.data(), buf.data() + len);
  }
  handle_request(fd);
}

void Worker::handle_request(int fd) {
  auto &conn = connections_[fd];
  auto request =
      HttpRequest::parse(std::string_view(conn.read_buf.data(), conn.read_buf.size()));

  if (!request) {
    const char *bad_request =
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 30\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Bad request: expected HTTP method and path\r\n";
    write_all(fd, bad_request, strlen(bad_request));
    close_fd(fd);
    return;
  }

  // Check Connection header for keep-alive
  bool keep_alive = false;
  auto it = request->headers.find("Connection");
  if (it != request->headers.end() && it->second.find("keep-alive") != std::string::npos)
    keep_alive = true;

  HttpResponse response;
  char *resolved_root = realpath(document_root_.c_str(), nullptr);
  if (!resolved_root)
    throw std::system_error(errno, std::generic_category(), "realpath root");
  const std::string root(resolved_root);
  free(resolved_root);

  char *resolved_path = realpath((document_root_ + request->path).c_str(), nullptr);
  if (!resolved_path) {
    response.status_code = 404;
    response.status_text = "Not Found";
    response.body = "404 Not Found";
  } else {
    const std::string rp(resolved_path);
    free(resolved_path);
    if (!rp.starts_with(root)) {
      response.status_code = 403;
      response.status_text = "Forbidden";
      response.body = "403 Forbidden";
    } else {
      std::string content = read_file(rp);
      if (!content.empty()) {
        response.body = std::move(content);
        response.status_code = 200;
        response.status_text = "OK";
        response.headers["Content-Type"] = HttpResponse::mime_type(request->path, mime_types_);
      } else {
        response.status_code = 404;
        response.status_text = "Not Found";
        response.body = "404 Not Found";
      }
    }
  }

  if (keep_alive) {
    response.headers["Connection"] = "keep-alive";
  } else {
    response.headers["Connection"] = "close";
  }
  conn.keep_alive = keep_alive;

  conn.write_buf = response.serialize();
  conn.write_offset = 0;
  conn.read_buf.clear();

  // Attempt first write
  ssize_t n = write(fd, conn.write_buf.data() + conn.write_offset,
                    conn.write_buf.size() - conn.write_offset);
  if (n == -1) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      close_fd(fd);
      return;
    }
    n = 0;
  }
  conn.write_offset += n;

  if (conn.write_offset < conn.write_buf.size()) {
    // Not all data written yet, register EPOLLOUT for next attempt
    struct epoll_event ev{.events = EPOLLIN | EPOLLOUT | EPOLLET, .data = {.fd = fd}};
    epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
  } else {
    finish_request(fd);
  }
}

void Worker::handle_client_write(int fd) {
  auto &conn = connections_[fd];
  const ssize_t n = write(fd, conn.write_buf.data() + conn.write_offset,
                          conn.write_buf.size() - conn.write_offset);
  if (n == -1) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      close_fd(fd);
      return;
    }
    return;
  }
  conn.write_offset += n;

  if (conn.write_offset >= conn.write_buf.size()) {
    finish_request(fd);
  }
}

void Worker::finish_request(int fd) {
  auto &conn = connections_[fd];

  conn.write_buf.clear();
  conn.write_offset = 0;

  if (conn.keep_alive) {
    // Wait for next request
    struct epoll_event ev{.events = EPOLLIN | EPOLLET, .data = {.fd = fd}};
    epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev);
  } else {
    connections_.erase(fd);
    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
  }
}

std::string Worker::read_file(const std::string &path) {
  auto it = file_cache_.find(path);
  if (it != file_cache_.end()) {
    auto mtime = std::filesystem::last_write_time(path);
    if (it->second.mtime == mtime) {
      it->second.hits++;
      return it->second.content;
    }
    // mtime changed, remove stale entry
    const size_t old_hits = it->second.hits;
    file_cache_.erase(it);
    auto range = cache_order_.equal_range(old_hits);
    for (auto o = range.first; o != range.second; ++o) {
      if (o->second == path) {
        cache_order_.erase(o);
        break;
      }
    }
  }

  // Evict least-frequently-used entry
  if (file_cache_.size() >= max_cache_size_) {
    auto lfu = cache_order_.begin();
    file_cache_.erase(lfu->second);
    cache_order_.erase(lfu);
  }

  std::ifstream file(path);
  if (!file.is_open())
    return {};

  FileCache entry;
  std::stringstream buf;
  buf << file.rdbuf();
  entry.content = buf.str();
  entry.mtime = std::filesystem::last_write_time(path);
  file_cache_.emplace(path, std::move(entry));
  cache_order_.emplace(0, path);
  return file_cache_.find(path)->second.content;
}

bool Worker::write_all(int fd, const char *data, size_t len) {
  size_t written = 0;
  while (written < len) {
    const ssize_t n = write(fd, data + written, len - written);
    if (n == -1)
      return false;
    written += n;
  }
  return true;
}
