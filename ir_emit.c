/*
 * IR - Lightweight JIT Compilation Framework
 * (Native code generator based on DynAsm)
 * Copyright (C) 2022 Zend by Perforce.
 * Authors: Dmitry Stogov <dmitry@php.net>
 */

#include "ir.h"

#if defined(IR_TARGET_X86) || defined(IR_TARGET_X64)
# include "ir_x86.h"
#elif defined(IR_TARGET_AARCH64)
# include "ir_aarch64.h"
#elif defined(IR_TARGET_RISCV64)
# include "ir_riscv.h"
#else
# error "Unknown IR target"
#endif

#include "ir_private.h"
#ifndef _WIN32
# include <dlfcn.h>
#else
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <psapi.h>
#endif

#define DASM_M_GROW(ctx, t, p, sz, need) \
  do { \
    size_t _sz = (sz), _need = (need); \
    if (_sz < _need) { \
      if (_sz < 16) _sz = 16; \
      while (_sz < _need) _sz += _sz; \
      (p) = (t *)ir_mem_realloc((p), _sz); \
      (sz) = _sz; \
    } \
  } while(0)

#define DASM_M_FREE(ctx, p, sz) ir_mem_free(p)

#if IR_DEBUG
# define DASM_CHECKS
#endif

typedef struct _ir_copy {
	ir_type type;
	ir_reg  from;
	ir_reg  to;
} ir_copy;

#if IR_REG_INT_ARGS
static const int8_t _ir_int_reg_params[IR_REG_INT_ARGS];
#else
static const int8_t *_ir_int_reg_params;
#endif
#if IR_REG_FP_ARGS
static const int8_t _ir_fp_reg_params[IR_REG_FP_ARGS];
#else
static const int8_t *_ir_fp_reg_params;
#endif

#ifdef IR_HAVE_FASTCALL
static const int8_t _ir_int_fc_reg_params[IR_REG_INT_FCARGS];
static const int8_t *_ir_fp_fc_reg_params;

static bool ir_is_fastcall(const ir_ctx *ctx, const ir_insn *insn)
{
	if (sizeof(void*) == 4) {
		if (IR_IS_CONST_REF(insn->op2)) {
			return (ctx->ir_base[insn->op2].const_flags & IR_CONST_FASTCALL_FUNC) != 0;
		} else if (ctx->ir_base[insn->op2].op == IR_BITCAST) {
			return (ctx->ir_base[insn->op2].op2 & IR_CONST_FASTCALL_FUNC) != 0;
		}
		return 0;
	}
	return 0;
}
#else
# define ir_is_fastcall(ctx, insn) 0
#endif

#ifdef _WIN64
static bool ir_is_vararg(const ir_ctx *ctx, ir_insn *insn)
{
	if (IR_IS_CONST_REF(insn->op2)) {
		return (ctx->ir_base[insn->op2].const_flags & IR_CONST_VARARG_FUNC) != 0;
	} else if (ctx->ir_base[insn->op2].op == IR_BITCAST) {
		return (ctx->ir_base[insn->op2].op2 & IR_CONST_VARARG_FUNC) != 0;
	}
	return 0;
}
#endif

IR_ALWAYS_INLINE uint32_t ir_rule(const ir_ctx *ctx, ir_ref ref)
{
	IR_ASSERT(!IR_IS_CONST_REF(ref));
	return ctx->rules[ref];
}

IR_ALWAYS_INLINE bool ir_in_same_block(ir_ctx *ctx, ir_ref ref)
{
	return ref > ctx->bb_start;
}


