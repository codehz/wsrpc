#pragma once

#include "promise.hpp"
#include <functional>
#include "json.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rpc {
using json = nlohmann::json;

struct InvalidParams : std::runtime_error {
  inline InvalidParams()
      : runtime_error("invalid params") {}
};

struct RemoteException : std::runtime_error {
  int code;
  json full;
  inline RemoteException(json ex)
      : runtime_error(ex["message"].get<std::string>())
      , code(ex["code"].get<int>())
      , full(ex) {}
};

struct server_io {
  struct client;
  using recv_fn   = std::function<void(std::shared_ptr<client>, std::string_view)>;
  using accept_fn = std::function<void(std::shared_ptr<client>)>;
  struct client {
    inline virtual ~client(){};
    virtual void shutdown()             = 0;
    virtual void send(std::string_view) = 0;
  };
  inline virtual ~server_io() {}
  virtual void shutdown()                 = 0;
  virtual void accept(accept_fn, recv_fn) = 0;
};

struct client_io {
  using recv_fn = std::function<void(std::string_view)>;

  inline virtual ~client_io(){};
  virtual void shutdown()                             = 0;
  virtual void recv(recv_fn, promise<void>::resolver) = 0;
  virtual void send(std::string_view)                 = 0;
  virtual bool alive()                                = 0;
  virtual void ondie(std::function<void()>)           = 0;
};

template <class T> struct wptr_less_than {
  inline bool operator()(const std::weak_ptr<T> &lhs, const std::weak_ptr<T> &rhs) const {
    return lhs.expired() || (!rhs.expired() && lhs.lock() < rhs.lock());
  }
};

class RPC {
  using maybe_async_handler       = std::variant<std::function<json(std::shared_ptr<server_io::client>, json)>,
                                           std::function<promise<json>(std::shared_ptr<server_io::client>, json)>>;
  using maybe_async_proxy_handler = std::variant<std::function<json(std::shared_ptr<server_io::client>, std::smatch, json)>,
                                                 std::function<promise<json>(std::shared_ptr<server_io::client>, std::smatch, json)>>;
  std::recursive_mutex mtx;
  std::unique_ptr<server_io> io;
  std::map<std::string, maybe_async_handler> methods;
  std::vector<std::tuple<std::regex, maybe_async_proxy_handler, size_t>> proxied_methods;
  std::vector<std::string> server_events;
  std::map<std::string, std::set<std::weak_ptr<server_io::client>, wptr_less_than<server_io::client>>> server_event_map;
  size_t unqid;

public:
  RPC(decltype(io) &&io);
  ~RPC();

  RPC(const RPC &) = delete;
  RPC &operator=(const RPC &) = delete;

  void event(std::string_view);
  void emit(std::string const &, json data);
  void reg(std::string_view, maybe_async_handler);
  size_t reg(std::regex, maybe_async_proxy_handler);
  void unreg(std::string const &);
  void unreg(size_t);

  void start();
  void stop();

  template <typename T = server_io> inline T &layer() { return dynamic_cast<T &>(*io); }

  class Client {
  public:
    using data_fn = std::function<void(json)>;

  private:
    std::recursive_mutex mtx;
    std::unique_ptr<client_io> io;
    std::map<std::string, data_fn> event_map;
    std::map<unsigned, promise<json>::resolver> regmap;
    unsigned last_id;

  public:
    Client(decltype(io) &&io);
    ~Client();

    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;

    promise<json> call(std::string const &name, json data);
    void notify(std::string_view name, json data);
    promise<bool> on(std::string_view name, data_fn);
    promise<bool> off(std::string const &name);

    promise<void> start();
    void stop();

    template <typename T = client_io> inline T &layer() { return dynamic_cast<T &>(*io); }

  private:
    void incoming(std::string_view);
  };

private:
  void incoming(std::shared_ptr<server_io::client>, std::string_view);
};

} // namespace rpc