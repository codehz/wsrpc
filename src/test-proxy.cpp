#include <condition_variable>
#include <iostream>
#include <rpcws.hpp>

int main() {
  using namespace rpcws;
  using namespace std::chrono_literals;

  try {
    auto ep = std::make_shared<epoll>();
    static RPC::Client client(std::make_unique<client_wsio>("ws://127.0.0.1:16400/", ep));
    static RPC server(std::make_unique<server_wsio>("ws://127.0.0.1:16401/", ep));
    client.start()
        .then([] {
          server.reg("test", [](auto x, json data) -> promise<json> { return client.call("test", data); });
          server.reg("error", [](auto x, json data) -> promise<json> { return client.call("error", data); });
          server.reg(std::regex("^proxied\\.(\\S+)$"),
                     [](auto x, auto matched, json data) -> promise<json> { return client.call(matched[1].str(), data); });
        })
        .fail([&](auto ex) {
          try {
            if (ex) std::rethrow_exception(ex);
          } catch (RemoteException const &ex) { std::cout << ex.full << std::endl; } catch (std::exception const &ex) {
            std::cout << ex.what() << std::endl;
          }
          ep->shutdown();
        });
    client.layer().ondie([&] { ep->shutdown(); });
    server.start();
    ep->wait();
  } catch (std::runtime_error &e) { std::cerr << typeid(e).name() << e.what() << std::endl; }
}