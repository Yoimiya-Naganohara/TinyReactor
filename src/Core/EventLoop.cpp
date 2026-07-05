#include "EventLoop.hpp"

#include <vector>
#include <cerrno>
#include <system_error>

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../Configuration/ConfigManager.hpp"
#include "../Worker/Worker.hpp"

EventLoop::EventLoop(const ConfigManager &config)
    : config_(config), max_epoll_events_(config.max_epoll_events) {
  workers_.reserve(config_.num_threads);
  for (int i = 0; i < config_.num_threads; ++i) {
    workers_.emplace_back(std::make_unique<Worker>(
        config_.document_root, config_.mime_types_,
        config_.max_epoll_events, config_.max_cache_size));
  }
}

EventLoop::~EventLoop() {
  if (epfd_ != -1)
    close(epfd_);
  if (sock_fd_ != -1)
    close(sock_fd_);
}

void EventLoop::setup_socket() {
  sock_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (sock_fd_ == -1)
    throw std::system_error(errno, std::generic_category(), "socket");

  const int reuse = 1;
  if (setsockopt(sock_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
    throw std::system_error(errno, std::generic_category(), "setsockopt SO_REUSEADDR");

  struct sockaddr_in addr{
      .sin_family = AF_INET,
      .sin_port = htons(config_.port),
      .sin_addr = {.s_addr = htonl(INADDR_ANY)},
  };
  if (bind(sock_fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == -1)
    throw std::system_error(errno, std::generic_category(), "bind");

  if (listen(sock_fd_, SOMAXCONN) == -1)
    throw std::system_error(errno, std::generic_category(), "listen");
}

void EventLoop::setup_epoll() {
  epfd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epfd_ == -1)
    throw std::system_error(errno, std::generic_category(), "epoll_create1");

  struct epoll_event ev{
      .events = EPOLLIN,
      .data = {.fd = sock_fd_},
  };
  if (epoll_ctl(epfd_, EPOLL_CTL_ADD, sock_fd_, &ev) == -1)
    throw std::system_error(errno, std::generic_category(), "epoll_ctl");
}

void EventLoop::handle_accept() {
  // Accept all pending connections (EPOLLET requires draining)
  while (true) {
    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd =
        accept4(sock_fd_, reinterpret_cast<struct sockaddr *>(&client_addr), &client_len,
                SOCK_NONBLOCK);
    if (client_fd == -1)
      break;
    const size_t idx =
        next_worker_.fetch_add(1, std::memory_order_relaxed) % workers_.size();
    workers_[idx]->add_fd(client_fd);
  }
}

void EventLoop::run() {
  setup_socket();
  setup_epoll();

  std::vector<struct epoll_event> events(max_epoll_events_);
  while (true) {
    const int n = epoll_wait(epfd_, events.data(), max_epoll_events_, -1);
    if (n == -1)
      throw std::system_error(errno, std::generic_category(), "epoll_wait");

    for (int i = 0; i < n; i++) {
      if (events[i].data.fd == sock_fd_)
        handle_accept();
    }
  }
}
