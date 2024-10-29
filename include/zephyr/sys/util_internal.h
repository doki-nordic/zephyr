/*
 * Copyright (c) 2011-2014, Wind River Systems, Inc.
 * Copyright (c) 2020, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Misc utilities
 *
 * Repetitive or obscure helper macros needed by sys/util.h.
 */

#ifndef ZEPHYR_INCLUDE_SYS_UTIL_INTERNAL_H_
#define ZEPHYR_INCLUDE_SYS_UTIL_INTERNAL_H_

#include "util_loops.h"

/* IS_ENABLED() helpers */

/* This is called from IS_ENABLED(), and sticks on a "_XXXX" prefix,
 * it will now be "_XXXX1" if config_macro is "1", or just "_XXXX" if it's
 * undefined.
 *   ENABLED:   Z_IS_ENABLED2(_XXXX1)
 *   DISABLED   Z_IS_ENABLED2(_XXXX)
 */
#define Z_IS_ENABLED1(config_macro) Z_IS_ENABLED2(_XXXX##config_macro)

/* Here's the core trick, we map "_XXXX1" to "_YYYY," (i.e. a string
 * with a trailing comma), so it has the effect of making this a
 * two-argument tuple to the preprocessor only in the case where the
 * value is defined to "1"
 *   ENABLED:    _YYYY,    <--- note comma!
 *   DISABLED:   _XXXX
 */
#define _XXXX1 _YYYY,

/* Then we append an extra argument to fool the gcc preprocessor into
 * accepting it as a varargs macro.
 *                         arg1   arg2  arg3
 *   ENABLED:   Z_IS_ENABLED3(_YYYY,    1,    0)
 *   DISABLED   Z_IS_ENABLED3(_XXXX 1,  0)
 */
#define Z_IS_ENABLED2(one_or_two_args) Z_IS_ENABLED3(one_or_two_args 1, 0)

/* And our second argument is thus now cooked to be 1 in the case
 * where the value is defined to 1, and 0 if not:
 */
#define Z_IS_ENABLED3(ignore_this, val, ...) val

/* Implementation of IS_EQ(). Returns 1 if _0 and _1 are the same integer from
 * 0 to 4095, 0 otherwise.
 */
#define Z_IS_EQ(_0, _1) Z_HAS_COMMA(Z_CAT4(Z_IS_, _0, _EQ_, _1)())

