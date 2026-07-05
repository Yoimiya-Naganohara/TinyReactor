#include "HttpParser.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

std::optional<HttpRequest> HttpRequest::parse(std::string_view data) {
  // sscanf needs null-terminated input; string_view doesn't guarantee it
  const std::string data_copy(data);
  std::array<char, 16> method{};
  std::array<char, 4096> path{};
  std::array<char, 16> version{};
  if (sscanf(data_copy.c_str(), "%15s %4095s %15s", method.data(), path.data(),
             version.data()) != 3)
    return std::nullopt;

  // Find end of request line
  size_t pos = data.find("\r\n");
  if (pos == std::string_view::npos)
    return std::nullopt;
  pos += 2;  // Skip \r\n

  std::unordered_map<std::string, std::string> headers;

  // Parse headers line by line
  while (pos < data.size()) {
    const size_t line_end = data.find("\r\n", pos);
    if (line_end == std::string_view::npos)
      break;

    const std::string_view line = data.substr(pos, line_end - pos);
    pos = line_end + 2;

    if (line.empty())  // Empty line marks end of headers
      break;

    const size_t colon = line.find(':');
    if (colon == std::string_view::npos)
      break;

    std::string key(line.substr(0, colon));
    std::string_view value_view = line.substr(colon + 1);
    // Trim leading whitespace from value
    while (!value_view.empty() && (value_view[0] == ' ' || value_view[0] == '\t'))
      value_view.remove_prefix(1);

    headers.emplace(std::move(key), std::string(value_view));
  }

  // Body follows the \r\n\r\n separator
  std::string body;
  const size_t body_start = data.find("\r\n\r\n");
  if (body_start != std::string_view::npos)
    body = std::string(data.substr(body_start + 4));

  return HttpRequest{
      .method = std::string(method.data()),
      .path = std::string(path.data()),
      .version = std::string(version.data()),
      .headers = std::move(headers),
      .body = std::move(body),
  };
}

std::string HttpResponse::serialize() const {
  std::string s;
  s += version + " " + std::to_string(status_code) + " " + status_text + "\r\n";
  s += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  for (const auto &[key, value] : headers) {
    s += key;
    s += ": ";
    s += value;
    s += "\r\n";
  }
  s += "\r\n";
  s += body;
  return s;
}

static const std::unordered_map<std::string_view, std::string_view> FALLBACK_MIME_TYPES{
    {".html", "text/html; charset=utf-8"},
    {".htm", "text/html; charset=utf-8"},
    {".css", "text/css; charset=utf-8"},
    {".js", "application/javascript; charset=utf-8"},
    {".json", "application/json"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".txt", "text/plain; charset=utf-8"},
    {".xml", "application/xml; charset=utf-8"},
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
};

std::string_view HttpResponse::mime_type(
    std::string_view path,
    const std::unordered_map<std::string, std::string> &custom_types) {
  auto dot = path.rfind('.');
  if (dot == std::string_view::npos)
    return "application/octet-stream";

  const std::string_view ext = path.substr(dot);

  // Check custom types from config first
  if (!custom_types.empty()) {
    // Convert string_view to string for lookup in the std::string-keyed map
    auto it = custom_types.find(std::string(ext));
    if (it != custom_types.end())
      return it->second;
  }

  // Fall back to built-in defaults
  auto it = FALLBACK_MIME_TYPES.find(ext);
  if (it == FALLBACK_MIME_TYPES.end())
    return "application/octet-stream";

  return it->second;
}
