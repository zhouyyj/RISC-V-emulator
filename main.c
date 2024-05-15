#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "io.h"

// Include file storing elf in char array.
#include "rvelf.h"
#define ELF_ARR rvelf
#define ELF_ARR_LEN rvelf_len

// Memory layout.
#define MEM_START 0x0
#define MEM_SIZE 0x100000  // 1MiB
// #define IO_START 0x100000
// #define IO_SIZE 0xFFF      // 4KiB
uint8_t memory[MEM_SIZE];

// Convert memory to target data width.
#define MEM_WORD_U(addr) *(uint32_t *)&memory[addr]
#define MEM_HALF_U(addr) *(uint16_t *)&memory[addr]
#define MEM_BYTE_U(addr) *(uint8_t *)&memory[addr]
#define MEM_WORD_S(addr) *(int32_t *)&memory[addr]
#define MEM_HALF_S(addr) *(int16_t *)&memory[addr]
#define MEM_BYTE_S(addr) *(int8_t *)&memory[addr]

// Register ABI Names.
enum RegName {
  _zero = 0,  // Hard-wired zero
  _ra,  // Return address
  _sp, _gp, _tp,  // Stack, Global, Thread pointer
  _t0,  // Temporary / alternate link register
  _t1, _t2,  // Temporaries
  _s0, _fp = _s0,  // Saved register / frame pointer 
  _s1,  // Saved register
  _a0, _a1,  // Function arguments / return values
  _a2, _a3, _a4, _a5, _a6, _a7,  // Function arguments
  _s2, _s3, _s4, _s5, _s6, _s7, _s8, _s9, _s10, _s11,  // Saved registers
  _t3, _t4, _t5, _t6,  // Temporaries
  REG_NUM
};

// Registers.
uint32_t reg[REG_NUM] = {0};
uint32_t pc = 0;

// ELF header.
#define ELFMAG_W0 0x464C457F /* 7F E L F */
#define ELFMAG_W4 0x00010101 /* 32-bit, LSB, SysV ABI */
#define ET_EXEC 2            /* Executable file */
#define EM_RISCV 243         /* RISC-V */
typedef struct {
  uint8_t e_ident[16]; /* Magic number and other info */
  uint16_t e_type;     /* Object file type */
  uint16_t e_machine;  /* Architecture */
  uint32_t e_version;  /* Object file version */
  uint32_t e_entry;    /* Entry point virtual address */
} Elf32_Ehdr;

// Load elf from array.
int load() {
  char msg[40] = {0};
  // Check ELF size.
  if (ELF_ARR_LEN >= MEM_SIZE || ELF_ARR_LEN <= 52) {
    sprintf(msg, "load: ELF bad size");
    termPuts(msg);
    return 1;
  }
  // Load ELF.
  memcpy(memory, ELF_ARR, ELF_ARR_LEN);
  Elf32_Ehdr *elf_h = (Elf32_Ehdr *)memory;

  // Check ELF header.
  int ret_val = 0;
  ret_val |= MEM_WORD_U(0) != ELFMAG_W0 || MEM_WORD_U(4) != ELFMAG_W4;
  ret_val |= elf_h->e_type != ET_EXEC;
  ret_val |= elf_h->e_machine != EM_RISCV;

  if (ret_val) {
    sprintf(msg, "load: ELF bad header");
  } else {
    pc = elf_h->e_entry; // Set pc to entry point.
    sprintf(msg, "load: entry point address 0x%x", pc);
  }
  termPuts(msg);
  return ret_val;
}

// Instruction types.
#define OPCODE(inst) inst&0b1111111
typedef struct {
  uint32_t opcode : 7, rd : 5, funct3 : 3, rs1 : 5, rs2 : 5, funct7 : 7;
} R_Inst;
typedef struct {
  uint32_t opcode : 7, rd : 5, funct3 : 3, rs1 : 5, imm11_0 : 12;
} I_Inst_u;
typedef struct {
  uint32_t opcode : 7, rd : 5, funct3 : 3, rs1 : 5;
  int32_t imm11_0 : 12;
} I_Inst_s;
typedef struct {
  uint32_t opcode : 7, imm4_0 : 5, funct3 : 3, rs1 : 5, rs2 : 5;
  int32_t imm11_5 : 7;
} S_Inst;
typedef struct {
  uint32_t opcode : 7, imm11 : 1, imm4_1 : 4, funct3 : 3, rs1 : 5, rs2 : 5,
           imm10_5 : 6, imm12 : 1;
} B_Inst;
typedef struct {
  uint32_t opcode : 7, rd : 5, imm31_12 : 20;
} U_Inst;
typedef struct {
  uint32_t opcode : 7, rd : 5, imm19_12 : 8, imm11 : 1, imm10_1 : 10, imm20 : 1;
} J_Inst;
typedef union {
  R_Inst R;
  I_Inst_u Iu;
  I_Inst_s Is;
  S_Inst S;
  B_Inst B;
  U_Inst U;
  J_Inst J;
} InstField;

