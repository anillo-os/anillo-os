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

use core::ffi::c_void;

use super::sync::SpinLock;
use crate::const_from_impl;
use crate::geometry::{Point, Rect};
use crate::sync::Lock;

#[repr(C)]
#[derive(Clone, Copy)]
pub(crate) struct Info {
	physical_base: *mut c_void,
	virtual_base: *mut c_void,
	width: usize,
	height: usize,
	scan_line_size: usize,
	pixel_bits: usize,
	red_mask: u32,
	green_mask: u32,
	blue_mask: u32,
	other_mask: u32,
	total_byte_size: usize,
	bytes_per_pixel: u8,
}

struct BasicInfo {
	width: usize,
	height: usize,
	scan_line_size: usize,
	pixel_bits: usize,
	masks: PixelMask,
	other_mask: u32,
	total_byte_size: usize,
	bytes_per_pixel: u8,
}

struct PixelMask {
	r: u32,
	g: u32,
	b: u32,
}

#[derive(Default, Clone, Copy)]
pub struct Pixel {
	pub r: u8,
	pub g: u8,
	pub b: u8,
}

impl Pixel {
	pub const fn new(r: u8, g: u8, b: u8) -> Self {
		Self { r, g, b }
	}

	const fn from_value(value: u32, mask: &PixelMask) -> Self {
		Self {
			r: ((value & mask.r) >> mask.r.trailing_zeros()) as u8,
			g: ((value & mask.g) >> mask.g.trailing_zeros()) as u8,
			b: ((value & mask.b) >> mask.b.trailing_zeros()) as u8,
		}
	}

	const fn from_bytes(bytes: &[u8], mask: &PixelMask) -> Self {
		let mut value: u32 = 0;

		let mut i: usize = 0;
		while i < bytes.len() {
			let byte = &bytes[i];
			value <<= 8;
			value |= *byte as u32;
			i += 1;
		}

		Self::from_value(value, mask)
	}

	const fn as_value(&self, mask: &PixelMask) -> u32 {
		(self.r as u32) << mask.r.trailing_zeros()
			| (self.g as u32) << mask.g.trailing_zeros()
			| (self.b as u32) << mask.b.trailing_zeros()
	}

	fn copy_to_bytes(&self, bytes: &mut [u8], mask: &PixelMask) -> () {
		let mut val = self.as_value(mask);

		for byte in bytes {
			*byte = (val & 0xff) as u8;
			val >>= 8;
		}
	}
}

const_from_impl! { value: (u8, u8, u8) => Pixel {
	Self::new(value.0, value.1, value.2)
}}

pub mod color {
	use super::Pixel;
	use crate::util::ConstInto;

	pub const BLACK: Pixel = (0, 0, 0).const_into();
	pub const WHITE: Pixel = (0xff, 0xff, 0xff).const_into();
	pub const RED: Pixel = (0xff, 0, 0).const_into();
	pub const GREEN: Pixel = (0, 0xff, 0).const_into();
	pub const BLUE: Pixel = (0, 0, 0xff).const_into();
	pub const CYAN: Pixel = (0, 0xff, 0xff).const_into();
	pub const MAGENTA: Pixel = (0xff, 0, 0xff).const_into();
	pub const YELLOW: Pixel = (0xff, 0xff, 0).const_into();
}

struct InnerFramebuffer<'a> {
	physical_base: *mut c_void,
	virtual_base: *mut c_void,
	front_buffer: &'a mut [u8],
}

// the raw pointers are protected by a spin lock, so we can be absolutely sure that this is safe
unsafe impl<'a> Send for InnerFramebuffer<'a> {}

pub struct Framebuffer<'a> {
	info: BasicInfo,
	inner: SpinLock<InnerFramebuffer<'a>>,
}

