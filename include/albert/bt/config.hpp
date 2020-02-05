#pragma once

#include <albert/config/config.hpp>

namespace albert::bt {

struct Config :public albert::config::Config {
  Config();

  void serialize(std::ostream &os) const override;

  std::string bind_ip = "0.0.0.0";
  uint16_t bind_port = 16667;

  size_t resolve_torrent_expiration_seconds = 30;

  std::string id;
};

}