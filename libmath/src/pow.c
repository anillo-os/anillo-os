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

#include <libmath/libmath.h>

#include <stdbool.h>

// TODO

#define math_pow_def(_suffix, _builtin_suffix, _type) \
	_type math_pow ## _suffix(_type base, _type exponent, math_error_t* out_error) { \
		if (out_error) { \
			*out_error = math_error_none; \
		} \
		return __builtin_nan ## _builtin_suffix ("0"); \
	};

math_pow_def(_d, , double);
math_pow_def(_f, f, float);
math_pow_def(_ld, l, long double);

// integer exponentiation is implemented using exponentiation-by-squaring with basic iterative method

uint64_t math_pow_u64(uint64_t base, uint64_t exponent, math_error_t* out_error) {
	if (out_error) {
		*out_error = math_error_none;
	}

	if (exponent == 0) {
		return 1;
	}

	uint64_t odd_accumulator = 1;
	uint64_t even_accumulator = base;

	while (exponent > 1) {
		// the compiler should optimize this to effectively `(exponent & 1) != 0`, but this is clearer about our intentions
		if ((exponent % 2) != 0) {
			// odd
			if (__builtin_mul_overflow(odd_accumulator, even_accumulator, &odd_accumulator) && out_error) {
				*out_error = math_error_overflow;
			}
			exponent -= 1;
		}

		if (__builtin_mul_overflow(even_accumulator, even_accumulator, &even_accumulator) && out_error) {
			*out_error = math_error_overflow;
		}
		exponent /= 2; // likewise, the compiler should optimize this to `exponent >>= 1`
	}

	return odd_accumulator * even_accumulator;
};

// this is implemented as above, except we handle the case of having a negative exponent

double math_pow_di(double base, int64_t exponent, math_error_t* out_error) {
	if (out_error) {
		*out_error = math_error_none;
	}

	if (exponent == 0) {
		return 1;
	}

	bool reciprocal = false;

	if (exponent < 0) {
		reciprocal = true;
		exponent *= -1;
	}

	double odd_accumulator = 1;
	double even_accumulator = base;

	while (exponent > 1) {
		// the compiler should optimize this to effectively `(exponent & 1) != 0`, but this is clearer about our intentions
		if ((exponent % 2) != 0) {
			// odd
			odd_accumulator *= even_accumulator;
			exponent -= 1;
		}

		even_accumulator *= even_accumulator;
		exponent /= 2; // likewise, the compiler should optimize this to `exponent >>= 1`
	}

	double result = odd_accumulator * even_accumulator;

	return reciprocal ? ((double)1 / result) : result;
};
