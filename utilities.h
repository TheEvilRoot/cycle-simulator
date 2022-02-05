#ifndef utilities_h_
#define utilities_h_

#include <concepts>
#include <type_traits>
#include <cstdio>

template<typename T>
concept BooleanConvertible = std::is_convertible_v<T, bool>;

// helper type for the visitor #4
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
// explicit deduction guide (not needed as of C++20)
template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;

template<typename T>
inline auto hexdump(const T* data, const std::size_t size) {
  for (std::size_t i = 0; i < size; i++) {
    if (i > 0 && i % 16 == 0) fprintf(stderr, "\n");
    if (i > 0 && i % 2 == 0) fprintf(stderr, " ");
    fprintf(stderr, "%02x", data[i]);
  }
  fprintf(stderr, "\n");
}

#endif