/* Used internally by COND_CODE_1 and COND_CODE_0. */
#define Z_COND_CODE_1(_flag, _if_1_code, _else_code) \
	__COND_CODE(_XXXX##_flag, _if_1_code, _else_code)
#define Z_COND_CODE_0(_flag, _if_0_code, _else_code) \
	__COND_CODE(_ZZZZ##_flag, _if_0_code, _else_code)
#define _ZZZZ0 _YYYY,
#define __COND_CODE(one_or_two_args, _if_code, _else_code) \
	__GET_ARG2_DEBRACKET(one_or_two_args _if_code, _else_code)

/* Gets second argument and removes brackets around that argument. It
 * is expected that the parameter is provided in brackets/parentheses.
 */
#define __GET_ARG2_DEBRACKET(ignore_this, val, ...) __DEBRACKET val

/* Used to remove brackets from around a single argument. */
#define __DEBRACKET(...) __VA_ARGS__

/* Used by IS_EMPTY() */
/* reference: https://gustedt.wordpress.com/2010/06/08/detect-empty-macro-arguments/ */
#define Z_HAS_COMMA(...) \
	NUM_VA_ARGS_LESS_1_IMPL(__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, \
	 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
	 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, \
	 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)
#define Z_TRIGGER_PARENTHESIS_(...) ,
#define Z_IS_EMPTY_(...) \
	Z_IS_EMPTY__( \
		Z_HAS_COMMA(__VA_ARGS__), \
		Z_HAS_COMMA(Z_TRIGGER_PARENTHESIS_ __VA_ARGS__), \
		Z_HAS_COMMA(__VA_ARGS__ (/*empty*/)), \
		Z_HAS_COMMA(Z_TRIGGER_PARENTHESIS_ __VA_ARGS__ (/*empty*/)))
#define Z_CAT4(_0, _1, _2, _3) _0 ## _1 ## _2 ## _3
#define Z_CAT5(_0, _1, _2, _3, _4) _0 ## _1 ## _2 ## _3 ## _4
#define Z_IS_EMPTY__(_0, _1, _2, _3) \
	Z_HAS_COMMA(Z_CAT5(Z_IS_EMPTY_CASE_, _0, _1, _2, _3))
#define Z_IS_EMPTY_CASE_0001 ,

/* Used by LIST_DROP_EMPTY() */
/* Adding ',' after each element would add empty element at the end of
 * list, which is hard to remove, so instead precede each element with ',',
 * this way first element is empty, and this one is easy to drop.
 */
#define Z_LIST_ADD_ELEM(e) EMPTY, e
#define Z_LIST_DROP_FIRST(...) GET_ARGS_LESS_N(1, __VA_ARGS__)
#define Z_LIST_NO_EMPTIES(e) \
	COND_CODE_1(IS_EMPTY(e), (), (Z_LIST_ADD_ELEM(e)))

#define UTIL_CAT(a, ...) UTIL_PRIMITIVE_CAT(a, __VA_ARGS__)
#define UTIL_PRIMITIVE_CAT(a, ...) a##__VA_ARGS__
#define UTIL_CHECK_N(x, n, ...) n
#define UTIL_CHECK(...) UTIL_CHECK_N(__VA_ARGS__, 0,)
#define UTIL_NOT(x) UTIL_CHECK(UTIL_PRIMITIVE_CAT(UTIL_NOT_, x))
#define UTIL_NOT_0 ~, 1,
#define UTIL_COMPL(b) UTIL_PRIMITIVE_CAT(UTIL_COMPL_, b)
#define UTIL_COMPL_0 1
#define UTIL_COMPL_1 0
#define UTIL_BOOL(x) UTIL_COMPL(UTIL_NOT(x))

#define UTIL_EVAL(...) __VA_ARGS__
#define UTIL_EXPAND(...) __VA_ARGS__
#define UTIL_REPEAT(...) UTIL_LISTIFY(__VA_ARGS__)

#define _CONCAT_0(arg, ...) arg
#define _CONCAT_1(arg, ...) UTIL_CAT(arg, _CONCAT_0(__VA_ARGS__))
#define _CONCAT_2(arg, ...) UTIL_CAT(arg, _CONCAT_1(__VA_ARGS__))
#define _CONCAT_3(arg, ...) UTIL_CAT(arg, _CONCAT_2(__VA_ARGS__))
#define _CONCAT_4(arg, ...) UTIL_CAT(arg, _CONCAT_3(__VA_ARGS__))
#define _CONCAT_5(arg, ...) UTIL_CAT(arg, _CONCAT_4(__VA_ARGS__))
#define _CONCAT_6(arg, ...) UTIL_CAT(arg, _CONCAT_5(__VA_ARGS__))
#define _CONCAT_7(arg, ...) UTIL_CAT(arg, _CONCAT_6(__VA_ARGS__))

/* Implementation details for NUM_VA_ARGS_LESS_1 */
#define NUM_VA_ARGS_LESS_1_IMPL(				\
	_ignored,						\
	_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,		\
	_11, _12, _13, _14, _15, _16, _17, _18, _19, _20,	\
	_21, _22, _23, _24, _25, _26, _27, _28, _29, _30,	\
	_31, _32, _33, _34, _35, _36, _37, _38, _39, _40,	\
	_41, _42, _43, _44, _45, _46, _47, _48, _49, _50,	\
	_51, _52, _53, _54, _55, _56, _57, _58, _59, _60,	\
	_61, _62, N, ...) N

/* Used by MACRO_MAP_CAT */
#define MACRO_MAP_CAT_(...)						\
	/* To make sure it works also for 2 arguments in total */	\
	MACRO_MAP_CAT_N(NUM_VA_ARGS_LESS_1(__VA_ARGS__), __VA_ARGS__)
#define MACRO_MAP_CAT_N_(N, ...) UTIL_CAT(MACRO_MC_, N)(__VA_ARGS__,)
#define MACRO_MC_0(...)
#define MACRO_MC_1(m, a, ...)  m(a)
#define MACRO_MC_2(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_1(m, __VA_ARGS__,))
#define MACRO_MC_3(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_2(m, __VA_ARGS__,))
#define MACRO_MC_4(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_3(m, __VA_ARGS__,))
#define MACRO_MC_5(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_4(m, __VA_ARGS__,))
#define MACRO_MC_6(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_5(m, __VA_ARGS__,))
#define MACRO_MC_7(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_6(m, __VA_ARGS__,))
#define MACRO_MC_8(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_7(m, __VA_ARGS__,))
#define MACRO_MC_9(m, a, ...)  UTIL_CAT(m(a), MACRO_MC_8(m, __VA_ARGS__,))
#define MACRO_MC_10(m, a, ...) UTIL_CAT(m(a), MACRO_MC_9(m, __VA_ARGS__,))
#define MACRO_MC_11(m, a, ...) UTIL_CAT(m(a), MACRO_MC_10(m, __VA_ARGS__,))
#define MACRO_MC_12(m, a, ...) UTIL_CAT(m(a), MACRO_MC_11(m, __VA_ARGS__,))
#define MACRO_MC_13(m, a, ...) UTIL_CAT(m(a), MACRO_MC_12(m, __VA_ARGS__,))
#define MACRO_MC_14(m, a, ...) UTIL_CAT(m(a), MACRO_MC_13(m, __VA_ARGS__,))
#define MACRO_MC_15(m, a, ...) UTIL_CAT(m(a), MACRO_MC_14(m, __VA_ARGS__,))

/* Used by Z_IS_EQ */
#include "util_internal_is_eq.h"

/*
 * Generic sparse list of odd numbers (check the implementation of
 * GPIO_DT_RESERVED_RANGES_NGPIOS as a usage example)
 */
#define Z_SPARSE_LIST_ODD_NUMBERS		\
	EMPTY,  1, EMPTY,  3, EMPTY,  5, EMPTY,  7, \
	EMPTY,  9, EMPTY, 11, EMPTY, 13, EMPTY, 15, \
	EMPTY, 17, EMPTY, 19, EMPTY, 21, EMPTY, 23, \
	EMPTY, 25, EMPTY, 27, EMPTY, 29, EMPTY, 31, \
	EMPTY, 33, EMPTY, 35, EMPTY, 37, EMPTY, 39, \
	EMPTY, 41, EMPTY, 43, EMPTY, 45, EMPTY, 47, \
	EMPTY, 49, EMPTY, 51, EMPTY, 53, EMPTY, 55, \
	EMPTY, 57, EMPTY, 59, EMPTY, 61, EMPTY, 63

/*
 * Generic sparse list of even numbers (check the implementation of
 * GPIO_DT_RESERVED_RANGES_NGPIOS as a usage example)
 */
#define Z_SPARSE_LIST_EVEN_NUMBERS		\
	 0, EMPTY,  2, EMPTY,  4, EMPTY,  6, EMPTY, \
	 8, EMPTY, 10, EMPTY, 12, EMPTY, 14, EMPTY, \
	16, EMPTY, 18, EMPTY, 20, EMPTY, 22, EMPTY, \
	24, EMPTY, 26, EMPTY, 28, EMPTY, 30, EMPTY, \
	32, EMPTY, 34, EMPTY, 36, EMPTY, 38, EMPTY, \
	40, EMPTY, 42, EMPTY, 44, EMPTY, 46, EMPTY, \
	48, EMPTY, 50, EMPTY, 52, EMPTY, 54, EMPTY, \
	56, EMPTY, 58, EMPTY, 60, EMPTY, 62, EMPTY

/*
 * Used to combine multiple MIN and MAX macros into one variadic macro.
 */
#define Z_COMBINE_BINOP(OP, first, ...) CONCAT(_COMBINE_BINOP_, \
	UTIL_INC(NUM_VA_ARGS(__VA_ARGS__)))(OP, first, ##__VA_ARGS__)

#define _COMBINE_BINOP_1(OP, a1) (a1)
#define _COMBINE_BINOP_2(OP, a1, a2) OP((a1), (a2))
#define _COMBINE_BINOP_3(OP, a1, a2, a3) OP((a1), OP(a2, a3))
#define _COMBINE_BINOP_4(OP, a1, a2, a3, a4) OP(OP(a1, a2), OP(a3, a4))
#define _COMBINE_BINOP_5(OP, a1, a2, a3, a4, a5) OP(OP(a1, a2),                \
	_COMBINE_BINOP_3(OP, (a3), (a4), (a5)))
