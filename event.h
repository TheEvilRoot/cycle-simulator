#ifndef event_h_
#define event_h_

#include <variant>
#include <string>

struct Op1Fetch {
  const Instruction ins;
};

struct Op2Fetch {
  const Instruction ins;
  int op1;
};

struct Execution {
  const Instruction ins;
  int op1;
  int op2;
};

struct Writeback {
  const Instruction ins;
  int res;
};

struct Exception {
  const std::string_view msg;
};

typedef std::variant<Op1Fetch, Op2Fetch, Execution, Writeback, Exception> ExecutionEvent;

#endif
