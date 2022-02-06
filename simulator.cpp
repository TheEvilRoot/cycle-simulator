#include <optional>
#include <stdexcept>
#include <array>
#include <chrono>

#include "utilities.h"
#include "source.h"
#include "instruction.h"
#include "event.h"

struct State {

  // Memory
  
  /// <summary>
  /// Registers are available in CPU and doesn't take an extra cycle to access.
  /// </summary>
  std::uint8_t regs[16]{0};
  /// <summary>
  /// RAM access is takes an extra cycle to access.
  /// </summary>
  std::uint8_t data[1024]{0};

  // Cycle counter
  std::size_t clk{0};

  // Metrics
  std::size_t fetch1{0};
  std::size_t fetch2{0};
  std::size_t exec{0};
  std::size_t writeback{0};
  std::size_t exceptions{0};

  /// <summary>
  /// Unwrap source into a raw value.
  /// </summary>
  /// <param name="source">
  /// Source to unwrap.
  /// DirectSource will read from register.
  /// IndirectSource will read from memory.
  /// ImmidiateSource will just return a immidiate value.
  /// </param>
  /// <returns>
  /// Unwarpped value.
  /// </returns>
  constexpr auto read_value_from_source(const Source& source) {
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

  /// <summary>
  /// Unwrap source and write value to referenced pointer.
  /// </summary>
  /// <param name="source">
  /// Source to unwrap and write value.
  /// DirectSource will write to registers.
  /// IndirectSource will write to memory.
  /// ImmidiateSource will throw an exception because writing to immidiate is quite a strange thing to do.
  /// </param>
  /// <param name="value">
  /// Value to write.
  /// </param>
  /// <returns>
  /// Nothing.
  /// </returns>
  constexpr auto put_value_to_source(const Source& source, int value) {
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

  /// <summary>
  /// Execute writeback on current cycle or return
  /// event for a next cycle.
  /// Writeback might be executed on this cycle when result is stored 
  /// in registers (DirectSoruce). 
  /// Writeback to memoryr (IndirectSource) will take an extra cycle
  /// to execute, so will return a Writeback event.
  /// </summary>
  /// <param name="wr">
  /// Writeback event to perform on this or next cycle.
  /// </param>
  /// <param name="res_addr">
  /// Source where result is stored.
  /// </param>
  /// <returns>
  /// Optional ExecutionEvent to execute on next cycle.
  /// If writeback is executed on this cycle, will return an empty optional.
  /// </returns>
  constexpr auto get_writeback(const Writeback& wr, const Source& res_addr) {
    return std::visit(overloaded  {
        [this, &wr](const DirectSource& s) -> std::optional<ExecutionEvent> {
             put_value_to_source(s, wr.res);
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

  /// <summary>
  /// Calculate value on this cycle and return writeback event for next cycle.
  /// </summary>
  /// <param name="i">
  /// Instruction to execute.
  /// </param>
  /// <param name="op1">
  /// First operand value.
  /// </param>
  /// <param name="op2">
  /// Second operand value.
  /// </param>
  /// <returns>
  /// Writeback event to execute on next cycle 
  /// with calculated value.
  /// </returns>
  constexpr auto calculate_value(const Instruction&i, const int op1, const int op2) {
    const auto value = std::visit(overloaded {
        [op1](const UnaryInstruction& x) {
          return UnaryInstruction::calculate(x, op1);
        },
        [op1, op2](const JumpInstruction& x) {
          return JumpInstruction::calculate(x, op1 + op2);
        },
        [op1, op2](const BinaryInstruction& x) {
          return BinaryInstruction::calculate(x, op1, op2);
        }
    }, i);
    return Writeback {i, value};
  }

  /// <summary>
  /// Fetch second operand for an instruction on this cycle
  /// if it's available. Return next cycle event to execute.
  /// Might return Writeback event when opearand is available on this cycle
  /// and could be calculated on this cycle and result should be stored on next event.
  /// Might return empty optional when writeback when operand is available on this cycle
  /// and could be calculated on this cycle and result could be stored on this cycle.
  /// Might return Op2Fetch event when operand is not available on this cycle and will take an 
  /// extra cycle to fetch.
  /// </summary>
  /// <param name="e">
  /// Op2Fetch event.
  /// </param>
  /// <returns>
  /// Optional next cycle event.
  /// Op2Fetch, Writeback or empty optional
  /// </returns>
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
        const auto wr = calculate_value(e.ins, e.op1, regs[s.reg]);
        return get_writeback(wr, res);
      },
      [this, &e, &res](const ImmidiateSource& s) -> std::optional<ExecutionEvent> {
        const auto wr = calculate_value(e.ins, e.op1, s.value);
        return get_writeback(wr, res);
      },
      [e](const IndirectSource&) -> std::optional<ExecutionEvent> {
        return e;
      }
    }, src);
  }

  /// <summary>
  /// Fetch first operand on this cycle if it's available.
  /// Might return Op1Fetch when first operand is not available.
  /// Might return Op2Fetch if first operand is available on this cycle
  /// but second operand is not.
  /// Might return Writeback if both operands are available on this cycle
  /// but result couldn't be stored on this cycle.
  /// Might return empty optional if both operands are available on this cycle
  /// and result could be stored on this cycle.
  /// </summary>
  /// <param name="ins">
  /// Instruction to fetch first operand.
  /// </param>
  /// <returns>
  /// Optional event for next cycle.
  /// Op1Fetch, Op2Fetch, Writeback or empty optional.
  /// </returns>
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

  /// <summary>
  /// Execute Op1Fetch on this cycle and return next cycle event.
  /// </summary>
  /// <param name="event">
  /// Op1Fetch event.
  /// </param>
  /// <returns>
  /// Optional next cycle event for this instruction.
  /// </returns>
  constexpr auto handle_event(const Op1Fetch& event) {
    fetch1++;
    return std::visit(overloaded {
      [this](const BinaryInstruction& i) -> std::optional<ExecutionEvent> {
          return get_fetch2(Op2Fetch{i, read_value_from_source(i.op1_addr)});
      },
      [this](const UnaryInstruction& i) -> std::optional<ExecutionEvent> {
          return get_writeback(Writeback{i, read_value_from_source(i.op1_addr)}, i.res_addr);
      },
      [this](const JumpInstruction& i) -> std::optional<ExecutionEvent> {
          return Writeback { i, read_value_from_source(i.offset_addr) }; // get_writeback(, DirectSource{0});
      }
    }, event.ins);
  }

  /// <summary>
  /// Execute Op2Fetch event on this cycle and get next cycle event.
  /// </summary>
  /// <param name="event">
  /// Op2Fetch event.
  /// </param>
  /// <returns>
  /// Optional next cycle event for this instruction.
  /// </returns>
  constexpr auto handle_event(const Op2Fetch& event) {
    fetch2++;
    return std::visit(overloaded {
      [this, &event](const BinaryInstruction& i) -> std::optional<ExecutionEvent> {
          return calculate_value(i, event.op1, read_value_from_source(i.op2_addr));
      },
      [](const UnaryInstruction&) -> std::optional<ExecutionEvent> {
          return Exception{std::string_view("UnaryInstruction pipelined Op2Fetch")};
      },
      [](const JumpInstruction&) -> std::optional<ExecutionEvent> {
          return Exception{std::string_view("JumpInstruction pipelined Op2Fetch")};
      }
    }, event.ins);
  }

  /// <summary>
  /// Execute Execution event on this cycle and get next cycle event.
  /// </summary>
  /// <param name="event">
  /// Execution event.
  /// </param>
  /// <returns>
  /// Optional next cycle event for this instruction.
  /// </returns>
  constexpr auto handle_event(const Execution& event) {
    exec++;
    return std::visit(overloaded {
      [&event] (const BinaryInstruction& i) -> std::optional<ExecutionEvent> {
          return Writeback{i, BinaryInstruction::calculate(i, event.op1, event.op2)};
      },
      [&event](const UnaryInstruction& i) -> std::optional<ExecutionEvent>{
          return Writeback{i, event.op1};
      },
      [&event](const JumpInstruction& i) -> std::optional<ExecutionEvent>{
          return Writeback{i, event.op1};
      }
    }, event.ins);
  }

  /// <summary>
  /// Execute writeback event on this cycle.
  /// </summary>
  /// <param name="event">
  /// Writeback event.
  /// </param>
  /// <returns>
  /// Next cycle event for this instruction.
  /// Since writeback is final pipeline step, always empty optional.
  /// </returns>
  constexpr auto handle_event(const Writeback& event) {
    writeback++;
    std::visit(overloaded {
      [this, &event] (const BinaryInstruction& i) {
         put_value_to_source(i.res_addr, event.res);
      },
      [this, &event](const UnaryInstruction& i) {
        put_value_to_source(i.res_addr, event.res);
      },
      [this, &event](const JumpInstruction&) {
        put_value_to_source(DirectSource{0}, event.res);
      }
    }, event.ins);
    return std::optional<ExecutionEvent> {};
  }

  /// <summary>
  /// Execute exception event on this cycle.
  /// </summary>
  /// <param name="">
  /// Exception event.
  /// </param>
  /// <returns>
  /// Optional next cycle event. 
  /// Always empty optional.
  /// </returns>
  constexpr auto handle_event(const Exception&) {
    exceptions++;
    return std::optional<ExecutionEvent>{};
  }

  /// <summary>
  /// Execute pipeline while next cycle event is avilable after execution this event.
  /// </summary>
  /// <param name="event">
  /// ExecutionEvent to execute on cycle.
  /// </param>
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

  /// <summary>
  /// Execute instruction.
  /// </summary>
  /// <param name="i">
  /// Instruction to execute.
  /// </param>
  /// <returns>
  /// Nothing.
  /// </returns>
  constexpr auto execute(const Instruction& i) {
    clk++; // instruction fetch+decode cycle
    const auto event = get_fetch1(i);
    if (event)
      handle_event(event.value());
  }

};

int main() {
  State state {};
  std::array<Instruction, 8> inss{
    UnaryInstruction { ImmidiateSource{1}, DirectSource{1} },
    UnaryInstruction { ImmidiateSource{2}, DirectSource{2} },

    UnaryInstruction { DirectSource{1}, IndirectSource{1} },
    UnaryInstruction { DirectSource{2}, IndirectSource{2} },

    BinaryInstruction { IndirectSource {1}, IndirectSource{2}, IndirectSource{3}, BinaryOperation::ADD },
    UnaryInstruction { IndirectSource {3}, DirectSource {3} },
    BinaryInstruction { DirectSource {1}, DirectSource {3}, DirectSource {1}, BinaryOperation::ADD },
    JumpInstruction { DirectSource {1} }
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

  const auto count = 12800000800;
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < count; i++) {
    state.execute(inss[i % inss.size()]);
  }
  fprintf(stderr, "CYCLE %d\n", state.clk);
  fprintf(stderr, "REGS ");
  hexdump(state.regs, 16);
  fprintf(stderr, "RAM  ");
  hexdump(state.data, 16);
  metrics(start, count, true);
}
