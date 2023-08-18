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

use core::iter::FusedIterator;

use bitflags::bitflags;

use crate::{
	framebuffer::{Framebuffer, Pixel},
	geometry::Point,
	util::{const_concat, const_subslice, const_unroll},
};

bitflags! {
	struct PSF2Flags: u32 {
		const UNICODE = 0x01;
	}
}

#[derive(Clone, Copy)]
struct PSF2Header {
	magic: [u8; 4],
	version: u32,
	header_size: u32,
	flags: PSF2Flags,
	glyph_count: u32,
	glyph_size: u32,
	glyph_height: u32,
	glyph_width: u32,
}

impl PSF2Header {
	pub const BYTE_SIZE: usize = 32;

	pub const fn new(bytes: &[u8; 32]) -> Self {
		Self {
			magic: const_unroll(bytes),
			version: u32::from_ne_bytes(const_unroll(const_subslice(bytes, 4..8))),
			header_size: u32::from_ne_bytes(const_unroll(const_subslice(bytes, 8..12))),
			flags: PSF2Flags::from_bits(u32::from_ne_bytes(const_unroll(const_subslice(
				bytes,
				12..16,
			))))
			.unwrap(),
			glyph_count: u32::from_ne_bytes(const_unroll(const_subslice(bytes, 16..20))),
			glyph_size: u32::from_ne_bytes(const_unroll(const_subslice(bytes, 20..24))),
			glyph_height: u32::from_ne_bytes(const_unroll(const_subslice(bytes, 24..28))),
			glyph_width: u32::from_ne_bytes(const_unroll(const_subslice(bytes, 28..32))),
		}
	}

	pub const fn as_bytes(&self) -> [u8; 32] {
		let tmp1 = self.magic;
		let tmp2 = const_concat(&tmp1, &self.version.to_ne_bytes());
		let tmp3 = const_concat(&tmp2, &self.header_size.to_ne_bytes());
		let tmp4 = const_concat(&tmp3, &self.flags.bits().to_ne_bytes());
		let tmp5 = const_concat(&tmp4, &self.glyph_count.to_ne_bytes());
		let tmp6 = const_concat(&tmp5, &self.glyph_size.to_ne_bytes());
		let tmp7 = const_concat(&tmp6, &self.glyph_height.to_ne_bytes());
		let tmp8 = const_concat(&tmp7, &self.glyph_width.to_ne_bytes());
		tmp8
	}
}

const PSF2_MAGIC: [u8; 4] = [0x72, 0xb5, 0x4a, 0x86];

#[cfg(target_arch = "x86_64")]
macro_rules! font_path {
	($component:literal) => {
		concat!("../../../build/x86_64/font/", $component, ".bin")
	};
}
#[cfg(target_arch = "aarch64")]
macro_rules! font_path {
	($component:literal) => {
		concat!("../../../build/aarch64/font/", $component, ".bin")
	};
}

const fn unicode_table_from(bytes: &[u8; 0xffff * 2]) -> [u16; 0xffff] {
	// SAFETY: this is safe because we know the length of the source is exactly twice the length of the destination, so exactly 2 u8 bytes become 1 u16.
	unsafe { core::mem::transmute_copy(bytes) }
}

const FONT_DATA: [u8; include_bytes!(font_path!("data")).len()] =
	*include_bytes!(font_path!("data"));
const UNICODE_TABLE: [u16; 0xffff] = unicode_table_from(include_bytes!(font_path!("unicode")));
const FONT_HEADER: PSF2Header = PSF2Header::new(include_bytes!(font_path!("header")));

// for debugging
#[used]
static FONT_DATA_COPY: [u8; FONT_DATA.len()] = FONT_DATA;
#[used]
static FONT_HEADER_COPY: PSF2Header = FONT_HEADER;
#[used]
static UNICODE_TABLE_COPY: [u16; 0xffff] = UNICODE_TABLE;

#[derive(Clone, Copy)]
pub struct Glyph {
	data: &'static [u8],
	font_header: &'static PSF2Header,
}

