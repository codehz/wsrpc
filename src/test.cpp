#include <rpcws.hpp>
#include <iostream>

int main() {
  using namespace rpcws;

  try {
    RPC instance{ std::make_unique<wsio>("ws://[::]:8898/") };
    instance.reg("test", [](auto client, json data) -> json { return data; });
    instance.start();
  } catch (std::runtime_error &e) {
    std::cerr << e.what() << std::endl;
  }
}