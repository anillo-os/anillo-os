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
	util::{decode_utf8_and_length, slices_are_equal, ConstSizedSlice, ConstSlice},
};

#[rustfmt::skip]
const RAW_FONT_DATA: [u8; include_bytes!("../../resources/ter-u16n.psf").len()] = *include_bytes!("../../resources/ter-u16n.psf");

bitflags! {
	struct PSF1Flags: u8 {
		const HAS_512_GLYPHS = 0x01;
		const UNICODE = 0x02;
	}

	struct PSF2Flags: u32 {
		const UNICODE = 0x01;
	}
}

#[derive(Clone, Copy)]
struct PSF1Header {
	magic: [u8; 2],
	flags: PSF1Flags,
	glyph_size: u8,
}

impl PSF1Header {
	pub const BYTE_SIZE: usize = 4;

	pub const fn new(bytes: &[u8]) -> Self {
		Self {
			magic: bytes.const_unroll(),
			flags: PSF1Flags::from_bits(bytes[2]).unwrap(),
			glyph_size: bytes[3],
		}
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

	pub const fn new(bytes: &[u8]) -> Self {
		Self {
			magic: bytes.const_unroll(),
			version: u32::from_ne_bytes(bytes.const_subslice(4..8).const_unroll()),
			header_size: u32::from_ne_bytes(bytes.const_subslice(8..12).const_unroll()),
			flags: PSF2Flags::from_bits(u32::from_ne_bytes(
				bytes.const_subslice(12..16).const_unroll(),
			))
			.unwrap(),
			glyph_count: u32::from_ne_bytes(bytes.const_subslice(16..20).const_unroll()),
			glyph_size: u32::from_ne_bytes(bytes.const_subslice(20..24).const_unroll()),
			glyph_height: u32::from_ne_bytes(bytes.const_subslice(24..28).const_unroll()),
			glyph_width: u32::from_ne_bytes(bytes.const_subslice(28..32).const_unroll()),
		}
	}

	pub const fn as_bytes(&self) -> [u8; 32] {
		let tmp1 = self.magic;
		let tmp2 = tmp1.const_concat(&self.version.to_ne_bytes());
		let tmp3 = tmp2.const_concat(&self.header_size.to_ne_bytes());
		let tmp4 = tmp3.const_concat(&self.flags.bits().to_ne_bytes());
		let tmp5 = tmp4.const_concat(&self.glyph_count.to_ne_bytes());
		let tmp6 = tmp5.const_concat(&self.glyph_size.to_ne_bytes());
		let tmp7 = tmp6.const_concat(&self.glyph_height.to_ne_bytes());
		let tmp8 = tmp7.const_concat(&self.glyph_width.to_ne_bytes());
		tmp8
	}
}

const PSF1_MAGIC: [u8; 2] = [0x36, 0x04];
const PSF2_MAGIC: [u8; 4] = [0x72, 0xb5, 0x4a, 0x86];

const HEADER_PAD: usize = PSF2Header::BYTE_SIZE - PSF1Header::BYTE_SIZE;

const fn process_font<const N: usize>(
	file_bytes: &[u8; N],
) -> ([u8; N], [Option<u16>; 0xffff], PSF2Header, usize) {
	#[rustfmt::skip]
	let is_psf1: bool = file_bytes.len() > PSF1_MAGIC.len() && slices_are_equal(file_bytes.const_subslice(0..2), &PSF1_MAGIC);
	#[rustfmt::skip]
	let is_psf2: bool = file_bytes.len() > PSF2_MAGIC.len() && slices_are_equal(file_bytes.const_subslice(0..4), &PSF2_MAGIC);
	assert!(is_psf1 || is_psf2);

	let psf2_header = if is_psf1 {
		let psf1_header = PSF1Header::new(file_bytes);
		PSF2Header {
			magic: PSF2_MAGIC,
			version: 0,
			header_size: 4,
			flags: if psf1_header.flags.contains(PSF1Flags::UNICODE) {
				PSF2Flags::UNICODE
			} else {
				PSF2Flags::empty()
			},
			glyph_count: if psf1_header.flags.contains(PSF1Flags::HAS_512_GLYPHS) {
				512
			} else {
				256
			},
			glyph_size: psf1_header.glyph_size as u32,
			glyph_height: psf1_header.glyph_size as u32,
			glyph_width: 8,
		}
	} else if is_psf2 {
		PSF2Header::new(file_bytes)
	} else {
		unreachable!()
	};

	let table_offset = psf2_header.header_size + (psf2_header.glyph_size * psf2_header.glyph_count);

	let mut font_data: [u8; N] = [0; N];
	let font_data_len = (table_offset as usize) - (psf2_header.header_size as usize);

	// can't use the standard library's `copy_from_slice` because it's not a const fn
	font_data
		.const_subslice_mut(0..font_data_len)
		.const_copy_from_slice(
			file_bytes.const_subslice(psf2_header.header_size as usize..table_offset as usize),
		);

	let mut unicode_table: [Option<u16>; 0xffff] = [None; 0xffff];

	if psf2_header.flags.contains(PSF2Flags::UNICODE) {
		let table = file_bytes.const_subslice(table_offset as usize..);

		let mut table_index: usize = 0;
		let mut glyph_index: usize = 0;

		if is_psf1 {
			while table_index < table.len() {
				let mut curr = u16::from_le_bytes([table[table_index], table[table_index + 1]]);

				if curr == 0xffff {
					// terminator
					table_index += 2;
					glyph_index += 1;
				} else if curr == 0xfffe {
					// combining symbol; we skip it
					while curr != 0xffff {
						table_index += 2;
						curr = u16::from_le_bytes([table[table_index], table[table_index + 1]]);
					}

					// consume the terminator
					table_index += 2;
					glyph_index += 1;
				} else {
					unicode_table[curr as usize] = Some(glyph_index as u16);
					table_index += 2;
				}
			}
		} else if is_psf2 {
			while table_index < table.len() {
				let mut curr = table[table_index] as u32;

				if curr == 0xff {
					// terminator
					table_index += 1;
					glyph_index += 1;
				} else if curr == 0xfe {
					// combining symbol; we skip it
					while curr != 0xff {
						if curr == 0xfe {
							table_index += 1;
							curr = table[table_index] as u32;
						} else {
							let (_, len) =
								decode_utf8_and_length(table.const_subslice(table_index..))
									.unwrap();
							table_index += len as usize;
							curr = table[table_index] as u32;
						}
					}

					// consume the terminator
					table_index += 1;
					glyph_index += 1;
				} else {
					let (character, len) =
						decode_utf8_and_length(table.const_subslice(table_index..)).unwrap();
					table_index += len as usize;
					curr = character as u32;
					unicode_table[curr as usize] = Some(glyph_index as u16);
				}
			}
		}
	}

	(font_data, unicode_table, psf2_header, font_data_len)
}

const PROCESSED_DATA: (
	[u8; RAW_FONT_DATA.len()],
	[Option<u16>; 0xffff],
	PSF2Header,
	usize,
) = process_font(&RAW_FONT_DATA);

const FONT_DATA: [u8; PROCESSED_DATA.3] = PROCESSED_DATA
	.0
	.const_subslice(..PROCESSED_DATA.3)
	.const_unroll();
const UNICODE_TABLE: [Option<u16>; 0xffff] = PROCESSED_DATA.1;
const FONT_HEADER: PSF2Header = PROCESSED_DATA.2;

// for debugging
#[used]
static FONT_DATA_COPY: [u8; FONT_DATA.len()] = FONT_DATA;
#[used]
static FONT_HEADER_COPY: PSF2Header = FONT_HEADER;
#[used]
static UNICODE_TABLE_COPY: [Option<u16>; 0xffff] = UNICODE_TABLE;

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
			UNICODE_TABLE[(character as u32) as usize]
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
		FONT_DATA.const_subslice(byte_start..byte_start + glyph_size),
	))
}

pub const GLYPH_WIDTH: usize = FONT_HEADER.glyph_width as usize;
pub const GLYPH_HEIGHT: usize = FONT_HEADER.glyph_height as usize;