/// Iterates over the pixels in a glyph from left to right, top to bottom.
#[derive(Clone, Copy)]
pub struct GlyphIter<'a>(&'a Glyph, Option<Point>, bool);

#[derive(Clone, Copy)]
pub struct GlyphPixel(Point, bool);

impl Glyph {
	const fn new(font_header: &'static PSF2Header, bytes: &'static [u8]) -> Self {
		Self {
			data: bytes,
			font_header,
		}
	}

	/// Width of the glyph in pixels (or bits)
	pub const fn width(&self) -> usize {
		self.font_header.glyph_width as usize
	}

	/// Height of the glyph in pixels (or bits)
	pub const fn height(&self) -> usize {
		self.font_header.glyph_height as usize
	}

	pub const fn size(&self) -> usize {
		self.width() * self.height()
	}

	pub const fn padded_width(&self) -> usize {
		self.byte_width() * 8
	}

	pub const fn byte_width(&self) -> usize {
		(self.width() + 7) / 8
	}

	pub const fn byte_size(&self) -> usize {
		self.font_header.glyph_size as usize
	}

	pub fn pixels(&self, skip_unset_pixels: bool) -> GlyphIter {
		GlyphIter(self, Some((0, 0).into()), skip_unset_pixels)
	}

	pub fn print(
		&self,
		framebuffer: &Framebuffer,
		foreground: &Pixel,
		background: Option<&Pixel>,
		top_left: &Point,
	) -> Result<(), ()> {
		if framebuffer.width() < top_left.x + self.width()
			|| framebuffer.height() < top_left.y + self.height()
		{
			return Err(());
		}

		for pixel in self.pixels(background.is_none()) {
			let result = framebuffer.set_pixel(
				&(&pixel.0 + top_left),
				if pixel.1 {
					foreground
				} else {
					background.unwrap()
				},
			);
			if result.is_err() {
				return result;
			}
		}

		Ok(())
	}
}

impl<'a> Iterator for GlyphIter<'a> {
	type Item = GlyphPixel;

	fn next(&mut self) -> Option<Self::Item> {
		while self.1.is_some() {
			let point = self.1.unwrap();
			let bit_index = point.as_index(self.0.padded_width());

			let byte = self.0.data[bit_index / 8];
			let bit = 7 - ((bit_index % 8) as u8);
			let present = (byte & (1 << bit)) != 0;

			self.1 = point.next_point(&(self.0.width(), self.0.height()).into());

			if !present && self.2 {
				continue;
			}

			return Some(GlyphPixel(point, present));
		}

		None
	}
}

impl<'a> FusedIterator for GlyphIter<'a> {}

impl<'a> IntoIterator for &'a Glyph {
	type IntoIter = GlyphIter<'a>;
	type Item = GlyphPixel;

	/// Creates an iterator that iterates over the pixels in this glyph (including unset pixels).
	fn into_iter(self) -> Self::IntoIter {
		self.pixels(false)
	}
}

pub const fn glyph_for_character(character: char) -> Option<Glyph> {
	let val = character as u32;
	let index = if FONT_HEADER.flags.contains(PSF2Flags::UNICODE) {
		if (val as usize) > UNICODE_TABLE.len() {
			None
		} else {
			match UNICODE_TABLE[(character as u32) as usize] {
				0xffff => None,
				res @ _ => Some(res),
			}
		}
	} else {
		if val > (u16::MAX as u32) {
			None
		} else {
			Some(val as u16)
		}
	};

	if index.is_none() {
		return None;
	}

	let index = index.unwrap();

	let glyph_size = FONT_HEADER.glyph_size as usize;
	let byte_start = (index as usize) * glyph_size;

	Some(Glyph::new(
		&FONT_HEADER,
		const_subslice(&FONT_DATA, byte_start..byte_start + glyph_size),
	))
}

pub const GLYPH_WIDTH: usize = FONT_HEADER.glyph_width as usize;
pub const GLYPH_HEIGHT: usize = FONT_HEADER.glyph_height as usize;
