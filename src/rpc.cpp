#include <exception>
#include <iostream>
#include <rpc.hpp>

namespace rpc {

RPC::RPC(decltype(io) &&io)
    : io(std::move(io)) {
  reg("rpc.on", [this](std::shared_ptr<rpc::io::client> client, json input) -> json {
    if (input.is_array()) {
      std::map<std::string, std::string> lists;
      for (auto item : input) {
        if (item.is_string()) {
          lists.emplace(item.get<std::string>(), "provided event invalid");
        } else
          throw InvalidParams{};
      }
      for (auto &[k, v] : lists) {
        auto it = std::find(server_events.begin(), server_events.end(), k);
        if (it != server_events.end()) {
          server_event_map[k].emplace(client);
          v = "ok";
        }
      }
      return lists;
    } else
      throw InvalidParams{};
  });
  reg("rpc.off", [this](std::shared_ptr<rpc::io::client> client, json input) -> json {
    if (input.is_array()) {
      std::map<std::string, std::string> lists;
      for (auto item : input) {
        if (item.is_string()) {
          lists.emplace(item.get<std::string>(), "provided event invalid");
        } else
          throw InvalidParams{};
      }
      for (auto &[k, v] : lists) {
        auto it = std::find(server_events.begin(), server_events.end(), k);
        if (it != server_events.end()) {
          if (server_event_map[k].erase(client) == 0)
            v = "not subscribed";
          else
            v = "ok";
        }
      }
      return lists;
    } else
      throw InvalidParams{};
  });
}

RPC::~RPC() {}

void RPC::event(std::string_view name) {
  std::lock_guard guard{ mtx };
  server_events.emplace_back(name);
}

void RPC::emit(std::string_view name, json data) {
  auto obj = json::object({ { "notification", name }, { "params", data } }).dump();
  std::lock_guard guard{ mtx };
  auto it = server_event_map.find(name);
  if (it != server_event_map.end()) {
    auto set = it->second;
    auto p = set.begin();
    auto end = set.end();
    while (p != end) {
      auto ptr = p->lock();
      if (ptr) {
        ptr->send(obj);
        ++p;
      } else {
        set.erase(p++);
      }
    }
  }
}

void RPC::reg(std::string_view name, std::function<json(std::shared_ptr<rpc::io::client>, json)> cb) {
  std::lock_guard guard{ mtx };
  methods.emplace(name, cb);
}

void RPC::unreg(std::string const &name) {
  std::lock_guard guard{ mtx };
  methods.erase(name);
}

void RPC::start() {
  io->accept([this](std::unique_ptr<rpc::io::client> client) {
    std::lock_guard guard{ mtx };
    std::shared_ptr ptr = std::move(client);
    clients.emplace(ptr, std::make_unique<std::thread>([this, ptr] {
                      ptr->recv([this, ptr](auto data) { incoming(ptr, data); });
                      std::lock_guard guard{ mtx };
                      clients.erase(ptr);
                    }));
  });
}

struct Invalid : std::runtime_error {
  Invalid(char const *msg)
      : runtime_error(msg) {}
};

struct NotFound : std::runtime_error {
  NotFound()
      : runtime_error("method not found") {}
};

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
      auto result = it->second(client, params);
      if (has_id) {
        auto ret = json::object({ { "jsonrpc", "2.0" }, { "result", result }, { "id", id } });
        client->send(ret.dump());
      }
    } catch (NotFound &e) {
      std::lock_guard guard{ mtx };
      auto err = json::object({ { "code", -32601 }, { "message", e.what() } });
      auto ret = json::object({ { "jsonrpc", "2.0" }, { "error", err }, { "id", nullptr } });
      if (has_id) ret["id"] = id;
      client->send(ret.dump());
    } catch (InvalidParams &e) {
      std::lock_guard guard{ mtx };
      auto err = json::object({ { "code", -32602 }, { "message", e.what() } });
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

} // namespace rpc