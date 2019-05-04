#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sha1.h>
#include <sstream>
#include <ws.hpp>

static constexpr auto rn = "\r\n";

uint64_t ntohll(uint64_t val) { return (((uint64_t)ntohl(val)) << 32) + ntohl(val >> 32); }

uint64_t htonll(uint64_t val) { return (((uint64_t)htonl(val)) << 32) + htonl(val >> 32); }

std::vector<std::string_view> split(std::string_view inp) {
  std::vector<std::string_view> output;
  size_t n = std::string_view::npos;
  while (!inp.empty()) {
    n = inp.find_first_not_of(" ");
    if (n == std::string_view::npos) break;
    inp.remove_prefix(n);
    n = inp.find_first_of(",");
    if (n == std::string_view::npos) break;
    output.emplace_back(inp.substr(0, n));
    inp.remove_prefix(n);
  }
  return output;
}

namespace ws {

inline size_t base64len(size_t n) { return (n + 2) / 3 * 4; }

Data<Output> base64(Data<Input> input) {
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

std::string_view eat(std::string_view &full, std::size_t length) {
  auto ret = full.substr(0, length);
  full.remove_prefix(length);
  return ret;
}

bool starts_with(std::string_view &full, std::string_view part) {
  bool res = full.compare(0, part.length(), part) == 0;
  if (res) full.remove_prefix(part.length());
  return res;
}

Handshake parseHandshake(Data<Input> input) {
  Handshake result{ FrameType::OPENING_FRAME };
  auto ending = input.rfind("\r\n\r\n");
  if (ending == std::string_view::npos) return { FrameType::INCOMPLETE_FRAME };
  input.remove_suffix(input.length() - ending - 2);
  if (!starts_with(input, "GET ") != 0) return { FrameType::ERROR_FRAME };
  auto res_end = input.find(' ');
  if (res_end == std::string_view::npos) return { FrameType::INCOMPLETE_FRAME };
  result.resource = eat(input, res_end);
  if (!starts_with(input, " HTTP/1.1\r\n")) return { FrameType::ERROR_FRAME };

  bool connectionFlag = false;
  bool upgradeFlag    = false;
  while (input.length() != 0) {
    auto end = input.find("\r\n");
    if (end == std::string_view::npos) return { FrameType::ERROR_FRAME };
    auto line = eat(input, end);
    input.remove_prefix(2);

    if (starts_with(line, "Host: ")) {
      result.host = line;
    } else if (starts_with(line, "Origin: ")) {
      result.origin = line;
    } else if (starts_with(line, "Sec-WebSocket-Protocol: ")) {
      result.protocols = split(line);
    } else if (starts_with(line, "Sec-WebSocket-Key: ")) {
      result.key = line;
    } else if (starts_with(line, "Sec-WebSocket-Version: ")) {
      auto version = line;
      if (version != "13") return { FrameType::ERROR_FRAME };
    } else if (starts_with(line, "Connection: ")) {
      if (line == "Upgrade")
        upgradeFlag = true;
      else
        return { FrameType::ERROR_FRAME };
    } else if (starts_with(line, "Upgrade: ")) {
      if (line == "websocket")
        connectionFlag = true;
      else
        return { FrameType::ERROR_FRAME };
    }
  }
  if (!connectionFlag || !upgradeFlag) return { FrameType::ERROR_FRAME };
  return result;
}

Data<Output> makeHandshakeAnswer(Data<Input> key, Data<Input> protocol) {
  static constexpr char secret[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::ostringstream oss;
  byte hash[20] = { 0 };
  SHA1_CTX ctx;
  SHA1Init(&ctx);
  SHA1Update(&ctx, (byte const *)&key[0], key.length());
  SHA1Update(&ctx, (byte const *)secret, 36);
  SHA1Final(hash, &ctx);
  auto reskey = base64(std::string_view{ (char const *)hash, 20 });
  oss << "HTTP/1.1 101 Switching Protocols" << rn;
  oss << "Upgrade: websocket" << rn;
  oss << "Connection: Upgrade" << rn;
  oss << "Sec-WebSocket-Accept: " << reskey << rn;
  if (!protocol.empty()) oss << "Sec-Websocket-Protocol: " << protocol << rn;
  oss << rn;
  return oss.str();
}

uint64_t getPayloadLength(Data<Input> input, char &extraBytes, FrameType &type) {
  uint64_t payloadLength = input[1] & 0x7f;
  extraBytes             = 0;
  if ((payloadLength == 0x7f && input.length() < 4) || (payloadLength == 0x7F && input.length() < 10)) {
    type = FrameType::INCOMPLETE_FRAME;
    return 0;
  }
  if (payloadLength == 0x7f && (input[3] & 0x80) != 0) {
    type = FrameType::ERROR_FRAME;
    return 0;
  }
  if (payloadLength == 0x7e) {
    uint16_t payloadLength16b = 0;
    extraBytes                = 2;
    memcpy(&payloadLength16b, &input[2], extraBytes);
    payloadLength = ntohs(payloadLength16b);
  } else if (payloadLength == 0x7F) {
    uint64_t payloadLength64b = 0;
    extraBytes                = 8;
    memcpy(&payloadLength64b, &input[2], extraBytes);
    payloadLength = ntohll(payloadLength64b);
  }
  return payloadLength;
}

Frame<Output> parseFrame(Data<Input> input) {
  if (input.length() < 2) return { FrameType::INCOMPLETE_FRAME };
  if ((input[0] & 0x70) != 0x0 || (input[0] & 0x80) != 0x80 || (input[1] & 0x80) != 0x80) return { FrameType::ERROR_FRAME };
  auto opcode = (FrameType)(input[0] & 0x0F);
  if (opcode == FrameType::TEXT_FRAME || opcode == FrameType::BINARY_FRAME || opcode == FrameType::CLOSING_FRAME || opcode == FrameType::PING_FRAME ||
      opcode == FrameType::PONG_FRAME) {
    char extraBytes    = 0;
    auto payloadLength = getPayloadLength(input, extraBytes, opcode);
    if (opcode != FrameType::ERROR_FRAME) {
      if (payloadLength + 6 + extraBytes > input.length()) return { FrameType::INCOMPLETE_FRAME };
      auto masking = input.substr(2 + extraBytes, 4);
      auto eaten   = input.length() - 6 - extraBytes;
      Frame<Output> frame{ opcode, eaten + 6 + extraBytes, std::string(input.substr(2 + extraBytes + 4, payloadLength)) };
      for (size_t i = 0; i < payloadLength; i++) frame.payload[i] ^= masking[i % 4];
      return frame;
    }
  }
  return { FrameType::ERROR_FRAME };
}

Data<Output> makeFrame(Frame<Input> frame) {
  std::ostringstream oss;
  oss << (char)((char)frame.type | 0x80);
  const auto dataLength = frame.payload.length();
  if (dataLength < 125) {
    oss << (char)dataLength;
  } else if (dataLength <= 0xFFFF) {
    oss << (char)126;
    uint16_t payloadLength16b = htons(dataLength);
    char buf[2];
    memcpy(buf, &payloadLength16b, 2);
    oss << std::string_view{ buf, 2 };
  } else {
    oss << (char)127;
    uint16_t payloadLength64b = htonll(dataLength);
    char buf[8];
    memcpy(buf, &payloadLength64b, 8);
    oss << std::string_view{ buf, 8 };
  }
  oss << frame.payload;
  return oss.str();
}

} // namespace ws