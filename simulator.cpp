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

  auto get_value(const Source& source) {
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

  auto put_value(const Source& source, int value) {
     std::visit(overloaded {
       [this, value](const DirectSource& s) {
         regs[s.reg] = value;
       },
       [this, value](const IndirectSource& s) {
         data[s.addr] = value;
       },
       [](const ImmidiateSource& s) {
         throw std::logic_error{"putting into immidiate source is prohibited by logic" };
       }
      }, source);
  }

  auto get_fetch2(const Op2Fetch& e) {
    const Source src = std::visit(overloaded {
      [](const BinaryInstruction& i) -> Source {
        return i.op2_addr;
      },
      [](const UnaryInstruction& i) -> Source {
        return DirectSource(0);
      },
      [](const JumpInstruction& i) -> Source {
        return DirectSource(0);
      }
    }, e.ins);
    const auto event = std::visit(overloaded {
      [this, &e](const DirectSource& s) -> ExecutionEvent {
        return std::visit(overloaded {
          [&e](const UnaryInstruction& x) -> ExecutionEvent {
             return Writeback(x, e.op1);
          },
          [&e](const JumpInstruction& x) -> ExecutionEvent {
             return Writeback(x, e.op1);
          },
          [this, &e, &s](const auto& x) -> ExecutionEvent {
             return Execution(x, e.op1, regs[s.reg]);
          }
        }, e.ins);
      },
      [this, &e](const ImmidiateSource& s) -> ExecutionEvent {
      return std::visit(overloaded {
        [&e](const UnaryInstruction& x) -> ExecutionEvent {
            return Writeback(x, e.op1);
        },
        [&e](const JumpInstruction& x) -> ExecutionEvent {
            return Writeback(x, e.op1);
        },
        [this, &e, &s](const auto& x) -> ExecutionEvent {
            return Execution(x, e.op1, s.value);
        }
        }, e.ins);
    },
      [e](const IndirectSource& s) -> ExecutionEvent {
        return e;
      }
    }, src);
    return event;
  }

  auto get_fetch1(const Instruction& ins) {
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
    const auto event = std::visit(overloaded {
       [this, &ins](const DirectSource& s) -> ExecutionEvent {
          return get_fetch2(Op2Fetch(ins, regs[s.reg]));
       },
       [this, &ins](const ImmidiateSource& s) -> ExecutionEvent {
          return get_fetch2(Op2Fetch(ins, s.value));
       },
       [&ins](const IndirectSource& s) -> ExecutionEvent {
         return Op1Fetch(ins);
       }
    }, src);
    return event;
  }

  auto handle_event(const Op1Fetch& event) {
    const auto next_event = std::visit(overloaded {
      [this](const BinaryInstruction& i) -> std::optional<ExecutionEvent> {
         return get_fetch2(Op2Fetch(i, get_value(i.op1_addr)));
      },
      [this](const UnaryInstruction& i) -> std::optional<ExecutionEvent> {
         return Writeback(i, get_value(i.op1_addr));
      },
      [this](const JumpInstruction& i) -> std::optional<ExecutionEvent> {
         return Writeback(i, get_value(i.offset_addr));
      }
    }, event.ins);
    return next_event;
  }

  auto handle_event(const Op2Fetch& event) {
    const auto next_event = std::visit(overloaded {
      [this, &event](const BinaryInstruction& i) -> std::optional<ExecutionEvent> {
         return Execution(i, event.op1, get_value(i.op2_addr));
      },
      [this](const UnaryInstruction& i) -> std::optional<ExecutionEvent> {
         return Exception(std::string("UnaryInstruction pipelined Op2Fetch"));
      },
      [this](const JumpInstruction& i) -> std::optional<ExecutionEvent> {
         return Exception(std::string("JumpInstruction pipelined Op2Fetch"));
      }
    }, event.ins);
    return next_event;
  }

  auto handle_event(const Execution& event) {
    const auto next_event = std::visit(overloaded {
      [this, &event] (const BinaryInstruction& i) -> std::optional<ExecutionEvent> {
         return Writeback(i, event.op1 + event.op2);
      },
      [this, &event](const UnaryInstruction& i) -> std::optional<ExecutionEvent>{
         return Writeback(i, event.op1);
      },
      [this, &event](const JumpInstruction& i) -> std::optional<ExecutionEvent>{
         return Writeback(i, event.op1);
      }
    }, event.ins);
    return next_event;
  }

  auto handle_event(const Writeback& event) {
    std::visit(overloaded {
      [this, &event] (const BinaryInstruction& i) {
         put_value(i.res_addr, event.res);
      },
      [this, &event](const UnaryInstruction& i) {
        put_value(i.res_addr, event.res);
      },
      [this, &event](const JumpInstruction& i) {
        put_value(DirectSource(0), event.res);
      }
    }, event.ins);
    return std::optional<ExecutionEvent> {};
  }

  auto handle_event(const Exception& event) {
    fprintf(stderr, "Exception: %s\n", event.msg.c_str());
    return std::optional<ExecutionEvent>{};
  }

  void handle_event(const ExecutionEvent& event) {
    clk++;
    const auto next_event = std::visit(overloaded {
      [this](const auto& e) {
        return handle_event(e);
      }
    }, event);
    if (next_event)
      handle_event(next_event.value());
  }

  auto execute(const Instruction& i) {
    handle_event(get_fetch1(i));
  }

};


int main() {
  State state {};
  std::array<Instruction, 8> inss{
    UnaryInstruction { ImmidiateSource(2), DirectSource(2) },
    UnaryInstruction { DirectSource(1), IndirectSource(1) },
    UnaryInstruction { DirectSource(2), IndirectSource(2) },
    UnaryInstruction { IndirectSource(1), IndirectSource(3) },
    UnaryInstruction { IndirectSource(2), IndirectSource(4) },
    BinaryInstruction { IndirectSource(3), IndirectSource(4), IndirectSource(5) },
    JumpInstruction { IndirectSource(5) },
    UnaryInstruction { DirectSource(0), DirectSource(1) }
  };
  
  const auto count = 10000000;
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < count; i++) {
    state.execute(inss[i % inss.size()]);
  }
  const auto end = std::chrono::steady_clock::now();
  const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  const auto ipms = (float) (count * inss.size()) / ((float)delta / 1000.f);
  fprintf(stderr, "delta: %u\n", delta);
  fprintf(stderr, "approx. %f khz\n", ipms);
  fprintf(stderr, "clk %zu\n", state.clk);
}