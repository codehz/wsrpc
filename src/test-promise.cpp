#include <iostream>
#include <promise.hpp>

template <typename T> promise<T> just(T x) {
  return promise<T>([=](auto resolver) { resolver.resolve(x); });
}
template <typename T> promise<void> print(T x) {
  return promise<void>([=](auto resolver) {
    std::cout << x << std::endl;
    resolver.resolve();
  });
}

template <typename T, typename X> promise<T> just_exception(X input) {
  return promise<T>([=](auto resolver) {
    try {
      std::cout << "!" << input << std::endl;
      throw std::runtime_error("expected");
    } catch (...) { resolver.reject(std::current_exception()); }
  });
}

int main() {
  just(5).then([](auto v) { std::cout << v << std::endl; });
  promise<int>::map_all(std::vector{ 1, 2 }, just<int>).then([](auto v) {
    for (auto i : v) { std::cout << i << std::endl; }
    std::cout << "done" << std::endl;
  });

  promise<void>::map_all(std::vector{ 3, 4 }, print<int>).then([] { std::cout << "done" << std::endl; });
  promise<int>::map_any(std::vector{ 1, 2 }, just<int>).then([](auto v) { std::cout << v << std::endl << "done" << std::endl; });
  promise<void>::map_any(std::vector{ 3, 4 }, print<int>).then([] { std::cout << "done" << std::endl; });

  auto print_ex = [](std::exception_ptr e) {
    try {
      if (e) std::rethrow_exception(e);
    } catch (std::exception &ex) { std::cout << ex.what() << std::endl; }
  };
  just_exception<int>(5).fail(print_ex);
  just_exception<void>(5).fail(print_ex);
  promise<int>::map_all(std::vector{ 1, 2 }, just_exception<int, int>).fail(print_ex);
  promise<void>::map_all(std::vector{ 3, 4 }, just_exception<void, int>).fail(print_ex);
  promise<int>::map_any(std::vector{ 1, 2 }, just_exception<int, int>).fail(print_ex);
  promise<void>::map_any(std::vector{ 3, 4 }, just_exception<void, int>).fail(print_ex);
}