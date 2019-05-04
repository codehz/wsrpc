#pragma once

#include "rpc.hpp"
#include "ws.hpp"
#include <exception>
#include <vector>

namespace rpcws {

using namespace rpc;
using namespace ws;

struct InvalidAddress : std::runtime_error {
  InvalidAddress();
};

struct InvalidSocketOp : std::runtime_error {
  InvalidSocketOp(char const *);
};

class CommonException : public std::runtime_error {
public:
  CommonException();
};

class RecvFailed : public CommonException {};
class SendFailed : public CommonException {};

struct wsio : io {
  struct client : io::client {
    client(int, std::string_view);
    ~client() override;
    void recv(recv_fn) override;
    void send(std::string_view) override;

  private:
    int fd;
    std::string_view path;
  };

  wsio(std::string_view address);
  ~wsio() override;
  void accept(accept_fn) override;
  void shutdown() override;

private:
  int fd;
  int family;
  std::string addr;
  std::string path;
};

} // namespace rpcws