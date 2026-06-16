#pragma once

#include "core/common/utils/json_utils.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace iotgw {
namespace core {
namespace control {

struct Condition {
  std::string sensor_id;
  std::string op;
  double value = 0.0;
};

struct Action {
  std::string type;
  std::string actuator_id;
  std::string value;
  std::string level;
  std::string message;
};

struct Rule {
  std::string id;
  std::string category;
  bool enabled = true;
  Condition when;
  std::vector<Action> then;
};

class RuleEngine {
 public:
  void Clear() { rules_.clear(); }
  const std::vector<Rule>& Rules() const { return rules_; }

  bool SetEnabled(const std::string& rule_id, bool enabled) {
    for (auto& rule : rules_) {
      if (rule.id == rule_id) {
        rule.enabled = enabled;
        return true;
      }
    }
    return false;
  }

  bool HasRule(const std::string& rule_id) const {
    return std::find_if(rules_.begin(), rules_.end(), [&](const Rule& rule) {
             return rule.id == rule_id;
           }) != rules_.end();
  }

  void AddRules(std::vector<Rule> rules) {
    rules_.insert(rules_.end(), rules.begin(), rules.end());
  }

  void OnSensorValue(const std::string& sensor_id,
                     double value,
                     const std::function<void(const Rule& rule,
                                              const Action& action)>& exec) {
    for (const auto& rule : rules_) {
      if (!rule.enabled || rule.when.sensor_id != sensor_id) {
        continue;
      }
      if (!Eval(rule.when, value)) {
        continue;
      }
      for (const auto& action : rule.then) {
        exec(rule, action);
      }
    }
  }

  std::string ToJson() const {
    namespace json = iotgw::core::common::json;
    std::vector<std::string> values;
    for (const auto& rule : rules_) {
      values.push_back(json::Object({
          {"id", json::Quote(rule.id)},
          {"category", json::Quote(rule.category)},
          {"enabled", json::Bool(rule.enabled)},
          {"sensor_id", json::Quote(rule.when.sensor_id)},
          {"op", json::Quote(rule.when.op)},
          {"value", json::Number(rule.when.value)},
      }));
    }
    return json::Array(values);
  }

 private:
  static std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    return text;
  }

  static bool Eval(const Condition& condition, double actual) {
    const std::string op = Lower(condition.op);
    if (op == ">") return actual > condition.value;
    if (op == ">=") return actual >= condition.value;
    if (op == "<") return actual < condition.value;
    if (op == "<=") return actual <= condition.value;
    if (op == "==" || op == "=") return actual == condition.value;
    if (op == "!=") return actual != condition.value;
    return false;
  }

  std::vector<Rule> rules_;
};

}  // namespace control
}  // namespace core
}  // namespace iotgw