#define _COMBINE_BINOP_6(OP, a1, a2, a3, a4, a5, a6) OP(_COMBINE_BINOP_3(OP,   \
	(a1), (a2), (a3)), _COMBINE_BINOP_3(OP, (a4), (a5), (a6)))
#define _COMBINE_BINOP_7(OP, a1, a2, a3, a4, a5, a6, a7)                       \
	OP(_COMBINE_BINOP_3(OP, (a1), (a2), (a3)), _COMBINE_BINOP_4(OP, (a4),  \
	(a5), (a6), (a7)))
#define _COMBINE_BINOP_8(OP, a1, a2, a3, a4, a5, a6, a7, a8)                   \
	OP(_COMBINE_BINOP_4(OP, (a1), (a2), (a3), (a4)), _COMBINE_BINOP_4(OP,  \
	(a5), (a6), (a7), (a8)))
#define _COMBINE_BINOP_9(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9)               \
	OP(_COMBINE_BINOP_4(OP, (a1), (a2), (a3), (a4)), _COMBINE_BINOP_5(OP,  \
	(a5), (a6), (a7), (a8), (a9)))
#define _COMBINE_BINOP_10(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10)         \
	OP(_COMBINE_BINOP_5(OP, (a1), (a2), (a3), (a4), (a5)),                 \
	_COMBINE_BINOP_5(OP, (a6), (a7), (a8), (a9), (a10)))
#define _COMBINE_BINOP_11(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11)    \
	OP(_COMBINE_BINOP_5(OP, (a1), (a2), (a3), (a4), (a5)),                 \
	_COMBINE_BINOP_6(OP, (a6), (a7), (a8), (a9), (a10), (a11)))
#define _COMBINE_BINOP_12(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12) OP(_COMBINE_BINOP_6(OP, (a1), (a2), (a3), (a4), (a5), (a6)),      \
	_COMBINE_BINOP_6(OP, (a7), (a8), (a9), (a10), (a11), (a12)))
