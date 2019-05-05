#include <csignal>
#include <iostream>
#include <rpcws.hpp>

int main() {
  using namespace rpcws;

  try {
    static RPC instance{ std::make_unique<wsio>("ws://127.0.0.1:8808/") };
    instance.reg("test", [](auto client, json data) -> json { return data; });
    signal(SIGINT, [](auto) { instance.stop(); });
    instance.start();
  } catch (std::runtime_error &e) { std::cerr << e.what() << std::endl; }
}