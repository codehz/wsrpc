#pragma once

#include "epoll.hpp"
#include "rpc.hpp"
#include "ws.hpp"
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

#ifndef OPENSSL_ENABLED
#define OPENSSL_ENABLED 0
#endif

#if OPENSSL_ENABLED
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace rpcws {

using namespace rpc;
using namespace ws;

#if OPENSSL_ENABLED
struct SSLError : std::exception {
  unsigned long errcode;
  char buffer[1024];
  SSLError();
  SSLError(unsigned long errcode);
  const char *what() const noexcept override;
};
#endif

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

class HandshakeFailed : public std::runtime_error {
public:
  HandshakeFailed();
};

class InvalidFrame : public std::runtime_error {
public:
  InvalidFrame();
};

class Buffer {
  char *start     = nullptr;
  char *head      = nullptr;
  char *allocated = nullptr;

public:
  Buffer();
  char *allocate(size_t size);
  void eat(size_t size);
  void drop(size_t size);
  void reset();
  char *begin() const;
  char *end() const;
  size_t length() const;
  std::string_view view() const;
  operator std::string_view() const;
  ~Buffer();
};
#if OPENSSL_ENABLED
struct ssl_context {
  SSL_CTX *ctx;

  ssl_context(std::filesystem::path cert, std::filesystem::path priv);
  ssl_context();
  ssl_context(ssl_context const &) = delete;
  ssl_context(ssl_context &&)      = delete;
  ~ssl_context();
};
struct ssl_client {
  SSL *client;

  ssl_client(ssl_context const &sslctx, int fd, bool do_connect = false);
  ssl_client(ssl_client const &) = delete;
  ssl_client(ssl_client &&)      = delete;
  void shutdown();
  ~ssl_client();
};
#endif

struct server_wsio : server_io {
  using cancel_fn = std::function<void(int)>;

  struct client : server_io::client, std::enable_shared_from_this<client> {
    enum struct result { EMPTY, ACCEPT, STOPPED };

    client(int, std::string_view);
#if OPENSSL_ENABLED
    client(std::shared_ptr<ssl_client> ssl, int, std::string_view);
#endif
    ~client() override;
    void shutdown() override;
    void send(std::string_view) override;
    result handle(recv_fn const &);

  private:
#if OPENSSL_ENABLED
    std::shared_ptr<ssl_client> ssl;
#endif
    int fd = {};
    std::string_view path;
    State state    = {};
    FrameType type = {};
    Buffer buffer;
  };

  server_wsio(std::string_view address, std::shared_ptr<epoll> ep = std::make_shared<epoll>());
#if OPENSSL_ENABLED
  server_wsio(std::unique_ptr<ssl_context> context, std::string_view address, std::shared_ptr<epoll> ep = std::make_shared<epoll>());
#endif
  ~server_wsio() override;
  void accept(accept_fn, recv_fn rcv) override;
  void shutdown() override;

  inline epoll &handler() { return *ep; }

private:
  int fd;
  std::shared_ptr<epoll> ep;
  std::map<int, std::shared_ptr<client>> fdmap;
  std::string path;
#if OPENSSL_ENABLED
  std::shared_ptr<ssl_context> ssl;
#endif
};

struct client_wsio : client_io {
  client_wsio(std::string_view address, std::shared_ptr<epoll> ep = std::make_shared<epoll>());
#if OPENSSL_ENABLED
  client_wsio(std::unique_ptr<ssl_context> context, std::string_view address, std::shared_ptr<epoll> ep = std::make_shared<epoll>());
#endif
  ~client_wsio();
  void shutdown() override;
  void recv(recv_fn, promise<void>::resolver) override;
  void send(std::string_view) override;
  bool alive() override;
  void ondie(std::function<void()>) override;

  inline epoll &handler() { return *ep; }

private:
  int fd;
  std::vector<std::function<void()>> ondie_cbs;
  std::shared_ptr<epoll> ep;
  std::string path, key;
  Buffer buffer;
  State state = {};
#if OPENSSL_ENABLED
  std::shared_ptr<ssl_context> sslctx;
  std::shared_ptr<ssl_client> ssl;
#endif
};

} // namespace rpcws