#include <exception>
#include <iostream>
#include <rpc.hpp>

namespace rpc {

RPC::RPC(decltype(io) &&io) { this->io = std::move(io); }

RPC::~RPC() {}

void RPC::event(std::string_view name) {
  std::lock_guard guard{ mtx };
  server_events.emplace_back(name);
}

void RPC::emit(std::string_view name, json data) {
  std::lock_guard guard{ mtx };
  auto [low, high] = server_event_map.equal_range(name);
  while (low != high) {
    auto ptr = low->second.lock();
    if (ptr) {
      ptr->send(data.dump());
      ++low;
    } else {
      server_event_map.erase(low++);
    }
  }
}

void RPC::on(std::string_view name, std::function<void(json)> cb) {
  std::lock_guard guard{ mtx };
  client_event_map.emplace(name, cb);
}

void RPC::off(std::string const &name) {
  std::lock_guard guard{ mtx };
  client_event_map.erase(name);
}

void RPC::reg(std::string_view name, std::function<json(json)> cb) {
  std::lock_guard guard{ mtx };
  methods.emplace(name, cb);
}

void RPC::unreg(std::string const &name) {
  std::lock_guard guard{ mtx };
  methods.erase(name);
}

void RPC::start() {
  io->accept(
      [this](std::unique_ptr<rpc::io::client> client) {
        std::lock_guard guard{ mtx };
        std::shared_ptr ptr = std::move(client);
        clients.emplace(ptr, std::make_unique<std::thread>([this, ptr] {
                          ptr->recv([this, ptr](auto data) { incoming(ptr, data); }, [this, ptr](auto err) { error(ptr, err); });
                          std::lock_guard guard{ mtx };
                          clients.erase(ptr);
                        }));
      },
      [this](auto err) { error(err); });
}

struct Invalid : std::exception {
  char const *msg;
  Invalid(char const *msg)
      : msg(msg) {}
  char const *what() { return msg; }
};

struct NotFound : std::exception {};

void RPC::incoming(std::shared_ptr<rpc::io::client> client, std::string_view data) {
  try {
    auto parsed = json::parse(data);
    if (!parsed.is_object()) throw Invalid{ "object required" };
    if (parsed["jsonrpc"] != "2.0") throw Invalid{ "jsonrpc version mismatch" };
    auto method_ = parsed["method"];
    auto params  = parsed["params"];
    auto has_id  = parsed.contains("id");
    auto id      = parsed["id"];
    if (!method_.is_string()) throw Invalid{ "method need to be a string" };
    if (!params.is_structured()) throw Invalid{ "params need to be a object or array" };
    if (has_id && !id.is_primitive()) throw Invalid{ "id need to be a primitive" };
    auto method = method_.get<std::string>();

    std::lock_guard guard{ mtx };
    auto it = methods.find(method);
    try {
      if (it == methods.end()) throw NotFound();
      auto result = it->second(params);
      if (has_id) {
        auto ret = json::object({ { "jsonrpc", "2.0" }, { "result", result }, { "id", id } });
        client->send(ret.dump());
      }
    } catch (NotFound &e) {
      std::lock_guard guard{ mtx };
      auto err = json::object({ { "code", -32601 }, { "message", "method not found" } });
      auto ret = json::object({ { "jsonrpc", "2.0" }, { "error", err }, { "id", nullptr } });
      if (has_id) ret["id"] = id;
      client->send(ret.dump());
    } catch (std::exception &e) {
      auto err = json::object({ { "code", -32000 }, { "message", e.what() } });
      auto ret = json::object({ { "jsonrpc", "2.0" }, { "error", err }, { "id", nullptr } });
      if (has_id) ret["id"] = id;
      client->send(ret.dump());
    }
  } catch (json::parse_error &e) {
    std::lock_guard guard{ mtx };
    auto err = json::object({ { "code", -32700 }, { "message", e.what() } });
    auto ret = json::object({ { "jsonrpc", "2.0" }, { "error", err }, { "id", nullptr } });
    client->send(ret.dump());
  } catch (Invalid &e) {
    std::lock_guard guard{ mtx };
    auto err = json::object({ { "code", -32600 }, { "message", e.what() } });
    auto ret = json::object({ { "jsonrpc", "2.0" }, { "error", err }, { "id", nullptr } });
    client->send(ret.dump());
  } catch (...) {
    std::lock_guard guard{ mtx };
    auto err = json::object({ { "code", -32000 }, { "message", "Unknown error" } });
    auto ret = json::object({ { "jsonrpc", "2.0" }, { "error", err }, { "id", nullptr } });
    client->send(ret.dump());
  }
}

void RPC::error(std::string_view data) { std::cerr << data << std::endl; }
void RPC::error(std::shared_ptr<rpc::io::client> client, std::string_view data) { std::cerr << data << std::endl; }

} // namespace rpc