impl<'a> Framebuffer<'a> {
	pub(crate) fn new(info: &Info) -> Self {
		Framebuffer {
			info: BasicInfo {
				width: info.width,
				height: info.height,
				scan_line_size: info.scan_line_size,
				pixel_bits: info.pixel_bits,
				masks: PixelMask {
					r: info.red_mask,
					g: info.green_mask,
					b: info.blue_mask,
				},
				other_mask: info.other_mask,
				total_byte_size: info.total_byte_size,
				bytes_per_pixel: info.bytes_per_pixel,
			},
			inner: SpinLock::new(InnerFramebuffer {
				physical_base: info.physical_base,
				virtual_base: info.virtual_base,
				front_buffer: unsafe {
					core::slice::from_raw_parts_mut::<'a>(
						info.virtual_base as *mut u8,
						info.total_byte_size,
					)
				},
			}),
		}
	}

	pub fn get_pixel(&self, point: &Point) -> Result<Pixel, ()> {
		if point.x > self.info.width || point.y > self.info.height {
			return Err(());
		}

		let bpp = self.info.bytes_per_pixel as usize;
		let base_index = (self.info.scan_line_size * point.y) + (bpp * point.x);
		let pixel;

		{
			let mut inner = self.inner.lock();
			pixel = Pixel::from_bytes(
				&mut inner.front_buffer[base_index..base_index + bpp],
				&self.info.masks,
			);
		}

		Ok(pixel)
	}

	pub fn set_pixel(&self, point: &Point, pixel: &Pixel) -> Result<(), ()> {
		if point.x > self.info.width || point.y > self.info.height {
			return Err(());
		}

		let bpp = self.info.bytes_per_pixel as usize;
		let base_index = (self.info.scan_line_size * point.y) + (bpp * point.x);

		{
			let mut inner = self.inner.lock();
			pixel.copy_to_bytes(
				&mut inner.front_buffer[base_index..base_index + bpp],
				&self.info.masks,
			);
		}

		Ok(())
	}

	pub fn fill_rect(&self, rect: &Rect, pixel: &Pixel) -> Result<(), ()> {
		let screen_rect = Rect::new((0, 0).into(), (self.info.width, self.info.height).into());

		if !rect.is_within_rect(&screen_rect) {
			return Err(());
		}

		if rect.size.width == 0 || rect.size.height == 0 {
			return Ok(());
		}

		let bpp = self.info.bytes_per_pixel as usize;
		let base_index = (self.info.scan_line_size * rect.top_left().y) + (bpp * rect.top_left().x);

		{
			let mut inner = self.inner.lock();

			// do the first row, which we'll use as a basis to copy to other rows
			pixel.copy_to_bytes(
				&mut inner.front_buffer[base_index..base_index + bpp],
				&self.info.masks,
			);

			// TODO: we could speed this up by copying in the biggest units possible (i.e. u16, u32, or u64) according to the available space

			for i in 1..rect.size.width {
				let curr_index = base_index + i * bpp;
				unsafe {
					core::ptr::copy_nonoverlapping(
						inner.front_buffer[base_index..base_index + bpp].as_ptr(),
						inner.front_buffer[curr_index..curr_index + bpp].as_mut_ptr(),
						bpp,
					);
				}
			}

			// now use the first row as a basis to copy to all other rows
			for i in 1..rect.size.height {
				let curr_index = base_index + (self.info.scan_line_size * i);
				let width_in_bytes = bpp * rect.size.width;
				unsafe {
					core::ptr::copy_nonoverlapping(
						inner.front_buffer[base_index..base_index + width_in_bytes].as_ptr(),
						inner.front_buffer[curr_index..curr_index + width_in_bytes].as_mut_ptr(),
						width_in_bytes,
					)
				}
			}
		}

		Ok(())
	}

	pub fn shift_up(&self, row_count: usize, fill_value: &Pixel) -> Result<(), ()> {
		if row_count > self.info.height {
			return Err(());
		}

		if row_count == 0 {
			return Ok(());
		}

		if row_count < self.info.height {
			// copy rows up
			{
				let mut inner = self.inner.lock();

				for i in 0..self.info.height - row_count {
					let orig_index = self.info.scan_line_size * (i + row_count);
					let new_index = self.info.scan_line_size * i;
					unsafe {
						core::ptr::copy_nonoverlapping(
							inner.front_buffer[orig_index..orig_index + self.info.scan_line_size]
								.as_ptr(),
							inner.front_buffer[new_index..new_index + self.info.scan_line_size]
								.as_mut_ptr(),
							self.info.scan_line_size,
						);
					}
				}
			}
		}

		self.fill_rect(
			&Rect::new(
				(0, self.info.height - row_count).into(),
				(self.info.width, row_count).into(),
			),
			fill_value,
		)
	}

	/// The width of the framebuffer, in pixels
	pub const fn width(&self) -> usize {
		self.info.width
	}

	/// The height of the framebuffer, in pixels
	pub const fn height(&self) -> usize {
		self.info.height
	}
}

static mut FRAMEBUFFER: Option<Framebuffer> = None;

pub(crate) unsafe fn init(info: &Info) -> () {
	match FRAMEBUFFER {
		Some(_) => panic!("cannot init framebuffer twice"),
		None => {},
	}

	FRAMEBUFFER = Some(Framebuffer::new(info));
	FRAMEBUFFER
		.as_ref()
		.unwrap()
		.fill_rect(
			&Rect::new((0, 0).into(), (info.width, info.height).into()),
			&(0, 0, 0).into(),
		)
		.expect("clearing the screen should work");
}

pub fn framebuffer() -> Option<&'static Framebuffer<'static>> {
	unsafe {
		// this is fine because we ensure in `init` that we cannot reassign `FRAMEBUFFER` after it has been assigned for the first time,
		// so when the Option is present, the value within will always remain valid
		FRAMEBUFFER.as_ref()
	}
}
