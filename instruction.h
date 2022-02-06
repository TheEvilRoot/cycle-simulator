#ifndef instricton_h_
#define instruction_h_

#include <variant>

enum struct BinaryOperation {
  ADD,
  SUB
};

enum struct UnaryOperation {
  MOV,
  SXT,
  SWB,
  ZER
};

struct UnaryInstruction {
  Source op1_addr;
  Source res_addr;

  static constexpr auto calculate(const UnaryInstruction& ui, const int op1) {
    return op1;
  }
};

struct BinaryInstruction {
  Source op1_addr;
  Source op2_addr;
  Source res_addr;
  BinaryOperation op;

  static constexpr auto calculate(const BinaryInstruction &bi, const int op1, const int op2) {
    switch (bi.op) {
      case BinaryOperation::ADD: return op1 + op2;
      case BinaryOperation::SUB: return op1 - op2;
    }
    return 0;
  }

};

struct JumpInstruction {
  Source offset_addr;

  static constexpr auto calculate(const JumpInstruction& ji, const int op1) {
    return op1;
  }
};

typedef std::variant<UnaryInstruction, BinaryInstruction, JumpInstruction> Instruction;


#endif