typedef enum OpcodeVal {
  U_LUI     = 0b0110111,
  U_AUIPC   = 0b0010111,
  J_JAL     = 0b1101111,
  Is_JALR   = 0b1100111,
  B_Branch  = 0b1100011, // Branch.
  I_Load    = 0b0000011, // Load.
  S_Store   = 0b0100011, // Store.
  I_OpImm   = 0b0010011, // Immediate computation.
  R_Op      = 0b0110011, // Register computation.
  I_MiscMem = 0b0001111, // FENCE (unimplemented).
  Iu_System  = 0b1110011  // ECALL & EBREAK.
} Opcode_T;

bool f_pause = false;
bool f_step = false;
bool f_ecall = false;
bool f_exit = false;

void handleEcall(){
  f_ecall = false;
  char term_str[40] = {'\0'};
  switch (reg[_a0]) {
    case 0:  // exit (status code)
      f_exit = true;
      sprintf(term_str, "exit with code %d", reg[_a1]);
      break;
    case 100:  // print (signed decimal)
      sprintf(term_str, ">> %d", reg[_a1]);
      break;
    case 101:  // print (null-terminated char*)
      sprintf(term_str, ">> %.36s", (char *)&memory[reg[_a1]]);
      break;
    // case 102:  // print to seven-segment displays
    //   break;
    case 103:  // print regs to terminal (not VGA)
      printf("\nRegisters\n");
      for (int i = 0; i < REG_NUM; ++i) {
        printf("x%-2d 0x%08x  ", i, reg[i]);
        if (i % 4 == 3) printf("\n");
      }
      printf("\n");
      sprintf(term_str, "Registers printed to terminal.");
      break;
    case 200:  // read int from switches
      reg[_a0] = readSwitches();
      sprintf(term_str, "<< %d", reg[_a0]);
      break;
    default:
      sprintf(term_str, "%d: unknown ecall\n", reg[_a0]);
      break;
  }
  termPuts(term_str);
}

void handleBranch(InstField *inst, char *out_op) {
  uint32_t tgt = pc + ((-inst->B.imm12 << 12) | (inst->B.imm11 << 11) |
                 (inst->B.imm10_5 << 5) | (inst->B.imm4_1 << 1)) - 4;
  switch (inst->B.funct3) {
    case 0b000:  // BEQ
      if (reg[inst->R.rs1] == reg[inst->R.rs2]) pc = tgt;
      sprintf(out_op, "beq x%d, x%d, 0x%x", inst->B.rs1, inst->B.rs2, tgt);
      break;
    case 0b001:  // BNE
      if (reg[inst->R.rs1] != reg[inst->R.rs2]) pc = tgt;
      sprintf(out_op, "bne x%d, x%d, 0x%x", inst->B.rs1, inst->B.rs2, tgt);
      break;
    case 0b100:  // BLT
      if ((int32_t)reg[inst->R.rs1] < (int32_t)reg[inst->R.rs2]) pc = tgt;
      sprintf(out_op, "blt x%d, x%d, 0x%x", inst->B.rs1, inst->B.rs2, tgt);
      break;
    case 0b101:  // BGE
      if ((int32_t)reg[inst->R.rs1] >= (int32_t)reg[inst->R.rs2]) pc = tgt;
      sprintf(out_op, "bge x%d, x%d, 0x%x", inst->B.rs1, inst->B.rs2, tgt);
      break;
    case 0b110:  // BLTU
      if (reg[inst->R.rs1] < reg[inst->R.rs2]) pc = tgt;
      sprintf(out_op, "bltu x%d, x%d, 0x%x", inst->B.rs1, inst->B.rs2, tgt);
      break;
    case 0b111:  // BGEU
      if (reg[inst->R.rs1] >= reg[inst->R.rs2]) pc = tgt;
      sprintf(out_op, "bgeu x%d, x%d, 0x%x", inst->B.rs1, inst->B.rs2, tgt);
      break;
  }
}

