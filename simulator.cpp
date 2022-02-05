#include <optional>
#include <stdexcept>
#include <array>
#include <chrono>

#include "utilities.h"
#include "source.h"
#include "instruction.h"
#include "event.h"

struct State {

  std::uint8_t regs[16]{0};
  std::uint8_t data[1024]{0};
  std::size_t clk{0};

  std::size_t fetch1{0};
  std::size_t fetch2{0};
  std::size_t exec{0};
  std::size_t writeback{0};
  std::size_t exceptions{0};

  static constexpr auto calculate(const BinaryInstruction& bi, const int op1, const int op2) {
    switch (bi.op) {
      case BinaryOperation::ADD: return op1 + op2;
      case BinaryOperation::SUB: return op1 - op2;
    }
    return 0;
  }

  constexpr auto get_value(const Source& source) {
    return std::visit(overloaded {
       [this](const DirectSource& s) {
         return static_cast<int>(regs[s.reg]);
       },
       [this](const IndirectSource& s) {
         return static_cast<int>(data[s.addr]);
       },
       [](const ImmidiateSource& s) {
         return s.value;
       }
    }, source);
  }

  constexpr auto put_value(const Source& source, int value) {
     std::visit(overloaded {
       [this, value](const DirectSource& s) {
         regs[s.reg] = value;
       },
       [this, value](const IndirectSource& s) {
         data[s.addr] = value;
       },
       [](const ImmidiateSource&) {
         throw std::logic_error{"putting into immidiate source is prohibited by logic" };
       }
      }, source);
  }

  constexpr auto get_writeback(const Writeback& wr, const Source& res_addr) {
    return std::visit(overloaded  {
        [this, &wr](const DirectSource& s) -> std::optional<ExecutionEvent> {
             data[s.reg] = wr.res;
             return std::optional<ExecutionEvent>{};
        },
        [&wr](const IndirectSource&) -> std::optional<ExecutionEvent> {
             return wr;
        },
        [](const ImmidiateSource&) -> std::optional<ExecutionEvent> {
             return Exception{"ImmidiateSource is prohibited as result source"};
        }
    }, res_addr);
  }

  constexpr auto fetch_direct_source(const Instruction&i, const int op1, const int op2) {
    return std::visit(overloaded {
        [op1](const UnaryInstruction& x) -> Writeback {
          return Writeback{x, op1};
        },
        [op1](const JumpInstruction& x) -> Writeback {
          return Writeback{x, op1};
        },
        [op1, op2](const BinaryInstruction& x) -> Writeback {
          return Writeback{x, calculate(x, op1, op2)};
        }
    }, i);
  }

  constexpr auto get_fetch2(const Op2Fetch& e) {
    const Source src = std::visit(overloaded {
      [](const BinaryInstruction& i) -> Source {
        return i.op2_addr;
      },
      [](const UnaryInstruction&) -> Source {
          return DirectSource{0};
      },
      [](const JumpInstruction&) -> Source {
          return DirectSource{0};
      }
    }, e.ins);
    const Source res = std::visit(overloaded {
        [](const BinaryInstruction& i) -> Source {
          return i.res_addr;
        },
        [](const UnaryInstruction& i) -> Source {
          return i.res_addr;
        },
        [](const JumpInstruction&) -> Source {
          return DirectSource{0};
        }
    }, e.ins);
    return std::visit(overloaded {
      [this, &e, &res](const DirectSource& s) -> std::optional<ExecutionEvent> {
        const Writeback wr = fetch_direct_source(e.ins, e.op1, regs[s.reg]);
        return get_writeback(wr, res);
      },
      [this, &e, &res](const ImmidiateSource& s) -> std::optional<ExecutionEvent> {
        const Writeback wr = fetch_direct_source(e.ins, e.op1, s.value);
        return get_writeback(wr, res);
      },
      [e](const IndirectSource&) -> std::optional<ExecutionEvent> {
        return e;
      }
    }, src);
  }

  constexpr auto get_fetch1(const Instruction& ins) {
    const auto src = std::visit(overloaded {
      [](const BinaryInstruction& i) {
        return i.op1_addr;
      },
      [](const UnaryInstruction& i) {
        return i.op1_addr;
      },
      [](const JumpInstruction& i) {
        return i.offset_addr;
      }
    }, ins);
    return std::visit(overloaded {
       [this, &ins](const DirectSource& s) -> std::optional<ExecutionEvent> {
           return get_fetch2(Op2Fetch{ins, regs[s.reg]});
       },
       [this, &ins](const ImmidiateSource& s) -> std::optional<ExecutionEvent> {
           return get_fetch2(Op2Fetch{ins, s.value});
       },
       [&ins](const IndirectSource&) -> std::optional<ExecutionEvent> {
         return Op1Fetch{ins};
       }
    }, src);
  }

