/*
 * Based on GNULIB intprops.h -- properties of integer types
 *
 * Copyright (C) 2001-2019 Free Software Foundation, Inc.
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Written by Paul Eggert.
 */

#pragma once

#include "internal.h"

#include <limits.h>

/* Return a value with the common real type of E and V and the value of V.
   Do not evaluate E.  */
#define VIR_INT_CONVERT(e, v) ((1 ? 0 : (e)) + (v))

/* Act like VIR_INT_CONVERT (E, -V) but work around a bug in IRIX 6.5 cc; see
   <https://lists.gnu.org/r/bug-gnulib/2011-05/msg00406.html>.  */
#define VIR_INT_NEGATE_CONVERT(e, v) ((1 ? 0 : (e)) - (v))

/* The extra casts in the following macros work around compiler bugs,
   e.g., in Cray C 5.0.3.0.  */

/* True if the arithmetic type T is an integer type.  bool counts as
   an integer.  */
#define TYPE_IS_INTEGER(t) ((t) 1.5 == 1)

/* True if the real type T is signed.  */
#define TYPE_SIGNED(t) (! ((t) 0 < (t) -1))

/* Return 1 if the real expression E, after promotion, has a
   signed or floating type.  Do not evaluate E.  */
#define EXPR_SIGNED(e) (VIR_INT_NEGATE_CONVERT (e, 1) < 0)


/* Minimum and maximum values for integer types and expressions.  */

/* The width in bits of the integer type or expression T.
   Do not evaluate T.
   Padding bits are not supported; this is checked at compile-time below.  */
#define TYPE_WIDTH(t) (sizeof(t) * CHAR_BIT)

/* The maximum and minimum values for the integer type T.  */
#define TYPE_MINIMUM(t) ((t) ~ TYPE_MAXIMUM (t))
#define TYPE_MAXIMUM(t) \
  ((t) (! TYPE_SIGNED (t) \
        ? (t) -1 \
        : ((((t) 1 << (TYPE_WIDTH (t) - 2)) - 1) * 2 + 1)))

/* The maximum and minimum values for the type of the expression E,
   after integer promotion.  E is not evaluated.  */
#define VIR_INT_MINIMUM(e) \
  (EXPR_SIGNED (e) \
   ? ~ VIR_SIGNED_INT_MAXIMUM (e) \
   : VIR_INT_CONVERT (e, 0))
#define VIR_INT_MAXIMUM(e) \
  (EXPR_SIGNED (e) \
   ? VIR_SIGNED_INT_MAXIMUM (e) \
   : VIR_INT_NEGATE_CONVERT (e, 1))
#define VIR_SIGNED_INT_MAXIMUM(e) \
  (((VIR_INT_CONVERT (e, 1) << (TYPE_WIDTH ((e) + 0) - 2)) - 1) * 2 + 1)

/* This include file assumes that signed types are two's complement without
   padding bits; the above macros have undefined behavior otherwise.
   If this is a problem for you, please let us know how to fix it for your host.
   This assumption is tested by the intprops-tests module.  */

/* Return 1 if the integer type or expression T might be signed.  Return 0
   if it is definitely unsigned.  This macro does not evaluate its argument,
   and expands to an integer constant expression.  */
#define VIR_SIGNED_TYPE_OR_EXPR(t) TYPE_SIGNED (__typeof__ (t))

/* Bound on length of the string representing an unsigned integer
   value representable in B bits.  log10 (2.0) < 146/485.  The
   smallest value of B where this bound is not tight is 2621.  */
#define INT_BITS_STRLEN_BOUND(b) (((b) * 146 + 484) / 485)

/* Bound on length of the string representing an integer type or expression T.
   Subtract 1 for the sign bit if T is signed, and then add 1 more for
   a minus sign if needed.

   Because VIR_SIGNED_TYPE_OR_EXPR sometimes returns 1 when its argument is
   unsigned, this macro may overestimate the true bound by one byte when
   applied to unsigned types of size 2, 4, 16, ... bytes.  */
#define INT_STRLEN_BOUND(t) \
  (INT_BITS_STRLEN_BOUND (TYPE_WIDTH (t) - VIR_SIGNED_TYPE_OR_EXPR (t)) \
   + VIR_SIGNED_TYPE_OR_EXPR (t))

/* Bound on buffer size needed to represent an integer type or expression T,
   including the terminating null.  */
#define INT_BUFSIZE_BOUND(t) (INT_STRLEN_BOUND (t) + 1)


