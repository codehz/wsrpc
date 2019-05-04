#pragma once

#include <functional>
#include <json.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace rpc {
using json = nlohmann::json;

struct io {
  struct client;
  using recv_fn   = std::function<void(std::string_view)>;
  using fail_fn   = std::function<void(std::string_view)>;
  using accept_fn = std::function<void(std::unique_ptr<client>)>;
  struct client {
    virtual ~client(){};
    virtual void recv(recv_fn, fail_fn) = 0;
    virtual void send(std::string_view) = 0;
  };
  virtual ~io() {}
  virtual void accept(accept_fn, fail_fn) = 0;
};

class RPC {
  std::mutex mtx;
  std::unique_ptr<rpc::io> io;
  std::map<std::shared_ptr<rpc::io::client>, std::unique_ptr<std::thread>> clients;
  std::map<std::string, std::function<json(json)>> methods;
  std::vector<std::string> server_events;
  std::multimap<std::string_view, std::weak_ptr<rpc::io::client>> server_event_map;
  std::multimap<std::string, std::function<void(json)>> client_event_map;

public:
  RPC(decltype(io) &&io);
  ~RPC();

  RPC(const RPC &) = delete;
  RPC &operator=(const RPC &) = delete;

  void event(std::string_view);
  void emit(std::string_view, json data);
  void on(std::string_view, std::function<void(json)>);
  void off(std::string const &);
  void reg(std::string_view, std::function<json(json)>);
  void unreg(std::string const &);

  void start();

private:
  void incoming(std::shared_ptr<rpc::io::client>, std::string_view);
  void error(std::string_view);
  void error(std::shared_ptr<rpc::io::client>, std::string_view);
};

} // namespace rpc