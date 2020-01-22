#include <condition_variable>
#include <iostream>
#include <rpcws.hpp>
#include <openssl/ssl.h>

int main() {
  using namespace rpcws;
  using namespace std::chrono_literals;

  try {
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
    auto ep = std::make_shared<epoll>();
    auto ctx       = std::make_unique<ssl_context>();
    static RPC::Client client(std::make_unique<client_wsio>(std::move(ctx), "wss://127.0.0.1:16443/", ep));
    client.start()
        .then<promise<json>>([] {
          std::cout << "ready!" << std::endl;
          return client.call("test", json::array({ "test" }));
        })
        .then<promise<json>>([](json data) {
          std::cout << "test: " << data.dump(2) << std::endl;
          return client.call("proxied.boom", json::array({ "boom" }));
        })
        .then<promise<json>>([](json data) {
          std::cout << "proxied.boom: " << data.dump(2) << std::endl;
          return client.call("error", json::array({ "boom" }));
        })
        .then([&](json data) {
          std::cout << "recv(failed): " << data.dump(2) << std::endl;
          client.stop();
          ep->shutdown();
        })
        .fail([&](auto ex) {
          try {
            if (ex) std::rethrow_exception(ex);
          } catch (RemoteException const &ex) { std::cout << ex.full << std::endl; } catch (std::exception const &ex) {
            std::cout << ex.what() << std::endl;
          }
          ep->shutdown();
        });
    ep->wait();
  } catch (std::runtime_error &e) { std::cerr << typeid(e).name() << e.what() << std::endl; }
}