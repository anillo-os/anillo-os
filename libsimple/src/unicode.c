/*
 * This file is part of Anillo OS
 * Copyright (C) 2023 Anillo OS Developers
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

#include <libsimple/unicode.h>

ferr_t simple_utf8_to_utf32(const char* utf8_sequence, size_t sequence_length, size_t* out_utf8_length, uint32_t* out_utf32) {
	ferr_t status = ferr_too_small;
	uint32_t utf32_char = UINT32_MAX;
	uint8_t required_length = 0;

	if (sequence_length > 0) {
		uint8_t first_char = utf8_sequence[0];

		if (first_char & 0x80) {
			if ((first_char & 0x20) == 0) {
				// 2 bytes
				required_length = 2;
				if (sequence_length < required_length) {
					status = ferr_too_small;
					goto out;
				}
				utf32_char = ((first_char & 0x1f) << 6) | (utf8_sequence[1] & 0x3f);
			} else if ((first_char & 0x10) == 0) {
				// 3 bytes
				required_length = 3;
				if (sequence_length < required_length) {
					status = ferr_too_small;
					goto out;
				}
				utf32_char = ((first_char & 0x0f) << 12) | ((utf8_sequence[1] & 0x3f) << 6) | (utf8_sequence[2] & 0x3f);
			} else if ((first_char & 0x08) == 0) {
				// 4 bytes
				required_length = 4;
				if (sequence_length < required_length) {
					status = ferr_too_small;
					goto out;
				}
				utf32_char = ((first_char & 0x07) << 18) | ((utf8_sequence[1] & 0x3f) << 12) | ((utf8_sequence[2] & 0x3f) << 6) | (utf8_sequence[3] & 0x3f);
			} else {
				// more than 4 bytes???
				status = ferr_invalid_argument;
				goto out;
			}
		} else {
			required_length = 1;
			utf32_char = first_char;
		}

		status = ferr_ok;
	}

out:
	if (out_utf8_length) {
		*out_utf8_length = required_length;
	}
	if (status == ferr_ok) {
		if (out_utf32) {
			*out_utf32 = utf32_char;
		}
	}
	return status;
};

ferr_t simple_utf8_to_utf16(const char* utf8_sequence, size_t sequence_length, size_t utf16_sequence_space, size_t* out_utf8_length, uint16_t* out_utf16_sequence, size_t* out_utf16_length) {
	// for ease of implementation, let's just transcode from UTF-8 to UTF-32 and then from UTF-32 to UTF-16
	uint32_t tmp = 0;
	ferr_t status = ferr_ok;

	status = simple_utf8_to_utf32(utf8_sequence, sequence_length, out_utf8_length, &tmp);
	if (status != ferr_ok) {
		goto out;
	}

	status = simple_utf32_to_utf16(tmp, utf16_sequence_space, out_utf16_sequence, out_utf16_length);
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
};

ferr_t simple_utf16_to_utf8(const uint16_t* utf16_sequence, size_t sequence_length, size_t utf8_sequence_space, size_t* out_utf16_length, char* out_utf8_sequence, size_t* out_utf8_length) {
	// for ease of implementation, let's just transcode from UTF-16 to UTF-32 and then from UTF-32 to UTF-8
	uint32_t tmp = 0;
	ferr_t status = ferr_ok;

	status = simple_utf16_to_utf32(utf16_sequence, sequence_length, out_utf16_length, &tmp);
	if (status != ferr_ok) {
		goto out;
	}

	status = simple_utf32_to_utf8(tmp, utf8_sequence_space, out_utf8_sequence, out_utf8_length);
	if (status != ferr_ok) {
		goto out;
	}

out:
	return status;
};

ferr_t simple_utf16_to_utf32(const uint16_t* utf16_sequence, size_t sequence_length, size_t* out_utf16_length, uint32_t* out_utf32) {
	ferr_t status = ferr_too_small;
	uint32_t utf32_char = UINT32_MAX;
	size_t required_length = 0;

	if (sequence_length > 0) {
		uint16_t first_char = utf16_sequence[0];

		if (first_char < 0xd800 || first_char >= 0xe000) {
			// a character in the Basic Multilingual Plane; simply the same value.
			utf32_char = first_char;
			required_length = 1;
		} else if (first_char >= 0xdc00 && first_char < 0xe000) {
			// a trailing/low surrogate; this *must* be unpaired since it's the first code unit we found.
			// just pass it on through.
			utf32_char = first_char;
			required_length = 1;
		} else {
			fassert(first_char >= 0xd800 && first_char < 0xdc00);
			// this must be in the range `[0xd800, 0xdc00)` meaning that it's the leading/high surrogate.
			// let's see if we can find a trailing/low surrogate.
			if (sequence_length > 1) {
				uint16_t second_char = utf16_sequence[0];
				if (second_char >= 0xdc00 && second_char < 0xe000) {
					// this is a trailing/low surrogate. let's decode it properly.
					utf32_char = (((uint32_t)(first_char - 0xd800) << 10) | (uint32_t)(second_char - 0xdc00)) + 0x010000;
					required_length = 2;
				} else {
					// this is either a character from the Basic Multilingual Plane or another leading/high surrogate.
					// either way, this means that we have an unpaired surrogate. let's just pass it through.
					utf32_char = first_char;
					required_length = 1;
				}
			} else {
				// no additional code unit? no problem. just encode it directly as an unpaired surrogate.
				utf32_char = first_char;
				required_length = 1;
			}
		}

		status = ferr_ok;
	}

out:
	if (out_utf16_length) {
		*out_utf16_length = required_length;
	}
	if (status == ferr_ok) {
		if (out_utf32) {
			*out_utf32 = utf32_char;
		}
	}
	return status;
};

ferr_t simple_utf32_to_utf8(uint32_t utf32, size_t sequence_space, char* out_utf8_sequence, size_t* out_utf8_length) {
	ferr_t status = ferr_too_small;
	size_t length = 0;

	if (utf32 < 0x80) {
		length = 1;

		if (sequence_space >= length && out_utf8_sequence) {
			status = ferr_ok;
			out_utf8_sequence[0] = utf32;
		} else if (!out_utf8_sequence) {
			status = ferr_ok;
		}
	} else if (utf32 < 0x800) {
		length = 2;

		if (sequence_space >= length && out_utf8_sequence) {
			status = ferr_ok;
			out_utf8_sequence[0] = 0xc0 | ((utf32 & (0x1fULL << 6)) >> 6);
			out_utf8_sequence[1] = 0x80 | (utf32 & 0x3f);
		} else if (!out_utf8_sequence) {
			status = ferr_ok;
		}
	} else if (utf32 < 0x10000) {
		length = 3;

		if (sequence_space >= length && out_utf8_sequence) {
			status = ferr_ok;
			out_utf8_sequence[0] = 0xe0 | ((utf32 & (0x0fULL << 12)) >> 12);
			out_utf8_sequence[1] = 0x80 | ((utf32 & (0x3fULL << 6)) >> 6);
			out_utf8_sequence[2] = 0x80 | (utf32 & 0x3f);
		} else if (!out_utf8_sequence) {
			status = ferr_ok;
		}
	} else if (utf32 < 0x110000) {
		length = 4;

		if (sequence_space >= length && out_utf8_sequence) {
			status = ferr_ok;
			out_utf8_sequence[0] = 0xf0 | ((utf32 & (0x07ULL << 18)) >> 18);
			out_utf8_sequence[1] = 0x80 | ((utf32 & (0x3fULL << 12)) >> 12);
			out_utf8_sequence[2] = 0x80 | ((utf32 & (0x3fULL << 6)) >> 6);
			out_utf8_sequence[3] = 0x80 | (utf32 & 0x3f);
		} else if (!out_utf8_sequence) {
			status = ferr_ok;
		}
	} else {
		length = 0;

		status = ferr_invalid_argument;
	}

	if (out_utf8_length) {
		*out_utf8_length = length;
	}

	return status;
};

ferr_t simple_utf32_to_utf16(uint32_t utf32, size_t sequence_space, uint16_t* out_utf16_sequence, size_t* out_utf16_length) {
	ferr_t status = ferr_too_small;
	size_t length = 0;

	if (utf32 < 0x010000) {
		// any value that fits in 16 bits is simply passed through.
		// this produces unpaired surrogates for the values in the range `[0xd800, 0xe000)`,
		// but if such a value appears in a UTF-32 codepoint, it's technically not valid anyways.
		length = 1;

		if (sequence_space >= length && out_utf16_sequence) {
			status = ferr_ok;
			out_utf16_sequence[0] = utf32;
		} else if (!out_utf16_sequence) {
			status = ferr_ok;
		}
	} else if (utf32 < 0x110000) {
		length = 2;

		if (sequence_space >= length && out_utf16_sequence) {
			status = ferr_ok;
			out_utf16_sequence[0] = ((utf32 - 0x010000) >> 10) + 0xd800;
			out_utf16_sequence[1] = ((utf32 - 0x010000) & 0x3ff) + 0xdc00;
		} else if (!out_utf16_sequence) {
			status = ferr_ok;
		}
	} else {
		length = 0;

		status = ferr_invalid_argument;
	}

	if (out_utf16_length) {
		*out_utf16_length = length;
	}

	return status;
};
