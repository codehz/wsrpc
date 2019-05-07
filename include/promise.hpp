#pragma once
#include <functional>
#include <type_traits>

template <typename T> class promise;

template <typename T> struct promise_ref { using type = T const &; };
template <typename T> struct promise_ref<promise<T>> { using type = T &&; };

template <typename T, typename R = void> struct void_fn { using type = std::function<R(T const &)>; };
template <typename R> struct void_fn<void, R> { using type = std::function<R()>; };
template <typename T, typename R = void> using void_fn_t = typename void_fn<T, R>::type;

template <typename T> struct unpromise;
template <typename T> struct unpromise<promise<T>> { using type = T; };
template <typename T> using unpromise_t = typename unpromise<T>::type;

template <typename T> struct is_promise { constexpr static auto value = false; };
template <typename T> struct is_promise<promise<T>> { constexpr static auto value = true; };
template <typename T> constexpr auto is_promise_v = is_promise<T>::value;

template <typename T> class promise {
  using then_fn                            = void_fn_t<T>;
  template <typename R> using transform_fn = void_fn_t<T, R>;
  using fail_fn                            = std::function<void(std::exception const &ex)>;

  then_fn _then;
  fail_fn _fail;
  std::function<void(then_fn, fail_fn)> body;

public:
  class resolver {
    then_fn _then;
    fail_fn _fail;
    resolver(then_fn _then, fail_fn _fail)
        : _then(_then)
        , _fail(_fail) {}

  public:
    std::enable_if_t<!std::is_void_v<T>> resolve(T const &value) { _then(value); }
    std::enable_if_t<std::is_void_v<T>> resolve() { _then(); }
    void reject(std::exception const &ex) { _fail(ex); }

    friend class promise;
  };
  promise(const promise &) = delete;
  promise &operator=(const promise &) = delete;
  promise(promise &&rhs)
      : body(rhs.body)
      , _then(rhs._then)
      , _fail(rhs._fail) {
    rhs.body = nullptr;
  }
  promise &operator=(promise &&rhs) {
    body     = rhs.body;
    rhs.body = nullptr;
    _then    = rhs._then;
    _fail    = rhs._fail;
  }
  promise(std::function<void(then_fn, fail_fn)> f)
      : body(f) {}
  promise(std::function<void(resolver)> f)
      : body([=](auto th, auto fn) {
        f({ th, fn });
      }){};
  ~promise() {
    if (body) {
      if constexpr (std::is_void_v<T>) {
        body(
            _then ?: []() {}, _fail ?: [](auto) {});
      } else {
        body(
            _then ?: [](T const &) {}, _fail ?: [](auto) {});
      }
    }
  }
  promise<T> &then(then_fn _then) {
    this->_then = _then;
    return *this;
  }
  template <typename R> promise<R> then(transform_fn<R> fn) {
    auto next = body;
    body      = nullptr;
    if constexpr (std::is_void_v<T>)
      return { [=](auto th, auto fa) { next([=]() { th(fn()); }, fa); } };
    else
      return { [=](auto th, auto fa) { next([=](T const &t) { th(fn(t)); }, fa); } };
  }
  promise<T> &fail(fail_fn) {
    this->_fail = _fail;
    return *this;
  }
  T force() {
    auto next = body;
    body      = nullptr;
    return { [=](auto th, auto fa) { next([=](T const &t) { const_cast<T &>(t).then(th).fail(fa); }, fa); } };
  }
};