#define _COMBINE_BINOP_13(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13) OP(_COMBINE_BINOP_6(OP, (a1), (a2), (a3), (a4), (a5), (a6)), \
	_COMBINE_BINOP_7(OP, (a7), (a8), (a9), (a10), (a11), (a12), (a13)))
#define _COMBINE_BINOP_14(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14) OP(_COMBINE_BINOP_7(OP, (a1), (a2), (a3), (a4), (a5),   \
	(a6), (a7)), _COMBINE_BINOP_7(OP, (a8), (a9), (a10), (a11), (a12),     \
	(a13), (a14)))
#define _COMBINE_BINOP_15(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15) OP(_COMBINE_BINOP_7(OP, (a1), (a2), (a3), (a4),    \
	(a5), (a6), (a7)), _COMBINE_BINOP_8(OP, (a8), (a9), (a10), (a11),      \
	(a12), (a13), (a14), (a15)))
#define _COMBINE_BINOP_16(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16) OP(_COMBINE_BINOP_8(OP, (a1), (a2), (a3),     \
	(a4), (a5), (a6), (a7), (a8)), _COMBINE_BINOP_8(OP, (a9), (a10),       \
	(a11), (a12), (a13), (a14), (a15), (a16)))
#define _COMBINE_BINOP_17(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17) OP(_COMBINE_BINOP_8(OP, (a1), (a2),      \
	(a3), (a4), (a5), (a6), (a7), (a8)), _COMBINE_BINOP_9(OP, (a9), (a10), \
	(a11), (a12), (a13), (a14), (a15), (a16), (a17)))
#define _COMBINE_BINOP_18(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18) OP(_COMBINE_BINOP_9(OP, (a1), (a2), \
	(a3), (a4), (a5), (a6), (a7), (a8), (a9)), _COMBINE_BINOP_9(OP, (a10), \
	(a11), (a12), (a13), (a14), (a15), (a16), (a17), (a18)))
#define _COMBINE_BINOP_19(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19) OP(_COMBINE_BINOP_9(OP, (a1),  \
	(a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9)), _COMBINE_BINOP_10(OP, \
	(a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17), (a18), (a19)))
#define _COMBINE_BINOP_20(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20) OP(_COMBINE_BINOP_10(OP,  \
	(a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10)),          \
	_COMBINE_BINOP_10(OP, (a11), (a12), (a13), (a14), (a15), (a16), (a17), \
	(a18), (a19), (a20)))
#define _COMBINE_BINOP_21(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21)                      \
	OP(_COMBINE_BINOP_10(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10)), _COMBINE_BINOP_11(OP, (a11), (a12), (a13), (a14),  \
	(a15), (a16), (a17), (a18), (a19), (a20), (a21)))
#define _COMBINE_BINOP_22(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22)                 \
	OP(_COMBINE_BINOP_11(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11)), _COMBINE_BINOP_11(OP, (a12), (a13), (a14),  \
	(a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22)))
#define _COMBINE_BINOP_23(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23)            \
	OP(_COMBINE_BINOP_11(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11)), _COMBINE_BINOP_12(OP, (a12), (a13), (a14),  \
	(a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22), (a23)))
#define _COMBINE_BINOP_24(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24)       \
	OP(_COMBINE_BINOP_12(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12)), _COMBINE_BINOP_12(OP, (a13), (a14),  \
	(a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22), (a23), (a24)))
#define _COMBINE_BINOP_25(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25)  \
	OP(_COMBINE_BINOP_12(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12)), _COMBINE_BINOP_13(OP, (a13), (a14),  \
	(a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22), (a23), (a24),  \
	(a25)))
#define _COMBINE_BINOP_26(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26) OP(_COMBINE_BINOP_13(OP, (a1), (a2), (a3), (a4), (a5), (a6),      \
	(a7), (a8), (a9), (a10), (a11), (a12), (a13)), _COMBINE_BINOP_13(OP,   \
	(a14), (a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22), (a23),  \
	(a24), (a25), (a26)))
#define _COMBINE_BINOP_27(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27) OP(_COMBINE_BINOP_13(OP, (a1), (a2), (a3), (a4), (a5), (a6), \
	(a7), (a8), (a9), (a10), (a11), (a12), (a13)), _COMBINE_BINOP_14(OP,   \
	(a14), (a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22), (a23),  \
	(a24), (a25), (a26), (a27)))
#define _COMBINE_BINOP_28(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28) OP(_COMBINE_BINOP_14(OP, (a1), (a2), (a3), (a4), (a5),  \
	(a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14)),            \
	_COMBINE_BINOP_14(OP, (a15), (a16), (a17), (a18), (a19), (a20), (a21), \
	(a22), (a23), (a24), (a25), (a26), (a27), (a28)))
