#pragma once

#include <string>
#include <string_view>
#include <tuple>
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
  Data<Output> host;
  Data<Output> origin;
  Data<Output> key;
  Data<Output> resource;
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
Data<Output> makeHandshakeAnswer(Handshake input);

Frame<Output> parseFrame(Data<Input>);
Data<Output> makeFrame(Frame<Input>);

template <typename io>
static constexpr Frame<io> NullHandshake = {FrameType::EMPTY_FRAME};

} // namespace ws