void handleLoad(InstField *inst, char *out_op) {
  uint32_t addr = reg[inst->Is.rs1] + inst->Is.imm11_0;
  switch (inst->Is.funct3) {
    case 0b000: // LB
      reg[inst->Is.rd] = MEM_BYTE_S(addr);
      sprintf(out_op, "lb x%d, %d(x%d)", inst->Is.rd, inst->Is.imm11_0, inst->Is.rs1);
      break;
    case 0b001: // LH
      reg[inst->Is.rd] = MEM_HALF_S(addr);
      sprintf(out_op, "lh x%d, %d(x%d)", inst->Is.rd, inst->Is.imm11_0, inst->Is.rs1);
      break;
    case 0b010: // LW
      reg[inst->Is.rd] = MEM_WORD_S(addr);
      sprintf(out_op, "lw x%d, %d(x%d)", inst->Is.rd, inst->Is.imm11_0, inst->Is.rs1);
      break;
    case 0b100: // LBU
      reg[inst->Is.rd] = MEM_BYTE_U(addr);
      sprintf(out_op, "lbu x%d, %d(x%d)", inst->Is.rd, inst->Is.imm11_0, inst->Is.rs1);
      break;
    case 0b101: // LHU
      reg[inst->Is.rd] = MEM_HALF_U(addr);
      sprintf(out_op, "lhu x%d, %d(x%d)", inst->Is.rd, inst->Is.imm11_0, inst->Is.rs1);
      break;
    default:
      sprintf(out_op, "unknown load");
      break;
  }
}

void handleStore(InstField *inst, char *out_op) {
  int32_t offset = inst->S.imm4_0 + (inst->S.imm11_5 << 5);
  uint32_t addr = reg[inst->S.rs1] + offset;
  switch (inst->S.funct3) {
    case 0b000: // SB
      MEM_BYTE_S(addr) = reg[inst->S.rs2];
      sprintf(out_op, "sb x%d, %d(x%d)", inst->S.rs2, offset, inst->Is.rs1);
      break;
    case 0b001: // SH
      MEM_HALF_S(addr) = reg[inst->S.rs2];
      sprintf(out_op, "sh x%d, %d(x%d)", inst->S.rs2, offset, inst->Is.rs1);
      break;
    case 0b010: // SW
      MEM_WORD_S(addr) = reg[inst->S.rs2];
      sprintf(out_op, "sw x%d, %d(x%d)", inst->S.rs2, offset, inst->Is.rs1);
      break;
    default:
      sprintf(out_op, "unknown store");
      break;
  }
}

