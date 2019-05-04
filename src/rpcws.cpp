#include <netdb.h>
#include <rpcws.hpp>
#include <sstream>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

class Buffer {
  char *start     = nullptr;
  char *head      = nullptr;
  char *allocated = nullptr;

public:
  Buffer() {}

  char *allocate(size_t size) {
    if (start) {
      if (allocated - head < size) {
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
    if (size == head - start) {
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

namespace rpcws {

InvalidAddress::InvalidAddress()
    : std::runtime_error("") {}

InvalidSocketOp::InvalidSocketOp(char const *msg)
    : std::runtime_error(std::string(msg) + ": " + strerror(errno)) {}

CommonException::CommonException()
    : std::runtime_error(strerror(errno)) {}

void safeSend(int fd, std::string_view data) {
  while (!data.empty()) {
    auto sent = send(fd, &data[0], data.size(), MSG_NOSIGNAL);
    if (sent == -1 || sent == 0) throw SendFailed();
    data.remove_prefix(sent);
  }
}

bool starts_with(std::string_view &full, std::string_view part) {
  bool res = full.compare(0, part.length(), part) == 0;
  if (res) full.remove_prefix(part.length());
  return res;
}

std::string_view eat(std::string_view &full, std::size_t length) {
  auto ret = full.substr(0, length);
  full.remove_prefix(length);
  return ret;
}

wsio::wsio(std::string_view address) {
  if (starts_with(address, "ws://")) {
    auto end = address.find_first_of("[:/");
    if (end == std::string_view::npos) throw InvalidAddress();
    bool quoted = false;
    if (address[end] == '[') {
      quoted = true;
      end    = address.find(']');
      if (end == std::string_view::npos) throw InvalidAddress();
      end++;
    }
    auto host = eat(address, end);
    if (quoted) {
      host.remove_prefix(1);
      host.remove_suffix(1);
    }
    std::string_view port = "80";
    if (address[0] == ':') {
      address.remove_prefix(1);
      end = address.find('/');
      if (end == std::string_view::npos) throw InvalidAddress();
      port = eat(address, end);
    }
    path = eat(address, address.find_first_of("?#"));
    {
      std::string host_str{ host };
      std::string port_str{ port };
      addrinfo hints = {
        .ai_flags    = AI_PASSIVE,
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
      };
      addrinfo *list;
      auto ret = getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &list);
      if (ret != 0) throw InvalidAddress();
      fd               = socket(list->ai_family, list->ai_socktype, list->ai_protocol);
      std::string addr = { (char *)list->ai_addr, list->ai_addrlen };
      freeaddrinfo(list);
      if (fd == -1) throw InvalidSocketOp("socket");
      int yes = 1;
      ret     = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
      if (ret != 0) throw InvalidSocketOp("setsockopt");
      ret = bind(fd, (sockaddr *)&addr[0], addr.length());
      if (ret != 0) throw InvalidSocketOp("bind");
      ret = listen(fd, 0xFF);
      if (ret != 0) throw InvalidSocketOp("listen");
    }
  } else if (starts_with(address, "ws+unix://")) {
    std::string host{ address };
    if (host.length() >= 108) throw InvalidAddress();
    path = "/";
    {
      sockaddr_un addr = { .sun_family = AF_UNIX };
      memcpy(addr.sun_path, &host[0], host.length());
      fd = socket(AF_UNIX, SOCK_STREAM, 0);
      if (fd == -1) throw InvalidSocketOp("socket");
      unlink(host.c_str());
      auto ret = bind(fd, (sockaddr *)&addr, sizeof(sockaddr_un));
      if (ret != 0) throw InvalidSocketOp("bind");
      ret = listen(fd, 0xFF);
      if (ret != 0) throw InvalidSocketOp("listen");
    }
  } else
    throw InvalidAddress();
  ev = eventfd(0, 0);
  if (ev == -1) throw InvalidSocketOp("eventfd");
}

wsio::~wsio() {
  close(fd);
  close(ev);
}

struct AutoClose {
  int fd;
  ~AutoClose() { close(fd); }
};

void wsio::accept(accept_fn process) {
  int ep = epoll_create(1);
  if (ep == -1) throw InvalidSocketOp("epoll_create");
  AutoClose epc{ ep };
  {
    epoll_event event = { .events = EPOLLERR | EPOLLIN, .data = { .fd = ev } };
    epoll_ctl(ep, EPOLL_CTL_ADD, ev, &event);
  }
  {
    epoll_event event = { .events = EPOLLERR | EPOLLIN, .data = { .fd = fd } };
    epoll_ctl(ep, EPOLL_CTL_ADD, fd, &event);
  }
  while (true) {
    epoll_event event = {};

    auto ret = epoll_wait(ep, &event, 1, -1);
    if (ret > 0) {
      if (event.events & EPOLLERR) break;
      if (event.data.fd == ev) {
        uint64_t count;
        read(ev, &count, sizeof(count));
        break;
      }
      sockaddr_storage ad = {};
      socklen_t len       = sizeof(ad);
      auto remote         = ::accept(fd, (sockaddr *)&ad, &len);
      if (remote == -1) throw InvalidSocketOp("accept");
      process(std::make_unique<wsio::client>(remote, path));
    }
  }
}

void wsio::shutdown() {
  uint64_t one = 1;
  write(ev, &one, 8);
  ::shutdown(fd, SHUT_WR);
}

wsio::client::client(int fd, std::string_view path)
    : fd(fd)
    , path(path) {}

wsio::client::~client() { close(fd); }

void wsio::client::shutdown() {
  ::shutdown(fd, SHUT_WR);
  close(fd);
}

void wsio::client::recv(recv_fn process) {
  State state    = State::STATE_OPENING;
  FrameType type = FrameType::INCOMPLETE_FRAME;
  Frame<Output> oframe;
  Handshake hs;
  Buffer buffer;

  while (type == FrameType::INCOMPLETE_FRAME) {
    ssize_t readed = ::recv(fd, buffer.allocate(0xFFFF), 0xFFFF, 0);
    if (!readed) return;
    if (readed == -1) { throw RecvFailed(); }

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
      if (hs.resource != path) {
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
      case FrameType::TEXT_FRAME: process(oframe.payload); break;
      default: break;
      }
      type = FrameType::INCOMPLETE_FRAME;
      buffer.drop(oframe.eaten);
      if (buffer.length()) goto midpoint;
    }
  }
}

void wsio::client::send(std::string_view data) { safeSend(fd, makeFrame({ FrameType::TEXT_FRAME, data })); }

} // namespace rpcws