#pragma once

#include "json.hpp"
#include "promise.hpp"
#include <functional>
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

enum struct message_type { TEXT, BINARY };

struct server_io {
  struct client;
  using recv_fn   = std::function<void(std::shared_ptr<client>, std::string_view, message_type type)>;
  using accept_fn = std::function<void(std::shared_ptr<client>)>;
  using remove_fn = std::function<void(std::shared_ptr<client>)>;
  struct client {
    inline virtual ~client(){};
    virtual void shutdown()                                                     = 0;
    virtual void send(std::string_view, message_type type = message_type::TEXT) = 0;
  };
  inline virtual ~server_io() {}
  virtual void shutdown()                            = 0;
  virtual void accept(accept_fn, remove_fn, recv_fn) = 0;
};

struct client_io {
  using recv_fn = std::function<void(std::string_view, message_type type)>;

  inline virtual ~client_io(){};
  virtual void shutdown()                                                     = 0;
  virtual void recv(recv_fn, promise<void>::resolver)                         = 0;
  virtual void send(std::string_view, message_type type = message_type::TEXT) = 0;
  virtual bool alive()                                                        = 0;
  virtual void ondie(std::function<void()>)                                   = 0;
};

template <class T> struct wptr_less_than {
  inline bool operator()(const std::weak_ptr<T> &lhs, const std::weak_ptr<T> &rhs) const {
    return lhs.expired() || (!rhs.expired() && lhs.lock() < rhs.lock());
  }
};

class RPC {
public:
  using client_handler = std::shared_ptr<server_io::client>;
  struct callback {
    virtual void on_accept(client_handler){};
    virtual void on_remove(client_handler){};
    virtual void on_binary(client_handler, std::string_view data){};
  };

private:
  using maybe_async_handler = std::variant<std::function<json(client_handler, json)>, std::function<promise<json>(client_handler, json)>>;
  using maybe_async_proxy_handler =
      std::variant<std::function<json(client_handler, std::smatch, json)>, std::function<promise<json>(client_handler, std::smatch, json)>>;
  using callback_ref_t = std::shared_ptr<callback>;
  std::recursive_mutex mtx;
  std::unique_ptr<server_io> io;
  std::map<std::string, maybe_async_handler> methods;
  std::vector<std::tuple<std::regex, maybe_async_proxy_handler, size_t>> proxied_methods;
  std::vector<std::string> server_events;
  std::map<std::string, std::set<std::weak_ptr<server_io::client>, wptr_less_than<server_io::client>>> server_event_map;
  callback_ref_t callback_ref;
  size_t unqid;

public:
  RPC(decltype(io) &&io, callback_ref_t handler = std::make_shared<callback>());
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
    struct callback {
      virtual void on_binary(std::string_view data){};
    };
    using data_fn        = std::function<void(json)>;
    using callback_ref_t = std::shared_ptr<callback>;

  private:
    std::recursive_mutex mtx;
    std::unique_ptr<client_io> io;
    std::map<std::string, data_fn> event_map;
    std::map<unsigned, promise<json>::resolver> regmap;
    callback_ref_t callback_ref;
    unsigned last_id;

  public:
    Client(decltype(io) &&io, callback_ref_t handler = std::make_shared<callback>());
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
    void incoming(std::string_view, message_type);
  };

private:
  void incoming(client_handler, std::string_view, message_type);
};

} // namespace rpc