#pragma once
#include <atomic>
#include <memory>
#include <vector>

class ConfigManager;
class Worker;

class EventLoop {
public:
  EventLoop(const ConfigManager &config);
  ~EventLoop();

  void run();

private:
  int max_epoll_events_;

  int sock_fd_ = -1;
  int epfd_ = -1;
  std::atomic<int> next_worker_{0};
  std::vector<std::unique_ptr<Worker>> workers_;
  const ConfigManager &config_;
  void setup_socket();
  void setup_epoll();
  void handle_accept();
};
