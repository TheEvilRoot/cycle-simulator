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
};

struct BinaryInstruction {
  Source op1_addr;
  Source op2_addr;
  Source res_addr;
  BinaryOperation op;
};

struct JumpInstruction {
  Source offset_addr;
};

typedef std::variant<UnaryInstruction, BinaryInstruction, JumpInstruction> Instruction;


#endif