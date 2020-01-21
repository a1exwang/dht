#include "routing_table.hpp"
#include "krpc.hpp"
#include "bencoding.hpp"
#include "transaction.hpp"

#include <cstdlib>
#include <iostream>
#include <boost/asio.hpp>

using boost::asio::ip::udp;

std::array<char, 65536> receive_buffer;

void send_cb(const boost::system::error_code &error, std::size_t bytes_transferred) {
  if (error || bytes_transferred <= 0) {
    std::cerr << "sendto failed: " << error.message() << std::endl;
  }
};


std::string create_query(
    transaction::TransactionManager &transaction_manager,
    const krpc::NodeID &self_node_id,
    krpc::Query &query) {

  transaction_manager.start([&query, self_node_id](transaction::Transaction &transaction) {
    transaction.method_name_ = query.method_name();
    query.set_transaction_id(transaction.id_);
  });
  std::stringstream ss;
  query.encode(ss, bencoding::EncodeMode::Bencoding);
//  query.encode(std::cout, bencoding::EncodeMode::JSON);
  return ss.str();
};

void send_find_node_query(
    transaction::TransactionManager &transaction_manager,
    udp::socket &socket,
    krpc::NodeID self_node_id,
    const krpc::NodeInfo &target) {

  auto find_node_query = std::make_shared<krpc::FindNodeQuery>(self_node_id, target.id());
  udp::endpoint ep{boost::asio::ip::make_address_v4(target.ip()), target.port()};
  socket.async_send_to(
      boost::asio::buffer(create_query(transaction_manager, self_node_id, *find_node_query)),
      ep,
      send_cb);
}


int main(int argc, char* argv[]) {
  uint16_t port = 16667;
  auto self_node_id = krpc::NodeID::random();
  std::vector<std::pair<std::string, std::string>> bootstrap_nodes = {
      {"router.utorrent.com", "6881"},
      {"router.bittorrent.com", "6881"},
      {"dht.transmissionbt.com", "6881"},
      {"dht.aelitis.com", "6881"},
  };
  transaction::TransactionManager transaction_manager;
  dht::RoutingTable routing_table(self_node_id);


  try {
    boost::asio::io_service io;
    udp::socket socket(io, udp::endpoint(udp::v4(), port));
    udp::endpoint sender_endpoint;

    std::function<void(const boost::system::error_code& error, std::size_t bytes_transferred)> cb =
        [&sender_endpoint, &socket, &cb, &transaction_manager, &routing_table](const boost::system::error_code& error, std::size_t bytes_transferred) {
          if (error || bytes_transferred <= 0) {
            std::cout << "receive failed: " << error.message() << std::endl;
            return;
          }

          // parse receive data into a Message
          std::stringstream ss(std::string(receive_buffer.data(), bytes_transferred));
          auto node = bencoding::Node::decode(ss);
          auto message = krpc::Message::decode(*node, [&transaction_manager](std::string id) -> std::string {
            std::string method_name;
            transaction_manager.end(id, [&method_name](const transaction::Transaction &transaction) {
              method_name = transaction.method_name_;
            });
            return method_name;
          });

          if (auto response = std::dynamic_pointer_cast<krpc::Response>(message); response) {
            if (auto find_node_response = std::dynamic_pointer_cast<krpc::FindNodeResponse>(response); find_node_response) {
              find_node_response->print_nodes();
              for (auto &target_node : find_node_response->nodes()) {
                dht::Entry entry(target_node.id(), target_node.ip(), target_node.port());
                routing_table.add_node(entry);
//                routing_table.encode(std::cout);
                routing_table.stat(std::cout);
              }
            } else if (auto ping_response = std::dynamic_pointer_cast<krpc::PingResponse>(response); ping_response) {
              std::cout << "received ping response" << std::endl;
            } else {
              std::cerr << "Warning! response type not supported" << std::endl;
            }
          } else if(auto query = std::dynamic_pointer_cast<krpc::Query>(message); query) {
            std::cout << "query received, ignored" << std::endl;
          }

          socket.async_receive_from(
              boost::asio::buffer(receive_buffer),
              sender_endpoint,
              cb
          );
        };


    socket.async_receive_from(
        boost::asio::buffer(receive_buffer),
        sender_endpoint,
        cb
    );

    for (const auto &item : bootstrap_nodes) {
      std::string node_host{}, node_port{};
      std::tie(node_host, node_port) = item;

      udp::resolver resolver(io);
      udp::endpoint ep;
      try {
        auto resolutions = resolver.resolve(udp::resolver::query(node_host, node_port));
        for (auto it = resolutions.begin(); it != resolutions.end(); it++) {
          if (it->endpoint().protocol() == udp::v4()) {
            ep = it->endpoint();
            break;
          }
        }
      } catch (std::exception &e) {
        std::cerr << "failed to resolve '" << node_host << ":" << node_port << ", skipping, reason: " <<  e.what() << std::endl;
        break;
      }

      auto ping_query = std::make_shared<krpc::PingQuery>(self_node_id);
      auto buf = create_query(transaction_manager, self_node_id, *ping_query);

      socket.async_send_to(
          boost::asio::buffer(buf),
          ep,
          send_cb);

      // bootstrap by finding self
      auto find_node_query = std::make_shared<krpc::FindNodeQuery>(self_node_id, self_node_id);
      buf = create_query(transaction_manager, self_node_id, *find_node_query);

      socket.async_send_to(
          boost::asio::buffer(buf),
          ep,
          send_cb);
    }

    boost::asio::steady_timer timer(io, boost::asio::chrono::seconds(5));

    std::function<void(const boost::system::error_code&)> timer_cb =
        [&timer_cb, &io, &socket, &routing_table, &self_node_id, &transaction_manager, &timer](const boost::system::error_code &e) {
          if (e) {
            std::cout << "Timer error: " << e.message() << std::endl;
            return;
          }

          timer.expires_at(timer.expiry() +
              boost::asio::chrono::seconds(5));

          timer.async_wait(timer_cb);
          if (!routing_table.is_full()) {
            std::cout << "sending find node query..." << std::endl;
            auto targets = routing_table.select_expand_route_targets();
            for (auto &target : targets) {
              send_find_node_query(
                  transaction_manager,
                  socket,
                  self_node_id,
                  krpc::NodeInfo{target.id(), target.ip(), target.port()}
              );
            }
          }
        };

    timer_cb(boost::system::error_code());

    io.run();
  } catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}