static ir_reg ir_get_param_reg(const ir_ctx *ctx, ir_ref ref)
{
	ir_use_list *use_list = &ctx->use_lists[1];
	int i;
	ir_ref use, *p;
	ir_insn *insn;
	int int_param = 0;
	int fp_param = 0;
	int int_reg_params_count = IR_REG_INT_ARGS;
	int fp_reg_params_count = IR_REG_FP_ARGS;
	const int8_t *int_reg_params = _ir_int_reg_params;
	const int8_t *fp_reg_params = _ir_fp_reg_params;

#ifdef IR_HAVE_FASTCALL
	if (sizeof(void*) == 4 && (ctx->flags & IR_FASTCALL_FUNC)) {
		int_reg_params_count = IR_REG_INT_FCARGS;
		fp_reg_params_count = IR_REG_FP_FCARGS;
		int_reg_params = _ir_int_fc_reg_params;
		fp_reg_params = _ir_fp_fc_reg_params;
	}
#endif

	for (i = 0, p = &ctx->use_edges[use_list->refs]; i < use_list->count; i++, p++) {
		use = *p;
		insn = &ctx->ir_base[use];
		if (insn->op == IR_PARAM) {
			if (IR_IS_TYPE_INT(insn->type)) {
				if (use == ref) {
					if (int_param < int_reg_params_count) {
						return int_reg_params[int_param];
					} else {
						return IR_REG_NONE;
					}
				}
				int_param++;
#ifdef _WIN64
				/* WIN64 calling convention use common couter for int and fp registers */
				fp_param++;
#endif
			} else if (IR_IS_TYPE_FP(insn->type)) {
				if (use == ref) {
					if (fp_param < fp_reg_params_count) {
						return fp_reg_params[fp_param];
					} else {
						return IR_REG_NONE;
					}
				}
				fp_param++;
#ifdef _WIN64
				/* WIN64 calling convention use common couter for int and fp registers */
				int_param++;
#endif
			} else {
				IR_ASSERT(0);
			}
		}
	}
	return IR_REG_NONE;
}

static int ir_get_args_regs(const ir_ctx *ctx, const ir_insn *insn, int8_t *regs)
{
	int j, n;
	ir_type type;
	int int_param = 0;
	int fp_param = 0;
	int count = 0;
	int int_reg_params_count = IR_REG_INT_ARGS;
	int fp_reg_params_count = IR_REG_FP_ARGS;
	const int8_t *int_reg_params = _ir_int_reg_params;
	const int8_t *fp_reg_params = _ir_fp_reg_params;

#ifdef IR_HAVE_FASTCALL
	if (sizeof(void*) == 4 && ir_is_fastcall(ctx, insn)) {
		int_reg_params_count = IR_REG_INT_FCARGS;
		fp_reg_params_count = IR_REG_FP_FCARGS;
		int_reg_params = _ir_int_fc_reg_params;
		fp_reg_params = _ir_fp_fc_reg_params;
	}
#endif

	n = insn->inputs_count;
	n = IR_MIN(n, IR_MAX_REG_ARGS + 2);
	for (j = 3; j <= n; j++) {
		type = ctx->ir_base[ir_insn_op(insn, j)].type;
		if (IR_IS_TYPE_INT(type)) {
			if (int_param < int_reg_params_count) {
				regs[j] = int_reg_params[int_param];
				count = j + 1;
			} else {
				regs[j] = IR_REG_NONE;
			}
			int_param++;
#ifdef _WIN64
			/* WIN64 calling convention use common couter for int and fp registers */
			fp_param++;
#endif
		} else if (IR_IS_TYPE_FP(type)) {
			if (fp_param < fp_reg_params_count) {
				regs[j] = fp_reg_params[fp_param];
				count = j + 1;
			} else {
				regs[j] = IR_REG_NONE;
			}
			fp_param++;
#ifdef _WIN64
			/* WIN64 calling convention use common couter for int and fp registers */
			int_param++;
#endif
		} else {
			IR_ASSERT(0);
		}
	}
	return count;
}

static bool ir_is_same_mem(const ir_ctx *ctx, ir_ref r1, ir_ref r2)
{
	ir_live_interval *ival1, *ival2;
	int32_t o1, o2;

	if (IR_IS_CONST_REF(r1) || IR_IS_CONST_REF(r2)) {
		return 0;
	}

	IR_ASSERT(ctx->vregs[r1] && ctx->vregs[r2]);
	ival1 = ctx->live_intervals[ctx->vregs[r1]];
	ival2 = ctx->live_intervals[ctx->vregs[r2]];
	IR_ASSERT(ival1 && ival2);
	o1 = ival1->stack_spill_pos;
	o2 = ival2->stack_spill_pos;
	IR_ASSERT(o1 != -1 && o2 != -1);
	return o1 == o2;
}

