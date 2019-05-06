#include <exception>
#include <iostream>
#include <rpc.hpp>

namespace rpc {

RPC::RPC(decltype(io) &&io)
    : io(std::move(io)) {
  reg("rpc.on", [this](std::shared_ptr<server_io::client> client, json input) -> json {
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
  reg("rpc.off", [this](std::shared_ptr<server_io::client> client, json input) -> json {
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

void RPC::emit(std::string const &name, json data) {
  auto obj = json::object({ { "notification", name }, { "params", data } }).dump();
  std::lock_guard guard{ mtx };
  auto it = server_event_map.find(name);
  if (it != server_event_map.end()) {
    auto set = it->second;
    auto p   = set.begin();
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

void RPC::reg(std::string_view name, std::function<json(std::shared_ptr<server_io::client>, json)> cb) {
  std::lock_guard guard{ mtx };
  methods.emplace(name, cb);
}

void RPC::unreg(std::string const &name) {
  std::lock_guard guard{ mtx };
  methods.erase(name);
}

void RPC::start() {
  io->accept([](auto...) {}, [this](auto... x) { incoming(x...); });
}

void RPC::stop() { io->shutdown(); }

struct Invalid : std::runtime_error {
  Invalid(char const *msg)
      : runtime_error(msg) {}
};

struct NotFound : std::runtime_error {
  NotFound()
      : runtime_error("method not found") {}
};

void RPC::incoming(std::shared_ptr<server_io::client> client, std::string_view data) {
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

RPC::Client::Client(std::unique_ptr<client_io> &&io)
    : io(std::move(io)) {}

RPC::Client::~Client() { io->shutdown(); }

promise<json> RPC::Client::call(std::string_view name, json data) {
  return { [=](auto resolver) {
    auto req = json::object({ { "jsonrpc", "2.0" }, { "method", name }, { "params", data }, { "id", last_id } });
    {
      std::lock_guard guard{ mtx };
      regmap.emplace(last_id++, resolver);
    }
    io->send(req.dump());
  } };
}

void RPC::Client::notify(std::string_view name, json data) {
  auto req = json::object({ { "jsonrpc", "2.0" }, { "method", name }, { "params", data } });
  io->send(req.dump());
}

promise<bool> RPC::Client::on(std::string_view name, RPC::Client::data_fn list) {
  event_map.emplace(name, list);
  return call("rpc.on", json::array({ "name" })).then<bool>([](json ret) { return ret.is_array() && ret.size() == 1 && ret[0] == "ok"; });
}

promise<bool> RPC::Client::off(std::string const &name) {
  event_map.erase(name);
  return call("rpc.off", json::array({ "name" })).then<bool>([](json ret) { return ret.is_array() && ret.size() == 1 && ret[0] == "ok"; });
}

void RPC::Client::incoming(std::string_view data) {
  try {
    auto parsed = json::parse(data);
    if (!parsed.is_object()) throw Invalid{ "object required" };
    if (parsed.contains("notification")) {
      auto name = parsed["notification"].get<std::string>();
      std::lock_guard guard{ mtx };
      if (auto it = event_map.find(name); it != event_map.end()) {
        auto &[k, fn] = *it;
        fn(parsed["params"]);
      }
    } else {
      if (parsed["jsonrpc"] != "2.0") throw Invalid{ "jsonrpc version mismatch" };
      auto result = parsed["result"];
      auto error  = parsed["error"];
      auto id     = parsed["id"];

      std::lock_guard guard{ mtx };
      if (auto it = regmap.find(id.get<unsigned>()); it != regmap.end()) {
        auto &[_, resolver] = *it;
        if (error.is_object()) {
          resolver.reject(RemoteException{ error });
        } else {
          resolver.resolve(result);
        }
        regmap.erase(it);
      }
    }
  } catch (std::exception &e) {
    std::cerr << e.what() << std::endl;
    std::lock_guard guard{ mtx };
    io->shutdown();
  }
}

promise<void> RPC::Client::start() {
  return { [this](auto resolver) { this->io->recv([=](auto data) { incoming(data); }, resolver); } };
}

void RPC::Client::stop() { this->io->shutdown(); }

} // namespace rpc
