#include <csignal>
#include <iostream>
#include <memory>
#include <openssl/ssl.h>
#include <rpcws.hpp>

int main() {
  using namespace rpcws;

  try {
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
    static auto ep = std::make_shared<epoll>();
    auto ctx       = std::make_unique<ssl_context>("./cert.pem", "./priv.key");
    static RPC instance{ std::make_unique<server_wsio>(std::move(ctx), "wss://127.0.0.1:16443/", ep) };
    instance.reg("test", [](auto client, json data) -> json { return data; });
    instance.reg("error", [](auto client, json data) -> json { throw std::runtime_error("expected"); });
    instance.reg(std::regex("^\\S+$"), [](auto client, auto matched, json data) -> json {
      return json::object({
          { "name", matched[0].str() },
          { "data", data },
      });
    });
    signal(SIGINT, [](auto) {
      instance.stop();
      ep->shutdown();
    });
    instance.start();
    ep->wait();
  } catch (std::runtime_error &e) { std::cerr << e.what() << std::endl; }
}