#define _COMBINE_BINOP_29(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29) OP(_COMBINE_BINOP_14(OP, (a1), (a2), (a3), (a4),   \
	(a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14)),      \
	_COMBINE_BINOP_15(OP, (a15), (a16), (a17), (a18), (a19), (a20), (a21), \
	(a22), (a23), (a24), (a25), (a26), (a27), (a28), (a29)))
#define _COMBINE_BINOP_30(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30) OP(_COMBINE_BINOP_15(OP, (a1), (a2), (a3),    \
	(a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14), \
	(a15)), _COMBINE_BINOP_15(OP, (a16), (a17), (a18), (a19), (a20),       \
	(a21), (a22), (a23), (a24), (a25), (a26), (a27), (a28), (a29), (a30)))
#define _COMBINE_BINOP_31(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31) OP(_COMBINE_BINOP_15(OP, (a1), (a2),     \
	(a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13),  \
	(a14), (a15)), _COMBINE_BINOP_16(OP, (a16), (a17), (a18), (a19),       \
	(a20), (a21), (a22), (a23), (a24), (a25), (a26), (a27), (a28), (a29),  \
	(a30), (a31)))
#define _COMBINE_BINOP_32(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32) OP(_COMBINE_BINOP_16(OP, (a1),      \
	(a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12),   \
	(a13), (a14), (a15), (a16)), _COMBINE_BINOP_16(OP, (a17), (a18),       \
	(a19), (a20), (a21), (a22), (a23), (a24), (a25), (a26), (a27), (a28),  \
	(a29), (a30), (a31), (a32)))
#define _COMBINE_BINOP_33(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33) OP(_COMBINE_BINOP_16(OP, (a1), \
	(a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12),   \
	(a13), (a14), (a15), (a16)), _COMBINE_BINOP_17(OP, (a17), (a18),       \
	(a19), (a20), (a21), (a22), (a23), (a24), (a25), (a26), (a27), (a28),  \
	(a29), (a30), (a31), (a32), (a33)))
#define _COMBINE_BINOP_34(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34) OP(_COMBINE_BINOP_17(OP,  \
	(a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11),    \
	(a12), (a13), (a14), (a15), (a16), (a17)), _COMBINE_BINOP_17(OP,       \
	(a18), (a19), (a20), (a21), (a22), (a23), (a24), (a25), (a26), (a27),  \
	(a28), (a29), (a30), (a31), (a32), (a33), (a34)))
#define _COMBINE_BINOP_35(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35)                      \
	OP(_COMBINE_BINOP_17(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17)),   \
	_COMBINE_BINOP_18(OP, (a18), (a19), (a20), (a21), (a22), (a23), (a24), \
	(a25), (a26), (a27), (a28), (a29), (a30), (a31), (a32), (a33), (a34),  \
	(a35)))
#define _COMBINE_BINOP_36(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36)                 \
	OP(_COMBINE_BINOP_18(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17),    \
	(a18)), _COMBINE_BINOP_18(OP, (a19), (a20), (a21), (a22), (a23),       \
	(a24), (a25), (a26), (a27), (a28), (a29), (a30), (a31), (a32), (a33),  \
	(a34), (a35), (a36)))
#define _COMBINE_BINOP_37(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37)            \
	OP(_COMBINE_BINOP_18(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17),    \
	(a18)), _COMBINE_BINOP_19(OP, (a19), (a20), (a21), (a22), (a23),       \
	(a24), (a25), (a26), (a27), (a28), (a29), (a30), (a31), (a32), (a33),  \
	(a34), (a35), (a36), (a37)))
#define _COMBINE_BINOP_38(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38)       \
	OP(_COMBINE_BINOP_19(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17),    \
	(a18), (a19)), _COMBINE_BINOP_19(OP, (a20), (a21), (a22), (a23),       \
	(a24), (a25), (a26), (a27), (a28), (a29), (a30), (a31), (a32), (a33),  \
	(a34), (a35), (a36), (a37), (a38)))
#define _COMBINE_BINOP_39(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39)  \
	OP(_COMBINE_BINOP_19(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17),    \
	(a18), (a19)), _COMBINE_BINOP_20(OP, (a20), (a21), (a22), (a23),       \
	(a24), (a25), (a26), (a27), (a28), (a29), (a30), (a31), (a32), (a33),  \
	(a34), (a35), (a36), (a37), (a38), (a39)))
#define _COMBINE_BINOP_40(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40) OP(_COMBINE_BINOP_20(OP, (a1), (a2), (a3), (a4), (a5), (a6),      \
	(a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16),     \
	(a17), (a18), (a19), (a20)), _COMBINE_BINOP_20(OP, (a21), (a22),       \
	(a23), (a24), (a25), (a26), (a27), (a28), (a29), (a30), (a31), (a32),  \
	(a33), (a34), (a35), (a36), (a37), (a38), (a39), (a40)))
