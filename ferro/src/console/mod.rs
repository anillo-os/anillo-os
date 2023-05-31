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

mod font;

use core::fmt::{write, Arguments, Error, Result, Write};

use self::font::{glyph_for_character, GLYPH_HEIGHT, GLYPH_WIDTH};
use crate::{
	framebuffer::{
		color::{BLACK, WHITE},
		framebuffer, Framebuffer, Pixel,
	},
	geometry::Point,
	sync::{Lock, SpinLock},
	util::ConstInto,
};

struct Console {
	foreground: Pixel,
	background: Pixel,
	location: Point,
	char_padding: usize,
	line_padding: usize,
}

impl Console {
	const fn new() -> Self {
		Self {
			foreground: WHITE,
			background: BLACK,
			location: (0, 0).const_into(),
			char_padding: 0,
			line_padding: 0,
		}
	}

	fn move_to_next_line(&mut self, fb: &Framebuffer) -> Result {
		self.location.x = 0;
		self.location.y += GLYPH_HEIGHT + self.line_padding;

		if self.location.y + GLYPH_HEIGHT >= fb.height() {
			if fb
				.shift_up(GLYPH_HEIGHT + self.line_padding, &self.background)
				.is_err()
			{
				return Err(Error);
			}
			self.location.y -= GLYPH_HEIGHT + self.line_padding;
		}

		Ok(())
	}
}

impl Write for Console {
	fn write_char(&mut self, c: char) -> Result {
		let fb = match framebuffer() {
			Some(fb) => fb,

			// if there's no framebuffer available, there's nowhere to write the character.
			// however, we don't consider this to be an error; there is simply nowhere to put the character.
			None => return Ok(()),
		};

		if c == '\n' || self.location.x + GLYPH_WIDTH >= fb.width() {
			self.move_to_next_line(fb)?;
		}

		if c != '\n' {
			let glyph = match glyph_for_character(c) {
				Some(x) => x,

				//None => return Err(Error),
				// we ignore missing glyphs
				None => return Ok(()),
			};

			if glyph
				.print(fb, &self.foreground, Some(&self.background), &self.location)
				.is_err()
			{
				return Err(Error);
			}

			self.location.x += glyph.width() + self.char_padding;
		}

		Ok(())
	}

	fn write_str(&mut self, string: &str) -> Result {
		for character in string.chars() {
			self.write_char(character)?;
		}

		Ok(())
	}
}

static CONSOLE: SpinLock<Console> = SpinLock::new(Console::new());

#[macro_export]
macro_rules! kprint {
	($($arg:tt)*) => {{
		$crate::console::kprint_args(::core::format_args!($($arg)*)).unwrap();
	}};
}

#[macro_export]
macro_rules! kprintln {
	() => {
		$crate::kprint!("\n")
	};
	($($arg:tt)*) => {{
		$crate::console::kprint_args(::core::format_args_nl!($($arg)*)).unwrap();
	}};
}

pub fn kprint_args(args: Arguments) -> Result {
	let mut console = CONSOLE.lock();
	write(&mut *console, args)
}
