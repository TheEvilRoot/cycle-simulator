#ifndef source_h_
#define source_h_

#include <variant>

struct ImmidiateSource {
  int value;
};

struct DirectSource {
  int reg;
};

struct IndirectSource {
  int addr;
};

typedef std::variant<DirectSource, IndirectSource, ImmidiateSource> Source;

#endif