void handleOp(InstField *inst, char *out_op) {
  switch (inst->R.funct3) {
    case 0b000:  // ADD / SUB
      if (inst->R.funct7 == 0) {  // ADD
        reg[inst->R.rd] = reg[inst->R.rs1] + reg[inst->R.rs2];
        sprintf(out_op, "add x%d, x%d, x%d", inst->R.rd, inst->R.rs1, inst->R.rs2);
      } else {  // SUB
        reg[inst->R.rd] = reg[inst->R.rs1] - reg[inst->R.rs2];
        sprintf(out_op, "sub x%d, x%d, x%d", inst->R.rd, inst->R.rs1, inst->R.rs2);
      }
      break;
    case 0b001:  // SLL
      reg[inst->R.rd] = reg[inst->R.rs1] << (0b11111 & reg[inst->R.rs2]);
      sprintf(out_op, "sll x%d, x%d, x%d", inst->R.rd, inst->R.rs1, inst->R.rs2);
      break;
    case 0b010:  // SLT: SLT and SLTU perform signed and unsigned compares
                 // respectively, writing 1 to rd if rs1 < rs2, 0 otherwise.
      reg[inst->R.rd] = ((int32_t)reg[inst->R.rs1] < (int32_t)reg[inst->R.rs2]);
      sprintf(out_op, "slt x%d, x%d, x%d", inst->R.rd, inst->R.rs1, inst->R.rs2);
      break;
    case 0b011:  // SLTU
      reg[inst->R.rd] = (reg[inst->R.rs1] < reg[inst->R.rs2]);
      sprintf(out_op, "sltu x%d, x%d, x%d", inst->R.rd, inst->R.rs1, inst->R.rs2);
      break;
    case 0b100:  // XOR
      reg[inst->R.rd] = reg[inst->R.rs1] ^ reg[inst->R.rs2];
      sprintf(out_op, "xor x%d, x%d, x%d", inst->R.rd, inst->R.rs1, inst->R.rs2);
      break;
    case 0b101:  // SRL / SRA
      if (inst->R.funct7 == 0) {  // SRL
        reg[inst->R.rd] = (uint32_t)reg[inst->R.rs1] >> (0b11111 & reg[inst->R.rs2]);
        sprintf(out_op, "srl x%d, x%d, x%d", inst->R.rd, inst->R.rs1, inst->R.rs2);
      } else {  // SRA
        reg[inst->R.rd] = (int32_t)reg[inst->R.rs1] >> (0b11111 & reg[inst->R.rs2]);
        sprintf(out_op, "sra x%d, x%d, x%d", inst->R.rd, inst->R.rs1, inst->R.rs2);
      }
      break;
    case 0b110:  // OR
      reg[inst->R.rd] = reg[inst->R.rs1] | reg[inst->R.rs2];
      sprintf(out_op, "or x%d, x%d, x%d", inst->R.rd, inst->R.rs1, inst->R.rs2);
      break;
    case 0b111:  // AND
      reg[inst->R.rd] = reg[inst->R.rs1] & reg[inst->R.rs2];
      sprintf(out_op, "and x%d, x%d, x%d", inst->R.rd, inst->R.rs1, inst->R.rs2);
      break;
    default:
      sprintf(out_op, "unknown op");
      break;
  }
}

void handleOpImm(InstField *inst, char* out_op) {
  sprintf(out_op, "opimm");
  switch (inst->Is.funct3) {
    case 0b000:  // ADDI
      reg[inst->Is.rd] = (int32_t)reg[inst->Is.rs1] + inst->Is.imm11_0;
      sprintf(out_op, "addi x%d, x%d, %d", inst->Is.rd, inst->Is.rs1, inst->Is.imm11_0);
      break;
    case 0b010:  // SLTI
      reg[inst->Is.rd] = (int32_t)reg[inst->Is.rs1] < inst->Is.imm11_0;
      sprintf(out_op, "slti x%d, x%d, %d", inst->Is.rd, inst->Is.rs1, inst->Is.imm11_0);
      break;
    case 0b011:  // SLTIU
      reg[inst->Is.rd] = reg[inst->Is.rs1] < (uint32_t)inst->Is.imm11_0;
      sprintf(out_op, "sltiu x%d, x%d, %d", inst->Is.rd, inst->Is.rs1, inst->Is.imm11_0);
      break;
    case 0b100:  // XORI
      reg[inst->Is.rd] = reg[inst->Is.rs1] ^ inst->Is.imm11_0;
      sprintf(out_op, "xori x%d, x%d, %d", inst->Is.rd, inst->Is.rs1, inst->Is.imm11_0);
      break;
    case 0b110:  // ORI
      reg[inst->Is.rd] = reg[inst->Is.rs1] | inst->Is.imm11_0;
      sprintf(out_op, "ori x%d, x%d, %d", inst->Is.rd, inst->Is.rs1, inst->Is.imm11_0);
      break;
    case 0b111:  // ANDI
      reg[inst->Is.rd] = reg[inst->Is.rs1] & inst->Is.imm11_0;
      sprintf(out_op, "andi x%d, x%d, %d", inst->Is.rd, inst->Is.rs1, inst->Is.imm11_0);
      break;
    case 0b001:  // SLLI
      // unsigned int shamt = inst->R.rs2;
      reg[inst->R.rd] = reg[inst->R.rs1] << inst->R.rs2;
      sprintf(out_op, "slli x%d, x%d, %u", inst->R.rd, inst->R.rs1, inst->R.rs2);
      break;
    case 0b101:  // SRLI / SRAI
      // unsigned int shamt = inst->R.rs2;
      if (inst->R.funct7 == 0) {  // SRLI
        reg[inst->Is.rd] = (uint32_t)reg[inst->R.rs1] >> inst->R.rs2;
        sprintf(out_op, "srli x%d, x%d, %u", inst->R.rd, inst->R.rs1, inst->R.rs2);
      } else {  // SRAI
        reg[inst->Is.rd] = (int32_t)reg[inst->R.rs1] >> inst->R.rs2;
        sprintf(out_op, "slai x%d, x%d, %u", inst->R.rd, inst->R.rs1, inst->R.rs2);
      }
      break;
    default:
      sprintf(out_op, "unknown op-imm");
      break;
  }
}