static bool ir_is_same_mem_var(const ir_ctx *ctx, ir_ref r1, int32_t offset)
{
	ir_live_interval *ival1;
	int32_t o1;

	if (IR_IS_CONST_REF(r1)) {
		return 0;
	}

	IR_ASSERT(ctx->vregs[r1]);
	ival1 = ctx->live_intervals[ctx->vregs[r1]];
	IR_ASSERT(ival1);
	o1 = ival1->stack_spill_pos;
	IR_ASSERT(o1 != -1);
	return o1 == offset;
}

static void *ir_resolve_sym_name(const char *name)
{
	void *handle = NULL;
	void *addr;

#ifndef _WIN32
# ifdef RTLD_DEFAULT
	handle = RTLD_DEFAULT;
# endif
	addr = dlsym(handle, name);
#else
	HMODULE mods[256];
	DWORD cbNeeded;
	uint32_t i = 0;

	/* Quick workaraund to prevent *.irt tests failures */
	// TODO: try to find a general solution ???
	if (strcmp(name, "printf") == 0) {
		return (void*)printf;
	}

	addr = NULL;

	EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &cbNeeded);

	while(i < (cbNeeded / sizeof(HMODULE))) {
		addr = GetProcAddress(mods[i], name);
		if (addr) {
			return addr;
		}
		i++;
	}
#endif
	IR_ASSERT(addr != NULL);
	return addr;
}

#ifdef IR_SNAPSHOT_HANDLER_DCL
	IR_SNAPSHOT_HANDLER_DCL();
#endif

static void *ir_jmp_addr(ir_ctx *ctx, ir_insn *insn, ir_insn *addr_insn)
{
	void *addr;

	IR_ASSERT(addr_insn->type == IR_ADDR);
	if (addr_insn->op == IR_FUNC) {
		addr = ir_resolve_sym_name(ir_get_str(ctx, addr_insn->val.i32));
	} else {
		IR_ASSERT(addr_insn->op == IR_ADDR || addr_insn->op == IR_FUNC_ADDR);
		addr = (void*)addr_insn->val.addr;
	}
#ifdef IR_SNAPSHOT_HANDLER
	if (ctx->ir_base[insn->op1].op == IR_SNAPSHOT) {
		addr = IR_SNAPSHOT_HANDLER(ctx, insn->op1, &ctx->ir_base[insn->op1], addr);
	}
#endif
	return addr;
}

#if defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Warray-bounds"
# pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

#if defined(IR_TARGET_X86) || defined(IR_TARGET_X64)
# include "dynasm/dasm_proto.h"
# include "dynasm/dasm_x86.h"
#elif defined(IR_TARGET_AARCH64)
# include "dynasm/dasm_proto.h"
# include "dynasm/dasm_arm64.h"
#elif defined(IR_TARGET_RISCV64)
# include "dynasm/dasm_proto.h"
# include "dynasm/dasm_riscv64.h"
#else
# error "Unknown IR target"
#endif

#if defined(__GNUC__)
# pragma GCC diagnostic pop
#endif


/* Forward Declarations */
static void ir_emit_osr_entry_loads(ir_ctx *ctx, int b, ir_block *bb);
static void ir_emit_dessa_moves(ir_ctx *ctx, int b, ir_block *bb);

#if defined(IR_TARGET_X86) || defined(IR_TARGET_X64)
# include "ir_emit_x86.h"
#elif defined(IR_TARGET_AARCH64)
# include "ir_emit_aarch64.h"
#elif defined(IR_TARGET_RISCV64)
# include "ir_emit_riscv64.h"
#else
# error "Unknown IR target"
#endif