/* Range overflow checks.

   The INT_<op>_RANGE_OVERFLOW macros return 1 if the corresponding C
   operators might not yield numerically correct answers due to
   arithmetic overflow.  They do not rely on undefined or
   implementation-defined behavior.  Their implementations are simple
   and straightforward, but they are a bit harder to use than the
   INT_<op>_OVERFLOW macros described below.

   Example usage:

     long int x = ...;
     long int y = ...;
     if (INT_MULTIPLY_RANGE_OVERFLOW (x, y, LONG_MIN, LONG_MAX))
       printf ("multiply would overflow");
     else
       printf ("product is %ld", x * y);

   Restrictions on *_RANGE_OVERFLOW macros:

   These macros do not check for all possible numerical problems or
   undefined or unspecified behavior: they do not check for division
   by zero, for bad shift counts, or for shifting negative numbers.

   These macros may evaluate their arguments zero or multiple times,
   so the arguments should not have side effects.  The arithmetic
   arguments (including the MIN and MAX arguments) must be of the same
   integer type after the usual arithmetic conversions, and the type
   must have minimum value MIN and maximum MAX.  Unsigned types should
   use a zero MIN of the proper type.

   These macros are tuned for constant MIN and MAX.  For commutative
   operations such as A + B, they are also tuned for constant B.  */

/* Return 1 if A + B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  */
#define INT_ADD_RANGE_OVERFLOW(a, b, min, max) \
  ((b) < 0 \
   ? (a) < (min) - (b) \
   : (max) - (b) < (a))

/* Return 1 if A - B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  */
#define INT_SUBTRACT_RANGE_OVERFLOW(a, b, min, max) \
  ((b) < 0 \
   ? (max) + (b) < (a) \
   : (a) < (min) + (b))

/* Return 1 if - A would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  */
#define INT_NEGATE_RANGE_OVERFLOW(a, min, max) \
  ((min) < 0 \
   ? (a) < - (max) \
   : 0 < (a))

/* Return 1 if A * B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  Avoid && and || as they tickle
   bugs in Sun C 5.11 2010/08/13 and other compilers; see
   <https://lists.gnu.org/r/bug-gnulib/2011-05/msg00401.html>.  */
#define INT_MULTIPLY_RANGE_OVERFLOW(a, b, min, max) \
  ((b) < 0 \
   ? ((a) < 0 \
      ? (a) < (max) / (b) \
      : (b) == -1 \
      ? 0 \
      : (min) / (b) < (a)) \
   : (b) == 0 \
   ? 0 \
   : ((a) < 0 \
      ? (a) < (min) / (b) \
      : (max) / (b) < (a)))

/* Return 1 if A / B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  Do not check for division by zero.  */
#define INT_DIVIDE_RANGE_OVERFLOW(a, b, min, max) \
  ((min) < 0 && (b) == -1 && (a) < - (max))

/* Return 1 if A % B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  Do not check for division by zero.
   Mathematically, % should never overflow, but on x86-like hosts
   INT_MIN % -1 traps, and the C standard permits this, so treat this
   as an overflow too.  */
#define INT_REMAINDER_RANGE_OVERFLOW(a, b, min, max) \
  INT_DIVIDE_RANGE_OVERFLOW (a, b, min, max)

/* Return 1 if A << B would overflow in [MIN,MAX] arithmetic.
   See above for restrictions.  Here, MIN and MAX are for A only, and B need
   not be of the same type as the other arguments.  The C standard says that
   behavior is undefined for shifts unless 0 <= B < wordwidth, and that when
   A is negative then A << B has undefined behavior and A >> B has
   implementation-defined behavior, but do not check these other
   restrictions.  */
#define INT_LEFT_SHIFT_RANGE_OVERFLOW(a, b, min, max) \
  ((a) < 0 \
   ? (a) < (min) >> (b) \
   : (max) >> (b) < (a))

/* True if __builtin_add_overflow (A, B, P) works when P is non-null.  */
#if 5 <= __GNUC__ && !defined __ICC
# define VIR_HAS_BUILTIN_OVERFLOW 1
#else
# define VIR_HAS_BUILTIN_OVERFLOW 0
#endif

/* True if __builtin_add_overflow_p (A, B, C) works.  */
#define VIR_HAS_BUILTIN_OVERFLOW_P (7 <= __GNUC__)

/* The VIR*_OVERFLOW macros have the same restrictions as the
   *_RANGE_OVERFLOW macros, except that they do not assume that operands
   (e.g., A and B) have the same type as MIN and MAX.  Instead, they assume
   that the result (e.g., A + B) has that type.  */
