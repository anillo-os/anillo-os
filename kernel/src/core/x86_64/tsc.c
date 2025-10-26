/*
 * This file is part of Anillo OS
 * Copyright (C) 2021 Anillo OS Developers
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

/**
 * @file
 *
 * TSC (timestamp counter) calibration.
 */

#include <ferro/core/x86_64/tsc.h>
#include <ferro/core/x86_64/legacy-io.h>
#include <ferro/core/panic.h>
#include <ferro/core/per-cpu.private.h>
#include <ferro/core/console.h>

#include <stddef.h>

#define MS_PER_SEC 1000
#define HZ_PER_KHZ 1000

// the Programmable Interval Timer's frequency, in Hz
#define PIT_RATE 1193182ULL

// the bit to check to determine whether the gate has been flipped yet
#define PC_SPEAKER_GATE_BIT (1 << 5)

// interval to time the TSC for (should be 50 milliseconds)
#define PIT_CALIBRATION_MS 50

// frequency of the calibration interval (should be 20Hz)
#define PIT_CALIBRATION_HZ (MS_PER_SEC / PIT_CALIBRATION_MS)

// value to put into register to achieve desired interval
// explanation:
// the PIT ticks at the rate given by PIT_RATE. on each tick, the counter will be decremented by one.
// therefore, the initial counter value (this constant) must be large enough to ensure the timer does not reach 0 until the desired interval has elapsed.
// dividing the PIT rate by the calibration interval frequency gives a value that, when decremented at the PIT rate, produces an interval of the desired frequency.
#define PIT_LATCH_VALUE (PIT_RATE / PIT_CALIBRATION_HZ)

// minimum loop iteration frequency (should be 100KHz)
// any CPU should be able to meet this requirement, as long as it isn't interrupted
// if the loop iteration frequency is less than this, that means someone interrupted us (e.g. an SMI)
#define PIT_LOOP_MIN_HZ (100 * HZ_PER_KHZ)

// the minimum number of loops that must be performed to ensure the minimum loop iteration frequency
#define PIT_LOOP_MIN_COUNT (PIT_LOOP_MIN_HZ / PIT_CALIBRATION_HZ)

// when the minimum delta is multiplied by this coefficient, it must be greater than or equal to the maximum delta
// if the maximum delta is still larger, then someone interrupted us (e.g. an SMI)
// "where did this number come from," you ask? trial and error, basically.
#define PIT_MIN_DELTA_COEFFICIENT 13000

// maximum number of attemps to make to calibrate the TSC using the PIT
// with current settings, this means we can try for at most 500ms before giving up
#define MAX_CALIBRATION_ATTEMPTS 10

FERRO_ENUM(uint8_t, fpit_channel) {
	fpit_channel_irq               = 0,
	fpit_channel_ram_refresh       = 1,
	fpit_channel_pc_speaker        = 2,
	fpit_channel_read_back_command = 3,
};

FERRO_OPTIONS(uint8_t, fpit_access_mode) {
	fpit_access_mode_latch = 0,
	fpit_access_mode_low   = 1 << 0,
	fpit_access_mode_high  = 1 << 1,
};

FERRO_ENUM(uint8_t, fpit_mode) {
	fpit_mode_raise_on_terminal     = 0,
	fpit_mode_hardware_oneshot      = 1,
	fpit_mode_rate_generator        = 2,
	fpit_mode_square_wave_generator = 3,
	fpit_mode_software_strobe       = 4,
	fpit_mode_hardware_strobe       = 5,
};

FERRO_ALWAYS_INLINE uint8_t fpit_make_command(fpit_channel_t channel, fpit_access_mode_t access_mode, fpit_mode_t mode) {
	return (channel << 6) | (access_mode << 4) | (mode << 1);
};

