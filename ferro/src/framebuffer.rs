use core::ffi::c_void;
use crate::sync::Lock;
use crate::geometry::{Point, Rect};

use super::sync::SpinLock;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Info {
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
	pub fn new(r: u8, g: u8, b: u8) -> Self {
		Self { r, g, b }
	}

	fn from_value(value: u32, mask: &PixelMask) -> Self {
		Self {
			r: ((value & mask.r) >> mask.r.trailing_zeros()) as u8,
			g: ((value & mask.g) >> mask.g.trailing_zeros()) as u8,
			b: ((value & mask.b) >> mask.b.trailing_zeros()) as u8,
		}
	}

	fn from_bytes(bytes: &[u8], mask: &PixelMask) -> Self {
		let mut value: u32 = 0;

		for byte in bytes {
			value <<= 8;
			value |= *byte as u32;
		}

		Self::from_value(value, mask)
	}

	fn as_value(&self, mask: &PixelMask) -> u32 {
		(self.r as u32) << mask.r.trailing_zeros() |
		(self.g as u32) << mask.g.trailing_zeros() |
		(self.b as u32) << mask.b.trailing_zeros()
	}

	fn copy_to_bytes(&self, bytes: &mut [u8], mask: &PixelMask) -> () {
		let mut val = self.as_value(mask);

		for byte in bytes {
			*byte = (val & 0xff) as u8;
			val >>= 8;
		}
	}
}

impl From<(u8, u8, u8)> for Pixel {
	fn from(value: (u8, u8, u8)) -> Self {
		Self::new(value.0, value.1, value.2)
	}
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
	inner: SpinLock<InnerFramebuffer<'a>>
}

impl<'a> Framebuffer<'a> {
	pub fn new(info: &Info) -> Self {
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
				front_buffer: unsafe { core::slice::from_raw_parts_mut::<'a>(info.virtual_base as *mut u8, info.total_byte_size) },
			})
		}
	}

	fn get_pixel(&self, point: &Point) -> Result<Pixel, ()> {
		if point.x > self.info.width || point.y > self.info.height {
			return Err(())
		}

		let bpp = self.info.bytes_per_pixel as usize;
		let base_index = (self.info.scan_line_size * point.y) + (bpp * point.x);
		let pixel;

		{
			let mut inner = self.inner.lock();
			pixel = Pixel::from_bytes(&mut inner.front_buffer[base_index..base_index + bpp], &self.info.masks);
		}

		Ok(pixel)
	}

	fn set_pixel(&self, point: &Point, pixel: &Pixel) -> Result<(), ()> {
		if point.x > self.info.width || point.y > self.info.height {
			return Err(())
		}

		let bpp = self.info.bytes_per_pixel as usize;
		let base_index = (self.info.scan_line_size * point.y) + (bpp * point.x);

		{
			let mut inner = self.inner.lock();
			pixel.copy_to_bytes(&mut inner.front_buffer[base_index..base_index + bpp], &self.info.masks);
		}

		Ok(())
	}

	fn fill_rect(&self, rect: &Rect, pixel: &Pixel) -> Result<(), ()> {
		let screen_rect = Rect::new((0, 0).into(), (self.info.width, self.info.height).into());

		if !rect.is_within_rect(&screen_rect) {
			return Err(())
		}

		if rect.size.width == 0 || rect.size.height == 0 {
			return Ok(())
		}

		let bpp = self.info.bytes_per_pixel as usize;
		let base_index = (self.info.scan_line_size * rect.top_left().y) + (bpp * rect.top_left().x);

		{
			let mut inner = self.inner.lock();

			// do the first row, which we'll use as a basis to copy to other rows
			pixel.copy_to_bytes(&mut inner.front_buffer[base_index..base_index + bpp], &self.info.masks);

			// TODO: we could speed this up by copying in the biggest units possible (i.e. u16, u32, or u64) according to the available space

			for i in 1..rect.size.width {
				let curr_index = base_index + i * bpp;
				unsafe {
					core::ptr::copy_nonoverlapping(inner.front_buffer[base_index..base_index + bpp].as_ptr(), inner.front_buffer[curr_index..curr_index + bpp].as_mut_ptr(), bpp);
				}
			}

			// now use the first row as a basis to copy to all other rows
			for i in 1..rect.size.height {
				let curr_index = base_index + (self.info.scan_line_size * i);
				let width_in_bytes = bpp * rect.size.width;
				unsafe {
					core::ptr::copy_nonoverlapping(inner.front_buffer[base_index..base_index + width_in_bytes].as_ptr(), inner.front_buffer[curr_index..curr_index + width_in_bytes].as_mut_ptr(), width_in_bytes)
				}
			}
		}

		Ok(())
	}

	fn shift_up(&self, row_count: usize, fill_value: &Pixel) -> Result<(), ()> {
		if row_count > self.info.height {
			return Err(())
		}

		if row_count == 0 {
			return Ok(())
		}

		if row_count < self.info.height {
			// copy rows up
			{
				let mut inner = self.inner.lock();

				for i in 0..self.info.height - row_count {
					let orig_index = self.info.scan_line_size * (i + row_count);
					let new_index = self.info.scan_line_size * i;
					unsafe {
						core::ptr::copy_nonoverlapping(inner.front_buffer[orig_index..orig_index + self.info.scan_line_size].as_ptr(), inner.front_buffer[new_index..new_index + self.info.scan_line_size].as_mut_ptr(), self.info.scan_line_size);
					}
				}
			}
		}

		self.fill_rect(&Rect::new((0, self.info.height - row_count).into(), (self.info.width, self.info.height).into()), fill_value)
	}
}

static mut FRAMEBUFFER: Option<Framebuffer> = None;

pub unsafe fn init(info: &Info) -> () {
	match FRAMEBUFFER {
		Some(_) => panic!("cannot init framebuffer twice"),
		None => {},
	}

	FRAMEBUFFER = Some(Framebuffer::new(info));
	FRAMEBUFFER.as_ref().unwrap().fill_rect(&Rect::new((0, 0).into(), (info.width, info.height).into()), &(0, 0, 0).into()).expect("clearing the screen should work");
}

pub fn framebuffer() -> Option<&'static Framebuffer<'static>> {
	unsafe {
		// this is fine because we ensure in `init` that we cannot reassign `FRAMEBUFFER` after it has been assigned for the first time,
		// so when the Option is present, the value within will always remain valid
		FRAMEBUFFER.as_ref()
	}
}
