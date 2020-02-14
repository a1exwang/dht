#include <albert/signal/cancel_all_io_services.hpp>

#include <albert/log/log.hpp>


namespace albert::signal {
CancelAllIOServices::CancelAllIOServices(boost::asio::io_service &io, std::vector<boost::asio::io_service *> args)
    :io_services_(std::move(args)), signals_(io, SIGINT) {

  // Start an asynchronous wait for one of the signals to occur.
  signals_.async_wait([this](const boost::system::error_code& error, int signal_number) {
    if (error) {
      LOG(error) << "Failed to async wait signals '" << error.message() << "'";
      return;
    }
    LOG(info) << "Exiting due to signal " << signal_number;
    for (auto &io : io_services_) {
      io->stop();
    }
  });
}

CancelAllIOServices::CancelAllIOServices(boost::asio::io_service &io)
    :signals_(io, SIGINT) {

  // Start an asynchronous wait for one of the signals to occur.
  signals_.async_wait([this](const boost::system::error_code& error, int signal_number) {
    if (error) {
      LOG(error) << "Failed to async wait signals '" << error.message() << "'";
      return;
    }
    LOG(info) << "Exiting due to signal " << signal_number;
    for (auto &io : io_services_) {
      io->stop();
    }
  });
}
}