#define _COMBINE_BINOP_41(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41) OP(_COMBINE_BINOP_20(OP, (a1), (a2), (a3), (a4), (a5), (a6), \
	(a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16),     \
	(a17), (a18), (a19), (a20)), _COMBINE_BINOP_21(OP, (a21), (a22),       \
	(a23), (a24), (a25), (a26), (a27), (a28), (a29), (a30), (a31), (a32),  \
	(a33), (a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41)))
#define _COMBINE_BINOP_42(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42) OP(_COMBINE_BINOP_21(OP, (a1), (a2), (a3), (a4), (a5),  \
	(a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15),      \
	(a16), (a17), (a18), (a19), (a20), (a21)), _COMBINE_BINOP_21(OP,       \
	(a22), (a23), (a24), (a25), (a26), (a27), (a28), (a29), (a30), (a31),  \
	(a32), (a33), (a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41),  \
	(a42)))
#define _COMBINE_BINOP_43(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43) OP(_COMBINE_BINOP_21(OP, (a1), (a2), (a3), (a4),   \
	(a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14),       \
	(a15), (a16), (a17), (a18), (a19), (a20), (a21)),                      \
	_COMBINE_BINOP_22(OP, (a22), (a23), (a24), (a25), (a26), (a27), (a28), \
	(a29), (a30), (a31), (a32), (a33), (a34), (a35), (a36), (a37), (a38),  \
	(a39), (a40), (a41), (a42), (a43)))
#define _COMBINE_BINOP_44(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44) OP(_COMBINE_BINOP_22(OP, (a1), (a2), (a3),    \
	(a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14), \
	(a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22)),               \
	_COMBINE_BINOP_22(OP, (a23), (a24), (a25), (a26), (a27), (a28), (a29), \
	(a30), (a31), (a32), (a33), (a34), (a35), (a36), (a37), (a38), (a39),  \
	(a40), (a41), (a42), (a43), (a44)))
#define _COMBINE_BINOP_45(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45) OP(_COMBINE_BINOP_22(OP, (a1), (a2),     \
	(a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13),  \
	(a14), (a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22)),        \
	_COMBINE_BINOP_23(OP, (a23), (a24), (a25), (a26), (a27), (a28), (a29), \
	(a30), (a31), (a32), (a33), (a34), (a35), (a36), (a37), (a38), (a39),  \
	(a40), (a41), (a42), (a43), (a44), (a45)))
#define _COMBINE_BINOP_46(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46) OP(_COMBINE_BINOP_23(OP, (a1),      \
	(a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12),   \
	(a13), (a14), (a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22),  \
	(a23)), _COMBINE_BINOP_23(OP, (a24), (a25), (a26), (a27), (a28),       \
	(a29), (a30), (a31), (a32), (a33), (a34), (a35), (a36), (a37), (a38),  \
	(a39), (a40), (a41), (a42), (a43), (a44), (a45), (a46)))
#define _COMBINE_BINOP_47(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47) OP(_COMBINE_BINOP_23(OP, (a1), \
	(a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12),   \
	(a13), (a14), (a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22),  \
	(a23)), _COMBINE_BINOP_24(OP, (a24), (a25), (a26), (a27), (a28),       \
	(a29), (a30), (a31), (a32), (a33), (a34), (a35), (a36), (a37), (a38),  \
	(a39), (a40), (a41), (a42), (a43), (a44), (a45), (a46), (a47)))
#define _COMBINE_BINOP_48(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48) OP(_COMBINE_BINOP_24(OP,  \
	(a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11),    \
	(a12), (a13), (a14), (a15), (a16), (a17), (a18), (a19), (a20), (a21),  \
	(a22), (a23), (a24)), _COMBINE_BINOP_24(OP, (a25), (a26), (a27),       \
	(a28), (a29), (a30), (a31), (a32), (a33), (a34), (a35), (a36), (a37),  \
	(a38), (a39), (a40), (a41), (a42), (a43), (a44), (a45), (a46), (a47),  \
	(a48)))
#define _COMBINE_BINOP_49(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49)                      \
	OP(_COMBINE_BINOP_24(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17),    \
	(a18), (a19), (a20), (a21), (a22), (a23), (a24)),                      \
	_COMBINE_BINOP_25(OP, (a25), (a26), (a27), (a28), (a29), (a30), (a31), \
	(a32), (a33), (a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41),  \
	(a42), (a43), (a44), (a45), (a46), (a47), (a48), (a49)))
#define _COMBINE_BINOP_50(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50)                 \
	OP(_COMBINE_BINOP_25(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17),    \
	(a18), (a19), (a20), (a21), (a22), (a23), (a24), (a25)),               \
	_COMBINE_BINOP_25(OP, (a26), (a27), (a28), (a29), (a30), (a31), (a32), \
	(a33), (a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41), (a42),  \
	(a43), (a44), (a45), (a46), (a47), (a48), (a49), (a50)))
