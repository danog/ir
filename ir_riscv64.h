/*
 * IR - Lightweight JIT Compilation Framework
 * (Aarch64 CPU specific definitions)
 * Copyright (C) 2022 Zend by Perforce.
 * Authors: Dmitry Stogov <dmitry@php.net>
 */

#ifndef IR_AARCH64_H
#define IR_AARCH64_H

#define IR_GP_REGS(_) \
	_(X0,    x0) \
	_(X1,    x1) \
	_(X2,    x2) \
	_(X3,    x3) \
	_(X4,    x4) \
	_(X5,    x5) \
	_(X6,    x6) \
	_(X7,    x7) \
	_(X8,    x8) \
	_(X9,    x9) \
	_(X10,  x10) \
	_(X11,  x11) \
	_(X12,  x12) \
	_(X13,  x13) \
	_(X14,  x14) \
	_(X15,  x15) \
	_(X16,  x16) \
	_(X17,  x17) \
	_(X18,  x18) \
	_(X19,  x19) \
	_(X20,  x20) \
	_(X21,  x21) \
	_(X22,  x22) \
	_(X23,  x23) \
	_(X24,  x24) \
	_(X25,  x25) \
	_(X26,  x26) \
	_(X27,  x27) \
	_(X28,  x28) \
	_(X29,  x29) \
	_(X30,  x30) \
	_(X31,  x31) \
	_(PC,  pc) \

# define IR_FP_REGS(_) \
	_(F0,  f0) \
	_(F1,  f1) \
	_(F2,  f2) \
	_(F3,  f3) \
	_(F4,  f4) \
	_(F5,  f5) \
	_(F6,  f6) \
	_(F7,  f7) \
	_(F8,  f8) \
	_(F9,  f9) \
	_(F10, f10) \
	_(F11, f11) \
	_(F12, f12) \
	_(F13, f13) \
	_(F14, f14) \
	_(F15, f15) \
	_(F16, f16) \
	_(F17, f17) \
	_(F18, f18) \
	_(F19, f19) \
	_(F20, f20) \
	_(F21, f21) \
	_(F22, f22) \
	_(F23, f23) \
	_(F24, f24) \
	_(F25, f25) \
	_(F26, f26) \
	_(F27, f27) \
	_(F28, f28) \
	_(F29, f29) \
	_(F30, f30) \
	_(F31, f31) \

#define IR_GP_REG_ENUM(code, name) \
	IR_REG_ ## code,

#define IR_FP_REG_ENUM(code, name) \
	IR_REG_ ## code,

enum _ir_reg {
	_IR_REG_NONE = -1,
	IR_GP_REGS(IR_GP_REG_ENUM)
	IR_FP_REGS(IR_FP_REG_ENUM)
	IR_REG_NUM,
};

#define IR_REG_GP_FIRST IR_REG_X0
#define IR_REG_FP_FIRST IR_REG_F0
#define IR_REG_GP_LAST  (IR_REG_FP_FIRST - 1)
#define IR_REG_FP_LAST  (IR_REG_NUM - 1)
#define IR_REG_SCRATCH  (IR_REG_NUM)        /* special name for regset */
#define IR_REG_ALL      (IR_REG_NUM + 1)    /* special name for regset */

#define IR_REGSET_64BIT 1

#define IR_REG_STACK_POINTER \
	IR_REG_X2
#define IR_REG_FRAME_POINTER \
	IR_REG_X8

#define IR_REG_LR  IR_REG_X1
#define IR_REG_ZR  IR_REG_X0

#define IR_REGSET_FIXED \
	( IR_REGSET(IR_REG_ZR) \
	| IR_REGSET(IR_REG_LR) \
	| IR_REGSET(IR_REG_STACK_POINTER) \
	| IR_REGSET(IR_REG_X3) /* platform specific register */ \
	| IR_REGSET(IR_REG_FRAME_POINTER))
#define IR_REGSET_GP \
	IR_REGSET_DIFFERENCE(IR_REGSET_INTERVAL(IR_REG_GP_FIRST, IR_REG_GP_LAST), IR_REGSET_FIXED)
#define IR_REGSET_FP \
	IR_REGSET_DIFFERENCE(IR_REGSET_INTERVAL(IR_REG_FP_FIRST, IR_REG_FP_LAST), IR_REGSET_FIXED)

/* Calling Convention */
#define IR_REG_INT_RET1 IR_REG_X10
#define IR_REG_FP_RET1  IR_REG_F10
#define IR_REG_INT_ARGS 8
#define IR_REG_FP_ARGS  8
#define IR_REG_INT_ARG1 IR_REG_X10
#define IR_REG_INT_ARG2 IR_REG_X11
#define IR_REG_INT_ARG3 IR_REG_X12
#define IR_REG_INT_ARG4 IR_REG_X13
#define IR_REG_INT_ARG5 IR_REG_X14
#define IR_REG_INT_ARG6 IR_REG_X15
#define IR_REG_INT_ARG7 IR_REG_X16
#define IR_REG_INT_ARG8 IR_REG_X17
#define IR_REG_FP_ARG1  IR_REG_F10
#define IR_REG_FP_ARG2  IR_REG_F11
#define IR_REG_FP_ARG3  IR_REG_F12
#define IR_REG_FP_ARG4  IR_REG_F13
#define IR_REG_FP_ARG5  IR_REG_F14
#define IR_REG_FP_ARG6  IR_REG_F15
#define IR_REG_FP_ARG7  IR_REG_F16
#define IR_REG_FP_ARG8  IR_REG_F17
#define IR_MAX_REG_ARGS 16
#define IR_SHADOW_ARGS  0

# define IR_REGSET_SCRATCH \
	(IR_REGSET_INTERVAL(IR_REG_X5, IR_REG_X7) \
	| IR_REGSET_INTERVAL(IR_REG_X10, IR_REG_X17) \
	| IR_REGSET_INTERVAL(IR_REG_X28, IR_REG_X31) \
	\
	| IR_REGSET_INTERVAL(IR_REG_F0, IR_REG_F7) \
	| IR_REGSET_INTERVAL(IR_REG_F10, IR_REG_F17) \
	| IR_REGSET_INTERVAL(IR_REG_F28, IR_REG_F31))

# define IR_REGSET_PRESERVED \
	(IR_REGSET_INTERVAL(IR_REG_X8, IR_REG_X9) \
	| IR_REGSET_INTERVAL(IR_REG_X18, IR_REG_X27) \
	\
	| IR_REGSET_INTERVAL(IR_REG_F8, IR_REG_F9) \
	| IR_REGSET_INTERVAL(IR_REG_F18, IR_REG_F27) \)

typedef struct _ir_tmp_reg {
	union {
		uint8_t num;
		int8_t  reg;
	};
	uint8_t     type;
	uint8_t     start;
	uint8_t     end;
} ir_tmp_reg;

struct _ir_target_constraints {
	int8_t      def_reg;
	uint8_t     tmps_count;
	uint8_t     hints_count;
	ir_tmp_reg  tmp_regs[3];
	int8_t      hints[IR_MAX_REG_ARGS + 3];
};

#endif /* IR_AARCH64_H */