#if VIR_HAS_BUILTIN_OVERFLOW_P
# define VIR_ADD_OVERFLOW(a, b, min, max) \
   __builtin_add_overflow_p (a, b, (__typeof__ ((a) + (b))) 0)
# define VIR_SUBTRACT_OVERFLOW(a, b, min, max) \
   __builtin_sub_overflow_p (a, b, (__typeof__ ((a) - (b))) 0)
# define VIR_MULTIPLY_OVERFLOW(a, b, min, max) \
   __builtin_mul_overflow_p (a, b, (__typeof__ ((a) * (b))) 0)
#else
# define VIR_ADD_OVERFLOW(a, b, min, max) \
   ((min) < 0 ? INT_ADD_RANGE_OVERFLOW (a, b, min, max) \
    : (a) < 0 ? (b) <= (a) + (b) \
    : (b) < 0 ? (a) <= (a) + (b) \
    : (a) + (b) < (b))
# define VIR_SUBTRACT_OVERFLOW(a, b, min, max) \
   ((min) < 0 ? INT_SUBTRACT_RANGE_OVERFLOW (a, b, min, max) \
    : (a) < 0 ? 1 \
    : (b) < 0 ? (a) - (b) <= (a) \
    : (a) < (b))
# define VIR_MULTIPLY_OVERFLOW(a, b, min, max) \
   (((min) == 0 && (((a) < 0 && 0 < (b)) || ((b) < 0 && 0 < (a)))) \
    || INT_MULTIPLY_RANGE_OVERFLOW (a, b, min, max))
#endif
#define VIR_DIVIDE_OVERFLOW(a, b, min, max) \
  ((min) < 0 ? (b) == VIR_INT_NEGATE_CONVERT (min, 1) && (a) < - (max) \
   : (a) < 0 ? (b) <= (a) + (b) - 1 \
   : (b) < 0 && (a) + (b) <= (a))
#define VIR_REMAINDER_OVERFLOW(a, b, min, max) \
  ((min) < 0 ? (b) == VIR_INT_NEGATE_CONVERT (min, 1) && (a) < - (max) \
   : (a) < 0 ? (a) % (b) != ((max) - (b) + 1) % (b) \
   : (b) < 0 && ! VIR_UNSIGNED_NEG_MULTIPLE (a, b, max))

/* Return a nonzero value if A is a mathematical multiple of B, where
   A is unsigned, B is negative, and MAX is the maximum value of A's
   type.  A's type must be the same as (A % B)'s type.  Normally (A %
   -B == 0) suffices, but things get tricky if -B would overflow.  */
#define VIR_UNSIGNED_NEG_MULTIPLE(a, b, max) \
  (((b) < -VIR_SIGNED_INT_MAXIMUM (b) \
    ? (VIR_SIGNED_INT_MAXIMUM (b) == (max) \
       ? (a) \
       : (a) % (VIR_INT_CONVERT (a, VIR_SIGNED_INT_MAXIMUM (b)) + 1)) \
    : (a) % - (b)) \
   == 0)

/* Check for integer overflow, and report low order bits of answer.

   The INT_<op>_OVERFLOW macros return 1 if the corresponding C operators
   might not yield numerically correct answers due to arithmetic overflow.
   The INT_<op>_WRAPV macros compute the low-order bits of the sum,
   difference, and product of two C integers, and return 1 if these
   low-order bits are not numerically correct.
   These macros work correctly on all known practical hosts, and do not rely
   on undefined behavior due to signed arithmetic overflow.

   Example usage, assuming A and B are long int:

     if (INT_MULTIPLY_OVERFLOW (a, b))
       printf ("result would overflow\n");
     else
       printf ("result is %ld (no overflow)\n", a * b);

   Example usage with WRAPV flavor:

     long int result;
     bool overflow = INT_MULTIPLY_WRAPV (a, b, &result);
     printf ("result is %ld (%s)\n", result,
             overflow ? "after overflow" : "no overflow");

   Restrictions on these macros:

   These macros do not check for all possible numerical problems or
   undefined or unspecified behavior: they do not check for division
   by zero, for bad shift counts, or for shifting negative numbers.

   These macros may evaluate their arguments zero or multiple times, so the
   arguments should not have side effects.

   The WRAPV macros are not constant expressions.  They support only
   +, binary -, and *.  Because the WRAPV macros convert the result,
   they report overflow in different circumstances than the OVERFLOW
   macros do.

   These macros are tuned for their last input argument being a constant.

   Return 1 if the integer expressions A * B, A - B, -A, A * B, A / B,
   A % B, and A << B would overflow, respectively.  */

