#include <csignal>
#include <iostream>
#include <rpcws.hpp>

int main() {
  using namespace rpcws;

  try {
    static auto ep = std::make_shared<epoll>();
    static RPC instance{ std::make_unique<server_wsio>("ws://127.0.0.1:16400/", ep) };
    instance.reg("test", [](auto client, json data) -> json { return data; });
    instance.reg("error", [](auto client, json data) -> json { throw std::runtime_error("expected"); });
    signal(SIGINT, [](auto) { instance.stop(); ep->shutdown(); });
    instance.start();
    ep->wait();
  } catch (std::runtime_error &e) { std::cerr << e.what() << std::endl; }
}