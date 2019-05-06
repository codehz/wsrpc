#pragma once
#include <functional>
#include <type_traits>

template <typename T, typename R = void> struct _fn { using type = std::function<R(T const &)>; };

template <typename R> struct _fn<void, R> { using type = std::function<R()>; };

template <typename T> class promise {
  using then_fn                            = typename _fn<T>::type;
  template <typename R> using transform_fn = typename _fn<T, R>::type;
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
  promise(std::function<void(then_fn, fail_fn)> f)
      : body(f) {}
  promise(std::function<void(resolver)> f)
      : body([=](auto th, auto fn) {
        f({ th, fn });
      }){};
  ~promise() {}

  promise<T> &then(then_fn _then) {
    this->_then = _then;
    return *this;
  }
  template <typename R> promise<R> then(transform_fn<R> fn) {
    return { [=, next = body](auto th, auto fa) { next([=](T const &t) { th(fn(t)); }, fa); } };
  }
  promise<T> &fail(fail_fn) {
    this->_fail = _fail;
    return *this;
  }
  void operator()() {
    body(
        _then ?: [](auto...) {}, _fail ?: [](auto) {});
  }
};