#define INT_ADD_OVERFLOW(a, b) \
  VIR_BINARY_OP_OVERFLOW (a, b, VIR_ADD_OVERFLOW)
#define INT_SUBTRACT_OVERFLOW(a, b) \
  VIR_BINARY_OP_OVERFLOW (a, b, VIR_SUBTRACT_OVERFLOW)
#if VIR_HAS_BUILTIN_OVERFLOW_P
# define INT_NEGATE_OVERFLOW(a) INT_SUBTRACT_OVERFLOW (0, a)
#else
# define INT_NEGATE_OVERFLOW(a) \
   INT_NEGATE_RANGE_OVERFLOW (a, VIR_INT_MINIMUM (a), VIR_INT_MAXIMUM (a))
#endif
#define INT_MULTIPLY_OVERFLOW(a, b) \
  VIR_BINARY_OP_OVERFLOW (a, b, VIR_MULTIPLY_OVERFLOW)
#define INT_DIVIDE_OVERFLOW(a, b) \
  VIR_BINARY_OP_OVERFLOW (a, b, VIR_DIVIDE_OVERFLOW)
#define INT_REMAINDER_OVERFLOW(a, b) \
  VIR_BINARY_OP_OVERFLOW (a, b, VIR_REMAINDER_OVERFLOW)
#define INT_LEFT_SHIFT_OVERFLOW(a, b) \
  INT_LEFT_SHIFT_RANGE_OVERFLOW (a, b, \
                                 VIR_INT_MINIMUM (a), VIR_INT_MAXIMUM (a))

/* Return 1 if the expression A <op> B would overflow,
   where OP_RESULT_OVERFLOW (A, B, MIN, MAX) does the actual test,
   assuming MIN and MAX are the minimum and maximum for the result type.
   Arguments should be free of side effects.  */
#define VIR_BINARY_OP_OVERFLOW(a, b, op_result_overflow) \
  op_result_overflow (a, b, \
                      VIR_INT_MINIMUM (VIR_INT_CONVERT (a, b)), \
                      VIR_INT_MAXIMUM (VIR_INT_CONVERT (a, b)))

/* Store the low-order bits of A + B, A - B, A * B, respectively, into *R.
   Return 1 if the result overflows.  See above for restrictions.  */
#define INT_ADD_WRAPV(a, b, r) \
  VIR_INT_OP_WRAPV (a, b, r, +, __builtin_add_overflow, \
                    VIR_INT_ADD_RANGE_OVERFLOW)
#define INT_SUBTRACT_WRAPV(a, b, r) \
  VIR_INT_OP_WRAPV (a, b, r, -, __builtin_sub_overflow, \
                    VIR_INT_SUBTRACT_RANGE_OVERFLOW)
#define INT_MULTIPLY_WRAPV(a, b, r) \
   VIR_INT_OP_WRAPV (a, b, r, *, VIR_BUILTIN_MUL_OVERFLOW, \
                     VIR_INT_MULTIPLY_RANGE_OVERFLOW)

/* Like __builtin_mul_overflow, but work around GCC bug 91450.  */
#define VIR_BUILTIN_MUL_OVERFLOW(a, b, r) \
  ((!VIR_SIGNED_TYPE_OR_EXPR (*(r)) && EXPR_SIGNED (a) && EXPR_SIGNED (b) \
    && VIR_INT_MULTIPLY_RANGE_OVERFLOW (a, b, 0, (__typeof__ (*(r))) -1)) \
   ? ((void) __builtin_mul_overflow (a, b, r), 1) \
   : __builtin_mul_overflow (a, b, r))

/* Nonzero if this compiler has GCC bug 68193 or Clang bug 25390.  See:
   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=68193
   https://llvm.org/bugs/show_bug.cgi?id=25390
   For now, assume all versions of GCC-like compilers generate bogus
   warnings for _Generic.  This matters only for older compilers that
   lack __builtin_add_overflow.  */
#if __GNUC__
# define VIR__GENERIC_BOGUS 1
#else
# define VIR__GENERIC_BOGUS 0
#endif

/* Store the low-order bits of A <op> B into *R, where OP specifies
   the operation.  BUILTIN is the builtin operation, and OVERFLOW the
   overflow predicate.  Return 1 if the result overflows.  See above
   for restrictions.  */