#define _COMBINE_BINOP_51(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51)            \
	OP(_COMBINE_BINOP_25(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17),    \
	(a18), (a19), (a20), (a21), (a22), (a23), (a24), (a25)),               \
	_COMBINE_BINOP_26(OP, (a26), (a27), (a28), (a29), (a30), (a31), (a32), \
	(a33), (a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41), (a42),  \
	(a43), (a44), (a45), (a46), (a47), (a48), (a49), (a50), (a51)))
#define _COMBINE_BINOP_52(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52)       \
	OP(_COMBINE_BINOP_26(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17),    \
	(a18), (a19), (a20), (a21), (a22), (a23), (a24), (a25), (a26)),        \
	_COMBINE_BINOP_26(OP, (a27), (a28), (a29), (a30), (a31), (a32), (a33), \
	(a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41), (a42), (a43),  \
	(a44), (a45), (a46), (a47), (a48), (a49), (a50), (a51), (a52)))
#define _COMBINE_BINOP_53(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53)  \
	OP(_COMBINE_BINOP_26(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17),    \
	(a18), (a19), (a20), (a21), (a22), (a23), (a24), (a25), (a26)),        \
	_COMBINE_BINOP_27(OP, (a27), (a28), (a29), (a30), (a31), (a32), (a33), \
	(a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41), (a42), (a43),  \
	(a44), (a45), (a46), (a47), (a48), (a49), (a50), (a51), (a52), (a53)))
#define _COMBINE_BINOP_54(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53,  \
	a54) OP(_COMBINE_BINOP_27(OP, (a1), (a2), (a3), (a4), (a5), (a6),      \
	(a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16),     \
	(a17), (a18), (a19), (a20), (a21), (a22), (a23), (a24), (a25), (a26),  \
	(a27)), _COMBINE_BINOP_27(OP, (a28), (a29), (a30), (a31), (a32),       \
	(a33), (a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41), (a42),  \
	(a43), (a44), (a45), (a46), (a47), (a48), (a49), (a50), (a51), (a52),  \
	(a53), (a54)))
#define _COMBINE_BINOP_55(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53,  \
	a54, a55) OP(_COMBINE_BINOP_27(OP, (a1), (a2), (a3), (a4), (a5), (a6), \
	(a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16),     \
	(a17), (a18), (a19), (a20), (a21), (a22), (a23), (a24), (a25), (a26),  \
	(a27)), _COMBINE_BINOP_28(OP, (a28), (a29), (a30), (a31), (a32),       \
	(a33), (a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41), (a42),  \
	(a43), (a44), (a45), (a46), (a47), (a48), (a49), (a50), (a51), (a52),  \
	(a53), (a54), (a55)))
#define _COMBINE_BINOP_56(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53,  \
	a54, a55, a56) OP(_COMBINE_BINOP_28(OP, (a1), (a2), (a3), (a4), (a5),  \
	(a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15),      \
	(a16), (a17), (a18), (a19), (a20), (a21), (a22), (a23), (a24), (a25),  \
	(a26), (a27), (a28)), _COMBINE_BINOP_28(OP, (a29), (a30), (a31),       \
	(a32), (a33), (a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41),  \
	(a42), (a43), (a44), (a45), (a46), (a47), (a48), (a49), (a50), (a51),  \
	(a52), (a53), (a54), (a55), (a56)))
#define _COMBINE_BINOP_57(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53,  \
	a54, a55, a56, a57) OP(_COMBINE_BINOP_28(OP, (a1), (a2), (a3), (a4),   \
	(a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14),       \
	(a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22), (a23), (a24),  \
	(a25), (a26), (a27), (a28)), _COMBINE_BINOP_29(OP, (a29), (a30),       \
	(a31), (a32), (a33), (a34), (a35), (a36), (a37), (a38), (a39), (a40),  \
	(a41), (a42), (a43), (a44), (a45), (a46), (a47), (a48), (a49), (a50),  \
	(a51), (a52), (a53), (a54), (a55), (a56), (a57)))
#define _COMBINE_BINOP_58(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53,  \
	a54, a55, a56, a57, a58) OP(_COMBINE_BINOP_29(OP, (a1), (a2), (a3),    \
	(a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13), (a14), \
	(a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22), (a23), (a24),  \
	(a25), (a26), (a27), (a28), (a29)), _COMBINE_BINOP_29(OP, (a30),       \
	(a31), (a32), (a33), (a34), (a35), (a36), (a37), (a38), (a39), (a40),  \
	(a41), (a42), (a43), (a44), (a45), (a46), (a47), (a48), (a49), (a50),  \
	(a51), (a52), (a53), (a54), (a55), (a56), (a57), (a58)))
#define _COMBINE_BINOP_59(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53,  \
	a54, a55, a56, a57, a58, a59) OP(_COMBINE_BINOP_29(OP, (a1), (a2),     \
	(a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12), (a13),  \
	(a14), (a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22), (a23),  \
	(a24), (a25), (a26), (a27), (a28), (a29)), _COMBINE_BINOP_30(OP,       \
	(a30), (a31), (a32), (a33), (a34), (a35), (a36), (a37), (a38), (a39),  \
	(a40), (a41), (a42), (a43), (a44), (a45), (a46), (a47), (a48), (a49),  \
	(a50), (a51), (a52), (a53), (a54), (a55), (a56), (a57), (a58), (a59)))
