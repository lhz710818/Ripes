#pragma once

#include "VSRTL/core/vsrtl_adder.h"
#include "VSRTL/core/vsrtl_constant.h"
#include "VSRTL/core/vsrtl_design.h"
#include "VSRTL/core/vsrtl_logicgate.h"
#include "VSRTL/core/vsrtl_multiplexer.h"

#include "../ripesprocessor.h"

#include "alu.h"
#include "branch.h"
#include "control.h"
#include "decode.h"
#include "ecallchecker.h"
#include "immediate.h"
#include "registerfile.h"
#include "riscv.h"
#include "rvmemory.h"

namespace vsrtl {
namespace core {
using namespace Ripes;

class SingleCycleRISCV : public RipesProcessor {
public:
    SingleCycleRISCV() : RipesProcessor("Single Cycle RISC-V Processor") {
        // -----------------------------------------------------------------------
        // Program counter
        pc_reg->out >> pc_4->op1;
        4 >> pc_4->op2;
        pc_src->out >> pc_reg->in;

        // Note: pc_src works uses the PcSrc enum, but is selected by the boolean signal
        // from the controlflow OR gate. PcSrc enum values must adhere to the boolean
        // 0/1 values.
        controlflow_or->out >> pc_src->select;

        // -----------------------------------------------------------------------
        // Instruction memory
        pc_reg->out >> instr_mem->addr;
        instr_mem->setMemory(m_memory);

        // -----------------------------------------------------------------------
        // Decode
        instr_mem->data_out >> decode->instr;

        // -----------------------------------------------------------------------
        // Control signals
        decode->opcode >> control->opcode;

        // -----------------------------------------------------------------------
        // Immediate
        decode->opcode >> immediate->opcode;
        instr_mem->data_out >> immediate->instr;

        // -----------------------------------------------------------------------
        // Registers
        decode->wr_reg_idx >> registerFile->wr_addr;
        decode->r1_reg_idx >> registerFile->r1_addr;
        decode->r2_reg_idx >> registerFile->r2_addr;
        control->reg_do_write_ctrl >> registerFile->wr_en;
        reg_wr_src->out >> registerFile->data_in;

        data_mem->data_out >> reg_wr_src->get(RegWrSrc::MEMREAD);
        alu->res >> reg_wr_src->get(RegWrSrc::ALURES);
        pc_4->out >> reg_wr_src->get(RegWrSrc::PC4);
        control->reg_wr_src_ctrl >> reg_wr_src->select;

        registerFile->setMemory(m_regMem);

        // -----------------------------------------------------------------------
        // Branch
        control->comp_ctrl >> branch->comp_op;
        registerFile->r1_out >> branch->op1;
        registerFile->r2_out >> branch->op2;

        branch->res >> *br_and->in[0];
        control->do_branch >> *br_and->in[1];
        br_and->out >> *controlflow_or->in[0];
        control->do_jump >> *controlflow_or->in[1];
        pc_4->out >> pc_src->get(PcSrc::PC4);
        alu->res >> pc_src->get(PcSrc::ALU);

        // -----------------------------------------------------------------------
        // ALU
        registerFile->r1_out >> alu_op1_src->get(AluSrc1::REG1);
        pc_reg->out >> alu_op1_src->get(AluSrc1::PC);
        control->alu_op1_ctrl >> alu_op1_src->select;

        registerFile->r2_out >> alu_op2_src->get(AluSrc2::REG2);
        immediate->imm >> alu_op2_src->get(AluSrc2::IMM);
        control->alu_op2_ctrl >> alu_op2_src->select;

        alu_op1_src->out >> alu->op1;
        alu_op2_src->out >> alu->op2;

        control->alu_ctrl >> alu->ctrl;

        // -----------------------------------------------------------------------
        // Data memory
        alu->res >> data_mem->addr;
        control->mem_do_write_ctrl >> data_mem->wr_en;
        registerFile->r2_out >> data_mem->data_in;
        control->mem_ctrl >> data_mem->op;
        data_mem->mem->setMemory(m_memory);

        // -----------------------------------------------------------------------
        // Ecall checker
        decode->opcode >> ecallChecker->opcode;
        ecallChecker->setSysCallSignal(&handleSysCall);
    }

