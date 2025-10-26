/*
 * This file is part of Anillo OS
 * Copyright (C) 2024 Anillo OS Developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _LIBMATH_LIBMATH_H_
#define _LIBMATH_LIBMATH_H_

#include <stdint.h>
#include <libmath/base.h>

LIBMATH_DECLARATIONS_BEGIN;

LIBMATH_ENUM(uint8_t, math_error) {
	math_error_none = 0,
	math_error_domain = 1,
	math_error_pole = 2,
	math_error_overflow = 3,
	math_error_underflow = 4,
	math_error_inexact = 5,
};

double math_pow_d(double base, double exponent, math_error_t* out_error);
float math_pow_f(float base, float exponent, math_error_t* out_error);
long double math_pow_ld(long double base, long double exponent, math_error_t* out_error);
uint64_t math_pow_u64(uint64_t base, uint64_t exponent, math_error_t* out_error);
double math_pow_di(double base, int64_t exponent, math_error_t* out_error);

LIBMATH_DECLARATIONS_END;

#endif // _LIBMATH_LIBMATH_H_