#define _COMBINE_BINOP_60(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53,  \
	a54, a55, a56, a57, a58, a59, a60) OP(_COMBINE_BINOP_30(OP, (a1),      \
	(a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12),   \
	(a13), (a14), (a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22),  \
	(a23), (a24), (a25), (a26), (a27), (a28), (a29), (a30)),               \
	_COMBINE_BINOP_30(OP, (a31), (a32), (a33), (a34), (a35), (a36), (a37), \
	(a38), (a39), (a40), (a41), (a42), (a43), (a44), (a45), (a46), (a47),  \
	(a48), (a49), (a50), (a51), (a52), (a53), (a54), (a55), (a56), (a57),  \
	(a58), (a59), (a60)))
#define _COMBINE_BINOP_61(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53,  \
	a54, a55, a56, a57, a58, a59, a60, a61) OP(_COMBINE_BINOP_30(OP, (a1), \
	(a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11), (a12),   \
	(a13), (a14), (a15), (a16), (a17), (a18), (a19), (a20), (a21), (a22),  \
	(a23), (a24), (a25), (a26), (a27), (a28), (a29), (a30)),               \
	_COMBINE_BINOP_31(OP, (a31), (a32), (a33), (a34), (a35), (a36), (a37), \
	(a38), (a39), (a40), (a41), (a42), (a43), (a44), (a45), (a46), (a47),  \
	(a48), (a49), (a50), (a51), (a52), (a53), (a54), (a55), (a56), (a57),  \
	(a58), (a59), (a60), (a61)))
#define _COMBINE_BINOP_62(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53,  \
	a54, a55, a56, a57, a58, a59, a60, a61, a62) OP(_COMBINE_BINOP_31(OP,  \
	(a1), (a2), (a3), (a4), (a5), (a6), (a7), (a8), (a9), (a10), (a11),    \
	(a12), (a13), (a14), (a15), (a16), (a17), (a18), (a19), (a20), (a21),  \
	(a22), (a23), (a24), (a25), (a26), (a27), (a28), (a29), (a30), (a31)), \
	_COMBINE_BINOP_31(OP, (a32), (a33), (a34), (a35), (a36), (a37), (a38), \
	(a39), (a40), (a41), (a42), (a43), (a44), (a45), (a46), (a47), (a48),  \
	(a49), (a50), (a51), (a52), (a53), (a54), (a55), (a56), (a57), (a58),  \
	(a59), (a60), (a61), (a62)))
#define _COMBINE_BINOP_63(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53,  \
	a54, a55, a56, a57, a58, a59, a60, a61, a62, a63)                      \
	OP(_COMBINE_BINOP_31(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17),    \
	(a18), (a19), (a20), (a21), (a22), (a23), (a24), (a25), (a26), (a27),  \
	(a28), (a29), (a30), (a31)), _COMBINE_BINOP_32(OP, (a32), (a33),       \
	(a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41), (a42), (a43),  \
	(a44), (a45), (a46), (a47), (a48), (a49), (a50), (a51), (a52), (a53),  \
	(a54), (a55), (a56), (a57), (a58), (a59), (a60), (a61), (a62), (a63)))
#define _COMBINE_BINOP_64(OP, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11,    \
	a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,  \
	a26, a27, a28, a29, a30, a31, a32, a33, a34, a35, a36, a37, a38, a39,  \
	a40, a41, a42, a43, a44, a45, a46, a47, a48, a49, a50, a51, a52, a53,  \
	a54, a55, a56, a57, a58, a59, a60, a61, a62, a63, a64)                 \
	OP(_COMBINE_BINOP_32(OP, (a1), (a2), (a3), (a4), (a5), (a6), (a7),     \
	(a8), (a9), (a10), (a11), (a12), (a13), (a14), (a15), (a16), (a17),    \
	(a18), (a19), (a20), (a21), (a22), (a23), (a24), (a25), (a26), (a27),  \
	(a28), (a29), (a30), (a31), (a32)), _COMBINE_BINOP_32(OP, (a33),       \
	(a34), (a35), (a36), (a37), (a38), (a39), (a40), (a41), (a42), (a43),  \
	(a44), (a45), (a46), (a47), (a48), (a49), (a50), (a51), (a52), (a53),  \
	(a54), (a55), (a56), (a57), (a58), (a59), (a60), (a61), (a62), (a63),  \
	(a64)))

/* Used by UTIL_INC */
#include "util_internal_util_inc.h"

/* Used by UTIL_DEC */
#include "util_internal_util_dec.h"

/* Used by UTIL_X2 */
#include "util_internal_util_x2.h"

#endif /* ZEPHYR_INCLUDE_SYS_UTIL_INTERNAL_H_ */