  constexpr auto handle_event(const Op1Fetch& event) {
    fetch1++;
    return std::visit(overloaded {
      [this](const BinaryInstruction& i) -> std::optional<ExecutionEvent> {
          return get_fetch2(Op2Fetch{i, get_value(i.op1_addr)});
      },
      [this](const UnaryInstruction& i) -> std::optional<ExecutionEvent> {
          return Writeback{i, get_value(i.op1_addr)};
      },
      [this](const JumpInstruction& i) -> std::optional<ExecutionEvent> {
          return Writeback{i, get_value(i.offset_addr)};
      }
    }, event.ins);
  }

  constexpr auto handle_event(const Op2Fetch& event) {
    fetch2++;
    return std::visit(overloaded {
      [this, &event](const BinaryInstruction& i) -> std::optional<ExecutionEvent> {
          return Writeback{i, event.op1 +get_value(i.op2_addr)};
      },
      [](const UnaryInstruction&) -> std::optional<ExecutionEvent> {
          return Exception{std::string("UnaryInstruction pipelined Op2Fetch")};
      },
      [](const JumpInstruction&) -> std::optional<ExecutionEvent> {
          return Exception{std::string("JumpInstruction pipelined Op2Fetch")};
      }
    }, event.ins);
  }

  constexpr auto handle_event(const Execution& event) {
    exec++;
    return std::visit(overloaded {
      [&event] (const BinaryInstruction& i) -> std::optional<ExecutionEvent> {
          return Writeback{i, calculate(i, event.op1, event.op2)};
      },
      [&event](const UnaryInstruction& i) -> std::optional<ExecutionEvent>{
          return Writeback{i, event.op1};
      },
      [&event](const JumpInstruction& i) -> std::optional<ExecutionEvent>{
          return Writeback{i, event.op1};
      }
    }, event.ins);
  }

  constexpr auto handle_event(const Writeback& event) {
    writeback++;
    std::visit(overloaded {
      [this, &event] (const BinaryInstruction& i) {
         put_value(i.res_addr, event.res);
      },
      [this, &event](const UnaryInstruction& i) {
        put_value(i.res_addr, event.res);
      },
      [this, &event](const JumpInstruction&) {
        put_value(DirectSource{0}, event.res);
      }
    }, event.ins);
    return std::optional<ExecutionEvent> {};
  }

  constexpr auto handle_event(const Exception&) {
    exceptions++;
    return std::optional<ExecutionEvent>{};
  }

  constexpr void handle_event(const ExecutionEvent& event) {
    clk++;
    const auto next_event = std::visit(overloaded {
      [this](const auto& e) {
        return handle_event(e);
      }
    }, event);
    if (next_event)
      handle_event(next_event.value());
  }

  constexpr auto execute(const Instruction& i) {
    clk++; // instruction fetch+decode cycle
    const auto event = get_fetch1(i);
    if (event)
      handle_event(event.value());
  }

};

int main() {
  State state {};
  std::array<Instruction, 9> inss{
      BinaryInstruction { IndirectSource{3}, IndirectSource{4}, IndirectSource{5} , BinaryOperation::ADD},
    UnaryInstruction { ImmidiateSource{2}, DirectSource{2} },
    UnaryInstruction { DirectSource{1}, IndirectSource{1} },
    UnaryInstruction { DirectSource{2}, IndirectSource{2} },
    UnaryInstruction { IndirectSource{1}, IndirectSource{3} },
    UnaryInstruction { IndirectSource{2}, IndirectSource{4} },
    BinaryInstruction { IndirectSource{3}, IndirectSource{4}, IndirectSource{5} , BinaryOperation::ADD },
    JumpInstruction { IndirectSource{5} },
    UnaryInstruction { DirectSource{0}, DirectSource{1} }
  };

  const auto metrics = [&state](const auto& start, const std::size_t count, const bool full) {
    const auto end = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    const auto ipms = state.clk / (delta > 0 ? delta : 1);
    if (full) {
      fprintf(stderr, "approx. %llu khz\n", ipms);
      fprintf(stderr, "instructions executed: %ld\n", count);
      fprintf(stderr, "delta: %lld\n", delta);
      fprintf(stderr, "clk %zu\n", state.clk);
      fprintf(stderr, "fetch1: %lu\n", state.fetch1);
      fprintf(stderr, "fetch2: %lu\n", state.fetch2);
      fprintf(stderr, "exec: %lu\n", state.exec);
      fprintf(stderr, "writeback: %lu\n", state.writeback);
      fprintf(stderr, "exceptions: %lu\n", state.exceptions);
    } else [[likely]] {
      fprintf(stderr, "approx. %llu khz\r", ipms);
    }
  };

  const auto count = 4000000000;
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < count; i++) {
    state.execute(inss[i % inss.size()]);
    if (i % 10000 == 0) {
      metrics(start, i, false);
    }
  }
  metrics(start, count, true);
}
