#include <iostream>
#include <system_error>

#include "Configuration/ConfigManager.hpp"
#include "Core/EventLoop.hpp"

int main(int argc, char *argv[]) {
  try {
    ConfigManager config;
    if (argc > 1)
      config.load(argv[1]);
    else
      config.load("tinyreactor.conf");

    EventLoop loop(config);
    loop.run();
  } catch (const std::system_error &e) {
    std::cerr << "Fatal: " << e.what() << " (" << e.code() << ")\n";
    return 1;
  }
  return 0;
}
