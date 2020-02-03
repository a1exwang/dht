#include <albert/config/config.hpp>

#include <fstream>
#include <iterator>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/cmdline.hpp>
#include <boost/program_options.hpp>

#include <albert/log/log.hpp>

namespace albert::config {

namespace po = boost::program_options;

std::string Config::basename_(const std::string& p) {
#ifdef HAVE_BOOST_FILESYSTEM
  return boost::filesystem::path(p).stem().string();
#else
  size_t start = p.find_last_of('/');
  if(start == std::string::npos)
    start = 0;
  else
    ++start;
  return p.substr(start);
#endif
}

std::string Config::make_usage_string_(const std::string &program_name, const po::options_description &desc) {
  std::vector<std::string> parts;
  parts.emplace_back("Usage: ");
  parts.push_back(program_name);
  std::ostringstream oss;
  std::copy(
      parts.begin(),
      parts.end(),
      std::ostream_iterator<std::string>(oss, " "));
  oss << '\n' << desc;
  return oss.str();
}

std::string Config::usage(const std::string &argv0) const {
  return make_usage_string_(argv0, *all_options_);
}

std::vector<std::string> Config::from_command_line(std::vector<std::string> args) {
  po::variables_map vm;
  auto parsed = po::command_line_parser(args).
      options(*all_options_).
      allow_unregistered().
      run();
  po::store(parsed, vm);

  auto remaining_args = po::collect_unrecognized(
      parsed.options,
      po::collect_unrecognized_mode::include_positional);

  if(vm.count("help")) {
    LOG(info) << usage(args[0]);
    return {args[0], "--help"};
  }

  if(vm.count("config") > 0) {
    auto config_fnames = vm["config"].as<std::vector<std::string> >();

    for (auto &config_fname : config_fnames) {
      std::ifstream ifs(config_fname.c_str());

      if(ifs.fail()) {
        throw std::invalid_argument("Error opening config file: " + config_fname);
      }

      auto parsed2 = po::parse_config_file(ifs, *all_options_);
      auto remaining_args2 = po::collect_unrecognized(
          parsed2.options,
          po::collect_unrecognized_mode::include_positional);
      po::store(parsed2, vm);
      remaining_args.insert(remaining_args.end(), remaining_args2.begin(), remaining_args2.end());
    }
  }

  po::notify(vm);

  after_parse(vm);

  std::stringstream ss;
  serialize(ss);
  LOG(info) << ss.str();

  return remaining_args;
}

Config::~Config() { }
Config::Config(Config &&rhs) :all_options_(std::move(rhs.all_options_)) { }

void throw_on_remaining_args(const std::vector<std::string> &rhs) {
  std::vector<std::string> args;
  for (auto &item : rhs) {
    if (item == "-h" || item == "--help") {
      exit(0);
    }
    args.push_back(item);
  }

  if (args.size() > 1) {
    LOG(error) << "Unrecognized options: ";
    std::stringstream ss;
    for (int i = 1; i < rhs.size(); i++) {
      ss << rhs[i] << " ";
    }
    LOG(error) << ss.str();
    throw std::invalid_argument("unrecognized program options");
  }
}
std::vector<std::string> argv2args(int argc, const char *const *argv) {
  std::vector<std::string> args;
  for (int i = 0; i < argc; i++) {
    args.emplace_back(argv[i]);
  }
  return std::move(args);
}
}

