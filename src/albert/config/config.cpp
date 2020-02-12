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

  po::options_description global_options;
  global_options.add_options()
      ("help,h", "Display help message")
      ("config", po::value<std::vector<std::string>>(),
          "Config file where options may be specified (can be specified more than once)")
      ;
  all_options_->add(global_options);

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

      auto parsed2 = po::parse_config_file(ifs, *all_options_, true);
      po::store(parsed2, vm);
      for (const auto& o : parsed.options) {
        if (o.unregistered) {
          // Convert unrecognized config option to command line option format;
          for (auto &value : o.value) {
            remaining_args.push_back("--" + o.string_key);
            remaining_args.push_back(value);
          }
        }
      }

      // preserved global config option
      remaining_args.push_back("--config");
      remaining_args.push_back(config_fname);
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
Config::Config(Config &&rhs) noexcept :all_options_(std::move(rhs.all_options_)) { }

void throw_on_remaining_args(const std::vector<std::string> &rhs) {
  std::vector<std::string> args;
  for (size_t i = 1; i < rhs.size(); i++) {
    if (rhs[i] == "-h" || rhs[i] == "--help") {
      exit(0);
    } else if (rhs[i].starts_with("--config=") || rhs[i] == "--config") {
      // ignore --config arg
      i++;
      continue;
    }
    args.push_back(rhs[i]);
  }

  if (args.size() > 0) {
    LOG(error) << "Unrecognized options: ";
    std::stringstream ss;
    for (size_t i = 1; i < rhs.size(); i++) {
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
  return args;
}
}

