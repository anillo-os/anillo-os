use crate::util::slices_are_equal;

use bitflags::bitflags;

const FONT_DATA: &[u8] = include_bytes!("../../resources/ter-u16n.psf");

bitflags! {
	#[derive(Debug, PartialEq, Eq)]
	struct PSF1Flags: u8 {
		const HAS_512_GLYPHS = 0x01;
		const UNICODE = 0x02;
	}

	#[derive(Debug, PartialEq, Eq)]
	struct PSF2Flags: u32 {
		const UNICODE = 0x01;
	}
}

struct PSF1Header {
	magic: [u8; 2],
	flags: PSF1Flags,
	glyph_size: u8,
}

impl PSF1Header {
	pub const fn new(bytes: &[u8]) -> Self {
		Self {
			magic: bytes[0..2],
			flags: PSF1Flags::from_bits(bytes[2]).unwrap(),
			glyph_size: bytes[3],
		}
	}
}

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
	pub const fn new(bytes: &[u8]) -> Self {
		Self {
			magic: bytes[0..4],
			version: u32::from_ne_bytes(&bytes[4..8]),
			header_size: u32::from_ne_bytes(&bytes[8..12]),
			flags: PSF2Flags::from_bits(u32::from_ne_bytes(&bytes[12..16])).unwrap(),
			glyph_count: u32::from_ne_bytes(&bytes[16..20]),
			glyph_size: u32::from_ne_bytes(&bytes[20..24]),
			glyph_height: u32::from_ne_bytes(&bytes[24..28]),
			glyph_width: u32::from_ne_bytes(&bytes[28..32]),
		}
	}

	pub const fn as_bytes(&self) -> [u8; 32] {
		[..self.magic, ..self.version.to_ne_bytes(), ..self.header_size.to_ne_bytes(), ..self.header_size.to_ne_bytes(), ..self.flags.bits().to_ne_bytes(), ..self.glyph_count.to_ne_bytes(), ..self.glyph_size.to_ne_bytes(), ..self.glyph_height.to_ne_bytes(), ..self.glyph_width.to_ne_bytes()]
	}
}

const PSF1_MAGIC: &[u8] = &[0x36, 0x04];
const PSF2_MAGIC: &[u8] = &[0x72, 0xb5, 0x4a, 0x86];

const fn process_font(file_bytes: &[u8]) -> (&[u8], &[u8]) {
	let is_psf1: bool = file_bytes.len() > PSF1_MAGIC.len() && slices_are_equal(&file_bytes[0..2], PSF1_MAGIC);
	let is_psf2: bool = file_bytes.len() > PSF2_MAGIC.len() && slices_are_equal(&file_bytes[0..4], PSF2_MAGIC);
	assert!(is_psf1 || is_psf2);

	let psf2_header = if is_psf1 {
		let psf1_header = PSF1Header::new(file_bytes);
		PSF2Header {
			magic: PSF2_MAGIC,
			version: 0,
			header_size: 4,
			flags: if psf1_header.flags & PSF1Flags::UNICODE { PSF2Flags::UNICODE } else { PSF2Flags::empty() },
			glyph_count: 0,
			glyph_size: psf1_header.glyph_size as u32,
			glyph_height: psf1_header.glyph_size as u32,
			glyph_width: 8,
		}
	} else if is_psf2 {
		PSF2Header::new(file_bytes);
	};

	let table_offset = psf2_header.header_size + (psf2_header.glyph_size * psf2_header.glyph_count);

	let font_data = &[..psf2_header.as_bytes(), ..file_bytes[table_offset..]];

	
}