static uint64_t determine_tsc_frequency(void) {
	// determine the TSC frequency using the PIT
	// uses a similar approach to Linux's `pit_calibrate_tsc`
	uint64_t initial_tsc = 0;
	uint64_t loop_initial_tsc = 0;
	uint64_t final_tsc = 0;
	uint64_t loop_count = 0;
	uint64_t delta_min = UINT64_MAX;
	uint64_t delta_max = 0;
	uint64_t delta = 0;

	// shut the PC speaker up (by clearing bit 2) and connect it to the PIT (by setting bit 1)
	farch_lio_write_u8(farch_lio_port_pc_speaker, (farch_lio_read_u8(farch_lio_port_pc_speaker) & ~0x02) | 0x01);

	// connect the PIT to the PC speaker, tell it to use a 16-bit latch, and also to use mode 0 to flip the PC speaker gate on termination
	farch_lio_write_u8(farch_lio_port_pit_command, fpit_make_command(fpit_channel_pc_speaker, fpit_access_mode_low | fpit_access_mode_high, fpit_mode_raise_on_terminal));

	// write the initial counter value
	// first the low byte, then the high byte
	farch_lio_write_u8(farch_lio_port_pit_data_channel_2, PIT_LATCH_VALUE & 0xff);
	farch_lio_write_u8(farch_lio_port_pit_data_channel_2, PIT_LATCH_VALUE >> 8);

	// read the initial TSC value
	initial_tsc = loop_initial_tsc = final_tsc = farch_tsc_read_weak();

	// loop until the gate bit is set
	while (!(farch_lio_read_u8(farch_lio_port_pc_speaker) & PC_SPEAKER_GATE_BIT)) {
		// read the current TSC value
		final_tsc = farch_tsc_read_weak();

		// calculate the difference
		delta = final_tsc - loop_initial_tsc;

		if (delta == 0) {
			// disregard as bogus
			loop_initial_tsc = final_tsc;
			continue;
		}

		// if it's lower than the minimum, it's the new minimum
		if (delta < delta_min) {
			delta_min = delta;
		}

		// likewise for the maximum
		if (delta > delta_max) {
			delta_max = delta;
		}

		// set the current TSC value as the initial value for the next loop
		loop_initial_tsc = final_tsc;

		// ...and increment the loop count
		++loop_count;
	}

	// if we didn't complete the minimum number of loops, someone interrupted us,
	// so our final poll results might be much larger than what they should be.
	// discard the results.
	if (loop_count < PIT_LOOP_MIN_COUNT) {
		fconsole_logf("TSC calibration failed; loop_count = " FERRO_U64_FORMAT "\n", loop_count);
		return UINT64_MAX;
	}

	// likewise, if the maximum delta is greater than the minimum delta multiplied by ::PIT_MIN_DELTA_COEFFICIENT,
	// then someone interrupted us and our results may be way off (e.g. maybe we were interrupted on the very last iteration).
	// discard the results.
	if (delta_max > (PIT_MIN_DELTA_COEFFICIENT * delta_min)) {
		fconsole_logf("TSC calibration failed; delta_max = " FERRO_U64_FORMAT ", delta_min = " FERRO_U64_FORMAT "\n", delta_max, delta_min);
		return UINT64_MAX;
	}

	delta = final_tsc - initial_tsc;

	// the TSC frequency can be found by dividing the change in TSC by the time it took to measure it.
	// dividing by milliseconds gives KHz, so multiply this value by ::HZ_PER_KHZ to get Hz
	return (delta / PIT_CALIBRATION_MS) * HZ_PER_KHZ;
};

void farch_tsc_init(void) {
	uint64_t tsc_frequency = UINT64_MAX;

	for (size_t i = 0; i < MAX_CALIBRATION_ATTEMPTS && tsc_frequency == UINT64_MAX; ++i) {
		tsc_frequency = determine_tsc_frequency();
	}

	if (tsc_frequency == UINT64_MAX) {
		fpanic("failed to calibrate TSC using PIT (reached max calibration attempts)");
	}

	fconsole_logf("Calculated TSC frequency: " FERRO_U64_FORMAT "Hz\n", tsc_frequency);
	FARCH_PER_CPU(tsc_frequency) = tsc_frequency;
};
