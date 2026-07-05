#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

struct HttpRequest {
  std::string method;
  std::string path;
  std::string version;
  std::unordered_map<std::string, std::string> headers;
  std::string body;

  static std::optional<HttpRequest> parse(std::string_view data);
};

struct HttpResponse {
  std::string version{"HTTP/1.1"};
  uint16_t status_code;
  std::string status_text;
  std::unordered_map<std::string, std::string> headers;
  std::string body;

  std::string serialize() const;
  static std::string_view mime_type(
      std::string_view path,
      const std::unordered_map<std::string, std::string> &custom_types = {});
};
