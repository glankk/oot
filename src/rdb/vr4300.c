#include <ultra64.h>
#include "vr4300.h"

static s32 sx_imm(u32 code)
{
	s32 imm = ((code >> 0) & 0xFFFF);
	if (imm >= 0x8000)
		imm = -0x10000 + imm;
	return imm;
}

static int decode_i_so(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_BRANCH;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	insn->opnd_value[1] = sx_imm(code) * 4;
	return 1;
}

static int decode_i_si(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_IMMEDIATE;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	insn->opnd_value[1] = sx_imm(code);
	return 1;
}

static int decode_i_o(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_BRANCH;
	insn->opnd_type[1] = VR4300_OPND_NULL;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = sx_imm(code) * 4;
	return 1;
}

static int decode_i_sto(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_CPU;
	insn->opnd_type[2] = VR4300_OPND_BRANCH;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	insn->opnd_value[2] = sx_imm(code) * 4;
	return 1;
}

static int decode_i_tsi(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_CPU;
	insn->opnd_type[2] = VR4300_OPND_IMMEDIATE;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	insn->opnd_value[2] = sx_imm(code);
	return 1;
}

static int decode_i_ti(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_IMMEDIATE;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	insn->opnd_value[1] = ((code >> 0) & 0xFFFF);
	return 1;
}

static int decode_i_tob(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_OFFSET;
	insn->opnd_type[2] = VR4300_OPND_CPU;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	insn->opnd_value[1] = sx_imm(code);
	insn->opnd_value[2] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	return 1;
}

static int decode_i_oob(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CACHE;
	insn->opnd_type[1] = VR4300_OPND_OFFSET;
	insn->opnd_type[2] = VR4300_OPND_CPU;
	insn->opnd_value[0] = ((code >> 16) & 0x1F);
	insn->opnd_value[1] = sx_imm(code);
	insn->opnd_value[2] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	return 1;
}

static int decode_i_t1ob(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CP1;
	insn->opnd_type[1] = VR4300_OPND_OFFSET;
	insn->opnd_type[2] = VR4300_OPND_CPU;
	insn->opnd_value[0] = VR4300_REG_CP1_FIRST + ((code >> 16) & 0x1F);
	insn->opnd_value[1] = sx_imm(code);
	insn->opnd_value[2] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	return 1;
}

static int decode_j(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_JUMP;
	insn->opnd_type[1] = VR4300_OPND_NULL;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = (((code >> 0) & 0x3FFFFFF) << 2);
	return 1;
}

static int decode_r_dta(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_CPU;
	insn->opnd_type[2] = VR4300_OPND_IMMEDIATE;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 11) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	insn->opnd_value[2] = ((code >> 6) & 0x1F);
	return 1;
}

static int decode_r_dts(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_CPU;
	insn->opnd_type[2] = VR4300_OPND_CPU;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 11) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	insn->opnd_value[2] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	return 1;
}

static int decode_r_s(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_NULL;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	return 1;
}

static int decode_r_ds(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_CPU;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 11) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	return 1;
}

static int decode_r_c(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_IMMEDIATE;
	insn->opnd_type[1] = VR4300_OPND_NULL;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = ((code >> 6) & 0xFFFFF);
	return 1;
}

static int decode_r(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_NULL;
	insn->opnd_type[1] = VR4300_OPND_NULL;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	return 1;
}

static int decode_r_d(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_NULL;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 11) & 0x1F);
	return 1;
}

static int decode_r_st(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_CPU;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	return 1;
}

static int decode_r_dst(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_CPU;
	insn->opnd_type[2] = VR4300_OPND_CPU;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 11) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	insn->opnd_value[2] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	return 1;
}

static int decode_r_stc(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_CPU;
	insn->opnd_type[2] = VR4300_OPND_IMMEDIATE;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 21) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	insn->opnd_value[2] = ((code >> 6) & 0x3FF);
	return 1;
}