// #define QUIT_N(n) { printf("quit %d\n", n); return n; }

int main() {
reset:
  resetIO();
  f_pause = f_step = f_ecall = f_exit = false;

  // Load ELF into memory.
  if (load()) return 1;

  // Initialize output.
  char out_str[40] = {0};
  char* out_op = out_str + 18;
  sprintf(out_str, "Addr    Inst      Disassembly");
  decodePuts(out_str);
  updateCharBuf();

  while (true) {
    updateLEDs();

    // Check pause/continue, step, reset.
    int keys = readKeys();
    if (keys & 0b1000) goto reset;
    if (f_exit) continue;
    if (keys & 0b10) {
      f_pause = !f_pause;
      if (f_pause) sprintf(out_str, "paused at 0x%-8x", pc);
      else sprintf(out_str, "continue from 0x%-8x", pc);
      termPuts(out_str);
      updateCharBuf();
    }
    if (keys & 0b1) f_step = true;
    if (f_step) {
      f_step = false;
      sprintf(out_str, "step to 0x%-8x", pc);
      termPuts(out_str);
    } else if (f_pause)
      continue;

    if (pc >= MEM_SIZE) {
      f_exit = true;
      sprintf(out_str, "pc=0x%d out of memory bound", pc);
      termPuts(out_str);
      updateCharBuf();
      continue;
    }
    // Fetch new instruction and update pc.
    uint32_t inst_u32 = MEM_WORD_U(pc);
    sprintf(out_str, "%-8x%08x  ", pc, inst_u32);
    pc += 4;

    // Decode new instruction.
    InstField *inst = (InstField *)&inst_u32;
    switch (OPCODE(inst_u32)) {
      case U_LUI:
        reg[inst->U.rd] = inst->U.imm31_12 << 12;
        sprintf(out_op, "lui x%d, 0x%x", inst->U.rd, inst->U.imm31_12);
        break;
      case U_AUIPC:
        reg[inst->U.rd] = pc - 4 + (inst->U.imm31_12 << 12);
        sprintf(out_op, "auipc x%d, 0x%x", inst->U.rd, inst->U.imm31_12);
        break;
      case J_JAL:
        reg[inst->J.rd] = pc;
        pc += ((-inst->J.imm20 << 20) | (inst->J.imm19_12 << 12) |
               (inst->J.imm11 << 11) | (inst->J.imm10_1 << 1)) - 4;
        sprintf(out_op, "jal x%d, 0x%x", inst->J.rd, pc);
        break;
      case Is_JALR:
        reg[inst->Is.rd] = pc;
        pc = (-2) & (reg[inst->Is.rs1] + inst->Is.imm11_0);
        sprintf(out_op, "jalr x%d, x%d, %d",
               inst->Is.rd, inst->Is.rs1, inst->Is.imm11_0);
        break;
      case B_Branch:
        handleBranch(inst, out_op);
        break;
      case I_Load:
        handleLoad(inst, out_op);
        break;
      case S_Store:
        handleStore(inst, out_op);
        break;
      case I_OpImm:
        handleOpImm(inst, out_op);
        break;
      case R_Op:
        handleOp(inst, out_op);
         break;
      case I_MiscMem:
        sprintf(out_op, "unsupported misc-mem");
        break;
      case Iu_System:
        if (inst->Iu.imm11_0 == 0x0) {
          sprintf(out_op, "ecall (%d)", reg[_a0]);
          f_ecall = true;
        } else {
          sprintf(out_op, "ebreak");
          f_pause = true;
        }
        break;
      default:
        sprintf(out_op, "unknown");
        break;
    }

    reg[_zero] = 0; // Reset x0 (hard zero).
    // Update outputs.
    decodePuts(out_str);
    updateReg(pc, reg);
    if (f_ecall) handleEcall();  // Deal with ecalls.
    updateCharBuf();
  }
  return 1; // Terminate if pc out of memory bound.
}