#if VIR_HAS_BUILTIN_OVERFLOW
# define VIR_INT_OP_WRAPV(a, b, r, op, builtin, overflow) builtin (a, b, r)
#elif 201112 <= __STDC_VERSION__ && !VIR__GENERIC_BOGUS
# define VIR_INT_OP_WRAPV(a, b, r, op, builtin, overflow) \
   (_Generic \
    (*(r), \
     signed char: \
       VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned int, \
                        signed char, SCHAR_MIN, SCHAR_MAX), \
     unsigned char: \
       VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned int, \
                        unsigned char, 0, UCHAR_MAX), \
     short int: \
       VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned int, \
                        short int, SHRT_MIN, SHRT_MAX), \
     unsigned short int: \
       VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned int, \
                        unsigned short int, 0, USHRT_MAX), \
     int: \
       VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned int, \
                        int, INT_MIN, INT_MAX), \
     unsigned int: \
       VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned int, \
                        unsigned int, 0, UINT_MAX), \
     long int: \
       VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned long int, \
                        long int, LONG_MIN, LONG_MAX), \
     unsigned long int: \
       VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned long int, \
                        unsigned long int, 0, ULONG_MAX), \
     long long int: \
       VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned long long int, \
                        long long int, LLONG_MIN, LLONG_MAX), \
     unsigned long long int: \
       VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned long long int, \
                        unsigned long long int, 0, ULLONG_MAX)))
#else
/* Store the low-order bits of A <op> B into *R, where OP specifies
   the operation and OVERFLOW the overflow predicate.  If *R is
   signed, its type is ST with bounds SMIN..SMAX; otherwise its type
   is UT with bounds U..UMAX.  ST and UT are narrower than int.
   Return 1 if the result overflows.  See above for restrictions.  */
# define VIR_INT_OP_WRAPV_SMALLISH(a,b,r,op,overflow,st,smin,smax,ut,umax) \
    (TYPE_SIGNED (__typeof__ (*(r))) \
     ? VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned int, st, smin, smax) \
     : VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned int, ut, 0, umax))

# define VIR_INT_OP_WRAPV(a, b, r, op, builtin, overflow) \
    (sizeof(*(r)) == sizeof(signed char) \
     ? VIR_INT_OP_WRAPV_SMALLISH(a, b, r, op, overflow, \
                                 signed char, SCHAR_MIN, SCHAR_MAX, \
                                 unsigned char, UCHAR_MAX) \
     : sizeof(*(r)) == sizeof(short int) \
    ? VIR_INT_OP_WRAPV_SMALLISH(a, b, r, op, overflow, \
                                short int, SHRT_MIN, SHRT_MAX, \
                                unsigned short int, USHRT_MAX) \
     : sizeof(*(r)) == sizeof(int) \
    ? (EXPR_SIGNED(*(r)) \
       ? VIR_INT_OP_CALC(a, b, r, op, overflow, unsigned int, \
                          int, INT_MIN, INT_MAX) \
       : VIR_INT_OP_CALC(a, b, r, op, overflow, unsigned int, \
                          unsigned int, 0, UINT_MAX)) \
    : VIR_INT_OP_WRAPV_LONGISH(a, b, r, op, overflow))
# define VIR_INT_OP_WRAPV_LONGISH(a, b, r, op, overflow) \
    (sizeof(*(r)) == sizeof(long int) \
     ? (EXPR_SIGNED (*(r)) \
        ? VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned long int, \
                           long int, LONG_MIN, LONG_MAX) \
        : VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned long int, \
                           unsigned long int, 0, ULONG_MAX)) \
     : (EXPR_SIGNED (*(r)) \
        ? VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned long long int, \
                           long long int, LLONG_MIN, LLONG_MAX) \
        : VIR_INT_OP_CALC (a, b, r, op, overflow, unsigned long long int, \
                           unsigned long long int, 0, ULLONG_MAX)))
#endif

/* Store the low-order bits of A <op> B into *R, where the operation
   is given by OP.  Use the unsigned type UT for calculation to avoid
   overflow problems.  *R's type is T, with extrema TMIN and TMAX.
   T must be a signed integer type.  Return 1 if the result overflows.  */
#define VIR_INT_OP_CALC(a, b, r, op, overflow, ut, t, tmin, tmax) \
  (overflow (a, b, tmin, tmax) \
   ? (*(r) = VIR_INT_OP_WRAPV_VIA_UNSIGNED (a, b, op, ut, t), 1) \
   : (*(r) = VIR_INT_OP_WRAPV_VIA_UNSIGNED (a, b, op, ut, t), 0))

