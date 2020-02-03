#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <memory>

namespace boost::program_options {
class options_description;
class variables_map;
}

namespace albert::config {

void throw_on_remaining_args(const std::vector<std::string> &rhs);
std::vector<std::string> argv2args(int argc, const char *const *argv);

struct Config {
  Config() = default;
  Config(Config &&rhs) noexcept;
  virtual ~Config();
  virtual std::string usage(const std::string &argv0) const;
  virtual std::vector<std::string> from_command_line(std::vector<std::string> args);
  virtual void after_parse(boost::program_options::variables_map &vm) { }

  virtual void serialize(std::ostream &os) const = 0;
 protected:
  static std::string basename_(const std::string& p);
  static std::string make_usage_string_(const std::string& program_name, const boost::program_options::options_description& desc);

  // using a pointer to prevent the situation that we have to include boost header here.
  std::unique_ptr<boost::program_options::options_description> all_options_;
};

}