static IR_NEVER_INLINE void ir_emit_osr_entry_loads(ir_ctx *ctx, int b, ir_block *bb)
{
	ir_list *list = (ir_list*)ctx->osr_entry_loads;
	int pos = 0, count, i;
	ir_ref ref;

	IR_ASSERT(ctx->binding);
	IR_ASSERT(list);
	while (1) {
		i = ir_list_at(list, pos);
		if (b == i) {
			break;
		}
		IR_ASSERT(i != 0); /* end marker */
		pos++;
		count = ir_list_at(list, pos);
		pos += count + 1;
	}
	pos++;
	count = ir_list_at(list, pos);
	pos++;

	for (i = 0; i < count; i++, pos++) {
		ref = ir_list_at(list, pos);
		IR_ASSERT(ref >= 0 && ctx->vregs[ref] && ctx->live_intervals[ctx->vregs[ref]]);
		if (ctx->live_intervals[ctx->vregs[ref]]->stack_spill_pos == -1) {
			/* not spilled */
			ir_reg reg = ctx->live_intervals[ctx->vregs[ref]]->reg;
			ir_type type = ctx->ir_base[ref].type;
			int32_t offset = -ir_binding_find(ctx, ref);

			IR_ASSERT(offset > 0);
			if (IR_IS_TYPE_INT(type)) {
				ir_emit_load_mem_int(ctx, type, reg, ctx->spill_base, offset);
			} else {
				ir_emit_load_mem_fp(ctx, type, reg, ctx->spill_base, offset);
			}
		}
	}
}

static void ir_emit_dessa_moves(ir_ctx *ctx, int b, ir_block *bb)
{
	uint32_t succ, k, n = 0;
	ir_block *succ_bb;
	ir_use_list *use_list;
	ir_ref i, *p, ref, input;
	ir_insn *insn;
	bool do_third_pass = 0;
	ir_copy *copies;
	ir_reg tmp_reg = ctx->regs[bb->end][0];
	ir_reg tmp_fp_reg = ctx->regs[bb->end][1];
	ir_reg src, dst;

	IR_ASSERT(bb->successors_count == 1);
	succ = ctx->cfg_edges[bb->successors];
	succ_bb = &ctx->cfg_blocks[succ];
	IR_ASSERT(succ_bb->predecessors_count > 1);
	use_list = &ctx->use_lists[succ_bb->start];
	k = ir_phi_input_number(ctx, succ_bb, b);

	copies = ir_mem_malloc(use_list->count * sizeof(ir_copy));

	for (i = 0, p = &ctx->use_edges[use_list->refs]; i < use_list->count; i++, p++) {
		ref = *p;
		insn = &ctx->ir_base[ref];
		if (insn->op == IR_PHI) {
			input = ir_insn_op(insn, k);
			if (IR_IS_CONST_REF(input)) {
				do_third_pass = 1;
			} else {
				dst = IR_REG_NUM(ctx->regs[ref][0]);
				src = ir_get_alocated_reg(ctx, ref, k);

				if (dst == IR_REG_NONE) {
					/* STORE to memory */
					if (src == IR_REG_NONE) {
						if (!ir_is_same_mem(ctx, input, ref)) {
							ir_reg tmp = IR_IS_TYPE_INT(insn->type) ?  tmp_reg : tmp_fp_reg;

							IR_ASSERT(tmp != IR_REG_NONE);
							ir_emit_load(ctx, insn->type, tmp, input);
							ir_emit_store(ctx, insn->type, ref, tmp);
						}
					} else {
						if (src & IR_REG_SPILL_LOAD) {
							src &= ~IR_REG_SPILL_LOAD;
							ir_emit_load(ctx, insn->type, src, input);
							if (ir_is_same_mem(ctx, input, ref)) {
								continue;
							}
						}
						ir_emit_store(ctx, insn->type, src, ref);
					}
				} else {
					if (src == IR_REG_NONE) {
						do_third_pass = 1;
					} else {
						if (src & IR_REG_SPILL_LOAD) {
							src &= ~IR_REG_SPILL_LOAD;
							ir_emit_load(ctx, insn->type, src, input);
						}
						if (src != dst) {
							copies[n].type = insn->type;
							copies[n].from = src;
							copies[n].to = dst;
							n++;
						}
					}
				}
				if (ctx->regs[ref][0] & IR_REG_SPILL_STORE) {
					do_third_pass = 1;
				}
			}
		}
	}

	if (n > 0) {
		ir_parallel_copy(ctx, copies, n, tmp_reg, tmp_fp_reg);
	}
	ir_mem_free(copies);

	if (do_third_pass) {
		for (i = 0, p = &ctx->use_edges[use_list->refs]; i < use_list->count; i++, p++) {
			ref = *p;
			insn = &ctx->ir_base[ref];
			if (insn->op == IR_PHI) {
				input = ir_insn_op(insn, k);
				dst = IR_REG_NUM(ctx->regs[ref][0]);

				if (IR_IS_CONST_REF(input)) {
					if (dst == IR_REG_NONE) {
#if defined(IR_TARGET_X86) || defined(IR_TARGET_X64)
						if (IR_IS_TYPE_INT(insn->type) && (ir_type_size[insn->type] != 8 || IR_IS_SIGNED_32BIT(ctx->ir_base[input].val.i64))) {
							ir_emit_store_imm(ctx, insn->type, ref, ctx->ir_base[input].val.i32);
							continue;
						}
#endif
						ir_reg tmp = IR_IS_TYPE_INT(insn->type) ?  tmp_reg : tmp_fp_reg;

						IR_ASSERT(tmp != IR_REG_NONE);
						ir_emit_load(ctx, insn->type, tmp, input);
						ir_emit_store(ctx, insn->type, ref, tmp);
					} else {
						ir_emit_load(ctx, insn->type, dst, input);
					}
				} else if (dst != IR_REG_NONE) {
					 src = ir_get_alocated_reg(ctx, ref, k);

					if (src == IR_REG_NONE) {
						if ((ctx->regs[ref][0] & IR_REG_SPILL_STORE) && ir_is_same_mem(ctx, input, ref)) {
							/* avoid LOAD and SAVE to the same memory */
							continue;
						}
						ir_emit_load(ctx, insn->type, dst, input);
					}
				}
				if (dst != IR_REG_NONE && (ctx->regs[ref][0] & IR_REG_SPILL_STORE)) {
					ir_emit_store(ctx, insn->type, ref, dst);
				}
			}
		}
	}
}

