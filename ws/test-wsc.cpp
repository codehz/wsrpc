#include "ws.hpp"
#include <arpa/inet.h>
#include <experimental/random>
#include <thread>
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

std::string base64(std::string_view input) {
  constexpr char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;

  int val = 0, valb = -6;
  for (unsigned char c : input) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(t[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) out.push_back(t[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

void safeSend(int fd, std::string_view data) {
  while (!data.empty()) {
    auto sent = send(fd, &data[0], data.size(), MSG_NOSIGNAL);
    if (sent == -1 || sent == 0) throw SendFailed();
    data.remove_prefix(sent);
  }
}

int main() {
  using namespace ws;
  constexpr auto port = 16400;
  auto client         = socket(AF_INET, SOCK_STREAM, 0);
  check(client, "socket");

  sockaddr_in local = {
    .sin_family = AF_INET,
    .sin_port   = htons(port),
    .sin_addr   = { .s_addr = htonl(INADDR_LOOPBACK) },
  };
  std::cout << "connecting..." << std::endl;
  check(connect(client, (sockaddr *)&local, sizeof(sockaddr_in)), "connect");
  std::cout << "connected, start websocket" << std::endl;

  union {
    uint64_t u2[2] = { std::experimental::randint(0ul, UINT64_MAX), std::experimental::randint(0ul, UINT64_MAX) };
    char b16[16];
  };

  auto key = base64(b16);

  {
    auto handshake = makeHandshake({
        .type     = FrameType::OPENING_FRAME,
        .host     = "127.0.0.1",
        .origin   = "127.0.0.1",
        .key      = key,
        .resource = "/",
    });
    check(send(client, handshake.c_str(), handshake.length(), 0), "send");
  }

  std::thread worker{ [&] {
    Buffer buffer;
    State state    = State::STATE_OPENING;
    FrameType type = FrameType::INCOMPLETE_FRAME;
    Frame<Input> oframe;
    while (type == FrameType::INCOMPLETE_FRAME) {
      auto readed = recv(client, buffer.allocate(0xFFFF), 0xFFFF, 0);
      if (!readed) {
        if (errno != 0) throw RecvFailed();
        return;
      }
      buffer.eat(readed);

      if (state == State::STATE_OPENING) {
        if (parseHandshakeAnswer(buffer, key).empty()) { break; }
        state = State::STATE_NORMAL;
        buffer.reset();
        continue;
      } else {
      midpoint:
        oframe = parseServerFrame(buffer);
        type   = oframe.type;
      }

      switch (type) {
      case FrameType::ERROR_FRAME:
      case FrameType::CLOSING_FRAME: goto out;
      case FrameType::PING_FRAME: safeSend(client, makeFrame({ FrameType::PONG_FRAME }, true)); break;
      case FrameType::TEXT_FRAME: std::cout << "recv: " << oframe.payload << std::endl; break;
      default: break;
      }
      type = FrameType::INCOMPLETE_FRAME;
      buffer.drop(oframe.eaten);
      if (buffer.length()) goto midpoint;
    }
  out:
    shutdown(client, SHUT_RDWR);
    close(client);
    return;
  } };

  std::string line;
  while (std::getline(std::cin, line)) {
    auto packet = makeFrame({ FrameType::TEXT_FRAME, line }, true);
    check(send(client, packet.c_str(), packet.length(), 0), "send");
    std::cout << "sent: " << line << std::endl;
  }
  shutdown(client, SHUT_RDWR);
  close(client);
}