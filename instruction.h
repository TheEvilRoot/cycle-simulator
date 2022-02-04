#ifndef instricton_h_
#define instruction_h_

#include <variant>

struct UnaryInstruction {
  Source op1_addr;
  Source res_addr;
};

struct BinaryInstruction {
  Source op1_addr;
  Source op2_addr;
  Source res_addr;
};

struct JumpInstruction {
  Source offset_addr;
};

typedef std::variant<UnaryInstruction, BinaryInstruction, JumpInstruction> Instruction;


#endif