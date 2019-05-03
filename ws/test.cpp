#include "ws.hpp"
#include <iostream>

static std::string t = R"a(GET / HTTP/1.1
Upgrade: websocket
Connection: Upgrade
Host: example.com
Origin: http://example.com
Sec-WebSocket-Key: sN9cRrP/n9NdMgdcy2VJFQ==
Sec-WebSocket-Version: 13

)a";

void replaceAll(std::string &str, const std::string &from,
                const std::string &to) {
  if (from.empty())
    return;
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length(); // In case 'to' contains 'from', like replacing
                              // 'x' with 'yx'
  }
}

int main() {
  using namespace ws;

  replaceAll(t, "\n", "\r\n");

  auto handshake = parseHandshake(t);
  std::cout << "resource: " << handshake.resource << std::endl;
  std::cout << "host: " << handshake.host << std::endl;
  std::cout << "origin: " << handshake.origin << std::endl;
  std::cout << "key: " << handshake.key << std::endl;
  std::cout << "answer: " << std::endl << makeHandshakeAnswer(handshake) << std::endl;
}