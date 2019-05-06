#pragma once

#include <functional>
#include <json.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace rpc {
using json = nlohmann::json;

struct InvalidParams : std::runtime_error {
  inline InvalidParams()
      : runtime_error("invalid params") {}
};

struct io {
  struct client;
  using recv_fn   = std::function<void(std::shared_ptr<client>, std::string_view)>;
  using accept_fn = std::function<void(std::shared_ptr<client>)>;
  struct client {
    inline virtual ~client(){};
    virtual void shutdown()             = 0;
    virtual void send(std::string_view) = 0;
  };
  virtual ~io() {}
  virtual void accept(accept_fn, recv_fn) = 0;
  virtual void shutdown()                 = 0;
};

template <class T> struct wptr_less_than {
  inline bool operator()(const std::weak_ptr<T> &lhs, const std::weak_ptr<T> &rhs) const {
    return lhs.expired() || (!rhs.expired() && lhs.lock() < rhs.lock());
  }
};

class RPC {
  std::recursive_mutex mtx;
  std::unique_ptr<rpc::io> io;
  std::map<std::string, std::function<json(std::shared_ptr<rpc::io::client>, json)>> methods;
  std::vector<std::string> server_events;
  std::map<std::string, std::set<std::weak_ptr<rpc::io::client>, wptr_less_than<rpc::io::client>>> server_event_map;

public:
  RPC(decltype(io) &&io);
  ~RPC();

  RPC(const RPC &) = delete;
  RPC &operator=(const RPC &) = delete;

  void event(std::string_view);
  void emit(std::string const &, json data);
  void reg(std::string_view, std::function<json(std::shared_ptr<rpc::io::client>, json)>);
  void unreg(std::string const &);

  void start();
  void stop();

private:
  void incoming(std::shared_ptr<rpc::io::client>, std::string_view);
};

} // namespace rpc