    // Design subcomponents
    SUBCOMPONENT(registerFile, RegisterFile);
    SUBCOMPONENT(alu, ALU);
    SUBCOMPONENT(control, Control);
    SUBCOMPONENT(immediate, Immediate);
    SUBCOMPONENT(decode, Decode);
    SUBCOMPONENT(branch, Branch);
    SUBCOMPONENT(pc_4, Adder<RV_REG_WIDTH>);

    // Registers
    SUBCOMPONENT(pc_reg, Register<RV_REG_WIDTH>);

    // Multiplexers
    SUBCOMPONENT(reg_wr_src, TYPE(EnumMultiplexer<RegWrSrc, RV_REG_WIDTH>));
    SUBCOMPONENT(pc_src, TYPE(EnumMultiplexer<PcSrc, RV_REG_WIDTH>));
    SUBCOMPONENT(alu_op1_src, TYPE(EnumMultiplexer<AluSrc1, RV_REG_WIDTH>));
    SUBCOMPONENT(alu_op2_src, TYPE(EnumMultiplexer<AluSrc2, RV_REG_WIDTH>));

    // Memories
    SUBCOMPONENT(instr_mem, TYPE(ROM<RV_REG_WIDTH, RV_INSTR_WIDTH>));
    SUBCOMPONENT(data_mem, TYPE(RVMemory<RV_REG_WIDTH, RV_REG_WIDTH>));

    // Gates
    SUBCOMPONENT(br_and, TYPE(And<1, 2>));
    SUBCOMPONENT(controlflow_or, TYPE(Or<1, 2>));

    // Address spaces
    ADDRESSSPACE(m_memory);
    ADDRESSSPACE(m_regMem);

    SUBCOMPONENT(ecallChecker, EcallChecker);

    // Ripes interface compliance
    virtual const ISAInfoBase* implementsISA() const override { return ISAInfo<ISA::RV32IM>::instance(); }
    unsigned int stageCount() const override { return 1; }
    unsigned int pcForStage(unsigned int) const override { return pc_reg->out.uValue(); }
    unsigned int nextPcForStage(unsigned int) const override { return pc_src->out.uValue(); }
    QString stageName(unsigned int) const override { return "•"; }
    StageInfo stageInfo(unsigned int) const override { return StageInfo(pc_reg->out.uValue(), true); }
    void setProgramCounter(uint32_t address) override {
        pc_reg->forceValue(0, address);
        propagateDesign();
    }
    void setPCInitialValue(uint32_t address) override { pc_reg->setInitValue(address); }
    SparseArray& getMemory() override { return *m_memory; }
    unsigned int getRegister(unsigned i) const override { return registerFile->getRegister(i); }
    SparseArray& getRegisters() override { return *m_regMem; }
    void finalize() override {
        // Allow one additional clock cycle to clear the current instruction
        m_finishInNextCycle = true;
    }

    void setRegister(unsigned i, uint32_t v) override { setSynchronousValue(registerFile->_wr_mem, i, v); }

    void clock() override {
        // m_finishInNextCycle may be set during Design::clock(). Store the value before clocking the processor, and
        // emit finished if this was the final clock cycle.
        const bool finishInThisCycle = m_finishInNextCycle;
        Design::clock();
        if (finishInThisCycle) {
            finished.Emit();
        }
    }

    void rewind() override {
        Design::rewind();
        // Ensure that rewinds performed when we expected to finish in the following cycle, clears this expectation.
        m_finishInNextCycle = false;
    }

    void reset() override {
        Design::reset();
        m_finishInNextCycle = false;
    }

private:
    bool m_finishInNextCycle = false;
};

}  // namespace core
}  // namespace vsrtl