static int decode_r_td0(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_CP0;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CP0_FIRST + ((code >> 11) & 0x1F);
	return 1;
}

static int decode_r_d1s1t1(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CP1;
	insn->opnd_type[1] = VR4300_OPND_CP1;
	insn->opnd_type[2] = VR4300_OPND_CP1;
	insn->opnd_value[0] = VR4300_REG_CP1_FIRST + ((code >> 6) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CP1_FIRST + ((code >> 11) & 0x1F);
	insn->opnd_value[2] = VR4300_REG_CP1_FIRST + ((code >> 16) & 0x1F);
	return 1;
}

static int decode_r_d1s1(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CP1;
	insn->opnd_type[1] = VR4300_OPND_CP1;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CP1_FIRST + ((code >> 6) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CP1_FIRST + ((code >> 11) & 0x1F);
	return 1;
}

static int decode_r_s1t1(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CP1;
	insn->opnd_type[1] = VR4300_OPND_CP1;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CP1_FIRST + ((code >> 11) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CP1_FIRST + ((code >> 16) & 0x1F);
	return 1;
}

static int decode_r_ts1(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_CP1;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_CP1_FIRST + ((code >> 11) & 0x1F);
	return 1;
}

static int decode_r_ts1c(enum vr4300_op opcode, u32 code,
	struct vr4300_insn *insn)
{
	insn->opcode = opcode;
	insn->opnd_type[0] = VR4300_OPND_CPU;
	insn->opnd_type[1] = VR4300_OPND_FCR;
	insn->opnd_type[2] = VR4300_OPND_NULL;
	insn->opnd_value[0] = VR4300_REG_CPU_FIRST + ((code >> 16) & 0x1F);
	insn->opnd_value[1] = VR4300_REG_FCR_FIRST + ((code >> 11) & 0x1F);
	return 1;
}

static int decode_special(u32 code, struct vr4300_insn *insn)
{
	int funct = ((code >> 0) & 0x3F);
	switch (funct) {
		case 0x00:	return decode_r_dta(VR4300_OP_SLL, code, insn);
		case 0x02:	return decode_r_dta(VR4300_OP_SRL, code, insn);
		case 0x03:	return decode_r_dta(VR4300_OP_SRA, code, insn);
		case 0x04:	return decode_r_dts(VR4300_OP_SLLV, code, insn);
		case 0x06:	return decode_r_dts(VR4300_OP_SRLV, code, insn);
		case 0x07:	return decode_r_dts(VR4300_OP_SRAV, code, insn);
		case 0x08:	return decode_r_s(VR4300_OP_JR, code, insn);
		case 0x09:	return decode_r_ds(VR4300_OP_JALR, code, insn);
		case 0x0C:	return decode_r_c(VR4300_OP_SYSCALL, code, insn);
		case 0x0D:	return decode_r_c(VR4300_OP_BREAK, code, insn);
		case 0x0F:	return decode_r(VR4300_OP_SYNC, code, insn);
		case 0x10:	return decode_r_d(VR4300_OP_MFHI, code, insn);
		case 0x11:	return decode_r_s(VR4300_OP_MTHI, code, insn);
		case 0x12:	return decode_r_d(VR4300_OP_MFLO, code, insn);
		case 0x13:	return decode_r_s(VR4300_OP_MTLO, code, insn);
		case 0x14:	return decode_r_dts(VR4300_OP_DSLLV, code, insn);
		case 0x16:	return decode_r_dts(VR4300_OP_DSRLV, code, insn);
		case 0x17:	return decode_r_dts(VR4300_OP_DSRAV, code, insn);
		case 0x18:	return decode_r_st(VR4300_OP_MULT, code, insn);
		case 0x19:	return decode_r_st(VR4300_OP_MULTU, code, insn);
		case 0x1A:	return decode_r_st(VR4300_OP_DIV, code, insn);
		case 0x1B:	return decode_r_st(VR4300_OP_DIVU, code, insn);
		case 0x1C:	return decode_r_st(VR4300_OP_DMULT, code, insn);
		case 0x1D:	return decode_r_st(VR4300_OP_DMULTU, code, insn);
		case 0x1E:	return decode_r_st(VR4300_OP_DDIV, code, insn);
		case 0x1F:	return decode_r_st(VR4300_OP_DDIVU, code, insn);
		case 0x20:	return decode_r_dst(VR4300_OP_ADD, code, insn);
		case 0x21:	return decode_r_dst(VR4300_OP_ADDU, code, insn);
		case 0x22:	return decode_r_dst(VR4300_OP_SUB, code, insn);
		case 0x23:	return decode_r_dst(VR4300_OP_SUBU, code, insn);
		case 0x24:	return decode_r_dst(VR4300_OP_AND, code, insn);
		case 0x25:	return decode_r_dst(VR4300_OP_OR, code, insn);
		case 0x26:	return decode_r_dst(VR4300_OP_XOR, code, insn);
		case 0x27:	return decode_r_dst(VR4300_OP_NOR, code, insn);
		case 0x2A:	return decode_r_dst(VR4300_OP_SLT, code, insn);
		case 0x2B:	return decode_r_dst(VR4300_OP_SLTU, code, insn);
		case 0x2C:	return decode_r_dst(VR4300_OP_DADD, code, insn);
		case 0x2D:	return decode_r_dst(VR4300_OP_DADDU, code, insn);
		case 0x2E:	return decode_r_dst(VR4300_OP_DSUB, code, insn);
		case 0x2F:	return decode_r_dst(VR4300_OP_DSUBU, code, insn);
		case 0x30:	return decode_r_stc(VR4300_OP_TGE, code, insn);
		case 0x31:	return decode_r_stc(VR4300_OP_TGEU, code, insn);
		case 0x32:	return decode_r_stc(VR4300_OP_TLT, code, insn);
		case 0x33:	return decode_r_stc(VR4300_OP_TLTU, code, insn);
		case 0x34:	return decode_r_stc(VR4300_OP_TEQ, code, insn);
		case 0x36:	return decode_r_stc(VR4300_OP_TNE, code, insn);
		case 0x38:	return decode_r_dta(VR4300_OP_DSLL, code, insn);
		case 0x3A:	return decode_r_dta(VR4300_OP_DSRL, code, insn);
		case 0x3B:	return decode_r_dta(VR4300_OP_DSRA, code, insn);
		case 0x3C:	return decode_r_dta(VR4300_OP_DSLL32, code, insn);
		case 0x3E:	return decode_r_dta(VR4300_OP_DSRL32, code, insn);
		case 0x3F:	return decode_r_dta(VR4300_OP_DSRA32, code, insn);
		default:	return 0;
	}
}

static int decode_regimm(u32 code, struct vr4300_insn *insn)
{
	int rt = ((code >> 16) & 0x1F);
	switch (rt) {
		case 0x00:	return decode_i_so(VR4300_OP_BLTZ, code, insn);
		case 0x01:	return decode_i_so(VR4300_OP_BGEZ, code, insn);
		case 0x02:	return decode_i_so(VR4300_OP_BLTZL, code, insn);
		case 0x03:	return decode_i_so(VR4300_OP_BGEZL, code, insn);
		case 0x08:	return decode_i_si(VR4300_OP_TGEI, code, insn);
		case 0x09:	return decode_i_si(VR4300_OP_TGEIU, code, insn);
		case 0x0A:	return decode_i_si(VR4300_OP_TLTI, code, insn);
		case 0x0B:	return decode_i_si(VR4300_OP_TLTIU, code, insn);
		case 0x0C:	return decode_i_si(VR4300_OP_TEQI, code, insn);
		case 0x0E:	return decode_i_si(VR4300_OP_TNEI, code, insn);
		case 0x10:	return decode_i_so(VR4300_OP_BLTZAL, code, insn);
		case 0x11:	return decode_i_so(VR4300_OP_BGEZAL, code, insn);
		case 0x12:	return decode_i_so(VR4300_OP_BLTZALL, code, insn);
		case 0x13:	return decode_i_so(VR4300_OP_BGEZALL, code, insn);
		default:	return 0;
	}
}

static int decode_cop0_co(u32 code, struct vr4300_insn *insn)
{
	int funct = ((code >> 0) & 0x3F);
	switch (funct) {
		case 0x01:	return decode_r(VR4300_OP_TLBR, code, insn);
		case 0x02:	return decode_r(VR4300_OP_TLBWI, code, insn);
		case 0x06:	return decode_r(VR4300_OP_TLBWR, code, insn);
		case 0x08:	return decode_r(VR4300_OP_TLBP, code, insn);
		case 0x18:	return decode_r(VR4300_OP_ERET, code, insn);
		default:	return 0;
	}
}

static int decode_cop0(u32 code, struct vr4300_insn *insn)
{
	int rs = ((code >> 21) & 0x1F);
	switch (rs) {
		case 0x00:	return decode_r_td0(VR4300_OP_MFC0, code, insn);
		case 0x04:	return decode_r_td0(VR4300_OP_MTC0, code, insn);
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
		case 0x1C:
		case 0x1D:
		case 0x1E:
		case 0x1F:	return decode_cop0_co(code, insn);
		default:	return 0;
	}
}

static int decode_cop1_bc1(u32 code, struct vr4300_insn *insn)
{
	int rt = ((code >> 16) & 0x1F);
	switch (rt) {
		case 0x00:	return decode_i_o(VR4300_OP_BC1F, code, insn);
		case 0x01:	return decode_i_o(VR4300_OP_BC1T, code, insn);
		case 0x02:	return decode_i_o(VR4300_OP_BC1FL, code, insn);
		case 0x03:	return decode_i_o(VR4300_OP_BC1TL, code, insn);
		default:	return 0;
	}
}

static int decode_cop1_s(u32 code, struct vr4300_insn *insn)
{
	int funct = ((code >> 0) & 0x3F);
	switch (funct) {
		case 0x00:	return decode_r_d1s1t1(VR4300_OP_ADDS, code, insn);
		case 0x01:	return decode_r_d1s1t1(VR4300_OP_SUBS, code, insn);
		case 0x02:	return decode_r_d1s1t1(VR4300_OP_MULS, code, insn);
		case 0x03:	return decode_r_d1s1t1(VR4300_OP_DIVS, code, insn);
		case 0x04:	return decode_r_d1s1(VR4300_OP_SQRTS, code, insn);
		case 0x05:	return decode_r_d1s1(VR4300_OP_ABSS, code, insn);
		case 0x06:	return decode_r_d1s1(VR4300_OP_MOVS, code, insn);
		case 0x07:	return decode_r_d1s1(VR4300_OP_NEGS, code, insn);
		case 0x08:	return decode_r_d1s1(VR4300_OP_ROUNDLS, code, insn);
		case 0x09:	return decode_r_d1s1(VR4300_OP_TRUNCLS, code, insn);
		case 0x0A:	return decode_r_d1s1(VR4300_OP_CEILLS, code, insn);
		case 0x0B:	return decode_r_d1s1(VR4300_OP_FLOORLS, code, insn);
		case 0x0C:	return decode_r_d1s1(VR4300_OP_ROUNDWS, code, insn);
		case 0x0D:	return decode_r_d1s1(VR4300_OP_TRUNCWS, code, insn);
		case 0x0E:	return decode_r_d1s1(VR4300_OP_CEILWS, code, insn);
		case 0x0F:	return decode_r_d1s1(VR4300_OP_FLOORWS, code, insn);
		case 0x21:	return decode_r_d1s1(VR4300_OP_CVTDS, code, insn);
		case 0x24:	return decode_r_d1s1(VR4300_OP_CVTWS, code, insn);
		case 0x25:	return decode_r_d1s1(VR4300_OP_CVTLS, code, insn);
		case 0x30:	return decode_r_s1t1(VR4300_OP_CFS, code, insn);
		case 0x31:	return decode_r_s1t1(VR4300_OP_CUNS, code, insn);
		case 0x32:	return decode_r_s1t1(VR4300_OP_CEQS, code, insn);
		case 0x33:	return decode_r_s1t1(VR4300_OP_CUEQS, code, insn);
		case 0x34:	return decode_r_s1t1(VR4300_OP_COLTS, code, insn);
		case 0x35:	return decode_r_s1t1(VR4300_OP_CULTS, code, insn);
		case 0x36:	return decode_r_s1t1(VR4300_OP_COLES, code, insn);
		case 0x37:	return decode_r_s1t1(VR4300_OP_CULES, code, insn);
		case 0x38:	return decode_r_s1t1(VR4300_OP_CSFS, code, insn);
		case 0x39:	return decode_r_s1t1(VR4300_OP_CNGLES, code, insn);
		case 0x3A:	return decode_r_s1t1(VR4300_OP_CSEQS, code, insn);
		case 0x3B:	return decode_r_s1t1(VR4300_OP_CNGLS, code, insn);
		case 0x3C:	return decode_r_s1t1(VR4300_OP_CLTS, code, insn);
		case 0x3D:	return decode_r_s1t1(VR4300_OP_CNGES, code, insn);
		case 0x3E:	return decode_r_s1t1(VR4300_OP_CLES, code, insn);
		case 0x3F:	return decode_r_s1t1(VR4300_OP_CNGTS, code, insn);
		default:	return 0;
	}
}

static int decode_cop1_d(u32 code, struct vr4300_insn *insn)
{
	int funct = ((code >> 0) & 0x3F);
	switch (funct) {
		case 0x00:	return decode_r_d1s1t1(VR4300_OP_ADDD, code, insn);
		case 0x01:	return decode_r_d1s1t1(VR4300_OP_SUBD, code, insn);
		case 0x02:	return decode_r_d1s1t1(VR4300_OP_MULD, code, insn);
		case 0x03:	return decode_r_d1s1t1(VR4300_OP_DIVD, code, insn);
		case 0x04:	return decode_r_d1s1(VR4300_OP_SQRTD, code, insn);
		case 0x05:	return decode_r_d1s1(VR4300_OP_ABSD, code, insn);
		case 0x06:	return decode_r_d1s1(VR4300_OP_MOVD, code, insn);
		case 0x07:	return decode_r_d1s1(VR4300_OP_NEGD, code, insn);
		case 0x08:	return decode_r_d1s1(VR4300_OP_ROUNDLD, code, insn);
		case 0x09:	return decode_r_d1s1(VR4300_OP_TRUNCLD, code, insn);
		case 0x0A:	return decode_r_d1s1(VR4300_OP_CEILLD, code, insn);
		case 0x0B:	return decode_r_d1s1(VR4300_OP_FLOORLD, code, insn);
		case 0x0C:	return decode_r_d1s1(VR4300_OP_ROUNDWD, code, insn);
		case 0x0D:	return decode_r_d1s1(VR4300_OP_TRUNCWD, code, insn);
		case 0x0E:	return decode_r_d1s1(VR4300_OP_CEILWD, code, insn);
		case 0x0F:	return decode_r_d1s1(VR4300_OP_FLOORWD, code, insn);
		case 0x20:	return decode_r_d1s1(VR4300_OP_CVTSD, code, insn);
		case 0x24:	return decode_r_d1s1(VR4300_OP_CVTWD, code, insn);
		case 0x25:	return decode_r_d1s1(VR4300_OP_CVTLD, code, insn);
		case 0x30:	return decode_r_s1t1(VR4300_OP_CFD, code, insn);
		case 0x31:	return decode_r_s1t1(VR4300_OP_CUND, code, insn);
		case 0x32:	return decode_r_s1t1(VR4300_OP_CEQD, code, insn);
		case 0x33:	return decode_r_s1t1(VR4300_OP_CUEQD, code, insn);
		case 0x34:	return decode_r_s1t1(VR4300_OP_COLTD, code, insn);
		case 0x35:	return decode_r_s1t1(VR4300_OP_CULTD, code, insn);
		case 0x36:	return decode_r_s1t1(VR4300_OP_COLED, code, insn);
		case 0x37:	return decode_r_s1t1(VR4300_OP_CULED, code, insn);
		case 0x38:	return decode_r_s1t1(VR4300_OP_CSFD, code, insn);
		case 0x39:	return decode_r_s1t1(VR4300_OP_CNGLED, code, insn);
		case 0x3A:	return decode_r_s1t1(VR4300_OP_CSEQD, code, insn);
		case 0x3B:	return decode_r_s1t1(VR4300_OP_CNGLD, code, insn);
		case 0x3C:	return decode_r_s1t1(VR4300_OP_CLTD, code, insn);
		case 0x3D:	return decode_r_s1t1(VR4300_OP_CNGED, code, insn);
		case 0x3E:	return decode_r_s1t1(VR4300_OP_CLED, code, insn);
		case 0x3F:	return decode_r_s1t1(VR4300_OP_CNGTD, code, insn);
		default:	return 0;
	}
}

static int decode_cop1_w(u32 code, struct vr4300_insn *insn)
{
	int funct = ((code >> 0) & 0x3F);
	switch (funct) {
		case 0x20:	return decode_r_d1s1(VR4300_OP_CVTSW, code, insn);
		case 0x21:	return decode_r_d1s1(VR4300_OP_CVTDW, code, insn);
		default:	return 0;
	}
}

static int decode_cop1_l(u32 code, struct vr4300_insn *insn)
{
	int funct = ((code >> 0) & 0x3F);
	switch (funct) {
		case 0x20:	return decode_r_d1s1(VR4300_OP_CVTSL, code, insn);
		case 0x21:	return decode_r_d1s1(VR4300_OP_CVTDL, code, insn);
		default:	return 0;
	}
}

static int decode_cop1(u32 code, struct vr4300_insn *insn)
{
	int rs = ((code >> 21) & 0x1F);
	switch (rs) {
		case 0x00:	return decode_r_ts1(VR4300_OP_MFC1, code, insn);
		case 0x01:	return decode_r_ts1(VR4300_OP_DMFC1, code, insn);
		case 0x02:	return decode_r_ts1c(VR4300_OP_CFC1, code, insn);
		case 0x04:	return decode_r_ts1(VR4300_OP_MTC1, code, insn);
		case 0x05:	return decode_r_ts1(VR4300_OP_DMTC1, code, insn);
		case 0x06:	return decode_r_ts1c(VR4300_OP_CTC1, code, insn);
		case 0x08:	return decode_cop1_bc1(code, insn);
		case 0x10:	return decode_cop1_s(code, insn);
		case 0x11:	return decode_cop1_d(code, insn);
		case 0x14:	return decode_cop1_w(code, insn);
		case 0x15:	return decode_cop1_l(code, insn);
		default:	return 0;
	}
}

int vr4300_decode_insn(u32 code, struct vr4300_insn *insn)
{
	int op = ((code >> 26) & 0x3F);
	switch (op) {
		case 0x00:	return decode_special(code, insn);
		case 0x01:	return decode_regimm(code, insn);
		case 0x02:	return decode_j(VR4300_OP_J, code, insn);
		case 0x03:	return decode_j(VR4300_OP_JAL, code, insn);
		case 0x04:	return decode_i_sto(VR4300_OP_BEQ, code, insn);
		case 0x05:	return decode_i_sto(VR4300_OP_BNE, code, insn);
		case 0x06:	return decode_i_so(VR4300_OP_BLEZ, code, insn);
		case 0x07:	return decode_i_so(VR4300_OP_BGTZ, code, insn);
		case 0x08:	return decode_i_tsi(VR4300_OP_ADDI, code, insn);
		case 0x09:	return decode_i_tsi(VR4300_OP_ADDIU, code, insn);
		case 0x0A:	return decode_i_tsi(VR4300_OP_SLTI, code, insn);
		case 0x0B:	return decode_i_tsi(VR4300_OP_SLTIU, code, insn);
		case 0x0C:	return decode_i_tsi(VR4300_OP_ANDI, code, insn);
		case 0x0D:	return decode_i_tsi(VR4300_OP_ORI, code, insn);
		case 0x0E:	return decode_i_tsi(VR4300_OP_XORI, code, insn);
		case 0x0F:	return decode_i_ti(VR4300_OP_LUI, code, insn);
		case 0x10:	return decode_cop0(code, insn);
		case 0x11:	return decode_cop1(code, insn);
		case 0x14:	return decode_i_sto(VR4300_OP_BEQL, code, insn);
		case 0x15:	return decode_i_sto(VR4300_OP_BNEL, code, insn);
		case 0x16:	return decode_i_so(VR4300_OP_BLEZL, code, insn);
		case 0x17:	return decode_i_so(VR4300_OP_BGTZL, code, insn);
		case 0x18:	return decode_i_tsi(VR4300_OP_DADDI, code, insn);
		case 0x19:	return decode_i_tsi(VR4300_OP_DADDIU, code, insn);
		case 0x1A:	return decode_i_tob(VR4300_OP_LDL, code, insn);
		case 0x1B:	return decode_i_tob(VR4300_OP_LDR, code, insn);
		case 0x20:	return decode_i_tob(VR4300_OP_LB, code, insn);
		case 0x21:	return decode_i_tob(VR4300_OP_LH, code, insn);
		case 0x22:	return decode_i_tob(VR4300_OP_LWL, code, insn);
		case 0x23:	return decode_i_tob(VR4300_OP_LW, code, insn);
		case 0x24:	return decode_i_tob(VR4300_OP_LBU, code, insn);
		case 0x25:	return decode_i_tob(VR4300_OP_LHU, code, insn);
		case 0x26:	return decode_i_tob(VR4300_OP_LWR, code, insn);
		case 0x27:	return decode_i_tob(VR4300_OP_LWU, code, insn);
		case 0x28:	return decode_i_tob(VR4300_OP_SB, code, insn);
		case 0x29:	return decode_i_tob(VR4300_OP_SH, code, insn);
		case 0x2A:	return decode_i_tob(VR4300_OP_SWL, code, insn);
		case 0x2B:	return decode_i_tob(VR4300_OP_SW, code, insn);
		case 0x2C:	return decode_i_tob(VR4300_OP_SDL, code, insn);
		case 0x2D:	return decode_i_tob(VR4300_OP_SDR, code, insn);
		case 0x2E:	return decode_i_tob(VR4300_OP_SWR, code, insn);
		case 0x2F:	return decode_i_oob(VR4300_OP_CACHE, code, insn);
		case 0x30:	return decode_i_tob(VR4300_OP_LL, code, insn);
		case 0x31:	return decode_i_t1ob(VR4300_OP_LWC1, code, insn);
		case 0x34:	return decode_i_tob(VR4300_OP_LLD, code, insn);
		case 0x35:	return decode_i_t1ob(VR4300_OP_LDC1, code, insn);
		case 0x37:	return decode_i_tob(VR4300_OP_LD, code, insn);
		case 0x38:	return decode_i_tob(VR4300_OP_SC, code, insn);
		case 0x39:	return decode_i_t1ob(VR4300_OP_SWC1, code, insn);
		case 0x3C:	return decode_i_tob(VR4300_OP_SCD, code, insn);
		case 0x3D:	return decode_i_t1ob(VR4300_OP_SDC1, code, insn);
		case 0x3F:	return decode_i_tob(VR4300_OP_SD, code, insn);
		default:	return 0;
	}
}
