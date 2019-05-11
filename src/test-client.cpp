#include <condition_variable>
#include <iostream>
#include <rpcws.hpp>
#include <thread>

int main() {
  using namespace rpcws;
  using namespace std::chrono_literals;

  try {
    static RPC::Client client(std::make_unique<client_wsio>("ws://127.0.0.1:16400/"));
    client.start()
        .then<promise<json>>([] {
          std::cout << "ready!" << std::endl;
          return client.call("test", json::array({ "test" }));
        })
        .then<promise<json>>([](json data) {
          std::cout << "recv: " << data.dump(2) << std::endl;
          return client.call("error", json::array({ "boom" }));
        })
        .then([&](json data) {
          std::cout << "recv(failed): " << data.dump(2) << std::endl;
          client.stop();
        })
        .fail([](auto ex) {
          try {
            if (ex) std::rethrow_exception(ex);
          } catch (RemoteException const &ex) { std::cout << ex.full << std::endl; } catch (std::exception const &ex) {
            std::cout << ex.what() << std::endl;
          }
        });

  } catch (std::runtime_error &e) { std::cerr << typeid(e).name() << e.what() << std::endl; }
}