int ir_match(ir_ctx *ctx)
{
	uint32_t b;
	ir_ref start, ref, *prev_ref;
	ir_block *bb;
	ir_insn *insn;
	uint32_t entries_count = 0;

	ctx->rules = ir_mem_calloc(ctx->insns_count, sizeof(uint32_t));

	prev_ref = ctx->prev_ref;
	if (!prev_ref) {
		ir_build_prev_refs(ctx);
		prev_ref = ctx->prev_ref;
	}

	if (ctx->entries_count) {
		ctx->entries = ir_mem_malloc(ctx->entries_count * sizeof(ir_ref));
	}

	for (b = ctx->cfg_blocks_count, bb = ctx->cfg_blocks + b; b > 0; b--, bb--) {
		IR_ASSERT(!(bb->flags & IR_BB_UNREACHABLE));
		start = bb->start;
		if (UNEXPECTED(bb->flags & IR_BB_ENTRY)) {
			IR_ASSERT(entries_count < ctx->entries_count);
			insn = &ctx->ir_base[start];
			IR_ASSERT(insn->op == IR_ENTRY);
			insn->op3 = entries_count;
			ctx->entries[entries_count] = b;
			entries_count++;
		}
		ctx->rules[start] = IR_SKIPPED | IR_NOP;
		ref = bb->end;
		if (bb->successors_count == 1) {
			insn = &ctx->ir_base[ref];
			if (insn->op == IR_END || insn->op == IR_LOOP_END) {
				ctx->rules[ref] = insn->op;
				ref = prev_ref[ref];
				if (ref == start) {
					if (EXPECTED(!(bb->flags & IR_BB_ENTRY))) {
						bb->flags |= IR_BB_EMPTY;
					} else if (ctx->flags & IR_MERGE_EMPTY_ENTRIES) {
						bb->flags |= IR_BB_EMPTY;
						if (ctx->cfg_edges[bb->successors] == b + 1) {
							(bb + 1)->flags |= IR_BB_PREV_EMPTY_ENTRY;
						}
					}
					continue;
				}
			}
		}

		ctx->bb_start = start; /* bb_start is used by matcher to avoid fusion of insns from different blocks */

		while (ref != start) {
			uint32_t rule = ctx->rules[ref];

			if (!rule) {
				ctx->rules[ref] = rule = ir_match_insn(ctx, ref);
			}
			ir_match_insn2(ctx, ref, rule);
			ref = prev_ref[ref];
		}
	}

	if (ctx->entries_count) {
		ctx->entries_count = entries_count;
		if (!entries_count) {
			ir_mem_free(ctx->entries);
			ctx->entries = NULL;
		}
	}

	return 1;
}
