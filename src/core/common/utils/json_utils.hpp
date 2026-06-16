#pragma once

#include <cctype>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace iotgw {
namespace core {
namespace common {
namespace json {

inline std::string Escape(const std::string& s) {
  std::ostringstream oss;
  for (unsigned char ch : s) {
    switch (ch) {
      case '"':
        oss << "\\\"";
        break;
      case '\\':
        oss << "\\\\";
        break;
      case '\b':
        oss << "\\b";
        break;
      case '\f':
        oss << "\\f";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        if (ch < 0x20) {
          oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(ch);
        } else {
          oss << static_cast<char>(ch);
        }
        break;
    }
  }
  return oss.str();
}

inline std::string Quote(const std::string& s) { return "\"" + Escape(s) + "\""; }

inline std::string Bool(bool v) { return v ? "true" : "false"; }

template <typename T>
inline std::string Number(T v) {
  std::ostringstream oss;
  oss << v;
  return oss.str();
}

inline std::string Object(
    std::initializer_list<std::pair<std::string, std::string>> fields) {
  std::ostringstream oss;
  oss << "{";
  bool first = true;
  for (const auto& field : fields) {
    if (!first) {
      oss << ",";
    }
    first = false;
    oss << Quote(field.first) << ":" << field.second;
  }
  oss << "}";
  return oss.str();
}

inline std::string Array(const std::vector<std::string>& values) {
  std::ostringstream oss;
  oss << "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0) {
      oss << ",";
    }
    oss << values[i];
  }
  oss << "]";
  return oss.str();
}

inline std::string GetJsonString(const std::string& body,
                                 const std::string& key,
                                 const std::string& fallback = "") {
  const std::string needle = "\"" + key + "\"";
  std::size_t pos = body.find(needle);
  if (pos == std::string::npos) {
    return fallback;
  }
  pos = body.find(':', pos + needle.size());
  if (pos == std::string::npos) {
    return fallback;
  }
  pos = body.find('"', pos + 1);
  if (pos == std::string::npos) {
    return fallback;
  }
  std::string out;
  bool escaped = false;
  for (std::size_t i = pos + 1; i < body.size(); ++i) {
    const char ch = body[i];
    if (escaped) {
      out.push_back(ch);
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      return out;
    }
    out.push_back(ch);
  }
  return fallback;
}

inline bool GetJsonNumber(const std::string& body,
                          const std::string& key,
                          double& out) {
  const std::string needle = "\"" + key + "\"";
  std::size_t pos = body.find(needle);
  if (pos == std::string::npos) {
    return false;
  }
  pos = body.find(':', pos + needle.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
    ++pos;
  }
  std::size_t end = pos;
  while (end < body.size()) {
    const char ch = body[end];
    if (!(std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' ||
          ch == '.' || ch == 'e' || ch == 'E')) {
      break;
    }
    ++end;
  }
  if (end == pos) {
    return false;
  }
  try {
    out = std::stod(body.substr(pos, end - pos));
    return true;
  } catch (...) {
    return false;
  }
}

inline bool GetJsonBool(const std::string& body,
                        const std::string& key,
                        bool& out) {
  const std::string needle = "\"" + key + "\"";
  std::size_t pos = body.find(needle);
  if (pos == std::string::npos) {
    return false;
  }
  pos = body.find(':', pos + needle.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) {
    ++pos;
  }
  if (body.compare(pos, 4, "true") == 0 || body.compare(pos, 1, "1") == 0) {
    out = true;
    return true;
  }
  if (body.compare(pos, 5, "false") == 0 || body.compare(pos, 1, "0") == 0) {
    out = false;
    return true;
  }
  if (pos < body.size() && body[pos] == '"') {
    const std::string value = GetJsonString(body, key);
    if (value == "true" || value == "1" || value == "on") {
      out = true;
      return true;
    }
    if (value == "false" || value == "0" || value == "off") {
      out = false;
      return true;
    }
  }
  return false;
}

}  // namespace json
}  // namespace common
}  // namespace core
}  // namespace iotgw
