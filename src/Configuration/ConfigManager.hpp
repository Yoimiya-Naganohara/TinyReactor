#pragma once

#include <fstream>
#include <string>
#include <unordered_map>

class ConfigManager {
public:
  ConfigManager() {
    // Default MIME types
    mime_types_[{".html"}] = "text/html; charset=utf-8";
    mime_types_[{".htm"}] = "text/html; charset=utf-8";
    mime_types_[{".css"}] = "text/css; charset=utf-8";
    mime_types_[{".js"}] = "application/javascript; charset=utf-8";
    mime_types_[{".json"}] = "application/json";
    mime_types_[{".png"}] = "image/png";
    mime_types_[{".jpg"}] = "image/jpeg";
    mime_types_[{".jpeg"}] = "image/jpeg";
    mime_types_[{".gif"}] = "image/gif";
    mime_types_[{".svg"}] = "image/svg+xml";
    mime_types_[{".ico"}] = "image/x-icon";
    mime_types_[{".txt"}] = "text/plain; charset=utf-8";
    mime_types_[{".xml"}] = "application/xml; charset=utf-8";
    mime_types_[{".pdf"}] = "application/pdf";
    mime_types_[{".zip"}] = "application/zip";
  }

  bool load(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open())
      return false;

    std::string line;
    while (std::getline(file, line)) {
      // Skip empty lines and comments
      if (line.empty() || line[0] == '#')
        continue;

      auto eq = line.find('=');
      if (eq == std::string::npos)
        continue;

      std::string const key = trim(line.substr(0, eq));
      std::string const value = trim(line.substr(eq + 1));

      if (key == "port")
        port = std::stoi(value);
      else if (key == "num_threads")
        num_threads = std::stoi(value);
      else if (key == "document_root")
        document_root = value;
      else if (key == "max_epoll_events")
        max_epoll_events = std::stoi(value);
      else if (key == "max_cache_size")
        max_cache_size = std::stoi(value);
      else if (key.starts_with("mime_type."))
        mime_types_[key.substr(10)] = value;
    }
    return true;
  }

  int port = 8080;
  int num_threads = 16;
  int max_epoll_events = 1024;
  size_t max_cache_size = 128;
  std::string document_root = ".";
  std::unordered_map<std::string, std::string> mime_types_;

private:
  static std::string trim(const std::string &s) {
    auto start = s.find_first_not_of(" \t\r");
    if (start == std::string::npos)
      return "";
    auto end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
  }
};
