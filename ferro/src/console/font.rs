use crate::util::{slices_are_equal, decode_utf8_and_length, ConstSlice, ConstSizedSlice};

use bitflags::bitflags;

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
			version: u32::from_ne_bytes(bytes[4..8].const_unroll()),
			header_size: u32::from_ne_bytes(bytes[8..12].const_unroll()),
			flags: PSF2Flags::from_bits(u32::from_ne_bytes(bytes[12..16].const_unroll())).unwrap(),
			glyph_count: u32::from_ne_bytes(bytes[16..20].const_unroll()),
			glyph_size: u32::from_ne_bytes(bytes[20..24].const_unroll()),
			glyph_height: u32::from_ne_bytes(bytes[24..28].const_unroll()),
			glyph_width: u32::from_ne_bytes(bytes[28..32].const_unroll()),
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

const fn process_font<const N: usize>(file_bytes: &[u8; N]) -> ([u8; N], [u16; 0xffff], PSF2Header, usize) {
	let is_psf1: bool = file_bytes.len() > PSF1_MAGIC.len() && slices_are_equal(&file_bytes[0..2], &PSF1_MAGIC);
	let is_psf2: bool = file_bytes.len() > PSF2_MAGIC.len() && slices_are_equal(&file_bytes[0..4], &PSF2_MAGIC);
	assert!(is_psf1 || is_psf2);

	let psf2_header = if is_psf1 {
		let psf1_header = PSF1Header::new(file_bytes);
		PSF2Header {
			magic: PSF2_MAGIC,
			version: 0,
			header_size: 4,
			flags: if psf1_header.flags.contains(PSF1Flags::UNICODE) { PSF2Flags::UNICODE } else { PSF2Flags::empty() },
			glyph_count: 0,
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
	let font_data_len = file_bytes.len() - (psf2_header.header_size as usize);

	// can't use the standard library's `copy_from_slice` because it's not a const fn
	font_data[0..font_data_len].const_copy_from_slice(&file_bytes[psf2_header.header_size as usize..]);

	let mut unicode_table: [u16; 0xffff] = [0; 0xffff];

	if psf2_header.flags.contains(PSF2Flags::UNICODE) {
		let table = &file_bytes[table_offset as usize..];

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
					unicode_table[curr as usize] = glyph_index as u16;
					table_index += 1;
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
							let (_, len) = decode_utf8_and_length(&table[table_index..]).unwrap();
							table_index += len as usize;
							curr = table[table_index] as u32;
						}
					}

					// consume the terminator
					table_index += 1;
					glyph_index += 1;
				} else {
					let (character, len) = decode_utf8_and_length(&table[table_index..]).unwrap();
					table_index += len as usize;
					curr = character as u32;
					unicode_table[curr as usize] = glyph_index as u16;
				}
			}
		}
	}

	(font_data, unicode_table, psf2_header, font_data_len)
}

const PROCESSED_DATA: ([u8; RAW_FONT_DATA.len()], [u16; 0xffff], PSF2Header, usize) = process_font(&RAW_FONT_DATA);

const FONT_DATA: [u8; PROCESSED_DATA.3] = PROCESSED_DATA.0[..PROCESSED_DATA.3].const_unroll();
const UNICODE_TABLE: [u16; 0xffff] = PROCESSED_DATA.1;
const FONT_HEADER: PSF2Header = PROCESSED_DATA.2;
