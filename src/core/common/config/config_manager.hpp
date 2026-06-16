#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace iotgw {
namespace core {
namespace common {
namespace config {

class ConfigManager {
 public:
  bool LoadYamlFile(const std::string& file_path) {
    values_.clear();
    return LoadYamlFileMerge(file_path);
  }

  bool LoadYamlFileMerge(const std::string& file_path) {
    std::ifstream in(file_path.c_str());
    if (!in.is_open()) {
      return false;
    }
    std::vector<std::string> stack;
    std::vector<int> indents;
    std::string line;
    while (std::getline(in, line)) {
      ParseLine(line, stack, indents);
    }
    return true;
  }

  bool Has(const std::string& key) const { return values_.count(key) != 0; }

  bool GetString(const std::string& key, std::string& out) const {
    auto it = values_.find(key);
    if (it == values_.end()) {
      return false;
    }
    out = it->second;
    return true;
  }

  std::string GetStringOr(const std::string& key,
                          const std::string& default_value) const {
    auto it = values_.find(key);
    return it == values_.end() ? default_value : it->second;
  }

  bool GetInt64(const std::string& key, int64_t& out) const {
    auto it = values_.find(key);
    if (it == values_.end()) {
      return false;
    }
    try {
      out = std::stoll(it->second);
      return true;
    } catch (...) {
      return false;
    }
  }

  int64_t GetInt64Or(const std::string& key, int64_t default_value) const {
    int64_t out = 0;
    return GetInt64(key, out) ? out : default_value;
  }

  bool GetBool(const std::string& key, bool& out) const {
    auto it = values_.find(key);
    if (it == values_.end()) {
      return false;
    }
    const std::string value = Lower(it->second);
    if (value == "true" || value == "yes" || value == "1" || value == "on") {
      out = true;
      return true;
    }
    if (value == "false" || value == "no" || value == "0" || value == "off") {
      out = false;
      return true;
    }
    return false;
  }

  bool GetBoolOr(const std::string& key, bool default_value) const {
    bool out = false;
    return GetBool(key, out) ? out : default_value;
  }

  const std::map<std::string, std::string>& Values() const { return values_; }

 private:
  static std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
      ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
      --end;
    }
    return text.substr(begin, end - begin);
  }

  static std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    return text;
  }

  static std::string StripQuotes(const std::string& text) {
    if (text.size() >= 2 &&
        ((text.front() == '"' && text.back() == '"') ||
         (text.front() == '\'' && text.back() == '\''))) {
      return text.substr(1, text.size() - 2);
    }
    return text;
  }

  static int CountIndent(const std::string& line) {
    int n = 0;
    for (char ch : line) {
      if (ch == ' ') {
        ++n;
      } else {
        break;
      }
    }
    return n;
  }

  static std::string RemoveComment(const std::string& line) {
    bool quote = false;
    char quote_ch = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
      const char ch = line[i];
      if ((ch == '"' || ch == '\'') && (i == 0 || line[i - 1] != '\\')) {
        if (!quote) {
          quote = true;
          quote_ch = ch;
        } else if (quote_ch == ch) {
          quote = false;
        }
      }
      if (!quote && ch == '#') {
        return line.substr(0, i);
      }
    }
    return line;
  }

  void ParseLine(const std::string& raw,
                 std::vector<std::string>& stack,
                 std::vector<int>& indents) {
    std::string line = RemoveComment(raw);
    if (Trim(line).empty()) {
      return;
    }
    const int indent = CountIndent(line);
    std::string text = Trim(line);

    while (!indents.empty() && indent <= indents.back()) {
      indents.pop_back();
      stack.pop_back();
    }

    if (text.find("- ") == 0) {
      return;
    }

    const std::size_t colon = text.find(':');
    if (colon == std::string::npos) {
      return;
    }
    const std::string key = Trim(text.substr(0, colon));
    const std::string value = Trim(text.substr(colon + 1));

    std::string full_key;
    for (const auto& part : stack) {
      if (!full_key.empty()) {
        full_key += ".";
      }
      full_key += part;
    }
    if (!full_key.empty()) {
      full_key += ".";
    }
    full_key += key;

    if (value.empty()) {
      stack.push_back(key);
      indents.push_back(indent);
    } else {
      values_[full_key] = StripQuotes(value);
    }
  }

  std::map<std::string, std::string> values_;
};

}  // namespace config
}  // namespace common
}  // namespace core
}  // namespace iotgw
