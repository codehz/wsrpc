#include "ws.hpp"
#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <exception>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

template <typename T> inline T check(T retcode, char const *str) {
  if (retcode == -1) {
    perror(str);
    exit(EXIT_FAILURE);
  }
  return retcode;
}

class Buffer {
  char *start     = nullptr;
  char *head      = nullptr;
  char *allocated = nullptr;

public:
  Buffer() {}

  char *allocate(size_t size) {
    if (start) {
      if (static_cast<size_t>(allocated - head) < size) {
        auto temp       = new char[allocated - start + size];
        auto nstart     = temp;
        auto nend       = temp + (head - start);
        auto nallocated = temp + (allocated - start) + size;
        delete[] start;
        start     = nstart;
        head      = nend;
        allocated = nallocated;
      }
    } else {
      start     = new char[size];
      head      = start;
      allocated = start + size;
    }
    return head;
  }

  void eat(size_t size) { head += size; }

  void drop(size_t size) {
    if (!size) return;
    if (size == static_cast<size_t>(head - start)) {
      head = start;
    } else {
      head -= size;
      memmove(start, start + size, head - start);
    }
  }

  void reset() {
    delete[] start;
    start = head = allocated = nullptr;
  }

  char *begin() const { return start; }

  char *end() const { return head; }

  size_t length() const { return head - start; }

  std::string_view view() const { return { start, length() }; }

  operator std::string_view() const { return view(); }

  ~Buffer() { delete[] start; }
};

class CommonException : public std::runtime_error {
public:
  CommonException()
      : runtime_error(strerror(errno)) {}
};

class RecvFailed : public CommonException {};
class SendFailed : public CommonException {};

void safeSend(int fd, std::string_view data) {
  while (!data.empty()) {
    auto sent = send(fd, &data[0], data.size(), MSG_NOSIGNAL);
    if (sent == -1 || sent == 0) throw SendFailed();
    data.remove_prefix(sent);
  }
}

struct AutoClose {
  int fd;
  ~AutoClose() { close(fd); }
};

void process(int fd) {
  using namespace ws;

  AutoClose ac{ fd };

  State state    = State::STATE_OPENING;
  FrameType type = FrameType::INCOMPLETE_FRAME;
  Frame<Output> oframe;
  Handshake hs;
  Buffer buffer;

  while (type == FrameType::INCOMPLETE_FRAME) {
    ssize_t readed = recv(fd, buffer.allocate(0xFFFF), 0xFFFF, 0);
    if (!readed) {
      if (errno != 0) throw RecvFailed();
      return;
    }
    buffer.eat(readed);

    if (state == State::STATE_OPENING) {
      hs   = parseHandshake(buffer);
      type = hs.type;
    } else {
    midpoint:
      oframe = parseFrame(buffer);
      type   = oframe.type;
    }

    if (type == FrameType::ERROR_FRAME) {
      if (state == State::STATE_OPENING) {
        std::ostringstream oss;
        oss << "HTTP/1.1 400 Bad Request\r\n"
            << "Sec-WebSocket-Version: 13\r\n\r\n";
        safeSend(fd, oss.str());
        return;
      } else {
        auto temp = makeFrame(Frame<Input>{ FrameType::CLOSING_FRAME });
        safeSend(fd, temp);
        state = State::STATE_CLOSING;
        type  = FrameType::INCOMPLETE_FRAME;
        buffer.reset();
      }
    }

    if (state == State::STATE_OPENING) {
      assert(type == FrameType::OPENING_FRAME);
      if (hs.resource != "/") {
        safeSend(fd, "HTTP/1.1 404 Not Found\r\n\r\n");
        return;
      }

      auto answer = makeHandshakeAnswer(hs.key);
      hs.reset();
      safeSend(fd, answer);
      state = State::STATE_NORMAL;
      type  = FrameType::INCOMPLETE_FRAME;
      buffer.drop(buffer.view().find("\r\n\r\n") + 4);
      if (buffer.length()) goto midpoint;
    } else {
      switch (type) {
      case FrameType::INCOMPLETE_FRAME: continue;
      case FrameType::CLOSING_FRAME:
        if (state != State::STATE_CLOSING) {
          auto temp = makeFrame({ FrameType::CLOSING_FRAME });
          safeSend(fd, temp);
        }
        return;
      case FrameType::PING_FRAME: safeSend(fd, makeFrame({ FrameType::PONG_FRAME })); break;
      case FrameType::TEXT_FRAME:
        std::cout << "recv: " << oframe.payload << std::endl;
        safeSend(fd, makeFrame({ FrameType::TEXT_FRAME, oframe.payload }));
        break;
      default: break;
      }
      type = FrameType::INCOMPLETE_FRAME;
      buffer.drop(oframe.eaten);
      if (buffer.length()) goto midpoint;
    }
  }
}

int main() {
  using namespace ws;

  constexpr auto port = 16400;

  auto server = socket(AF_INET, SOCK_STREAM, 0);
  check(server, "socket");

  int val = 1;

  check(setsockopt(server, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val)), "setsockopt");

  sockaddr_in local = {
    .sin_family = AF_INET,
    .sin_port   = htons(port),
    .sin_addr   = { .s_addr = INADDR_ANY },
  };

  check(bind(server, (sockaddr *)&local, sizeof(local)), "bind");
  check(listen(server, 1), "listen");

  std::cout << "Listen: " << port << std::endl;

  while (true) {
    char buffer[32]       = {};
    sockaddr_in remote    = {};
    socklen_t sockaddrlen = sizeof(remote);
    auto client           = check(accept(server, (sockaddr *)&remote, &sockaddrlen), "accept");
    std::cout << "connected: " << inet_ntop(AF_INET, &remote.sin_addr, buffer, 32) << std::endl;
    try {
      process(client);
    } catch (CommonException *e) { std::cerr << e->what() << std::endl; }
  }
}