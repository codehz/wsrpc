#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <type_traits>

namespace ws {

using byte = unsigned char;

enum class FrameType : byte {
  EMPTY_FRAME = 0xF0,
  ERROR_FRAME = 0xF1,
  INCOMPLETE_FRAME = 0xF2,
  TEXT_FRAME = 0x01,
  BINARY_FRAME = 0x02,
  PING_FRAME = 0x09,
  PONG_FRAME = 0x0A,
  OPENING_FRAME = 0xF3,
  CLOSING_FRAME = 0x08,
};

enum class State {
  STATE_OPENING,
  STATE_NORMAL,
  STATE_CLOSING,
};

struct Input;
struct Output;

template <typename io>
using Data = std::conditional_t<std::is_same_v<io, Input>, std::string_view,
                                std::string>;

struct Handshake {
  FrameType type;
  Data<Input> host;
  Data<Input> origin;
  Data<Input> key;
  Data<Input> resource;
  std::vector<Data<Input>> protocols;

  inline void reset() {
    host = origin = key = resource = {};
    std::vector<Data<Input>> empty;
    protocols.swap(empty);
  }
};

template <typename io = Output> struct Frame;

template <> struct Frame<Output> {
  FrameType type;
  uint64_t eaten;
  Data<Output> payload;
};

template <> struct Frame<Input> {
  FrameType type;
  Data<Input> payload;
};

Handshake parseHandshake(Data<Input> input);
Data<Output> makeHandshakeAnswer(Data<Input> key, Data<Input> protocol = {});

Frame<Output> parseFrame(Data<Input>);
Data<Output> makeFrame(Frame<Input>);
} // namespace ws