/* Return the low-order bits of A <op> B, where the operation is given
   by OP.  Use the unsigned type UT for calculation to avoid undefined
   behavior on signed integer overflow, and convert the result to type T.
   UT is at least as wide as T and is no narrower than unsigned int,
   T is two's complement, and there is no padding or trap representations.
   Assume that converting UT to T yields the low-order bits, as is
   done in all known two's-complement C compilers.  E.g., see:
   https://gcc.gnu.org/onlinedocs/gcc/Integers-implementation.html

   According to the C standard, converting UT to T yields an
   implementation-defined result or signal for values outside T's
   range.  However, code that works around this theoretical problem
   runs afoul of a compiler bug in Oracle Studio 12.3 x86.  See:
   https://lists.gnu.org/r/bug-gnulib/2017-04/msg00049.html
   As the compiler bug is real, don't try to work around the
   theoretical problem.  */

#define VIR_INT_OP_WRAPV_VIA_UNSIGNED(a, b, op, ut, t) \
  ((t) ((ut) (a) op (ut) (b)))

/* Return true if the numeric values A + B, A - B, A * B fall outside
   the range TMIN..TMAX.  Arguments should be integer expressions
   without side effects.  TMIN should be signed and nonpositive.
   TMAX should be positive, and should be signed unless TMIN is zero.  */
#define VIR_INT_ADD_RANGE_OVERFLOW(a, b, tmin, tmax) \
  ((b) < 0 \
   ? (((tmin) \
       ? ((EXPR_SIGNED (VIR_INT_CONVERT (a, (tmin) - (b))) || (b) < (tmin)) \
          && (a) < (tmin) - (b)) \
       : (a) <= -1 - (b)) \
      || ((EXPR_SIGNED (a) ? 0 <= (a) : (tmax) < (a)) && (tmax) < (a) + (b))) \
   : (a) < 0 \
   ? (((tmin) \
       ? ((EXPR_SIGNED (VIR_INT_CONVERT (b, (tmin) - (a))) || (a) < (tmin)) \
          && (b) < (tmin) - (a)) \
       : (b) <= -1 - (a)) \
      || ((EXPR_SIGNED (VIR_INT_CONVERT (a, b)) || (tmax) < (b)) \
          && (tmax) < (a) + (b))) \
   : (tmax) < (b) || (tmax) - (b) < (a))
#define VIR_INT_SUBTRACT_RANGE_OVERFLOW(a, b, tmin, tmax) \
  (((a) < 0) == ((b) < 0) \
   ? ((a) < (b) \
      ? !(tmin) || -1 - (tmin) < (b) - (a) - 1 \
      : (tmax) < (a) - (b)) \
   : (a) < 0 \
   ? ((!EXPR_SIGNED (VIR_INT_CONVERT ((a) - (tmin), b)) && (a) - (tmin) < 0) \
      || (a) - (tmin) < (b)) \
   : ((! (EXPR_SIGNED (VIR_INT_CONVERT (tmax, b)) \
          && EXPR_SIGNED (VIR_INT_CONVERT ((tmax) + (b), a))) \
       && (tmax) <= -1 - (b)) \
      || (tmax) + (b) < (a)))
#define VIR_INT_MULTIPLY_RANGE_OVERFLOW(a, b, tmin, tmax) \
  ((b) < 0 \
   ? ((a) < 0 \
      ? (EXPR_SIGNED (VIR_INT_CONVERT (tmax, b)) \
         ? (a) < (tmax) / (b) \
         : ((INT_NEGATE_OVERFLOW (b) \
             ? VIR_INT_CONVERT (b, tmax) >> (TYPE_WIDTH (b) - 1) \
             : (tmax) / -(b)) \
            <= -1 - (a))) \
      : INT_NEGATE_OVERFLOW (VIR_INT_CONVERT (b, tmin)) && (b) == -1 \
      ? (EXPR_SIGNED (a) \
         ? 0 < (a) + (tmin) \
         : 0 < (a) && -1 - (tmin) < (a) - 1) \
      : (tmin) / (b) < (a)) \
   : (b) == 0 \
   ? 0 \
   : ((a) < 0 \
      ? (INT_NEGATE_OVERFLOW (VIR_INT_CONVERT (a, tmin)) && (a) == -1 \
         ? (EXPR_SIGNED (b) ? 0 < (b) + (tmin) : -1 - (tmin) < (b) - 1) \
         : (tmin) / (a) < (b)) \
      : (tmax) / (b) < (a)))
